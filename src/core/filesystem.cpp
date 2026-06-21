#include "expp/core/filesystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
// clang-format off
#include <windows.h>
    // clang-format on
    #undef max
    #undef min
#else
    #include <fcntl.h>
    #include <sys/stat.h>
#endif  // _WIN32
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>

#include <expp/core/error.hpp>
#include <malloc.h>

namespace expp::core::filesystem {
namespace rng = std::ranges;

// Static data for file classification
namespace {

// constexpr std::array kTextExtensions = {
//     ".txt",  ".cpp", ".c",    ".h",    ".hpp", ".py",  ".js",    ".ts",
//     ".json", ".xml", ".html", ".css",  ".md",  ".yml", ".yaml",  ".toml",
//     ".ini",  ".cfg", ".conf", ".sh",   ".bat", ".cmd", ".cmake", ".make",
//     ".log",  ".rs",  ".go",   ".java", ".lua", ".zig", ".swift", ".kt"};

constexpr std::array kArchiveExtensions = {".zip", ".tar", ".gz", ".7z",
                                           ".rar", ".bz2", ".xz", ".tgz"};

constexpr std::array kSourceExtensions = {".cpp", ".c",  ".h",  ".hpp", ".py",
                                          ".js",  ".ts", ".rs", ".go",  ".java"};

constexpr std::array kImageExtensions = {".jpg", ".jpeg", ".png",  ".gif",
                                         ".bmp", ".svg",  ".webp", ".ico"};

constexpr std::array kDocumentExtensions = {".pdf", ".doc", ".docx", ".odt",
                                            ".rtf", ".xls", ".xlsx"};

constexpr std::array kConfigExtensions = {".toml", ".yaml", ".yml", ".json",
                                          ".ini",  ".cfg",  ".conf"};

[[nodiscard]] std::string to_lower(std::string_view sv) {
    std::string result{sv};
    rng::transform(result, result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

template <typename Container>
[[nodiscard]] bool contains_extension(const Container& container, std::string_view ext) {
    return rng::find(container, ext) != container.end();
}

// /**
//  * @brief Converts a system clock time point to a file clock time point.
//  * @param system_time The system clock time point to convert.
//  * @return The equivalent time point in the file clock.
//  */
// [[nodiscard]] std::chrono::file_clock::time_point to_file_clock(
//     const std::chrono::system_clock::time_point& system_time) {
//     const auto now_sys = std::chrono::system_clock::now();
//     const auto now_file = std::chrono::file_clock::now();
//     return now_file + std::chrono::duration_cast<std::chrono::file_clock::duration>(system_time -
//     now_sys);
// }

//[[nodiscard]] std::chrono::file_clock::time_point query_birth_time(
//    const fs::path& path, std::chrono::file_clock::time_point fallback) {
// #ifdef _WIN32
//    WIN32_FILE_ATTRIBUTE_DATA attributes{};
//    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
//        return fallback;
//    }
//
//    ULARGE_INTEGER value{};
//    value.LowPart = attributes.ftCreationTime.dwLowDateTime;
//    value.HighPart = attributes.ftCreationTime.dwHighDateTime;
//
//    constexpr std::uint64_t kWindowsEpochToUnixEpoch100ns = 116444736000000000ULL;
//    if (value.QuadPart <= kWindowsEpochToUnixEpoch100ns) {
//        return fallback;
//    }
//
//    const auto unix_100ns = value.QuadPart - kWindowsEpochToUnixEpoch100ns;
//    const auto unix_duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
//        std::chrono::duration<std::int64_t, std::ratio<1,
//        10000000>>(static_cast<std::int64_t>(unix_100ns)));
//
//    return to_file_clock(std::chrono::system_clock::time_point(unix_duration));
// #else
//    (void)path;
//    return fallback;
//
// #endif
//}

/**
 * @brief Queries the birth (creation) time of a file or directory.
 * @param path The filesystem path to the file or directory whose birth time is to be queried.
 * @return A Result containing the file's creation time as a file_clock time_point on success, or an
 * error if the operation fails or birth time is not supported on the platform.
 */
[[nodiscard]] core::Result<std::chrono::file_clock::time_point> query_birth_time(
    const fs::path& path) {
    using namespace std::chrono;

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return core::make_error(
            ErrorCategory::FileSystem,
            std::format("Failed to get file attributes for '{}'", path.string()));
    }
    ULARGE_INTEGER value{};

    value.LowPart = attributes.ftCreationTime.dwLowDateTime;
    value.HighPart = attributes.ftCreationTime.dwHighDateTime;

    // FILETIME unit is 100 ns on Windows
    auto filetime_duration = duration<std::int64_t, std::ratio<1, 10'000'000>>(value.QuadPart);
    // Windows epoch starts on January 1, 1601, while Unix epoch starts on January 1, 1970
    auto win_epoch = sys_days(January / 1 / 1601);
    // get the system time on Unix
    auto sys_time = win_epoch + filetime_duration;

    // Convert to file_clock time point (using standard library)
    return clock_cast<file_clock>(sys_time);
#else
    struct statx stx;
    // linux 4.11+ introduce statx, support BTIME
    if (statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BTIME, &stx) == 0) {
        // currently

    #if defined(_LIBCPP_VERSION)
        // TODO: solve the problem in libc++ that missing clock_cast which cause the code below fail
        // to compile,
        if (stx.stx_mask & STATX_BTIME) {  // make sure it support BTIME
            auto duration = std::chrono::seconds(stx.stx_btime.tv_sec) +
                            std::chrono::nanoseconds(stx.stx_btime.tv_nsec);

            // Directly construct a file_clock time_point from the duration since epoch.
            // This avoids the missing clock_cast in libc++ entirely.
            return std::chrono::time_point<std::chrono::file_clock, std::chrono::nanoseconds>(
                duration);
        }
    #else
        // we just construct a file_clock time_point directly from the duration since epoch, which
        // is not elegant but works
        if (stx.stx_mask & STATX_BTIME) {  // make sure it support BTIME
            auto sys_time = system_clock::time_point(seconds(stx.stx_btime.tv_sec) +
                                                     nanoseconds(stx.stx_btime.tv_nsec));
            return clock_cast<file_clock>(sys_time);
        }
    #endif
    }
    // for old linux kernel or other unix-like system, we can only get the birth time by fallback to
    // last modified time, which is not accurate but better than nothing
    return core::make_error(ErrorCategory::NoSupport, "Birth time not supported yet");
#endif  // _WIN32
}

}  // anonymous namespace

// bool is_executable(const fs::path& filepath) noexcept {
//     try {
//         if (!fs::is_regular_file(filepath)) {
//             return false;
//         }

// #ifdef _WIN32
//         // Windows: Check extension against PATHEXT environment variable
//         // This is a common convention on Windows, but not foolproof. For a more robust solution,
//         you might need to use
//         // Windows API calls. But slower and more complex, so we stick to extension check for
//         simplicity. std::string ext = filepath.extension().string(); if (ext.empty()) {
//             return false;
//         }

//         std::ranges::transform(ext, ext.begin(), ::toupper);

//         size_t required_size{};
//         char* pathext_buffer = nullptr;
//         _dupenv_s(&pathext_buffer, &required_size, "PATHEXT");

//         std::string pathext = pathext_buffer ? pathext_buffer :
//         ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF"; if (pathext_buffer) {
//             free(pathext_buffer);  // NOLINT
//         }

//         size_t pos = pathext.find(ext);
//         while (pos != std::string::npos) {
//             bool start_valid = (pos == 0) || (pathext[pos - 1] == ';');
//             bool end_valid = (pos + ext.length() == pathext.length()) || (pathext[pos +
//             ext.length()] == ';'); if (start_valid && end_valid) {
//                 return true;
//             }
//             pos = pathext.find(ext, pos + 1);
//         }
//         return false;
// #else
//         // Linux/macOS: Rely on standard POSIX execute bits
//         auto perm = fs::status(filepath).permissions();
//         return (perm & fs::perms::owner_exec) != fs::perms::none || (perm &
//         fs::perms::group_exec) != fs::perms::none
//         ||
//                (perm & fs::perms::others_exec) != fs::perms::none;
// #endif
//     } catch (...) {
//         return false;
//     }
// }

bool is_executable(const fs::path& filepath) noexcept {
    std::error_code ec;

    if (!fs::is_regular_file(filepath, ec) || ec) {
        return false;
    }
#ifdef _WIN32
    // Windows: Check extension against PATHEXT environment variable
    // This is a common convention on Windows, but not foolproof. For a more robust solution, you
    // might need to use Windows API calls. But slower and more complex, so we stick to extension
    // check for simplicity.
    std::string ext = filepath.extension().string();
    if (ext.empty()) {
        return false;
    }

    std::ranges::transform(ext, ext.begin(), ::toupper);

    size_t required_size{};
    char* pathext_buffer = nullptr;
    _dupenv_s(&pathext_buffer, &required_size, "PATHEXT");

    std::string pathext = pathext_buffer ? pathext_buffer : ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF";
    if (pathext_buffer) {
        free(pathext_buffer);  // NOLINT
    }

    size_t pos = pathext.find(ext);
    while (pos != std::string::npos) {
        bool start_valid = (pos == 0) || (pathext[pos - 1] == ';');
        bool end_valid =
            (pos + ext.length() == pathext.length()) || (pathext[pos + ext.length()] == ';');
        if (start_valid && end_valid) {
            return true;
        }
        pos = pathext.find(ext, pos + 1);
    }
    return false;
#else
    // Linux/macOS: Rely on standard POSIX execute bits
    auto file_status = fs::status(filepath, ec);
    if (ec) {
        return false;
    }
    auto perm = file_status.permissions();
    return (perm & fs::perms::owner_exec) != fs::perms::none ||
           (perm & fs::perms::group_exec) != fs::perms::none ||
           (perm & fs::perms::others_exec) != fs::perms::none;
#endif
}

[[nodiscard]] FileType classify_file(const fs::directory_entry& entry) noexcept {
    //     try {
    //         if (entry.is_directory()) {
    //             return FileType::Directory;
    //         }
    //         if (entry.is_symlink()) {
    //             return FileType::Symlink;
    //         }
    //         if (!entry.is_regular_file()) {
    //             return FileType::Unknown;
    //         }

    //         auto ext = to_lower(entry.path().extension().string());

    //         // Check executable first (platform-specific)
    // #ifndef _WIN32
    //         if (is_executable(entry.path())) {
    //             return FileType::Executable;
    //         }
    // #else
    //         if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com" || ext == ".vbs"
    //         || ext == ".js" ||
    //             ext == ".wsf") {
    //             return FileType::Executable;
    //         }
    // #endif

    //         if (contains_extension(kArchiveExtensions, ext)) {
    //             return FileType::Archive;
    //         }
    //         if (contains_extension(kImageExtensions, ext)) {
    //             return FileType::Image;
    //         }
    //         if (contains_extension(kDocumentExtensions, ext)) {
    //             return FileType::Document;
    //         }
    //         if (contains_extension(kSourceExtensions, ext)) {
    //             return FileType::SourceCode;
    //         }
    //         if (contains_extension(kConfigExtensions, ext)) {
    //             return FileType::Config;
    //         }

    //         return FileType::RegularFile;
    //     } catch (...) {
    //         return FileType::Unknown;
    //     }

    std::error_code ec;

    fs::file_status st = entry.symlink_status(ec);

    if (ec) {
        return FileType::Unknown;
    }

    switch (st.type()) {
        case fs::file_type::directory:
            return FileType::Directory;
        case fs::file_type::symlink:
            return FileType::Symlink;
        case fs::file_type::regular:
            break;
        default:
            return FileType::Unknown;
    }

    auto ext = to_lower(entry.path().extension().string());
    // Check executable first (platform-specific)
#ifndef _WIN32
    if (is_executable(entry.path())) {
        return FileType::Executable;
    }
#else
    if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com" || ext == ".vbs" ||
        ext == ".js" || ext == ".wsf") {
        return FileType::Executable;
    }
#endif

