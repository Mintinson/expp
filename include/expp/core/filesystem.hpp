/**
 * @file filesystem.hpp
 * @brief Cross-platform filesystem abstraction layer
 *
 * This module provides:
 * - Safe filesystem operations with Result<T>
 * - File type detection and classification
 * - Permission checking
 * - Directory traversal utilities
 *
 * Thread-safety: Functions are thread-safe unless otherwise noted.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_CORE_FILESYSTEM_HPP
#define EXPP_CORE_FILESYSTEM_HPP

#include "expp/core/error.hpp"

#include <bitset>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace expp::core::filesystem {

namespace fs = std::filesystem;

/**
 * @brief File type classification for UI display
 */
enum class FileType : std::uint8_t {
    Directory,
    RegularFile,
    Symlink,
    Executable,
    Archive,
    SourceCode,
    Image,
    Document,
    Config,
    Unknown
};
/**
 * @brief File entry with cached metadata
 *
 * Caches frequently accessed file properties to avoid
 * repeated filesystem calls.
 */
struct FileEntry {
    fs::path path;
    [[no_unique_address]] FileType type{FileType::Unknown};
    [[no_unique_address]] std::uintmax_t size{0};
    [[no_unique_address]] std::chrono::file_clock::time_point birthTime;
    [[no_unique_address]] std::chrono::file_clock::time_point lastModified;
    fs::path symlinkTarget;
    [[no_unique_address]] bool isHidden{false};
    [[no_unique_address]] bool isReadable{false};
    [[no_unique_address]] bool isWritable{false};
    [[no_unique_address]] bool isBrokenSymlink{false};
    [[no_unique_address]] bool isRecursiveSymlink{false};

    [[nodiscard]] std::string filename() const { return path.filename().string(); }
    [[nodiscard]] std::string extension() const { return path.extension().string(); }

    [[nodiscard]] bool isDirectory() const noexcept { return type == FileType::Directory; }
    [[nodiscard]] bool isSymlink() const noexcept { return type == FileType::Symlink; }
};

/**
 * @brief Checks if a path has execute permission
 * @param path Path to check
 * @return True if any execute bit is set
 */
[[nodiscard]] bool is_executable(const fs::path& path) noexcept;

/**
 * @brief Classifies a file based on extension and permissions
 * @param entry Directory entry to classify
 * @return FileType classification
 */
[[nodiscard]] FileType classify_file(const fs::directory_entry& entry) noexcept;

/**
 * @brief Reads and caches metadata for one directory entry.
 * @param entry Filesystem directory entry to inspect.
 * @return Fully populated FileEntry or Error.
 */
[[nodiscard]] Result<FileEntry> inspect_directory_entry(const fs::directory_entry& entry) noexcept;

/**
 * @brief Checks if a file can be previewed as text
 * @param path Path to check
 * @return True if file is likely text-based
 */
[[nodiscard]] bool is_previewable(const fs::path& path) noexcept;

/**
 * @brief Lists directory contents with metadata
 * @param dir Directory path
 * @param include_hidden Include hidden files (default: false)
 * @return Vector of FileEntry or Error
 *
 * Entries are sorted: directories first, then alphabetically.
 */
[[nodiscard]] Result<std::vector<FileEntry>> list_directory(const fs::path& dir, bool include_hidden = false) noexcept;

/**
 * @brief Gets the canonical (absolute, resolved) path
 * @param path Input path
 * @return Canonical path or Error
 */
[[nodiscard]] Result<fs::path> canonicalize(const fs::path& path);

/**
 * @brief Normalizes a filesystem path by resolving symlinks, removing redundant components, and converting to preferred
 * format
 * @param path The filesystem path to normalize
 * @return The normalized filesystem path (if canonicalization fails, returns best effort normalized path)
 */
[[nodiscard]] fs::path normalize(const fs::path& path);

/**
 * @brief Creates a directory (and parents if needed)
 * @param path Directory path to create
 * @return Success or Error
 */
[[nodiscard]] VoidResult create_directory(const fs::path& path);

/**
 * @brief Creates an empty file
 * @param path File path to create
 * @return Success or Error
 */
[[nodiscard]] VoidResult create_file(const fs::path& path);

/**
 * @brief Renames/moves a file or directory
 * @param from Source path
 * @param to Destination path
 * @return Success or Error
 */
[[nodiscard]] VoidResult rename(const fs::path& old_path, const fs::path& new_path);

/**
 * @brief Removes a file
 * @param path Path to remove
 * @return Success or Error
 */
[[nodiscard]] VoidResult remove_file(const fs::path& path);

/**
 * @brief Removes a directory and all its contents
 * @param path Directory path to remove
 * @return Success or Error
 */
[[nodiscard]] VoidResult remove_directory(const fs::path& path);

/**
 * @brief Moves a file/directory to system trash
 * @param path Path to trash
 * @return Success or Error
 *
 * Uses platform-specific APIs:
 * - Windows: SHFileOperation with FOF_ALLOWUNDO
 * - Linux: gio trash
 * - macOS: trash command or AppleScript
 */
[[nodiscard]] VoidResult move_to_trash(const fs::path& path);

/**
 * @brief Opens a file with the system default application
 * @param path Path to open
 * @return Success or Error
 */
[[nodiscard]] VoidResult open_with_default(const fs::path& path);

/**
 * @brief Reads file preview (first N lines)
 * @param path Path to read
 * @param max_lines Maximum lines to read
 * @return Vector of lines or Error
 */
[[nodiscard]] Result<std::vector<std::string>> read_preview(const fs::path& path, int max_lines);

/**
 * @brief Gets human-readable file size string
 * @param bytes Size in bytes
 * @return Formatted size (e.g., "1.5 MB")
 */
[[nodiscard]] std::string format_file_size(std::uintmax_t bytes);

// EXTENSION POINT: Remote filesystem support
// Future: Add interfaces for SSH, FTP, cloud storage protocols
// These would implement a common FileSystemProvider interface

}  // namespace expp::core::filesystem

#endif  // EXPP_CORE_FILESYSTEM_HPP
