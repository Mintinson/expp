#include "expp/app/explorer_services.hpp"

#include "expp/core/config.hpp"

#include <asio/this_coro.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if EXPP_HAS_LIBMAGIC
    #include <magic.h>
#endif

#ifdef _WIN32
    #include <windows.h>
    #undef max
    #undef min
#endif

namespace expp::app {

namespace fs = std::filesystem;

namespace {



// Move the original directory listing logic to a blocking function that can be called on a background thread.
[[nodiscard]] core::Result<DirectoryListResult> slice_directory_result(const DirectoryListRequest& request) {
    if (request.cancellation.isCancellationRequested()) {
        return core::make_error(core::ErrorCategory::InvalidState, "Directory listing cancelled");
    }

    auto entries_result = core::filesystem::list_directory(request.directory, request.includeHidden);
    if (!entries_result) {
        return std::unexpected(entries_result.error());
    }

    const auto total_entries = entries_result->size();
    DirectoryListResult result{
        .entries = std::move(*entries_result),
        .totalEntries = total_entries,
        .hasMore = false,
    };

    if (request.offset >= result.entries.size()) {
        result.entries.clear();
        return result;
    }

    if (request.offset > 0 || request.limit > 0) {
        const auto begin = request.offset;
        const auto end =
            request.limit == 0 ? result.entries.size() : std::min(result.entries.size(), begin + request.limit);
        result.hasMore = end < result.entries.size();
        result.entries = {result.entries.begin() + static_cast<std::ptrdiff_t>(begin),
                          result.entries.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    return result;
}

[[nodiscard]] std::string lower_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

/**
 * @brief Quickly estimates the MIME type of a file based solely on its file extension.
 *
 * This function performs a fast, memory-only lookup using a predefined mapping of common 
 * extensions. It does not perform any file I/O. If the extension is unknown, it falls back
 * to basic text or binary stream classifications based on the `core::filesystem::is_previewable` heuristic.
 *
 * @param path The file path to analyze.
 * @return A string representing the guessed MIME type.
 * 
 * @note This is a heuristic approach. A file's extension does not guarantee its actual content.
 */
[[nodiscard]] std::string guess_mime_from_extension(const fs::path& path) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 15> kMimeMap{
        {
         {".txt", "text/plain"},
         {".md", "text/markdown"},
         {".toml", "application/toml"},
         {".json", "application/json"},
         {".xml", "application/xml"},
         {".yml", "application/yaml"},
         {".yaml", "application/yaml"},
         {".cpp", "text/x-c++src"},
         {".hpp", "text/x-c++hdr"},
         {".c", "text/x-csrc"},
         {".h", "text/x-chdr"},
         {".png", "image/png"},
         {".jpg", "image/jpeg"},
         {".jpeg", "image/jpeg"},
         {".gif", "image/gif"},
         }
    };

    const auto extension = lower_extension(path);
    if (const auto  it = std::ranges::find(kMimeMap, extension, &decltype(kMimeMap)::value_type::first);
        it != kMimeMap.end()) {
        return std::string{it->second};
    }

    if (core::filesystem::is_previewable(path)) {
        return "text/plain";
    }
    return "application/octet-stream";
}

/**
 * @brief Synchronously detects the MIME type of a file, potentially using deep content sniffing.
 *
 * This function determines the MIME type by first performing a fast extension-based guess. 
 * If compiled with libmagic support (`EXPP_HAS_LIBMAGIC`) and if content sniffing is enabled 
 * in the global configuration, it will proceed to read the file's magic numbers (binary headers) 
 * for an accurate detection, overriding the initial guess if necessary.
 *
 * @warning This function performs blocking disk I/O and should not be executed directly on the UI/IO threads.
 *
 * @param request The request object containing the target file path and a cancellation token.
 * 
 * @return A `core::Result` containing the `MimePayload` on success, or an error if the operation 
 *         was cancelled or failed.
 */
[[nodiscard]] core::Result<MimePayload> detect_mime_blocking(const MimeRequest& request) {
    if (request.cancellation.isCancellationRequested()) {
        return core::make_error(core::ErrorCategory::InvalidState, "MIME detection cancelled");
    }

    MimePayload payload{
        .mimeType = guess_mime_from_extension(request.target),
        .previewable = core::filesystem::is_previewable(request.target),
    };

#if EXPP_HAS_LIBMAGIC
    if (core::global_config().config().analysis.mimeSniffing) {
        magic_t magic = magic_open(MAGIC_MIME_TYPE);
        if (magic != nullptr) {
            if (magic_load(magic, nullptr) == 0) {
                if (const char* detected = magic_file(magic, request.target.string().c_str()); detected != nullptr) {
                    payload.mimeType = detected;
                    payload.previewable = payload.mimeType.starts_with("text/");
                }
            }
            magic_close(magic);
        }
    }
#endif

    return payload;
}

class DefaultFileSystemService final : public ExplorerFileSystemService {
public:
    explicit DefaultFileSystemService(std::shared_ptr<core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<void>> streamDirectory(const DirectoryListRequest& request,
                                                                 DirectoryChunkHandler on_chunk) const override {
        auto caller = co_await asio::this_coro::executor;
        co_await core::switch_to(runtime_->diskExecutor());

        std::error_code ec;
        auto iterator = fs::directory_iterator(request.directory, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            co_await core::switch_to(caller);
            if (ec == std::errc::permission_denied) {
                co_return core::make_error(core::ErrorCategory::Permission,
                                           std::format("Cannot access directory: {}", request.directory.string()));
            }
            if (ec == std::errc::no_such_file_or_directory) {
                co_return core::make_error(core::ErrorCategory::NotFound,
                                           std::format("Directory not found: {}", request.directory.string()));
            }
            co_return core::make_error(core::ErrorCategory::FileSystem,
                                       std::format("Cannot list directory: {}", ec.message()));
        }

        const std::size_t chunk_entries = std::max<std::size_t>(1, request.chunkEntries);
        std::vector<core::filesystem::FileEntry> chunk;
        chunk.reserve(chunk_entries);
        std::size_t loaded_entries = 0;

        for (const auto& entry : iterator) {
            if (request.cancellation.isCancellationRequested()) {
                co_await core::switch_to(caller);
                co_return core::make_error(core::ErrorCategory::InvalidState, "Directory listing cancelled");
            }

            const auto filename = entry.path().filename().string();
            if (!request.includeHidden && !filename.empty() && filename[0] == '.') {
                continue;
            }

            auto file_entry_result = core::filesystem::inspect_directory_entry(entry);
            if (!file_entry_result) {
                co_await core::switch_to(caller);
                co_return std::unexpected(file_entry_result.error());
            }

            chunk.push_back(std::move(*file_entry_result));
            ++loaded_entries;

            if (chunk.size() >= chunk_entries) {
                auto emitted = DirectoryListChunk{
                    .entries = std::move(chunk),
                    .loadedEntries = loaded_entries,
                    .totalEntries = loaded_entries,
                    .hasMore = true,
                };
                co_await core::switch_to(caller);
                on_chunk(std::move(emitted));
                co_await core::switch_to(runtime_->diskExecutor());
                chunk.clear();
                chunk.reserve(chunk_entries);
            }
        }

        auto emitted = DirectoryListChunk{
            .entries = std::move(chunk),
            .loadedEntries = loaded_entries,
            .totalEntries = loaded_entries,
            .hasMore = false,
        };
        co_await core::switch_to(caller);
        on_chunk(std::move(emitted));
        co_return core::Result<void>{};
    }

    [[nodiscard]] core::Task<core::Result<DirectoryListResult>> listDirectory(
        const DirectoryListRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request] { return slice_directory_result(request); });
    }

    [[nodiscard]] core::Task<core::Result<fs::path>> canonicalize(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [path] { return core::filesystem::canonicalize(path); });
    }