    if (contains_extension(kArchiveExtensions, ext)) {
        return FileType::Archive;
    }
    if (contains_extension(kImageExtensions, ext)) {
        return FileType::Image;
    }
    if (contains_extension(kDocumentExtensions, ext)) {
        return FileType::Document;
    }
    if (contains_extension(kSourceExtensions, ext)) {
        return FileType::SourceCode;
    }
    if (contains_extension(kConfigExtensions, ext)) {
        return FileType::Config;
    }

    return FileType::RegularFile;
}

bool is_previewable(const fs::path& path) noexcept {
    return open_if_previewable(path).is_open();
}

std::ifstream open_if_previewable(const fs::path& path) noexcept {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    constexpr std::size_t kSampleSize = 512;
    std::array<char, kSampleSize> buffer{};
    file.read(buffer.data(), buffer.size());
    const std::streamsize bytes_read = file.gcount();

    if (bytes_read == 0) {
        file.clear();
        file.seekg(0);
        return file;
    }

    std::size_t text_chars = 0;
    std::size_t binary_chars = 0;

    for (std::size_t i = 0; i < static_cast<std::size_t>(bytes_read); ++i) {
        const auto byte = static_cast<unsigned char>(buffer[i]);

        // 空字节 → 二进制文件
        if (byte == 0x00) {
            return {};
        }

        if ((std::isprint(byte) != 0) || byte == '\n' || byte == '\r' || byte == '\t' ||
            byte == '\v' || byte == '\f') {
            ++text_chars;
        } else {
            ++binary_chars;
        }
    }

    // 可打印字符（含空白）占主导 → 文本文件
    if (binary_chars < text_chars) {
        file.clear();   // 清除可能因读取而设置的 eofbit
        file.seekg(0);  // 重置到开头，便于调用者完整读取
        return file;
    }
    return {};
}

