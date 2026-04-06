#ifndef EXPP_APP_EXPLORER_SERVICES_HPP
#define EXPP_APP_EXPLORER_SERVICES_HPP

/**
 * @file explorer_services.hpp
 * @brief Async-ready service seams consumed by the explorer domain and view.
 *
 * These interfaces isolate filesystem, preview, and clipboard side effects from
 * navigation and rendering logic so the current synchronous implementation can
 * be replaced by background workers later without re-shaping the higher layers.
 */

#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"
#include "expp/core/task.hpp"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace expp::app {

namespace fs = std::filesystem;

/**
 * @brief Request for directory listing work.
 *
 * `offset` and `limit` are part of the API now so pagination and chunked
 * loading can be introduced later without changing callers.
 */
struct DirectoryListRequest {
    fs::path directory;
    bool includeHidden{false};
    std::size_t offset{0};
    std::size_t limit{0};
    core::CancellationToken cancellation;
};

/**
 * @brief Result of a directory listing request.
 */
struct DirectoryListResult {
    /// The currently returned slice of entries.
    std::vector<core::filesystem::FileEntry> entries;
    /// Total number of entries before slicing.
    std::size_t totalEntries{0};
    /// Whether another slice could be requested after this one.
    bool hasMore{false};
};

/**
 * @brief Request for preview loading.
 */
struct PreviewRequest {
    fs::path target;
    int maxLines{0};
    core::CancellationToken cancellation;
};

/**
 * @brief Loaded preview payload for a file-like entry.
 */
struct PreviewPayload {
    std::vector<std::string> lines;
};

/**
 * @brief Filesystem-side operations needed by the explorer domain.
 */
class ExplorerFileSystemService {
public:
    virtual ~ExplorerFileSystemService() = default;

    /// Lists the requested directory, optionally returning only a slice.
    [[nodiscard]] virtual core::Result<DirectoryListResult> listDirectory(
        const DirectoryListRequest& request) const = 0;
    /// Canonicalizes a path for navigation and identity checks.
    [[nodiscard]] virtual core::Result<fs::path> canonicalize(const fs::path& path) const = 0;
    /// Normalizes a path without requiring it to exist.
    [[nodiscard]] virtual fs::path normalize(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult createDirectory(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult createFile(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult rename(const fs::path& old_path, const fs::path& new_path) const = 0;
    [[nodiscard]] virtual core::VoidResult removeFile(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult removeDirectory(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult moveToTrash(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult openWithDefault(const fs::path& path) const = 0;
    [[nodiscard]] virtual core::VoidResult copy(const fs::path& source,
                                                const fs::path& destination,
                                                bool overwrite) const = 0;
};

/**
 * @brief Service responsible for turning a path into previewable content.
 */
class ExplorerPreviewService {
public:
    virtual ~ExplorerPreviewService() = default;

    [[nodiscard]] virtual core::Result<PreviewPayload> loadPreview(const PreviewRequest& request) const = 0;
};

/**
 * @brief Service for copying plain text into the platform clipboard.
 */
class ExplorerClipboardService {
public:
    virtual ~ExplorerClipboardService() = default;

    [[nodiscard]] virtual core::VoidResult copyText(std::string_view text) const = 0;
};

/**
 * @brief Aggregated side-effect services used by the explorer stack.
 */
struct ExplorerServices {
    std::shared_ptr<ExplorerFileSystemService> fileSystem;
    std::shared_ptr<ExplorerPreviewService> preview;
    std::shared_ptr<ExplorerClipboardService> clipboard;
};

/**
 * @brief Creates the default synchronous service bundle.
 */
[[nodiscard]] ExplorerServices make_default_explorer_services();

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_SERVICES_HPP
