#include "expp/core/filesystem.hpp"

#include <algorithm>
#include <array>
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
#endif
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <expp/core/error.hpp>
#include <malloc.h>

namespace expp::core::filesystem {
namespace rng = std::ranges;

// Static data for file classification
namespace {

constexpr std::array kTextExtensions = {".txt",  ".cpp",   ".c",    ".h",    ".hpp",  ".py", ".js",
                                        ".ts",   ".json",  ".xml",  ".html", ".css",  ".md", ".yml",
                                        ".yaml", ".toml",  ".ini",  ".cfg",  ".conf", ".sh", ".bat",
                                        ".cmd",  ".cmake", ".make", ".log",  ".rs",   ".go", ".java"};

constexpr std::array kArchiveExtensions = {".zip", ".tar", ".gz", ".7z", ".rar", ".bz2", ".xz", ".tgz"};

constexpr std::array kSourceExtensions = {".cpp", ".c", ".h", ".hpp", ".py", ".js", ".ts", ".rs", ".go", ".java"};

constexpr std::array kImageExtensions = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg", ".webp", ".ico"};

constexpr std::array kDocumentExtensions = {".pdf", ".doc", ".docx", ".odt", ".rtf", ".xls", ".xlsx"};

constexpr std::array kConfigExtensions = {".toml", ".yaml", ".yml", ".json", ".ini", ".cfg", ".conf"};

[[nodiscard]] std::string to_lower(std::string_view sv) {
    std::string result{sv};
    rng::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

template <typename Container>
[[nodiscard]] bool contains_extension(const Container& container, std::string_view ext) {
    return rng::find(container, ext) != container.end();
}

}  // anonymous namespace

bool is_executable(const fs::path& filepath) noexcept {
    try {
        if (!fs::is_regular_file(filepath)) {
            return false;
        }

#ifdef _WIN32
        // Windows: Check extension against PATHEXT environment variable
        // This is a common convention on Windows, but not foolproof. For a more robust solution, you might need to use
        // Windows API calls. But slower and more complex, so we stick to extension check for simplicity.
        std::string ext = filepath.extension().string();
        if (ext.empty()) {
            return false;
        }

        std::ranges::transform(ext, ext.begin(), ::toupper);

        size_t required_size;
        char* pathext_buffer = nullptr;
        _dupenv_s(&pathext_buffer, &required_size, "PATHEXT");

        std::string pathext = pathext_buffer ? pathext_buffer : ".COM;.EXE;.BAT;.CMD;.VBS;.JS;.WSF";
        if (pathext_buffer) {
            free(pathext_buffer);
        }

        size_t pos = pathext.find(ext);
        while (pos != std::string::npos) {
            bool start_valid = (pos == 0) || (pathext[pos - 1] == ';');
            bool end_valid = (pos + ext.length() == pathext.length()) || (pathext[pos + ext.length()] == ';');
            if (start_valid && end_valid) {
                return true;
            }
            pos = pathext.find(ext, pos + 1);
        }
        return false;
#else
        // Linux/macOS: Rely on standard POSIX execute bits
        auto perm = fs::status(filepath).permissions();
        return (perm & fs::perms::owner_exec) != fs::perms::none || (perm & fs::perms::group_exec) != fs::perms::none ||
               (perm & fs::perms::others_exec) != fs::perms::none;
#endif
    } catch (...) {
        return false;
    }
}

[[nodiscard]] FileType classify_file(const fs::directory_entry& entry) noexcept {
    try {
        if (entry.is_directory()) {
            return FileType::Directory;
        }
        if (entry.is_symlink()) {
            return FileType::Symlink;
        }
        if (!entry.is_regular_file()) {
            return FileType::Unknown;
        }

        auto ext = to_lower(entry.path().extension().string());

        // Check executable first (platform-specific)
#ifndef _WIN32
        if (is_executable(entry.path())) {
            return FileType::Executable;
        }
#else
        if (ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com" || ext == ".vbs" || ext == ".js" ||
            ext == ".wsf") {
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
    } catch (...) {
        return FileType::Unknown;
    }
}

bool is_previewable(const fs::path& path) noexcept {
    auto ext = to_lower(path.extension().string());
    return contains_extension(kTextExtensions, ext);
}

[[nodiscard]] Result<std::vector<FileEntry>> list_directory(const fs::path& dir, bool include_hidden) noexcept {
    std::vector<FileEntry> entries;

    std::error_code ec;
    auto iter = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (ec == std::errc::permission_denied) {
            return make_error(ErrorCategory::Permission, std::format("Cannot access directory: {}", dir.string()));
        }
        if (ec == std::errc::no_such_file_or_directory) {
            return make_error(ErrorCategory::NotFound, std::format("Directory not found: {}", dir.string()));
        }
        return make_error(ErrorCategory::FileSystem, std::format("Cannot list directory: {}", ec.message()));
    }
    for (const auto& entry : iter) {
        auto filename = entry.path().filename().string();

        // Skip hidden files if not requested
        if (!include_hidden && !filename.empty() && filename[0] == '.') {
            continue;
        }

        FileEntry fe;
        fe.path = entry.path();

        fe.type = classify_file(entry);
        if (fe.isSymlink()) {
            std::error_code readlink_ec;
            fe.symlinkTarget = fs::read_symlink(fe.path, readlink_ec);
            if (readlink_ec) {
                fe.symlinkTarget.clear();
            }

            std::error_code status_ec;
            const auto symlink_status = fs::status(fe.path, status_ec);
            if (status_ec == std::errc::too_many_symbolic_link_levels) {
                fe.isRecursiveSymlink = true;
            } else if (status_ec || symlink_status.type() == fs::file_type::not_found) {
                fe.isBrokenSymlink = true;
            }
        }

        fe.isHidden = !filename.empty() && filename[0] == '.';

        // Cache size (only for regular files)

        if (fe.type == FileType::RegularFile) {
            std::error_code size_ec;
            fe.size = entry.file_size(size_ec);
        }

        // Cache last modified time
        std::error_code time_ec;
        fe.lastModified = entry.last_write_time(time_ec);
        if (time_ec) {
            // if we can't get the last modified time, set it to epoch (or some sentinel value)
            fe.lastModified = fs::file_time_type::min();
        }

        entries.push_back(std::move(fe));
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
        return make_error(ErrorCategory::FileSystem, std::format("Cannot canonicalize path: {}", ec.message()));
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
        return make_error(ErrorCategory::FileSystem, std::format("Cannot create directory: {}", ec.message()));
    }
    return {};
}

VoidResult create_file(const fs::path& path) {
    if (path.has_parent_path()) {
        auto create_parent_result = filesystem::create_directory(path.parent_path());
        if (!create_parent_result) {
            return make_error(ErrorCategory::FileSystem, std::format("Cannot create parent directory: {}",
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
        return make_error(ErrorCategory::FileSystem, std::format("Cannot rename: {}", ec.message()));
    }
    return {};
}

VoidResult remove_file(const fs::path& path) {
    std::error_code ec;
    if (!fs::remove(path, ec)) {
        if (ec) {
            return make_error(ErrorCategory::FileSystem, std::format("Cannot remove file: {}", ec.message()));
        }
        return make_error(ErrorCategory::NotFound, std::format("File not found: {}", path.string()));
    }
    return {};
}

VoidResult remove_directory(const fs::path& path) {
    std::error_code ec;
    auto removed = fs::remove_all(path, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem, std::format("Cannot remove directory: {}", ec.message()));
    }
    if (removed == 0) {
        return make_error(ErrorCategory::NotFound, std::format("Directory not found: {}", path.string()));
    }
    return {};
}

VoidResult move_to_trash(const fs::path& path) {
    if (!fs::exists(path)) {
        return make_error(ErrorCategory::NotFound, std::format("Path not found: {}", path.string()));
    }

#ifdef _WIN32
    std::wstring doubleNullPath = path.wstring() + L'\0';
    SHFILEOPSTRUCTW fileOp = {};
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = doubleNullPath.c_str();
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

    if (SHFileOperationW(&fileOp) != 0 || fileOp.fAnyOperationsAborted) {
        return make_error(ErrorCategory::System, std::format("Failed to move to Recycle Bin: {}", path.string()));
    }
#else
    // Linux/macOS: Use gio trash
    std::string command = "gio trash \"" + path.string() + "\"";
    int result = std::system(command.c_str());  // NOLINT
    if (result != 0) {
        return make_error(ErrorCategory::System, std::format("Failed to move to trash: {}", path.string()));
    }
#endif
    return {};
}

VoidResult open_with_default(const fs::path& path) {
    if (!fs::exists(path)) {
        return make_error(ErrorCategory::NotFound, std::format("Path not found: {}", path.string()));
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
        return make_error(ErrorCategory::System,
                          std::format("Failed to open with default application: {}", path.string()));
    }
    return {};
}
// [[nodiscard]] Result<std::vector<std::string>> read_preview(const FileEntry& entry, int max_lines)
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
[[nodiscard]] Result<std::vector<std::string>> read_preview(const fs::path& path, int max_lines) {
    std::vector<std::string> lines;

    std::error_code symlink_ec;
    if (fs::is_symlink(path, symlink_ec)) {
        lines.emplace_back("[Symbolic Link]");

        std::error_code target_ec;
        const auto target = fs::read_symlink(path, target_ec);
        if (!target_ec) {
            lines.push_back("Target: " + target.string());
        }

        std::error_code status_ec;
        const auto link_status = fs::status(path, status_ec);
        if (status_ec == std::errc::too_many_symbolic_link_levels) {
            lines.emplace_back("[Recursive symlink detected]");
            return lines;
        }
        if (status_ec || link_status.type() == fs::file_type::not_found) {
            lines.emplace_back("[Broken symlink]");
            return lines;
        }

        lines.emplace_back("");
    }

    std::error_code directory_ec;
    if (fs::is_directory(path, directory_ec)) {
        lines.emplace_back("[Directory Contents]");
        lines.emplace_back("");

        auto entries_result = list_directory(path);
        if (!entries_result) {
            lines.push_back("[" + entries_result.error().message() + "]");
            return lines;
        }

        for (const auto& entry : *entries_result) {
            if (static_cast<int>(lines.size()) >= max_lines) {
                break;
            }
            std::string prefix = entry.isDirectory() ? "[D] " : "[F] ";
            lines.push_back(prefix + entry.filename());
        }
        return lines;
    }
    if (directory_ec == std::errc::too_many_symbolic_link_levels) {
        lines.emplace_back("[Recursive symlink detected]");
        return lines;
    }

    std::error_code regular_file_ec;
    if (!fs::is_regular_file(path, regular_file_ec)) {
        if (regular_file_ec == std::errc::too_many_symbolic_link_levels) {
            lines.emplace_back("[Recursive symlink detected]");
            return lines;
        }
        lines.emplace_back("[Not a regular file]");
        return lines;
    }

    if (!is_previewable(path)) {
        lines.emplace_back("[Binary or unsupported file]");
        lines.emplace_back("");
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        lines.push_back("Size: " + format_file_size(size));
        return lines;
    }

    std::ifstream file(path);
    if (!file) {
        return make_error(ErrorCategory::IO, std::format("Cannot read file: {}", path.string()));
    }

    std::string line;
    while (std::getline(file, line) && static_cast<int>(lines.size()) < max_lines) {
        // Truncate long lines
        constexpr size_t kMaxLineLength = 80;
        if (line.length() > kMaxLineLength) {
            line = line.substr(0, kMaxLineLength - 3) + "...";
        }
        lines.push_back(std::move(line));
    }

    if (lines.empty()) {
        lines.emplace_back("[Empty file]");
    }

    return lines;
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