[[nodiscard]] Result<FileEntry> inspect_directory_entry(const fs::directory_entry& entry) noexcept {
    // try {
    FileEntry file_entry;
    file_entry.path = entry.path();
    file_entry.type = classify_file(entry);

    const auto filename = entry.path().filename().string();
    file_entry.isHidden = !filename.empty() && filename[0] == '.';

    if (file_entry.isSymlink()) {
        std::error_code readlink_ec;
        file_entry.symlinkTarget = fs::read_symlink(file_entry.path, readlink_ec);
        if (readlink_ec) {
            file_entry.symlinkTarget.clear();
        }

        std::error_code status_ec;
        const auto symlink_status = fs::status(file_entry.path, status_ec);
        if (status_ec == std::errc::too_many_symbolic_link_levels) {
            file_entry.isRecursiveSymlink = true;
        } else if (status_ec || symlink_status.type() == fs::file_type::not_found) {
            file_entry.isBrokenSymlink = true;
        }
    }

    if (file_entry.type == FileType::RegularFile) {
        std::error_code size_ec;
        file_entry.size = entry.file_size(size_ec);
        if (size_ec) {
            file_entry.size = 0;
        }
    }

    std::error_code time_ec;
    file_entry.lastModified = entry.last_write_time(time_ec);
    if (time_ec) {
        file_entry.lastModified = fs::file_time_type::min();
    }

    auto birth_time_result = query_birth_time(file_entry.path);
    file_entry.birthTime = birth_time_result ? *birth_time_result : file_entry.lastModified;

    return file_entry;
    // } catch (...) {
    //     return make_error(ErrorCategory::FileSystem,
    //                       std::format("Failed to inspect directory entry '{}'",
    //                       entry.path().string()));
    // }
}