    [[nodiscard]] fs::path normalize(const fs::path& path) const override { return core::filesystem::normalize(path); }

    [[nodiscard]] core::Task<core::VoidResult> createDirectory(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                     [path] { return core::filesystem::create_directory(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> createFile(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [path] { return core::filesystem::create_file(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> rename(const fs::path& old_path,
                                                      const fs::path& new_path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                     [old_path, new_path] { return core::filesystem::rename(old_path, new_path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> removeFile(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [path] { return core::filesystem::remove_file(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> removeDirectory(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                     [path] { return core::filesystem::remove_directory(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> moveToTrash(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                     [path] { return core::filesystem::move_to_trash(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> openWithDefault(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                     [path] { return core::filesystem::open_with_default(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> copy(const fs::path& source,
                                                    const fs::path& destination,
                                                    bool overwrite) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [source, destination, overwrite] {
            const bool is_directory = fs::is_directory(source);

            std::error_code ec;
            fs::copy_options options = fs::copy_options::copy_symlinks;
            if (is_directory) {
                options |= fs::copy_options::recursive;
            }
            options |= overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing;

            fs::copy(source, destination, options, ec);
            if (ec) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::FileSystem,
                                     std::format("Failed to copy to '{}': {}", destination.string(), ec.message())));
            }
            return core::VoidResult{};
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultMimeService final : public ExplorerMimeService {
public:
    explicit DefaultMimeService(std::shared_ptr<core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<MimePayload>> detectMime(const MimeRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [request] { return detect_mime_blocking(request); });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultHighlightService final : public ExplorerHighlightService {
public:
    explicit DefaultHighlightService(std::shared_ptr<core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<HighlightPayload>> highlight(const HighlightRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<HighlightPayload>(
                    core::make_error(core::ErrorCategory::InvalidState, "Preview highlighting cancelled"));
            }

            return core::Result<HighlightPayload>(HighlightPayload{.lines = request.lines});
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultImageService final : public ExplorerImageService {
public:
    explicit DefaultImageService(std::shared_ptr<core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<ImageInfo>> inspect(const ImageRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<ImageInfo>(
                    core::make_error(core::ErrorCategory::InvalidState, "Image inspection cancelled"));
            }
            return core::Result<ImageInfo>(
                core::make_error(core::ErrorCategory::NoSupport, "Image inspection is not implemented yet"));
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultPreviewService final : public ExplorerPreviewService {
public:
    DefaultPreviewService(std::shared_ptr<core::AsioRuntime> runtime,
                          std::shared_ptr<ExplorerMimeService> mime_service,
                          std::shared_ptr<ExplorerHighlightService> highlight_service,
                          std::shared_ptr<ExplorerImageService> image_service)
        : runtime_(std::move(runtime))
        , mimeService_(std::move(mime_service))
        , highlightService_(std::move(highlight_service))
        , imageService_(std::move(image_service)) {}

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> loadPreview(const PreviewRequest& request) const override {
        if (request.cancellation.isCancellationRequested()) {
            co_return core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled");
        }

        auto preview_result = co_await core::invoke_on(runtime_->diskExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<std::vector<std::string>>(
                    core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled"));
            }
            return core::filesystem::read_preview(request.target, request.maxLines);
        });
        if (!preview_result) {
            co_return std::unexpected(preview_result.error());
        }

        PreviewPayload payload{
            .lines = std::move(*preview_result),
            .mimeType = guess_mime_from_extension(request.target),
            .previewable = core::filesystem::is_previewable(request.target),
        };

        if (mimeService_) {
            auto mime_result = co_await mimeService_->detectMime(MimeRequest{
                .target = request.target,
                .cancellation = request.cancellation,
            });
            if (mime_result) {
                payload.mimeType = mime_result->mimeType;
                payload.previewable = mime_result->previewable;
            }
        }

        if (payload.mimeType.starts_with("image/") && imageService_) {
            (void)co_await imageService_->inspect(ImageRequest{
                .target = request.target,
                .cancellation = request.cancellation,
            });
        }

        if (core::global_config().config().analysis.highlightPreviews && highlightService_) {
            auto highlight_result = co_await highlightService_->highlight(HighlightRequest{
                .target = request.target,
                .mimeType = payload.mimeType,
                .lines = payload.lines,
                .cancellation = request.cancellation,
            });
            if (highlight_result) {
                payload.lines = std::move(highlight_result->lines);
            }
        }

        co_return payload;
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
    std::shared_ptr<ExplorerMimeService> mimeService_;
    std::shared_ptr<ExplorerHighlightService> highlightService_;
    std::shared_ptr<ExplorerImageService> imageService_;
};

class DefaultClipboardService final : public ExplorerClipboardService {
public:
    explicit DefaultClipboardService(std::shared_ptr<core::AsioRuntime> runtime) : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::VoidResult> copyText(std::string_view text) const override {
        const std::string text_copy{text};
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [text_copy] {
#ifdef _WIN32
            const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text_copy.c_str(), -1, nullptr, 0);
            if (wide_length <= 0) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to prepare clipboard text"));
            }

            std::wstring wide_text(static_cast<std::size_t>(wide_length), L'\0');
            if (MultiByteToWideChar(CP_UTF8, 0, text_copy.c_str(), -1, wide_text.data(), wide_length) == 0) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to convert clipboard text"));
            }

            if (OpenClipboard(nullptr) == 0) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to open system clipboard"));
            }

            if (EmptyClipboard() == 0) {
                CloseClipboard();
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to clear system clipboard"));
            }

            const SIZE_T byte_count = static_cast<SIZE_T>(wide_text.size() * sizeof(wchar_t));
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
            if (memory == nullptr) {
                CloseClipboard();
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to allocate clipboard memory"));
            }

            void* destination = GlobalLock(memory);
            if (destination == nullptr) {
                GlobalFree(memory);
                CloseClipboard();
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to lock clipboard memory"));
            }

            std::memcpy(destination, wide_text.data(), byte_count);
            GlobalUnlock(memory);

            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
                GlobalFree(memory);
                CloseClipboard();
                return core::VoidResult(core::make_error(core::ErrorCategory::System, "Failed to set clipboard data"));
            }

            CloseClipboard();
            return core::VoidResult{};
#elif defined(__APPLE__)
            FILE* pipe = popen("pbcopy", "w");
            if (pipe == nullptr) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to access system clipboard"));
            }

            const std::size_t written = std::fwrite(text_copy.data(), 1, text_copy.size(), pipe);
            const int close_result = pclose(pipe);
            if (written != text_copy.size() || close_result != 0) {
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to write to system clipboard"));
            }
            return core::VoidResult{};
#else
            constexpr std::array<std::string_view, 3> kCommands = {
                "wl-copy",
                "xclip -selection clipboard",
                "xsel --clipboard --input",
            };

            for (std::string_view command : kCommands) {
                FILE* pipe = popen(std::string{command}.c_str(), "w");
                if (pipe == nullptr) {
                    continue;
                }

                const std::size_t written = std::fwrite(text_copy.data(), 1, text_copy.size(), pipe);
                const int close_result = pclose(pipe);
                if (written == text_copy.size() && close_result == 0) {
                    return core::VoidResult{};
                }
            }

            return core::VoidResult(core::make_error(
                core::ErrorCategory::System,
                "Failed to write to system clipboard (install wl-copy, xclip, or xsel)"));
#endif
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

}  // namespace

ExplorerServices make_default_explorer_services(std::shared_ptr<core::AsioRuntime> runtime) {
    if (!runtime) {
        const auto& config = core::global_config().config();
        runtime = std::make_shared<core::AsioRuntime>(config.runtime.ioThreads, config.runtime.cpuThreads);
    }

    auto shared_runtime = runtime;
    auto file_system = std::make_shared<DefaultFileSystemService>(shared_runtime);
    auto mime = std::make_shared<DefaultMimeService>(shared_runtime);
    auto highlight = std::make_shared<DefaultHighlightService>(shared_runtime);
    auto image = std::make_shared<DefaultImageService>(shared_runtime);
    auto preview = std::make_shared<DefaultPreviewService>(shared_runtime, mime, highlight, image);
    auto clipboard = std::make_shared<DefaultClipboardService>(shared_runtime);

    return ExplorerServices{
        .runtime = std::move(shared_runtime),
        .fileSystem = std::move(file_system),
        .preview = std::move(preview),
        .mime = std::move(mime),
        .highlight = std::move(highlight),
        .image = std::move(image),
        .clipboard = std::move(clipboard),
    };
}

}  // namespace expp::app
