#ifndef EXPP_APP_EXPLORER_DIRECTORY_CONTROLLER_HPP
#define EXPP_APP_EXPLORER_DIRECTORY_CONTROLLER_HPP

#include "expp/app/explorer.hpp"
#include "expp/app/notification_center.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace expp::app {

/**
 * @brief Async coordinator for directory loading, navigation, and viewport-scoped MIME preload.
 *
 * This controller orchestrates asynchronous filesystem operations. It ensures that heavy
 * tasks (like reading large directories or detecting MIME types via file magic) do not
 * block the UI thread. It also implements cancellation and generation-tracking to prevent
 * stale asynchronous results from corrupting the current view.
 */
class ExplorerDirectoryController {
public:
    /**
     * @brief A lightweight structure representing a locally cached MIME detection result.
     */
    struct CachedMime {
        std::string mimeType;
        bool previewable{false};
    };

    ExplorerDirectoryController(std::shared_ptr<Explorer> explorer, NotificationCenter& notifications);

    /// Reloads the currently active directory, optionally restoring the selection.
    void reloadCurrentDirectory(std::optional<std::filesystem::path> reselect = {});

    /// Starts loading a new directory asynchronously.
    void navigateTo(std::filesystem::path directory, std::optional<std::filesystem::path> reselect = {});

    /// Parses user string input, resolves it to a path, and navigates to it.
    void navigateToInput(std::string input);

    /// Navigates to the user's OS home directory.
    void navigateToHomeDirectory();

    /// Navigates to the application's configuration directory.
    void navigateToConfigDirectory();

    /// Resolves the currently selected symbolic link and navigates to its actual target.
    void navigateToSelectedLinkTargetDirectory();

    /// Navigates up one level in the directory tree.
    void goParent();

    /// Toggles the visibility of hidden files and reloads the current directory.
    void toggleHidden();

    /**
     * @brief Called by the UI when the user scrolls. Recalculates the visible window
     *        and schedules MIME detection for newly visible files.
     */
    void updateViewportInterest();

    /**
     * @brief Synchronously retrieves the cached MIME type for a path, if available.
     * @param path The file path.
     * @return The cached MIME info, or std::nullopt if it hasn't been resolved yet.
     */
    [[nodiscard]] std::optional<CachedMime> cachedMime(const std::filesystem::path& path) const;

private:
    std::shared_ptr<Explorer> explorer_;
    NotificationCenter& notifications_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)

    // Cancellation tokens to abort ongoing operations when the user navigates away.
    core::CancellationSource listingCancellation_;
    core::CancellationSource enrichmentCancellation_;

    // Monotonically increasing counters to discard results from aborted/past operations.
    std::uint64_t listingGeneration_{0};
    std::uint64_t enrichmentGeneration_{0};

    // Sliding window state for MIME preloading.
    int preloadStart_{-1};
    int preloadEnd_{-1};
    std::size_t preloadEntryCount_{0};

    // Holds a path that should be selected once the directory listing completes.
    std::optional<std::filesystem::path> reselectPath_;

    // In-memory cache for MIME types within the current directory.
    std::unordered_map<std::filesystem::path, CachedMime> mimeCache_;

    /**
     * @brief The core engine for loading a directory. Spawns coroutines to fetch data in chunks.
     * @param directory The absolute or relative directory to load.
     * @param reselect An item to automatically select once loaded.
     */
    void startDirectoryLoad(std::filesystem::path directory, std::optional<std::filesystem::path> reselect = {});

    /**
     * @brief Asynchronously fetches the entries of the parent directory (often used for breadcrumbs).
     * @param directory The current directory whose parent needs to be listed.
     * @param generation The listing generation this request belongs to.
     */
    void scheduleParentEntries(const std::filesystem::path& directory, std::uint64_t generation);

    /**
     * @brief Calculates the visible viewport and schedules background MIME detection for files inside it.
     */
    void scheduleMimePreload();
};

}  // namespace expp::app
#endif  // EXPP_APP_EXPLORER_DIRECTORY_CONTROLLER_HPP