[[nodiscard]] Result<std::vector<FileEntry>> list_directory(const fs::path& dir,
                                                            bool include_hidden) noexcept {
    std::vector<FileEntry> entries;

    std::error_code ec;
    auto iter = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (ec == std::errc::permission_denied) {
            return make_error(ErrorCategory::Permission,
                              std::format("Cannot access directory: {}", dir.string()));
        }
        if (ec == std::errc::no_such_file_or_directory) {
            return make_error(ErrorCategory::NotFound,
                              std::format("Directory not found: {}", dir.string()));
        }
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot list directory: {}", ec.message()));
    }
    for (const auto& entry : iter) {
        auto filename = entry.path().filename().string();

        // Skip hidden files if not requested
        if (!include_hidden && !filename.empty() && filename[0] == '.') {
            continue;
        }

        auto entry_result = inspect_directory_entry(entry);
        if (!entry_result) {
            return std::unexpected(entry_result.error());
        }
        entries.push_back(std::move(*entry_result));
    }
    // Sort: directories first, then alphabetically
    rng::sort(entries, [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory() != b.isDirectory()) {
            return a.isDirectory() > b.isDirectory();
        }
        return a.filename() < b.filename();
    });

    return entries;
}

[[nodiscard]] Result<fs::path> canonicalize(const fs::path& path) {
    std::error_code ec;
    auto result = fs::canonical(path, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot canonicalize path: {}", ec.message()));
    }
    return result;
}

