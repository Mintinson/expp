#include "expp/app/explorer.hpp"
#include "expp/app/explorer_directory_controller.hpp"
#include "expp/app/explorer_preview_controller.hpp"
#include "expp/app/notification_center.hpp"
#include "expp/app/explorer_services.hpp"
#include "expp/core/async_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace fs = std::filesystem;

namespace {

class StubFileSystemService final : public expp::app::ExplorerFileSystemService {
public:
    explicit StubFileSystemService(std::vector<expp::core::filesystem::FileEntry> initial_entries = {})
        : initialEntries_(std::move(initial_entries)) {}

    expp::core::Task<expp::core::Result<void>> streamDirectory(
        const expp::app::DirectoryListRequest& request,
        expp::app::DirectoryChunkHandler on_chunk) const override {
        on_chunk(expp::app::DirectoryListChunk{
            .entries = initialEntries_,
            .loadedEntries = initialEntries_.size(),
            .totalEntries = initialEntries_.size(),
            .hasMore = false,
        });
        co_return expp::core::Result<void>{};
    }

    expp::core::Task<expp::core::Result<expp::app::DirectoryListResult>> listDirectory(
        const expp::app::DirectoryListRequest& request) const override {
        (void)request;
        co_return expp::app::DirectoryListResult{
            .entries = initialEntries_,
            .totalEntries = initialEntries_.size(),
            .hasMore = false,
        };
    }

    expp::core::Task<expp::core::Result<fs::path>> canonicalize(const fs::path& path) const override {
        co_return path;
    }

    fs::path normalize(const fs::path& path) const override {
        return path.lexically_normal();
    }

