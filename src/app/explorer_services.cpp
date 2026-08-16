#include "expp/app/explorer_services.hpp"

#include "expp/app/preview_provider.hpp"
#include "expp/app/tree_sitter_highlighter.hpp"
#include "expp/core/config.hpp"

#include <asio/this_coro.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if EXPP_HAS_LIBMAGIC
    #include <magic.h>
#endif

#if EXPP_HAS_LIBARCHIVE
    #include <archive.h>
    #include <archive_entry.h>
#endif

#ifdef _WIN32
    #include <windows.h>
    #undef max
    #undef min
#endif

namespace expp::app {

using namespace std::literals;

namespace fs = std::filesystem;

namespace {

// Move the original directory listing logic to a blocking function that can be called on a
// background thread.
[[nodiscard]] core::Result<DirectoryListResult> slice_directory_result(
    const DirectoryListRequest& request) {
    if (request.cancellation.isCancellationRequested()) {
        return core::make_error(core::ErrorCategory::InvalidState, "Directory listing cancelled");
    }

    auto entries_result =
        core::filesystem::list_directory(request.directory, request.includeHidden);
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
        const auto end = request.limit == 0
                             ? result.entries.size()
                             : std::min(result.entries.size(), begin + request.limit);
        result.hasMore = end < result.entries.size();
        result.entries = {result.entries.begin() + static_cast<std::ptrdiff_t>(begin),
                          result.entries.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    return result;
}

[[nodiscard]] bool bytes_start_with(std::span<const unsigned char> bytes,
                                    std::string_view signature) noexcept {
    if (bytes.size() < signature.size()) {
        return false;
    }
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (bytes[index] != static_cast<unsigned char>(signature[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool bytes_match(std::span<const unsigned char> bytes,
                               std::size_t offset,
                               std::string_view signature) noexcept {
    if (bytes.size() < offset + signature.size()) {
        return false;
    }
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (bytes[offset + index] != static_cast<unsigned char>(signature[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool looks_like_text(std::span<const unsigned char> bytes) noexcept {
    if (bytes.empty()) {
        return true;
    }

    std::size_t text_chars = 0;
    std::size_t control_chars = 0;
    for (const unsigned char byte : bytes) {
        if (byte == 0x00) {
            return false;
        }
        if ((std::isprint(byte) != 0) || byte == '\n' || byte == '\r' || byte == '\t' ||
            byte == '\v' || byte == '\f') {
            ++text_chars;
        } else {
            ++control_chars;
        }
    }
    return text_chars >= control_chars;
}

[[nodiscard]] core::Result<std::vector<unsigned char>> read_header_blocking(const fs::path& path,
                                                                            std::size_t max_bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::make_error(core::ErrorCategory::IO,
                                std::format("Cannot read header: {}", path.string()));
    }

    std::vector<unsigned char> header(max_bytes);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, file.gcount())));
    return header;
}

[[nodiscard]] MimePayload detect_mime_from_header(std::span<const unsigned char> header) {
    if (bytes_start_with(header, "\x7F"
                                 "ELF")) {
        return MimePayload{
            .mimeType = "application/x-elf",
            .description = "ELF executable/shared object",
            .previewable = false,
            .binary = true,
        };
    }
    if (bytes_start_with(header, "MZ")) {
        return MimePayload{
            .mimeType = "application/vnd.microsoft.portable-executable",
            .description = "PE/COFF executable",
            .previewable = false,
            .binary = true,
        };
    }
    if (bytes_start_with(header, "\x89"
                                 "PNG\r\n\x1A\n")) {
        return MimePayload{
            .mimeType = "image/png",
            .description = "PNG image",
            .previewable = true,
            .binary = true,
        };
    }
    if (header.size() >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return MimePayload{
            .mimeType = "image/jpeg",
            .description = "JPEG image",
            .previewable = true,
            .binary = true,
        };
    }
    if (bytes_start_with(header, "GIF87a") || bytes_start_with(header, "GIF89a")) {
        return MimePayload{
            .mimeType = "image/gif",
            .description = "GIF image",
            .previewable = true,
            .binary = true,
        };
    }
    if (bytes_start_with(header, "PK\x03\x04") || bytes_start_with(header, "PK\x05\x06") ||
        bytes_start_with(header, "PK\x07\x08")) {
        return MimePayload{
            .mimeType = "application/zip",
            .description = "ZIP archive",
            .previewable = true,
            .binary = true,
        };
    }
    if (header.size() >= 2 && header[0] == 0x1F && header[1] == 0x8B) {
        return MimePayload{
            .mimeType = "application/gzip",
            .description = "gzip compressed data",
            .previewable = true,
            .binary = true,
        };
    }
    if (bytes_match(header, 257, "ustar")) {
        return MimePayload{
            .mimeType = "application/x-tar",
            .description = "tar archive",
            .previewable = true,
            .binary = true,
        };
    }
    if (bytes_start_with(header, "%PDF-")) {
        return MimePayload{
            .mimeType = "application/pdf",
            .description = "PDF document",
            .previewable = false,
            .binary = true,
        };
    }
    if (looks_like_text(header)) {
        return MimePayload{
            .mimeType = "text/plain",
            .description = "plain text",
            .previewable = true,
            .binary = false,
        };
    }
    return MimePayload{
        .mimeType = "application/octet-stream",
        .description = "binary data",
        .previewable = false,
        .binary = true,
    };
}

[[nodiscard]] core::Result<MimePayload> detect_mime_blocking(const MimeRequest& request) {
    if (request.cancellation.isCancellationRequested()) {
        return core::make_error(core::ErrorCategory::InvalidState, "MIME detection cancelled");
    }

    std::error_code ec;
    const auto status = fs::status(request.target, ec);
    if (ec) {
        return core::make_error(core::ErrorCategory::FileSystem,
                                std::format("Cannot inspect MIME target '{}': {}",
                                            request.target.string(), ec.message()));
    }
    if (fs::is_directory(status)) {
        return MimePayload{
            .mimeType = "inode/directory",
            .description = "directory",
            .previewable = true,
            .binary = false,
        };
    }
    if (!fs::is_regular_file(status)) {
        return MimePayload{
            .mimeType = "application/octet-stream",
            .description = "non-regular filesystem entry",
            .previewable = false,
            .binary = true,
        };
    }

    const auto header_result =
        read_header_blocking(request.target, std::max<std::size_t>(1, request.headerBytes));
    if (!header_result) {
        return std::unexpected(header_result.error());
    }

    MimePayload payload = detect_mime_from_header(*header_result);

#if EXPP_HAS_LIBMAGIC
    if (core::global_config().config().analysis.mimeSniffing) {
        magic_t magic = magic_open(MAGIC_MIME_TYPE);
        if (magic != nullptr) {
            if (magic_load(magic, nullptr) == 0) {
                if (const char* detected =
                        magic_buffer(magic, header_result->data(), header_result->size());
                    detected != nullptr) {
                    payload.mimeType = detected;
                    payload.previewable = payload.mimeType.starts_with("text/");
                    payload.binary = !payload.previewable;
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
    explicit DefaultFileSystemService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<void>> streamDirectory(
        const DirectoryListRequest& request, DirectoryChunkHandler on_chunk) const override {
        auto caller = co_await asio::this_coro::executor;
        co_await core::switch_to(runtime_->diskExecutor());

        std::error_code ec;
        auto iterator = fs::directory_iterator(request.directory,
                                               fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            co_await core::switch_to(caller);
            if (ec == std::errc::permission_denied) {
                co_return core::make_error(
                    core::ErrorCategory::Permission,
                    std::format("Cannot access directory: {}", request.directory.string()));
            }
            if (ec == std::errc::no_such_file_or_directory) {
                co_return core::make_error(
                    core::ErrorCategory::NotFound,
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
                co_return core::make_error(core::ErrorCategory::InvalidState,
                                           "Directory listing cancelled");
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
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                           [request] { return slice_directory_result(request); });
    }

    [[nodiscard]] core::Task<core::Result<fs::path>> canonicalize(
        const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                           [path] { return core::filesystem::canonicalize(path); });
    }

    [[nodiscard]] fs::path normalize(const fs::path& path) const override {
        return core::filesystem::normalize(path);
    }

    [[nodiscard]] core::Task<core::VoidResult> createDirectory(
        const fs::path& path) const override {
        co_return co_await core::invoke_on(
            runtime_->diskExecutor(), [path] { return core::filesystem::create_directory(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> createFile(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                           [path] { return core::filesystem::create_file(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> rename(const fs::path& old_path,
                                                      const fs::path& new_path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [old_path, new_path] {
            return core::filesystem::rename(old_path, new_path);
        });
    }

    [[nodiscard]] core::Task<core::VoidResult> removeFile(const fs::path& path) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                           [path] { return core::filesystem::remove_file(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> removeDirectory(
        const fs::path& path) const override {
        co_return co_await core::invoke_on(
            runtime_->diskExecutor(), [path] { return core::filesystem::remove_directory(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> moveToTrash(const fs::path& path) const override {
        co_return co_await core::invoke_on(
            runtime_->diskExecutor(), [path] { return core::filesystem::move_to_trash(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> openWithDefault(
        const fs::path& path) const override {
        co_return co_await core::invoke_on(
            runtime_->diskExecutor(), [path] { return core::filesystem::open_with_default(path); });
    }

    [[nodiscard]] core::Task<core::VoidResult> copy(const fs::path& source,
                                                    const fs::path& destination,
                                                    bool overwrite) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [source, destination,
                                                                      overwrite] {
            const bool is_directory = fs::is_directory(source);

            std::error_code ec;
            fs::copy_options options = fs::copy_options::copy_symlinks;
            if (is_directory) {
                options |= fs::copy_options::recursive;
            }
            options |=
                overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing;

            fs::copy(source, destination, options, ec);
            if (ec) {
                return core::VoidResult(core::make_error(
                    core::ErrorCategory::FileSystem,
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
    explicit DefaultMimeService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<MimePayload>> detectMime(
        const MimeRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(),
                                           [request] { return detect_mime_blocking(request); });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultHighlightService final : public ExplorerHighlightService {
public:
    explicit DefaultHighlightService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<HighlightPayload>> highlight(
        const HighlightRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<HighlightPayload>(core::make_error(
                    core::ErrorCategory::InvalidState, "Preview highlighting cancelled"));
            }

            return core::Result<HighlightPayload>(HighlightPayload{.lines = request.lines});
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultImageService final : public ExplorerImageService {
public:
    explicit DefaultImageService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<ImageInfo>> inspect(
        const ImageRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<ImageInfo>(core::make_error(core::ErrorCategory::InvalidState,
                                                                "Image inspection cancelled"));
            }
            return core::Result<ImageInfo>(core::make_error(
                core::ErrorCategory::NoSupport, "Image inspection is not implemented yet"));
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class DefaultVersionControlService final : public ExplorerVersionControlService {
public:
    explicit DefaultVersionControlService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::Result<core::VersionStatusSnapshot>> loadStatus(
        const VersionStatusRequest& request) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<core::VersionStatusSnapshot>(core::make_error(
                    core::ErrorCategory::InvalidState, "Version status load cancelled"));
            }
            return core::load_git_status(request.directory);
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

[[nodiscard]] std::string sanitize_preview_line(std::string line, int max_line_length) {
    for (char& ch : line) {
        const auto byte = static_cast<unsigned char>(ch);
        if (ch == '\t') {
            continue;
        }
        if (byte < 0x20 || byte == 0x7F) {
            ch = ' ';
        }
    }

    const auto line_limit = static_cast<std::size_t>(std::max(3, max_line_length));
    if (line.size() > line_limit) {
        line.resize(line_limit - 3);
        line += "...";
    }
    return line;
}

[[nodiscard]] std::string permission_string(fs::perms permissions) {
    auto bit = [permissions](fs::perms value, char marker) {
        return (permissions & value) == fs::perms::none ? '-' : marker;
    };
    std::string result;
    result.reserve(9);
    result.push_back(bit(fs::perms::owner_read, 'r'));
    result.push_back(bit(fs::perms::owner_write, 'w'));
    result.push_back(bit(fs::perms::owner_exec, 'x'));
    result.push_back(bit(fs::perms::group_read, 'r'));
    result.push_back(bit(fs::perms::group_write, 'w'));
    result.push_back(bit(fs::perms::group_exec, 'x'));
    result.push_back(bit(fs::perms::others_read, 'r'));
    result.push_back(bit(fs::perms::others_write, 'w'));
    result.push_back(bit(fs::perms::others_exec, 'x'));
    return result;
}

[[nodiscard]] std::string modified_time_string(const fs::path& path) {
    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    if (ec) {
        return "unavailable";
    }

    const auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(modified);
    return std::format("{:%F %T}", std::chrono::floor<std::chrono::seconds>(sys_time));
}

[[nodiscard]] std::vector<std::string> metadata_lines_for(const fs::path& path,
                                                          const MimePayload& mime,
                                                          std::string_view diagnostic = {}) {
    std::vector<std::string> lines;
    lines.push_back("[Metadata]");
    lines.push_back("Type: " + mime.description);
    lines.push_back("MIME: " + mime.mimeType);
    if (!diagnostic.empty()) {
        lines.push_back("Note: " + std::string{diagnostic});
    }

    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (!ec) {
        lines.push_back("Permissions: " + permission_string(status.permissions()));
        lines.push_back("Modified: " + modified_time_string(path));
        if (fs::is_regular_file(status)) {
            const auto size = fs::file_size(path, ec);
            if (!ec) {
                lines.push_back("Size: " + core::filesystem::format_file_size(size));
            }
        }
    }
    return lines;
}

[[nodiscard]] std::uint16_t read_le16(std::span<const unsigned char> bytes,
                                      std::size_t offset) noexcept {
    if (bytes.size() < offset + 2) {
        return 0;
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

[[nodiscard]] std::uint32_t read_le32(std::span<const unsigned char> bytes,
                                      std::size_t offset) noexcept {
    if (bytes.size() < offset + 4) {
        return 0;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool read_file_chunk(const fs::path& path,
                                   std::uint64_t offset,
                                   std::size_t byte_count,
                                   std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        return false;
    }
    out.resize(byte_count);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    out.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, file.gcount())));
    return true;
}

[[nodiscard]] std::string base64_encode(std::span<const unsigned char> bytes) {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t octet_a = bytes[index];
        const std::uint32_t octet_b = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t octet_c = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        encoded.push_back(kAlphabet[(triple >> 18U) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 12U) & 0x3F]);
        encoded.push_back(index + 1 < bytes.size() ? kAlphabet[(triple >> 6U) & 0x3F] : '=');
        encoded.push_back(index + 2 < bytes.size() ? kAlphabet[triple & 0x3F] : '=');
    }
    return encoded;
}

[[nodiscard]] std::optional<ImageInfo> detect_image_dimensions(
    std::span<const unsigned char> header, std::string_view mime_type) {
    if (mime_type == "image/png" && header.size() >= 24) {
        const int width = static_cast<int>((static_cast<std::uint32_t>(header[16]) << 24U) |
                                           (static_cast<std::uint32_t>(header[17]) << 16U) |
                                           (static_cast<std::uint32_t>(header[18]) << 8U) |
                                           static_cast<std::uint32_t>(header[19]));
        const int height = static_cast<int>((static_cast<std::uint32_t>(header[20]) << 24U) |
                                            (static_cast<std::uint32_t>(header[21]) << 16U) |
                                            (static_cast<std::uint32_t>(header[22]) << 8U) |
                                            static_cast<std::uint32_t>(header[23]));
        return ImageInfo{.width = width, .height = height};
    }
    if (mime_type == "image/gif" && header.size() >= 10) {
        return ImageInfo{
            .width = static_cast<int>(read_le16(header, 6)),
            .height = static_cast<int>(read_le16(header, 8)),
        };
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> environment_value(const char* name) {
#ifdef _WIN32
    std::size_t size = 0;
    if (getenv_s(&size, nullptr, 0, name) != 0 || size == 0) {
        return std::nullopt;
    }
    std::string value(size, '\0');
    if (getenv_s(&size, value.data(), value.size(), name) != 0 || size == 0) {
        return std::nullopt;
    }
    value.resize(size - 1);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
#endif
}

[[nodiscard]] std::string terminal_image_protocol() {
#if EXPP_HAS_TERMINAL_IMAGES
    if (environment_value("KITTY_WINDOW_ID").has_value()) {
        return "kitty";
    }
    if (const auto program = environment_value("TERM_PROGRAM"); program == "iTerm.app") {
        return "iterm2";
    }
    if (const auto term = environment_value("TERM");
        term.has_value() && term->find("sixel") != std::string::npos) {
        return "sixel";
    }
#endif
    return {};
}

enum class PreviewLanguage : std::uint8_t {
    Plain,
    Cpp,
    CMake,
    Json,
    Config,
    GenericSource,
};

[[nodiscard]] std::string to_lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] bool equals_any(std::string_view value,
                              std::span<const std::string_view> candidates) noexcept {
    for (const auto candidate : candidates) {
        if (value == candidate) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_identifier_start(char ch) noexcept {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalpha(byte) != 0 || ch == '_';
}

[[nodiscard]] bool is_identifier_body(char ch) noexcept {
    const auto byte = static_cast<unsigned char>(ch);
    return std::isalnum(byte) != 0 || ch == '_';
}

[[nodiscard]] bool starts_with_at(std::string_view line,
                                  std::size_t offset,
                                  std::string_view value) noexcept {
    return offset <= line.size() && line.substr(offset).starts_with(value);
}

void append_fragment(std::vector<RichTextFragment>& fragments,
                     std::string_view text,
                     PreviewTextRole role) {
    if (text.empty()) {
        return;
    }
    if (!fragments.empty() && fragments.back().role == role) {
        fragments.back().text += text;
        return;
    }
    fragments.push_back(RichTextFragment{.text = std::string{text}, .role = role});
}

[[nodiscard]] PreviewTextRole classify_cpp_identifier(std::string_view word) noexcept {
    static constexpr std::array kKeywords{
        "alignas"sv,       "alignof"sv,   "and"sv,       "asm"sv,       "auto"sv,
        "bitand"sv,        "bitor"sv,     "break"sv,     "case"sv,      "catch"sv,
        "class"sv,         "co_await"sv,  "co_return"sv, "co_yield"sv,  "concept"sv,
        "const"sv,         "consteval"sv, "constexpr"sv, "constinit"sv, "continue"sv,
        "decltype"sv,      "default"sv,   "delete"sv,    "do"sv,        "else"sv,
        "enum"sv,          "explicit"sv,  "export"sv,    "extern"sv,    "final"sv,
        "for"sv,           "friend"sv,    "if"sv,        "import"sv,    "inline"sv,
        "mutable"sv,       "namespace"sv, "new"sv,       "noexcept"sv,  "not"sv,
        "operator"sv,      "or"sv,        "override"sv,  "private"sv,   "protected"sv,
        "public"sv,        "requires"sv,  "return"sv,    "sizeof"sv,    "static"sv,
        "static_assert"sv, "struct"sv,    "switch"sv,    "template"sv,  "this"sv,
        "thread_local"sv,  "throw"sv,     "try"sv,       "typedef"sv,   "typename"sv,
        "using"sv,         "virtual"sv,   "volatile"sv,  "while"sv,
    };
    static constexpr std::array kTypes{
        "bool"sv,     "char"sv,        "char8_t"sv, "char16_t"sv, "char32_t"sv, "double"sv,
        "float"sv,    "int"sv,         "long"sv,    "short"sv,    "signed"sv,   "std"sv,
        "string"sv,   "string_view"sv, "uint8_t"sv, "uint16_t"sv, "uint32_t"sv, "uint64_t"sv,
        "unsigned"sv, "void"sv,        "wchar_t"sv,
    };
    if (equals_any(word, kKeywords)) {
        return PreviewTextRole::Keyword;
    }
    if (equals_any(word, kTypes)) {
        return PreviewTextRole::Type;
    }
    return PreviewTextRole::Normal;
}

[[nodiscard]] PreviewTextRole classify_generic_identifier(std::string_view word) noexcept {
    static constexpr std::array kKeywords{
        "as"sv,      "async"sv,    "await"sv,     "break"sv,   "case"sv,   "catch"sv,    "class"sv,
        "const"sv,   "continue"sv, "def"sv,       "else"sv,    "enum"sv,   "export"sv,   "false"sv,
        "finally"sv, "fn"sv,       "for"sv,       "from"sv,    "func"sv,   "function"sv, "if"sv,
        "import"sv,  "in"sv,       "interface"sv, "let"sv,     "match"sv,  "module"sv,   "mut"sv,
        "nil"sv,     "none"sv,     "null"sv,      "package"sv, "return"sv, "self"sv,     "struct"sv,
        "switch"sv,  "throw"sv,    "true"sv,      "try"sv,     "type"sv,   "var"sv,      "while"sv,
    };
    return equals_any(word, kKeywords) ? PreviewTextRole::Keyword : PreviewTextRole::Normal;
}

[[nodiscard]] PreviewTextRole classify_config_identifier(std::string_view word) noexcept {
    static constexpr std::array kKeywords{
        "false"sv, "no"sv, "null"sv, "off"sv, "on"sv, "true"sv, "yes"sv,
    };
    return equals_any(to_lower_ascii(std::string{word}), kKeywords) ? PreviewTextRole::Keyword
                                                                    : PreviewTextRole::Normal;
}

[[nodiscard]] std::size_t quoted_end(std::string_view line, std::size_t start) noexcept {
    const char quote = line[start];
    std::size_t position = start + 1;
    bool escaped = false;
    while (position < line.size()) {
        const char ch = line[position++];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == quote) {
            break;
        }
    }
    return position;
}

[[nodiscard]] std::vector<RichTextFragment> highlight_cpp_line(std::string_view line,
                                                               bool& block_comment) {
    std::vector<RichTextFragment> fragments;
    std::size_t position = 0;
    const auto first = line.find_first_not_of(" \t");
    if (!block_comment && first != std::string_view::npos && line[first] == '#') {
        append_fragment(fragments, line.substr(0, first), PreviewTextRole::Normal);
        append_fragment(fragments, line.substr(first), PreviewTextRole::Keyword);
        return fragments;
    }

    while (position < line.size()) {
        if (block_comment) {
            const auto end = line.find("*/", position);
            if (end == std::string_view::npos) {
                append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
                return fragments;
            }
            append_fragment(fragments, line.substr(position, end + 2 - position),
                            PreviewTextRole::Comment);
            position = end + 2;
            block_comment = false;
            continue;
        }
        if (starts_with_at(line, position, "//")) {
            append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
            break;
        }
        if (starts_with_at(line, position, "/*")) {
            const auto end = line.find("*/", position + 2);
            if (end == std::string_view::npos) {
                append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
                block_comment = true;
                break;
            }
            append_fragment(fragments, line.substr(position, end + 2 - position),
                            PreviewTextRole::Comment);
            position = end + 2;
            continue;
        }
        if (line[position] == '"' || line[position] == '\'') {
            const auto end = quoted_end(line, position);
            append_fragment(fragments, line.substr(position, end - position),
                            PreviewTextRole::String);
            position = end;
            continue;
        }
        if (is_identifier_start(line[position])) {
            const auto start = position++;
            while (position < line.size() && is_identifier_body(line[position])) {
                ++position;
            }
            const auto word = line.substr(start, position - start);
            append_fragment(fragments, word, classify_cpp_identifier(word));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[position])) != 0) {
            const auto start = position++;
            while (position < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[position])) != 0 ||
                    line[position] == '.' || line[position] == '\'')) {
                ++position;
            }
            append_fragment(fragments, line.substr(start, position - start),
                            PreviewTextRole::Number);
            continue;
        }
        append_fragment(fragments, line.substr(position, 1), PreviewTextRole::Normal);
        ++position;
    }
    return fragments;
}

[[nodiscard]] std::vector<RichTextFragment> highlight_cmake_line(std::string_view line) {
    std::vector<RichTextFragment> fragments;
    std::size_t position = 0;
    while (position < line.size()) {
        if (line[position] == '#') {
            append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
            break;
        }
        if (line[position] == '"' || line[position] == '\'') {
            const auto end = quoted_end(line, position);
            append_fragment(fragments, line.substr(position, end - position),
                            PreviewTextRole::String);
            position = end;
            continue;
        }
        if (starts_with_at(line, position, "${")) {
            const auto end = line.find('}', position + 2);
            const auto count =
                end == std::string_view::npos ? line.size() - position : end + 1 - position;
            append_fragment(fragments, line.substr(position, count), PreviewTextRole::Type);
            position += count;
            continue;
        }
        if (is_identifier_start(line[position])) {
            const auto start = position++;
            while (position < line.size() &&
                   (is_identifier_body(line[position]) || line[position] == '-')) {
                ++position;
            }
            auto lookahead = position;
            while (lookahead < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[lookahead])) != 0) {
                ++lookahead;
            }
            const auto word = line.substr(start, position - start);
            const auto role = lookahead < line.size() && line[lookahead] == '('
                                  ? PreviewTextRole::Keyword
                                  : classify_config_identifier(word);
            append_fragment(fragments, word, role);
            continue;
        }
        append_fragment(fragments, line.substr(position, 1), PreviewTextRole::Normal);
        ++position;
    }
    return fragments;
}

[[nodiscard]] std::vector<RichTextFragment> highlight_json_line(std::string_view line) {
    std::vector<RichTextFragment> fragments;
    std::size_t position = 0;
    while (position < line.size()) {
        if (starts_with_at(line, position, "//")) {
            append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
            break;
        }
        if (line[position] == '"') {
            const auto end = quoted_end(line, position);
            append_fragment(fragments, line.substr(position, end - position),
                            PreviewTextRole::String);
            position = end;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[position])) != 0 ||
            line[position] == '-') {
            const auto start = position++;
            while (position < line.size() &&
                   (std::isdigit(static_cast<unsigned char>(line[position])) != 0 ||
                    line[position] == '.' || line[position] == 'e' || line[position] == 'E' ||
                    line[position] == '+' || line[position] == '-')) {
                ++position;
            }
            append_fragment(fragments, line.substr(start, position - start),
                            PreviewTextRole::Number);
            continue;
        }
        if (is_identifier_start(line[position])) {
            const auto start = position++;
            while (position < line.size() && is_identifier_body(line[position])) {
                ++position;
            }
            const auto word = line.substr(start, position - start);
            const auto lower = to_lower_ascii(std::string{word});
            static constexpr std::array kLiterals{"false"sv, "null"sv, "true"sv};
            append_fragment(fragments, word,
                            equals_any(lower, kLiterals) ? PreviewTextRole::Keyword
                                                         : PreviewTextRole::Normal);
            continue;
        }
        append_fragment(fragments, line.substr(position, 1), PreviewTextRole::Normal);
        ++position;
    }
    return fragments;
}

[[nodiscard]] bool assignment_follows(std::string_view line, std::size_t position) noexcept {
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position])) != 0) {
        ++position;
    }
    return position < line.size() && (line[position] == '=' || line[position] == ':');
}

[[nodiscard]] std::vector<RichTextFragment> highlight_config_line(std::string_view line) {
    std::vector<RichTextFragment> fragments;
    std::size_t position = 0;
    while (position < line.size()) {
        if (line[position] == '#' || line[position] == ';' ||
            starts_with_at(line, position, "//")) {
            append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
            break;
        }
        if (line[position] == '"' || line[position] == '\'') {
            const auto end = quoted_end(line, position);
            append_fragment(fragments, line.substr(position, end - position),
                            PreviewTextRole::String);
            position = end;
            continue;
        }
        if (is_identifier_start(line[position])) {
            const auto start = position++;
            while (position < line.size() && (is_identifier_body(line[position]) ||
                                              line[position] == '-' || line[position] == '.')) {
                ++position;
            }
            const auto word = line.substr(start, position - start);
            const auto role = assignment_follows(line, position) ? PreviewTextRole::Type
                                                                 : classify_config_identifier(word);
            append_fragment(fragments, word, role);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[position])) != 0 ||
            line[position] == '-') {
            const auto start = position++;
            while (position < line.size() &&
                   (std::isdigit(static_cast<unsigned char>(line[position])) != 0 ||
                    line[position] == '.')) {
                ++position;
            }
            append_fragment(fragments, line.substr(start, position - start),
                            PreviewTextRole::Number);
            continue;
        }
        append_fragment(fragments, line.substr(position, 1), PreviewTextRole::Normal);
        ++position;
    }
    return fragments;
}

[[nodiscard]] std::vector<RichTextFragment> highlight_generic_source_line(std::string_view line) {
    std::vector<RichTextFragment> fragments;
    std::size_t position = 0;
    while (position < line.size()) {
        if (line[position] == '#' || starts_with_at(line, position, "//")) {
            append_fragment(fragments, line.substr(position), PreviewTextRole::Comment);
            break;
        }
        if (line[position] == '"' || line[position] == '\'' || line[position] == '`') {
            const auto end = quoted_end(line, position);
            append_fragment(fragments, line.substr(position, end - position),
                            PreviewTextRole::String);
            position = end;
            continue;
        }
        if (is_identifier_start(line[position])) {
            const auto start = position++;
            while (position < line.size() && is_identifier_body(line[position])) {
                ++position;
            }
            const auto word = line.substr(start, position - start);
            append_fragment(fragments, word, classify_generic_identifier(word));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(line[position])) != 0) {
            const auto start = position++;
            while (position < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[position])) != 0 ||
                    line[position] == '.')) {
                ++position;
            }
            append_fragment(fragments, line.substr(start, position - start),
                            PreviewTextRole::Number);
            continue;
        }
        append_fragment(fragments, line.substr(position, 1), PreviewTextRole::Normal);
        ++position;
    }
    return fragments;
}

[[nodiscard]] PreviewLanguage infer_preview_language(const fs::path& path,
                                                     std::string_view mime_type) {
    const auto filename = to_lower_ascii(path.filename().string());
    const auto extension = to_lower_ascii(path.extension().string());
    if (filename == "cmakelists.txt" || extension == ".cmake") {
        return PreviewLanguage::CMake;
    }
    static constexpr std::array kCppExtensions{
        ".c"sv,   ".cc"sv,  ".cpp"sv, ".cxx"sv, ".h"sv,   ".hh"sv,
        ".hpp"sv, ".hxx"sv, ".ixx"sv, ".mpp"sv, ".ipp"sv,
    };
    if (equals_any(extension, kCppExtensions)) {
        return PreviewLanguage::Cpp;
    }
    if (mime_type == "application/json" || mime_type == "application/x-ndjson" ||
        extension == ".json" || extension == ".jsonc") {
        return PreviewLanguage::Json;
    }
    static constexpr std::array kConfigExtensions{
        ".cfg"sv, ".conf"sv,       ".editorconfig"sv, ".env"sv,  ".gitconfig"sv,
        ".ini"sv, ".properties"sv, ".toml"sv,         ".yaml"sv, ".yml"sv,
    };
    static constexpr std::array kConfigNames{
        ".gitignore"sv,  ".gitattributes"sv, ".npmrc"sv,     ".clang-format"sv,
        ".clang-tidy"sv, "makefile"sv,       "dockerfile"sv,
    };
    if (equals_any(extension, kConfigExtensions) || equals_any(filename, kConfigNames) ||
        mime_type == "application/toml" || mime_type == "application/x-yaml") {
        return PreviewLanguage::Config;
    }
    static constexpr std::array kSourceExtensions{
        ".cs"sv,  ".go"sv, ".java"sv, ".js"sv, ".jsx"sv, ".kt"sv, ".lua"sv, ".php"sv,
        ".ps1"sv, ".py"sv, ".rb"sv,   ".rs"sv, ".sh"sv,  ".ts"sv, ".tsx"sv,
    };
    if (equals_any(extension, kSourceExtensions)) {
        return PreviewLanguage::GenericSource;
    }
    return PreviewLanguage::Plain;
}

[[nodiscard]] PreviewContent make_text_preview_content(const PreviewRequest& request,
                                                       const MimePayload& mime,
                                                       const std::vector<std::string>& lines) {
    if (!core::global_config().config().preview.syntaxHighlight) {
        return PlainTextPreview{.lines = lines};
    }

    if (auto highlighted = highlight_with_tree_sitter(request.target, mime.mimeType, lines)) {
        return std::move(*highlighted);
    }

    const auto language = infer_preview_language(request.target, mime.mimeType);
    if (language == PreviewLanguage::Plain) {
        return PlainTextPreview{.lines = lines};
    }

    RichTextPreview rich;
    rich.lines.reserve(lines.size());
    bool cpp_block_comment = false;
    for (const auto& line : lines) {
        switch (language) {
            case PreviewLanguage::Cpp:
                rich.lines.push_back(highlight_cpp_line(line, cpp_block_comment));
                break;
            case PreviewLanguage::CMake:
                rich.lines.push_back(highlight_cmake_line(line));
                break;
            case PreviewLanguage::Json:
                rich.lines.push_back(highlight_json_line(line));
                break;
            case PreviewLanguage::Config:
                rich.lines.push_back(highlight_config_line(line));
                break;
            case PreviewLanguage::GenericSource:
                rich.lines.push_back(highlight_generic_source_line(line));
                break;
            case PreviewLanguage::Plain:
            default:
                rich.lines.push_back({RichTextFragment{.text = line}});
                break;
        }
    }
    return rich;
}

class TextPreviewProvider final : public PreviewProvider {
public:
    explicit TextPreviewProvider(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] std::span<const PreviewProviderCapability> capabilities()
        const noexcept override {
        static const std::array capabilities = {
            PreviewProviderCapability{.mimePattern = "text/*",                 .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/json",       .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/x-ndjson",   .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/toml",       .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/x-yaml",     .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/xml",        .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/javascript", .priority = 100},
        };
        return capabilities;
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request, mime] {
            if (request.cancellation.isCancellationRequested()) {
                return core::Result<PreviewPayload>(
                    core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled"));
            }

            std::ifstream file(request.target, std::ios::binary);
            if (!file) {
                return core::Result<PreviewPayload>(core::make_error(
                    core::ErrorCategory::IO,
                    std::format("Cannot read text preview: {}", request.target.string())));
            }

            std::string line;
            for (std::uint64_t row = 0; row < request.offset.row && std::getline(file, line);
                 ++row) {
                if (request.cancellation.isCancellationRequested()) {
                    return core::Result<PreviewPayload>(core::make_error(
                        core::ErrorCategory::InvalidState, "Preview load cancelled"));
                }
            }

            const int max_lines = std::max(1, request.chunkLines);
            const std::size_t max_bytes = std::max<std::size_t>(1, request.maxBytes);
            std::vector<std::string> lines;
            lines.reserve(static_cast<std::size_t>(max_lines));
            std::size_t bytes_read = 0;
            bool truncated = false;

            while (static_cast<int>(lines.size()) < max_lines && std::getline(file, line)) {
                if (request.cancellation.isCancellationRequested()) {
                    return core::Result<PreviewPayload>(core::make_error(
                        core::ErrorCategory::InvalidState, "Preview load cancelled"));
                }
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                bytes_read += line.size() + 1;
                if (bytes_read > max_bytes) {
                    truncated = true;
                    break;
                }
                lines.push_back(sanitize_preview_line(std::move(line), request.maxLineLength));
            }

            const bool can_scroll_down = !file.eof();
            if (lines.empty()) {
                lines.emplace_back("[Empty file]");
            }
            auto content = make_text_preview_content(request, mime, lines);

            return core::Result<PreviewPayload>(PreviewPayload{
                .lines = lines,
                .mimeType = mime.mimeType,
                .previewable = true,
                .content = std::move(content),
                .truncated = truncated,
                .canScrollUp = request.offset.row > 0,
                .canScrollDown = can_scroll_down || truncated,
                .diagnostic = {},
            });
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class ListingPreviewProvider final : public PreviewProvider {
public:
    explicit ListingPreviewProvider(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] std::span<const PreviewProviderCapability> capabilities()
        const noexcept override {
        static const std::array capabilities = {
            PreviewProviderCapability{.mimePattern = "inode/directory",   .priority = 100},
            PreviewProviderCapability{.mimePattern = "application/zip",   .priority = 95 },
            PreviewProviderCapability{.mimePattern = "application/x-tar", .priority = 95 },
            PreviewProviderCapability{.mimePattern = "application/gzip",  .priority = 95 },
        };
        return capabilities;
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request, mime] {
            if (mime.mimeType == "inode/directory") {
                return loadDirectory(request, mime);
            }
            if (mime.mimeType == "application/zip") {
                return loadZip(request, mime);
            }
#if EXPP_HAS_LIBARCHIVE
            if (mime.mimeType == "application/x-tar" || mime.mimeType == "application/gzip") {
                return loadLibarchive(request, mime);
            }
#endif
            return core::Result<PreviewPayload>(core::make_error(
                core::ErrorCategory::NoSupport,
                std::format("Archive listing is not available for {}", mime.mimeType)));
        });
    }

private:
#if EXPP_HAS_LIBARCHIVE
    struct ArchiveReaderDeleter {
        void operator()(archive* reader) const noexcept {
            if (reader != nullptr) {
                archive_read_free(reader);
            }
        }
    };

    [[nodiscard]] static core::Result<PreviewPayload> loadLibarchive(const PreviewRequest& request,
                                                                     const MimePayload& mime) {
        std::unique_ptr<archive, ArchiveReaderDeleter> reader{archive_read_new()};
        if (!reader) {
            return core::make_error(core::ErrorCategory::InvalidState,
                                    "Cannot initialize archive reader");
        }

        archive_read_support_filter_all(reader.get());
        archive_read_support_format_all(reader.get());
        if (archive_read_open_filename(reader.get(), request.target.string().c_str(), 10240) !=
            ARCHIVE_OK) {
            const char* reason = archive_error_string(reader.get());
            return core::make_error(
                core::ErrorCategory::IO,
                std::format("Cannot open archive '{}': {}", request.target.string(),
                            reason != nullptr ? reason : "unknown archive error"));
        }

        const auto config = core::global_config().config();
        const int max_entries = std::max(
            1, std::min(config.preview.maxArchiveEntries, std::max(1, request.chunkLines)));
        std::vector<std::string> entries{"[Archive Contents]"};
        std::uint64_t entry_index = 0;
        bool has_more = false;
        archive_entry* entry = nullptr;
        while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK) {
            if (request.cancellation.isCancellationRequested()) {
                return core::make_error(core::ErrorCategory::InvalidState,
                                        "Preview load cancelled");
            }
            if (entry_index++ < request.offset.row) {
                archive_read_data_skip(reader.get());
                continue;
            }
            if (static_cast<int>(entries.size()) > max_entries) {
                has_more = true;
                break;
            }

            const char* pathname = archive_entry_pathname_utf8(entry);
            if (pathname == nullptr) {
                pathname = archive_entry_pathname(entry);
            }
            const bool directory = archive_entry_filetype(entry) == AE_IFDIR;
            const auto entry_size = std::max<std::int64_t>(0, archive_entry_size(entry));
            entries.push_back(std::format(
                "{} {} ({})", directory ? "[D]" : "[F]",
                pathname != nullptr ? pathname : "[unnamed]",
                core::filesystem::format_file_size(static_cast<std::uintmax_t>(entry_size))));
            archive_read_data_skip(reader.get());
        }

        auto payload_lines = entries;
        return PreviewPayload{
            .lines = payload_lines,
            .mimeType = mime.mimeType,
            .previewable = true,
            .content = ListingPreview{.entries = std::move(entries), .tree = false},
            .truncated = has_more,
            .canScrollUp = request.offset.row > 0,
            .canScrollDown = has_more,
            .diagnostic = {},
        };
    }
#endif

    [[nodiscard]] static core::Result<PreviewPayload> loadDirectory(const PreviewRequest& request,
                                                                    const MimePayload& mime) {
        std::error_code ec;
        fs::directory_iterator iter(request.target, fs::directory_options::skip_permission_denied,
                                    ec);
        if (ec) {
            return core::make_error(core::ErrorCategory::FileSystem,
                                    std::format("Cannot list directory preview '{}': {}",
                                                request.target.string(), ec.message()));
        }

        std::vector<std::string> entries;
        entries.emplace_back("[Directory Contents]");
        const int max_entries = std::max(1, request.chunkLines);
        std::uint64_t skipped = 0;
        bool has_more = false;
        for (const auto& entry : iter) {
            if (request.cancellation.isCancellationRequested()) {
                return core::make_error(core::ErrorCategory::InvalidState,
                                        "Preview load cancelled");
            }
            if (skipped < request.offset.row) {
                ++skipped;
                continue;
            }
            if (static_cast<int>(entries.size()) > max_entries) {
                has_more = true;
                break;
            }
            const bool is_directory = entry.is_directory(ec);
            const std::string prefix = is_directory && !ec ? "[D] " : "[F] ";
            entries.push_back(prefix + entry.path().filename().string());
        }

        auto payload_lines = entries;
        return PreviewPayload{
            .lines = payload_lines,
            .mimeType = mime.mimeType,
            .previewable = true,
            .content = ListingPreview{.entries = std::move(entries), .tree = false},
            .canScrollUp = request.offset.row > 0,
            .canScrollDown = has_more,
            .diagnostic = {},
        };
    }

    [[nodiscard]] static core::Result<PreviewPayload> loadZip(const PreviewRequest& request,
                                                              const MimePayload& mime) {
        std::error_code ec;
        const auto file_size = fs::file_size(request.target, ec);
        if (ec) {
            return core::make_error(core::ErrorCategory::FileSystem,
                                    std::format("Cannot stat ZIP archive '{}': {}",
                                                request.target.string(), ec.message()));
        }

        constexpr std::size_t kMaxEocdSearch = 66'000;
        const auto tail_size =
            static_cast<std::size_t>(std::min<std::uintmax_t>(file_size, kMaxEocdSearch));
        std::vector<unsigned char> tail;
        if (!read_file_chunk(request.target, static_cast<std::uint64_t>(file_size - tail_size),
                             tail_size, tail)) {
            return core::make_error(
                core::ErrorCategory::IO,
                std::format("Cannot read ZIP archive: {}", request.target.string()));
        }

        std::optional<std::size_t> eocd_offset;
        for (std::size_t pos = tail.size(); pos >= 4; --pos) {
            const std::size_t index = pos - 4;
            if (read_le32(tail, index) == 0x06054B50U) {
                eocd_offset = index;
                break;
            }
        }
        if (!eocd_offset) {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    "ZIP central directory was not found");
        }

        const std::uint32_t central_size = read_le32(tail, *eocd_offset + 12);
        const std::uint32_t central_offset = read_le32(tail, *eocd_offset + 16);
        if (central_size == 0) {
            return PreviewPayload{
                .lines = {"[Archive Contents]", "[Empty ZIP archive]"},
                .mimeType = mime.mimeType,
                .previewable = true,
                .content =
                    ListingPreview{
                          .entries = {"[Archive Contents]", "[Empty ZIP archive]"},
                          .tree = false,
                          },
                .diagnostic = {},
            };
        }

        std::vector<unsigned char> central;
        if (!read_file_chunk(request.target, central_offset, central_size, central)) {
            return core::make_error(core::ErrorCategory::IO, "Cannot read ZIP central directory");
        }

        const auto cfg = core::global_config().config();
        const int max_entries =
            std::max(1, std::min(cfg.preview.maxArchiveEntries, std::max(1, request.chunkLines)));
        std::vector<std::string> entries;
        entries.emplace_back("[Archive Contents]");
        std::size_t pos = 0;
        std::uint64_t entry_index = 0;
        bool has_more = false;
        while (pos + 46 <= central.size() && read_le32(central, pos) == 0x02014B50U) {
            if (request.cancellation.isCancellationRequested()) {
                return core::make_error(core::ErrorCategory::InvalidState,
                                        "Preview load cancelled");
            }
            const std::uint16_t name_len = read_le16(central, pos + 28);
            const std::uint16_t extra_len = read_le16(central, pos + 30);
            const std::uint16_t comment_len = read_le16(central, pos + 32);
            const std::uint32_t uncompressed_size = read_le32(central, pos + 24);
            const std::size_t name_pos = pos + 46;
            if (name_pos + name_len > central.size()) {
                break;
            }

            if (entry_index >= request.offset.row) {
                if (static_cast<int>(entries.size()) > max_entries) {
                    has_more = true;
                    break;
                }
                std::string name{reinterpret_cast<const char*>(central.data() + name_pos),
                                 name_len};
                const bool directory = name.ends_with('/') || name.ends_with('\\');
                entries.push_back(
                    std::format("{} {} ({})", directory ? "[D]" : "[F]", name,
                                core::filesystem::format_file_size(uncompressed_size)));
            }
            ++entry_index;
            pos = name_pos + name_len + extra_len + comment_len;
        }

        auto payload_lines = entries;
        return PreviewPayload{
            .lines = payload_lines,
            .mimeType = mime.mimeType,
            .previewable = true,
            .content = ListingPreview{.entries = std::move(entries), .tree = false},
            .truncated = has_more,
            .canScrollUp = request.offset.row > 0,
            .canScrollDown = has_more,
            .diagnostic = {},
        };
    }

    std::shared_ptr<core::AsioRuntime> runtime_;
};

class ImagePreviewProvider final : public PreviewProvider {
public:
    explicit ImagePreviewProvider(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] std::span<const PreviewProviderCapability> capabilities()
        const noexcept override {
        static const std::array capabilities = {
            PreviewProviderCapability{.mimePattern = "image/*", .priority = 90},
        };
        return capabilities;
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request, mime] {
            const auto header_result = read_header_blocking(
                request.target, std::max<std::size_t>(512, request.headerBytes));
            if (!header_result) {
                return core::Result<PreviewPayload>(std::unexpected(header_result.error()));
            }

            const auto dimensions =
                detect_image_dimensions(*header_result, mime.mimeType).value_or(ImageInfo{});
            std::string protocol;
            std::string stream;
            bool inline_rendered = false;
            const auto cfg = core::global_config().config();

            if (cfg.preview.inlineImages) {
                protocol = terminal_image_protocol();
            }

#if EXPP_HAS_TERMINAL_IMAGES
            if (!protocol.empty()) {
                std::error_code ec;
                const auto size = fs::file_size(request.target, ec);
                if (!ec && size <= request.maxBytes) {
                    std::vector<unsigned char> image_data;
                    if (read_file_chunk(request.target, 0, static_cast<std::size_t>(size),
                                        image_data)) {
                        const auto encoded = base64_encode(image_data);
                        if (protocol == "kitty") {
                            stream = std::format("\x1b_Gf=100,a=T,c={},r={};{}\x1b\\",
                                                 std::max(1, request.viewport.width),
                                                 std::max(1, request.viewport.height), encoded);
                            inline_rendered = true;
                        } else if (protocol == "iterm2") {
                            stream = std::format("\x1b]1337;File=inline=1;width={}px;height={}px;"
                                                 "preserveAspectRatio=1:{}\a",
                                                 std::max(1, request.viewport.width),
                                                 std::max(1, request.viewport.height), encoded);
                            inline_rendered = true;
                        }
                    }
                }
            }
#endif

            std::vector<std::string> lines{
                "[Image]",
                "MIME: " + mime.mimeType,
                std::format("Dimensions: {}x{}", dimensions.width, dimensions.height),
            };
            if (!inline_rendered) {
                lines.emplace_back("Inline preview: unavailable");
            }

            return core::Result<PreviewPayload>(PreviewPayload{
                .lines = lines,
                .mimeType = mime.mimeType,
                .previewable = true,
                .content =
                    ImagePreview{
                                 .protocol = std::move(protocol),
                                 .escapeStream = std::move(stream),
                                 .width = dimensions.width,
                                 .height = dimensions.height,
                                 .renderedInline = inline_rendered,
                                 },
                .diagnostic =
                    inline_rendered ? std::string{}
                    : "terminal image protocol unavailable",
            });
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class HexDumpPreviewProvider final : public PreviewProvider {
public:
    explicit HexDumpPreviewProvider(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] std::span<const PreviewProviderCapability> capabilities()
        const noexcept override {
        static const std::array capabilities = {
            PreviewProviderCapability{.mimePattern = "application/octet-stream",                      .priority = 20},
            PreviewProviderCapability{.mimePattern = "application/x-elf",                             .priority = 20},
            PreviewProviderCapability{
                                      .mimePattern = "application/vnd.microsoft.portable-executable", .priority = 20},
            PreviewProviderCapability{.mimePattern = "application/pdf",                               .priority = 20},
        };
        return capabilities;
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request, mime] {
            const int bytes_per_line = request.viewport.width >= 96 ? 16 : 8;
            const int lines_to_read = std::max(1, request.chunkLines);
            const std::size_t bytes_to_read = std::min(
                request.maxBytes, static_cast<std::size_t>(bytes_per_line * lines_to_read));
            const std::uint64_t offset = request.offset.byte;

            std::vector<unsigned char> bytes;
            if (!read_file_chunk(request.target, offset, bytes_to_read, bytes)) {
                return core::Result<PreviewPayload>(core::make_error(
                    core::ErrorCategory::IO,
                    std::format("Cannot read hex preview: {}", request.target.string())));
            }

            std::vector<std::string> lines;
            lines.reserve((bytes.size() / static_cast<std::size_t>(bytes_per_line)) + 1);
            for (std::size_t index = 0; index < bytes.size();
                 index += static_cast<std::size_t>(bytes_per_line)) {
                std::ostringstream row;
                row << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
                    << (offset + index) << "  ";

                std::string ascii;
                for (int byte_index = 0; byte_index < bytes_per_line; ++byte_index) {
                    const std::size_t byte_pos = index + static_cast<std::size_t>(byte_index);
                    if (byte_pos < bytes.size()) {
                        const auto byte = bytes[byte_pos];
                        row << std::setw(2) << static_cast<int>(byte) << ' ';
                        ascii.push_back(std::isprint(byte) != 0 ? static_cast<char>(byte) : '.');
                    } else {
                        row << "   ";
                        ascii.push_back(' ');
                    }
                }
                row << " |" << ascii << '|';
                lines.push_back(row.str());
            }

            std::error_code ec;
            const auto size = fs::file_size(request.target, ec);
            const bool can_scroll_down = !ec && offset + bytes.size() < size;
            auto payload_lines = lines;
            return core::Result<PreviewPayload>(PreviewPayload{
                .lines = payload_lines,
                .mimeType = mime.mimeType,
                .previewable = true,
                .content = HexDumpPreview{.lines = std::move(lines), .baseOffset = offset},
                .canScrollUp = offset > 0,
                .canScrollDown = can_scroll_down,
                .diagnostic = {},
            });
        });
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
};

class MetadataPreviewProvider final : public PreviewProvider {
public:
    explicit MetadataPreviewProvider(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] std::span<const PreviewProviderCapability> capabilities()
        const noexcept override {
        static const std::array capabilities = {
            PreviewProviderCapability{.mimePattern = "*/*", .priority = 0},
        };
        return capabilities;
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> load(
        const PreviewRequest& request, const MimePayload& mime) const override {
        co_return co_await core::invoke_on(runtime_->diskExecutor(), [request, mime] {
            auto lines = metadata_lines_for(request.target, mime);
            auto payload_lines = lines;
            return core::Result<PreviewPayload>(PreviewPayload{
                .lines = payload_lines,
                .mimeType = mime.mimeType,
                .previewable = true,
                .content = MetadataPreview{.lines = std::move(lines)},
                .diagnostic = {},
            });
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
        , mimeService_(std::move(mime_service)) {
        (void)highlight_service;
        (void)image_service;

        fallbackProvider_ = std::make_shared<MetadataPreviewProvider>(runtime_);
        registry_.registerProvider(std::make_shared<TextPreviewProvider>(runtime_));
        registry_.registerProvider(std::make_shared<ListingPreviewProvider>(runtime_));
        registry_.registerProvider(std::make_shared<ImagePreviewProvider>(runtime_));
        registry_.registerProvider(std::make_shared<HexDumpPreviewProvider>(runtime_));
        registry_.registerProvider(fallbackProvider_);
    }

    [[nodiscard]] core::Task<core::Result<PreviewPayload>> loadPreview(
        const PreviewRequest& request) const override {
        if (request.cancellation.isCancellationRequested()) {
            co_return core::make_error(core::ErrorCategory::InvalidState, "Preview load cancelled");
        }

        MimePayload mime{
            .mimeType = "application/octet-stream",
            .description = "binary data",
            .previewable = false,
            .binary = true,
        };
        if (mimeService_) {
            auto mime_result = co_await mimeService_->detectMime(MimeRequest{
                .target = request.target,
                .headerBytes = request.headerBytes,
                .cancellation = request.cancellation,
            });
            if (mime_result) {
                mime = std::move(*mime_result);
            } else {
                co_return std::unexpected(mime_result.error());
            }
        }

        const auto provider = registry_.findProvider(mime.mimeType);
        if (!provider) {
            co_return core::make_error(core::ErrorCategory::NoSupport,
                                       std::format("No preview provider for {}", mime.mimeType));
        }

        auto result = co_await provider->load(request, mime);
        if (result) {
            co_return result;
        }

        if (provider == fallbackProvider_) {
            co_return std::unexpected(result.error());
        }

        auto fallback = co_await fallbackProvider_->load(request, mime);
        if (!fallback) {
            co_return std::unexpected(fallback.error());
        }
        fallback->diagnostic = result.error().message();
        fallback->lines.push_back("Note: " + result.error().message());
        if (auto* metadata = std::get_if<MetadataPreview>(&fallback->content)) {
            metadata->lines.push_back("Note: " + result.error().message());
        }
        co_return fallback;
    }

private:
    std::shared_ptr<core::AsioRuntime> runtime_;
    std::shared_ptr<ExplorerMimeService> mimeService_;
    PreviewProviderRegistry registry_;
    std::shared_ptr<PreviewProvider> fallbackProvider_;
};

class DefaultClipboardService final : public ExplorerClipboardService {
public:
    explicit DefaultClipboardService(std::shared_ptr<core::AsioRuntime> runtime)
        : runtime_(std::move(runtime)) {}

    [[nodiscard]] core::Task<core::VoidResult> copyText(std::string_view text) const override {
        const std::string text_copy{text};
        co_return co_await core::invoke_on(runtime_->cpuExecutor(), [text_copy] {
#ifdef _WIN32
            const int wide_length =
                MultiByteToWideChar(CP_UTF8, 0, text_copy.c_str(), -1, nullptr, 0);
            if (wide_length <= 0) {
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to prepare clipboard text"));
            }

            std::wstring wide_text(static_cast<std::size_t>(wide_length), L'\0');
            if (MultiByteToWideChar(CP_UTF8, 0, text_copy.c_str(), -1, wide_text.data(),
                                    wide_length) == 0) {
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to convert clipboard text"));
            }

            if (OpenClipboard(nullptr) == 0) {
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to open system clipboard"));
            }

            if (EmptyClipboard() == 0) {
                CloseClipboard();
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to clear system clipboard"));
            }

            const SIZE_T byte_count = static_cast<SIZE_T>(wide_text.size() * sizeof(wchar_t));
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
            if (memory == nullptr) {
                CloseClipboard();
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to allocate clipboard memory"));
            }

            void* destination = GlobalLock(memory);
            if (destination == nullptr) {
                GlobalFree(memory);
                CloseClipboard();
                return core::VoidResult(core::make_error(core::ErrorCategory::System,
                                                         "Failed to lock clipboard memory"));
            }

            std::memcpy(destination, wide_text.data(), byte_count);
            GlobalUnlock(memory);

            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
                GlobalFree(memory);
                CloseClipboard();
                return core::VoidResult(
                    core::make_error(core::ErrorCategory::System, "Failed to set clipboard data"));
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
        runtime = std::make_shared<core::AsioRuntime>(config.runtime.ioThreads,
                                                      config.runtime.cpuThreads);
    }

    auto shared_runtime = runtime;
    auto file_system = std::make_shared<DefaultFileSystemService>(shared_runtime);
    auto mime = std::make_shared<DefaultMimeService>(shared_runtime);
    auto highlight = std::make_shared<DefaultHighlightService>(shared_runtime);
    auto image = std::make_shared<DefaultImageService>(shared_runtime);
    auto version_control = std::make_shared<DefaultVersionControlService>(shared_runtime);
    auto preview = std::make_shared<DefaultPreviewService>(shared_runtime, mime, highlight, image);
    auto clipboard = std::make_shared<DefaultClipboardService>(shared_runtime);

    return ExplorerServices{
        .runtime = std::move(shared_runtime),
        .fileSystem = std::move(file_system),
        .preview = std::move(preview),
        .mime = std::move(mime),
        .highlight = std::move(highlight),
        .image = std::move(image),
        .versionControl = std::move(version_control),
        .clipboard = std::move(clipboard),
    };
}

}  // namespace expp::app