[[nodiscard]] fs::path normalize(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = fs::absolute(path, ec);
        if (ec || normalized.empty()) {
            normalized = path;
        }
    }
    normalized = normalized.lexically_normal();  // Remove redundant components
    normalized.make_preferred();                 // \\ on Windows and / on Unix
    return normalized;
}

[[nodiscard]] VoidResult create_directory(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot create directory: {}", ec.message()));
    }
    return {};
}

VoidResult create_file(const fs::path& path) {
    if (path.has_parent_path()) {
        auto create_parent_result = filesystem::create_directory(path.parent_path());
        if (!create_parent_result) {
            return make_error(ErrorCategory::FileSystem,
                              std::format("Cannot create parent directory: {}",
                                          create_parent_result.error().message()));
        }
    }
    std::ofstream file(path);
    if (!file) {
        return make_error(ErrorCategory::IO, std::format("Cannot create file: {}", path.string()));
    }
    return {};
}

[[nodiscard]] VoidResult rename(const fs::path& old_path, const fs::path& new_path) {
    std::error_code ec;
    fs::rename(old_path, new_path, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot rename: {}", ec.message()));
    }
    return {};
}

VoidResult remove_file(const fs::path& path) {
    std::error_code ec;
    if (!fs::remove(path, ec)) {
        if (ec) {
            return make_error(ErrorCategory::FileSystem,
                              std::format("Cannot remove file: {}", ec.message()));
        }
        return make_error(ErrorCategory::NotFound,
                          std::format("File not found: {}", path.string()));
    }
    return {};
}

VoidResult remove_directory(const fs::path& path) {
    std::error_code ec;
    auto removed = fs::remove_all(path, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot remove directory: {}", ec.message()));
    }
    if (removed == 0) {
        return make_error(ErrorCategory::NotFound,
                          std::format("Directory not found: {}", path.string()));
    }
    return {};
}

VoidResult move_to_trash(const fs::path& path) {
    if (!fs::exists(path)) {
        return make_error(ErrorCategory::NotFound,
                          std::format("Path not found: {}", path.string()));
    }

#ifdef _WIN32
    std::wstring double_null_path = path.wstring() + L'\0';
    SHFILEOPSTRUCTW file_op = {};
    file_op.wFunc = FO_DELETE;
    file_op.pFrom = double_null_path.c_str();
    file_op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    if (SHFileOperationW(&file_op) != 0 || file_op.fAnyOperationsAborted) {
        return make_error(ErrorCategory::System,
                          std::format("Failed to move to Recycle Bin: {}", path.string()));
    }
#else
    // Linux/macOS: Use gio trash
    std::string command = "gio trash \"" + path.string() + "\"";
    int result = std::system(command.c_str());  // NOLINT
    if (result != 0) {
        return make_error(ErrorCategory::System,
                          std::format("Failed to move to trash: {}", path.string()));
    }
#endif
    return {};
}

