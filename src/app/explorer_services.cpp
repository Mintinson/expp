#include "expp/app/explorer_services.hpp"

#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#ifdef _WIN32
    #include <windows.h>
    #undef max
    #undef min
#endif

namespace expp::app {

namespace fs = std::filesystem;

namespace {

class DefaultFileSystemService final : public ExplorerFileSystemService {
public:
    [[nodiscard]] core::Result<DirectoryListResult> listDirectory(const DirectoryListRequest& request) const override {
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
            result.hasMore = false;
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

    [[nodiscard]] core::Result<fs::path> canonicalize(const fs::path& path) const override {
        return core::filesystem::canonicalize(path);
    }

    [[nodiscard]] fs::path normalize(const fs::path& path) const override { return core::filesystem::normalize(path); }

    [[nodiscard]] core::VoidResult createDirectory(const fs::path& path) const override {
        return core::filesystem::create_directory(path);
    }

    [[nodiscard]] core::VoidResult createFile(const fs::path& path) const override {
        return core::filesystem::create_file(path);
    }

    [[nodiscard]] core::VoidResult rename(const fs::path& old_path, const fs::path& new_path) const override {
        return core::filesystem::rename(old_path, new_path);
    }

    [[nodiscard]] core::VoidResult removeFile(const fs::path& path) const override {
        return core::filesystem::remove_file(path);
    }

    [[nodiscard]] core::VoidResult removeDirectory(const fs::path& path) const override {
        return core::filesystem::remove_directory(path);
    }

    [[nodiscard]] core::VoidResult moveToTrash(const fs::path& path) const override {
        return core::filesystem::move_to_trash(path);
    }

    [[nodiscard]] core::VoidResult openWithDefault(const fs::path& path) const override {
        return core::filesystem::open_with_default(path);
    }

    [[nodiscard]] core::VoidResult copy(const fs::path& source,
                                        const fs::path& destination,
                                        bool overwrite) const override {
        const bool is_directory = fs::is_directory(source);

        std::error_code ec;
        fs::copy_options options = fs::copy_options::copy_symlinks;
        if (is_directory) {
            options |= fs::copy_options::recursive;
        }
        options |= overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing;

        fs::copy(source, destination, options, ec);
        if (ec) {
            return core::make_error(core::ErrorCategory::FileSystem,
                                    std::format("Failed to copy to '{}': {}", destination.string(), ec.message()));
        }

        return {};
    }
};

class DefaultPreviewService final : public ExplorerPreviewService {
public:
    [[nodiscard]] core::Result<PreviewPayload> loadPreview(const PreviewRequest& request) const override {
        if (request.cancellation.isCancellationRequested()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled");
        }

        auto preview_result = core::filesystem::read_preview(request.target, request.maxLines);
        if (!preview_result) {
            return std::unexpected(preview_result.error());
        }

        if (request.cancellation.isCancellationRequested()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled");
        }

        return PreviewPayload{.lines = std::move(*preview_result)};
    }
};

class DefaultClipboardService final : public ExplorerClipboardService {
public:
    [[nodiscard]] core::VoidResult copyText(std::string_view text) const override {
#ifdef _WIN32
        const std::string text_string{text};
        const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text_string.c_str(), -1, nullptr, 0);
        if (wide_length <= 0) {
            return core::make_error(core::ErrorCategory::System, "Failed to prepare clipboard text");
        }

        std::wstring wide_text(static_cast<std::size_t>(wide_length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, text_string.c_str(), -1, wide_text.data(), wide_length) == 0) {
            return core::make_error(core::ErrorCategory::System, "Failed to convert clipboard text");
        }

        if (OpenClipboard(nullptr) == 0) {
            return core::make_error(core::ErrorCategory::System, "Failed to open system clipboard");
        }

        if (EmptyClipboard() == 0) {
            CloseClipboard();
            return core::make_error(core::ErrorCategory::System, "Failed to clear system clipboard");
        }

        const SIZE_T byte_count = static_cast<SIZE_T>(wide_text.size() * sizeof(wchar_t));
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
        if (memory == nullptr) {
            CloseClipboard();
            return core::make_error(core::ErrorCategory::System, "Failed to allocate clipboard memory");
        }

        void* destination = GlobalLock(memory);
        if (destination == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return core::make_error(core::ErrorCategory::System, "Failed to lock clipboard memory");
        }

        std::memcpy(destination, wide_text.data(), byte_count);
        GlobalUnlock(memory);

        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            GlobalFree(memory);
            CloseClipboard();
            return core::make_error(core::ErrorCategory::System, "Failed to set clipboard data");
        }

        CloseClipboard();
        return {};
#elif defined(__APPLE__)
        FILE* pipe = popen("pbcopy", "w");
        if (pipe == nullptr) {
            return core::make_error(core::ErrorCategory::System, "Failed to access system clipboard");
        }

        const std::size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
        const int close_result = pclose(pipe);
        if (written != text.size() || close_result != 0) {
            return core::make_error(core::ErrorCategory::System, "Failed to write to system clipboard");
        }

        return {};
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

            const std::size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
            const int close_result = pclose(pipe);
            if (written == text.size() && close_result == 0) {
                return {};
            }
        }

        return core::make_error(core::ErrorCategory::System,
                                "Failed to write to system clipboard (install wl-copy, xclip, or xsel)");
#endif
    }
};

}  // namespace

ExplorerServices make_default_explorer_services() {
    return ExplorerServices{
        .fileSystem = std::make_shared<DefaultFileSystemService>(),
        .preview = std::make_shared<DefaultPreviewService>(),
        .clipboard = std::make_shared<DefaultClipboardService>(),
    };
}

}  // namespace expp::app