    expp::core::Task<expp::core::VoidResult> createDirectory(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> createFile(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> rename(const fs::path& old_path, const fs::path& new_path) const override {
        (void)old_path;
        (void)new_path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> removeFile(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> removeDirectory(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> moveToTrash(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> openWithDefault(const fs::path& path) const override {
        (void)path;
        co_return expp::core::VoidResult{};
    }

    expp::core::Task<expp::core::VoidResult> copy(const fs::path& source,
                                                  const fs::path& destination,
                                                  bool overwrite) const override {
        (void)source;
        (void)destination;
        (void)overwrite;
        co_return expp::core::VoidResult{};
    }

private:
    std::vector<expp::core::filesystem::FileEntry> initialEntries_;
};

class RecordingMimeService final : public expp::app::ExplorerMimeService {
public:
    expp::core::Task<expp::core::Result<expp::app::MimePayload>> detectMime(
        const expp::app::MimeRequest& request) const override {
        {
            std::scoped_lock lock(mutex_);
            requested_.push_back(request.target);
        }
        co_return expp::app::MimePayload{
            .mimeType = "text/plain",
            .previewable = true,
        };
    }

    [[nodiscard]] std::vector<fs::path> requested() const {
        std::scoped_lock lock(mutex_);
        return requested_;
    }

private:
    mutable std::mutex mutex_;
    mutable std::vector<fs::path> requested_;
};

class PassThroughHighlightService final : public expp::app::ExplorerHighlightService {
public:
    expp::core::Task<expp::core::Result<expp::app::HighlightPayload>> highlight(
        const expp::app::HighlightRequest& request) const override {
        co_return expp::app::HighlightPayload{.lines = request.lines};
    }
};

class StubImageService final : public expp::app::ExplorerImageService {
public:
    expp::core::Task<expp::core::Result<expp::app::ImageInfo>> inspect(
        const expp::app::ImageRequest& request) const override {
        (void)request;
        co_return expp::core::make_error(expp::core::ErrorCategory::NoSupport, "not used");
    }
};

class StubClipboardService final : public expp::app::ExplorerClipboardService {
public:
    expp::core::Task<expp::core::VoidResult> copyText(std::string_view text) const override {
        (void)text;
        co_return expp::core::VoidResult{};
    }
};

class DelayedPreviewService final : public expp::app::ExplorerPreviewService {
public:
    explicit DelayedPreviewService(std::shared_ptr<expp::core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    expp::core::Task<expp::core::Result<expp::app::PreviewPayload>> loadPreview(
        const expp::app::PreviewRequest& request) const override {
        co_await expp::core::switch_to(runtime_->cpuExecutor());
        std::this_thread::sleep_for(request.target.filename() == fs::path{"slow.txt"} ? 40ms : 5ms);
        co_return expp::app::PreviewPayload{
            .lines = {request.target.filename().string()},
            .mimeType = "text/plain",
            .previewable = true,
        };
    }

private:
    std::shared_ptr<expp::core::AsioRuntime> runtime_;
};

[[nodiscard]] expp::app::ExplorerServices make_test_services(
    std::shared_ptr<expp::core::AsioRuntime> runtime,
    std::shared_ptr<expp::app::ExplorerFileSystemService> file_system,
    std::shared_ptr<expp::app::ExplorerPreviewService> preview,
    std::shared_ptr<expp::app::ExplorerMimeService> mime) {
    return expp::app::ExplorerServices{
        .runtime = std::move(runtime),
        .fileSystem = std::move(file_system),
        .preview = std::move(preview),
        .mime = std::move(mime),
        .highlight = std::make_shared<PassThroughHighlightService>(),
        .image = std::make_shared<StubImageService>(),
        .clipboard = std::make_shared<StubClipboardService>(),
    };
}

}  // namespace

TEST_CASE("ExplorerPreviewController ignores stale preview completions", "[app][async][preview]") {
    auto runtime = std::make_shared<expp::core::AsioRuntime>(1, 1);
    auto services = make_test_services(runtime, std::make_shared<StubFileSystemService>(),
                                       std::make_shared<DelayedPreviewService>(runtime),
                                       std::make_shared<RecordingMimeService>());

    auto explorer_result = expp::app::Explorer::create(fs::path{"preview-root"}, std::move(services));
    REQUIRE(explorer_result.has_value());

    expp::app::ExplorerPreviewController preview_controller(*explorer_result);
    preview_controller.sync(fs::path{"slow.txt"}, true);
    preview_controller.sync(fs::path{"fast.txt"}, true);

    for (int attempt = 0; attempt < 40; ++attempt) {
        std::this_thread::sleep_for(5ms);
        (void)runtime->mailbox().drain();
    }

    const auto* ready = std::get_if<expp::ui::PreviewReadyState>(&preview_controller.model());
    REQUIRE(ready != nullptr);
    CHECK(ready->target == fs::path{"fast.txt"});
    REQUIRE(ready->lines.size() == 1);
    CHECK(ready->lines.front() == "fast.txt");
}

TEST_CASE("ExplorerDirectoryController preloads MIME only for visible page plus one page radius",
          "[app][async][preload]") {
    auto runtime = std::make_shared<expp::core::AsioRuntime>(1, 1);
    auto mime = std::make_shared<RecordingMimeService>();
    auto services = make_test_services(runtime, std::make_shared<StubFileSystemService>(),
                                       std::make_shared<DelayedPreviewService>(runtime), mime);

    auto explorer_result = expp::app::Explorer::create(fs::path{"preload-root"}, std::move(services));
    REQUIRE(explorer_result.has_value());
    auto explorer = *explorer_result;

    std::vector<expp::core::filesystem::FileEntry> entries;
    entries.reserve(100);
    for (int index = 0; index < 100; ++index) {
        entries.push_back(expp::core::filesystem::FileEntry{
            .path = fs::path{"preload-root"} / std::format("file_{:03}.txt", index),
            .type = expp::core::filesystem::FileType::RegularFile,
        });
    }

    explorer->beginDirectoryListing(fs::path{"preload-root"}, 1);
    explorer->appendDirectoryChunk(entries, entries.size(), entries.size(), false, 1);
    explorer->completeDirectoryListing(1);
    explorer->setViewportRows(10);
    explorer->moveDown(20);

    expp::app::NotificationCenter notifications;
    expp::app::ExplorerDirectoryController controller(explorer, notifications);
    controller.updateViewportInterest();

    for (int attempt = 0; attempt < 20; ++attempt) {
        std::this_thread::sleep_for(5ms);
        (void)runtime->mailbox().drain();
    }

    const auto requested = mime->requested();
    REQUIRE_FALSE(requested.empty());

    const auto& state = explorer->state();
    const int offset = state.selection.currentScrollOffset;
    const int viewport = state.selection.currentViewportRows;
    const int expected_start = std::max(0, offset - viewport);
    const int expected_end = std::min(static_cast<int>(state.entries.size()), offset + viewport * 2);

    REQUIRE(static_cast<int>(requested.size()) == expected_end - expected_start);
    for (int index = expected_start; index < expected_end; ++index) {
        CHECK(requested[static_cast<std::size_t>(index - expected_start)] ==
              state.entries[static_cast<std::size_t>(index)].path);
    }
}