VoidResult open_with_default(const fs::path& path) {
    if (!fs::exists(path)) {
        return make_error(ErrorCategory::NotFound,
                          std::format("Path not found: {}", path.string()));
    }

    std::string cmd;
#ifdef _WIN32
    cmd = R"(start "" ")" + path.string() + "\"";
#elif __APPLE__
    cmd = "open \"" + path.string() + "\"";
#else
    cmd = "xdg-open \"" + path.string() + "\"";
#endif

    int result = std::system(cmd.c_str());  // NOLINT
    if (result != 0) {
        return make_error(
            ErrorCategory::System,
            std::format("Failed to open with default application: {}", path.string()));
    }
    return {};
}

// [[nodiscard]] Result<std::vector<std::string>> read_preview(const FileEntry& entry, int
// max_lines)
// {
//     if (entry.isDirectory())
//     {
//         std::vector<std::string> lines;
//         lines.emplace_back("[Directory Contents]");
//         lines.emplace_back("");

//         auto entries_result = list_directory(entry.path);
//         if (!entries_result) {
//             lines.push_back("[" + entries_result.error().message() + "]");
//             return lines;
//         }

//         for (const auto& e : *entries_result) {
//             if (static_cast<int>(lines.size()) >= max_lines) {
//                 break;
//             }
//             std::string prefix = e.isDirectory() ? "[D] " : "[F] ";
//             lines.push_back(prefix + e.filename());
//         }
//         return lines;
//     }

// }

// [[nodiscard]] Result<std::vector<std::string>> read_preview(const fs::path& path, int max_lines)
// {
//     std::vector<std::string> lines;

//     std::error_code symlink_ec;
//     if (fs::is_symlink(path, symlink_ec)) {
//         lines.emplace_back("[Symbolic Link]");

//         std::error_code target_ec;
//         const auto target = fs::read_symlink(path, target_ec);
//         if (!target_ec) {
//             lines.push_back("Target: " + target.string());
//         }

//         std::error_code status_ec;
//         const auto link_status = fs::status(path, status_ec);
//         if (status_ec == std::errc::too_many_symbolic_link_levels) {
//             lines.emplace_back("[Recursive symlink detected]");
//             return lines;
//         }
//         if (status_ec || link_status.type() == fs::file_type::not_found) {
//             lines.emplace_back("[Broken symlink]");
//             return lines;
//         }

//         lines.emplace_back("");
//     }

//     std::error_code directory_ec;
//     if (fs::is_directory(path, directory_ec)) {
//         lines.emplace_back("[Directory Contents]");
//         lines.emplace_back("");

//         auto entries_result = list_directory(path);
//         if (!entries_result) {
//             lines.push_back("[" + entries_result.error().message() + "]");
//             return lines;
//         }

//         for (const auto& entry : *entries_result) {
//             if (static_cast<int>(lines.size()) >= max_lines) {
//                 break;
//             }
//             std::string prefix = entry.isDirectory() ? "[D] " : "[F] ";
//             lines.push_back(prefix + entry.filename());
//         }
//         return lines;
//     }
//     if (directory_ec == std::errc::too_many_symbolic_link_levels) {
//         lines.emplace_back("[Recursive symlink detected]");
//         return lines;
//     }

//     std::error_code regular_file_ec;
//     if (!fs::is_regular_file(path, regular_file_ec)) {
//         if (regular_file_ec == std::errc::too_many_symbolic_link_levels) {
//             lines.emplace_back("[Recursive symlink detected]");
//             return lines;
//         }
//         lines.emplace_back("[Not a regular file]");
//         return lines;
//     }
//     auto preview_file = is_previewable_fs(path);
//     if (!preview_file) {
//         lines.emplace_back("[Binary or unsupported file]");
//         lines.emplace_back("");
//         std::error_code ec;
//         auto size = fs::file_size(path, ec);
//         lines.push_back("Size: " + format_file_size(size));
//         return lines;
//     }

//     std::string line;
//     while (std::getline(preview_file, line) && static_cast<int>(lines.size()) < max_lines) {
//         // Truncate long lines
//         constexpr size_t kMaxLineLength = 80;
//         if (line.length() > kMaxLineLength) {
//             line = line.substr(0, kMaxLineLength - 3) + "...";
//         }
//         lines.push_back(std::move(line));
//     }

//     if (lines.empty()) {
//         lines.emplace_back("[Empty file]");
//     }

//     return lines;
// }

Result<bool> read_preview(const fs::path& path,
                          std::vector<std::string>& out_lines,
                          int max_lines,
                          int max_line_length) {
    out_lines.clear();
    std::error_code ec;

    // one system call
    fs::file_status sym_status = fs::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Cannot access path: {}", ec.message()));
    }
    if (fs::is_symlink(sym_status)) {
        out_lines.emplace_back("[Symbolic Link]");
        const auto target = fs::read_symlink(path, ec);
        if (!ec) {
            out_lines.push_back("Target: " + target.string());
        }

        fs::file_status link_status = fs::status(path, ec);
        if (ec == std::errc::too_many_symbolic_link_levels) {
            out_lines.emplace_back("[Recursive symlink detected]");
            return false;
        }
        if (ec || link_status.type() == fs::file_type::not_found) {
            out_lines.emplace_back("[Broken symlink]");
            return false;
        }
    }

    // one system call
    fs::file_status status = fs::status(path, ec);
    if (ec == std::errc::too_many_symbolic_link_levels) {
        out_lines.emplace_back("[Recursive symlink detected]");
        return false;
    }
    // directory
    if (fs::is_directory(status)) {
        out_lines.emplace_back("[Directory Contents]");
        out_lines.emplace_back("");

        auto entries_result = list_directory(path);
        if (!entries_result) {
            out_lines.push_back("[" + entries_result.error().message() + "]");
            return false;
        }
        int count = static_cast<int>(out_lines.size());
        for (const auto& entry : *entries_result) {
            if (count >= max_lines) {
                break;
            }
            std::string prefix = entry.isDirectory() ? "[D]" : "[F]";
            out_lines.push_back(std::format("{} {}", prefix, entry.filename()));
            count++;
        }
        return false;
    }
    if (!fs::is_regular_file(status)) {
        out_lines.emplace_back("[Not a regular file]");
        return true;
    }

    auto preview_file = open_if_previewable(path);
    if (!preview_file.is_open()) {
        out_lines.emplace_back("[Binary or unsupported file]");
        out_lines.emplace_back("");

        auto size = fs::file_size(path, ec);
        if (!ec) {
            out_lines.push_back("Size: " + format_file_size(size));
        }
        return false;
    }

    std::string line;
    int current_lines = static_cast<int>(out_lines.size());

    // Clamp to leave room for the "..." suffix; values <= 0 are treated as 3.
    const auto line_limit = static_cast<std::size_t>(std::max(3, max_line_length));

    while (current_lines < max_lines && std::getline(preview_file, line)) {
        // fix the '\r' on windows
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.length() > line_limit) {
            line.resize(line_limit - 3);
            line += "...";
        }
        out_lines.push_back(std::move(line));
        current_lines++;
    }

    if (out_lines.empty()) {
        out_lines.emplace_back("[Empty file]");
    }

    return true;
}

[[nodiscard]] std::string format_file_size(std::uintmax_t bytes) {
    constexpr std::array kUnits = {"B", "KB", "MB", "GB", "TB"};
    constexpr double kThreshold = 1024.0;

    if (bytes == 0) {
        return "0 B";
    }
    std::size_t unit_index = 0;
    auto size = static_cast<double>(bytes);
    while (size >= kThreshold && unit_index < kUnits.size() - 1) {
        size /= kThreshold;
        ++unit_index;
    }
    char buffer[32];
    if (unit_index == 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", size, kUnits[unit_index]);  // NOLINT
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, kUnits[unit_index]);  // NOLINT
    }
    return {buffer};
}

}  // namespace expp::core::filesystem
