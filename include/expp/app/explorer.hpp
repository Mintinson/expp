/**
 * @file explorer.hpp
 * @brief Explorer domain model and application-facing controller API.
 *
 * `Explorer` owns the mutable file-manager state used by the UI layer:
 * directory navigation, selection, search, sort order, and clipboard-style
 * file operations. Side effects are delegated through `ExplorerServices` so the
 * domain logic stays independent from the concrete filesystem and preview
 * implementation.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_APP_EXPLORER_HPP
#define EXPP_APP_EXPLORER_HPP

#include "expp/app/explorer_services.hpp"
#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace expp::app {

/**
 * @brief Active sort configuration for explorer entries.
 */
struct SortOrder {
    /**
     * @brief Sort fields supported by the explorer.
     */
    enum class Field : std::uint8_t {
        ModifiedTime,
        BirthTime,
        Extension,
        Alphabetical,
        Natural,
        Size,
    };

    /**
     * @brief Sort direction for the selected field.
     */
    enum class Direction : std::uint8_t {
        Ascending,
        Descending,
    };

    /// Field currently used to order directory entries.
    Field field{Field::Natural};
    /// Direction applied to the selected field.
    Direction direction{Direction::Ascending};
};

/**
 * @brief Search projection over the current entry list.
 */
struct SearchState {
    /// Raw search pattern entered by the user.
    std::string pattern;
    /// Absolute indices in `ExplorerState::entries` matching `pattern`.
    std::vector<int> matches;
    /// Whether search highlighting/navigation is active.
    bool highlightActive{false};
    /// Active index inside `matches`, or `-1` when no match is selected.
    int currentMatchIndex{-1};
};

/**
 * @brief Selection and viewport state for the current explorer listing.
 */
struct SelectionState {
    /// Absolute selection index in the current entry list.
    int currentSelected{0};
    /// Absolute scroll offset of the current list viewport.
    int currentScrollOffset{0};
    /// Number of visible rows available to the current list.
    int currentViewportRows{1000};  // NOLINT
    /// Selection index used for the parent-directory panel.
    int parentSelected{0};
    /// Whether visual range selection is currently active.
    bool visualModeActive{false};
    /// Selection anchor used when extending a visual range.
    int visualAnchor{-1};
    /// Absolute indices participating in the current visual selection.
    std::vector<int> visualSelectedIndices;
};

/**
 * @brief Clipboard-like state for copy/cut/paste operations.
 */
struct ClipboardState {
    /**
     * @brief Pending operation type associated with clipboard paths.
     */
    enum class Operation : std::uint8_t {
        None,
        Copy,
        Cut,
    };

    /// Source paths captured by yank or cut.
    std::vector<std::filesystem::path> paths;
    /// Operation to apply when the clipboard is pasted.
    Operation operation{Operation::None};

    /**
     * @brief Returns whether the clipboard carries a usable operation.
     */
    [[nodiscard]] bool empty() const noexcept { return operation == Operation::None || paths.empty(); }
};

/**
 * @brief Explorer domain state exposed to the UI and presenter layers.
 *
 * This structure intentionally contains only domain and viewport state. Dialog
 * visibility, input buffers, and preview loading state live in the view layer.
 */
struct ExplorerState {
    using SortField = SortOrder::Field;
    using SortDirection = SortOrder::Direction;
    using ClipboardOperation = ClipboardState::Operation;

    /// Directory currently being browsed.
    std::filesystem::path currentDir;
    /// Entries of the current directory after refresh and sort projection.
    std::vector<core::filesystem::FileEntry> entries;
    /// Entries of the parent directory for the optional parent panel.
    std::vector<core::filesystem::FileEntry> parentEntries;
    /// Current selection and viewport state.
    SelectionState selection;
    /// Active search state over `entries`.
    SearchState search;
    /// Pending copy/cut clipboard state.
    ClipboardState clipboard;
    /// Current sort configuration.
    SortOrder sortOrder{};
};

/**
 * @brief Explorer application controller.
 *
 * The public API is intentionally high-level: callers mutate navigation,
 * selection, search, and file operations through explicit commands while the
 * explorer preserves consistent derived state such as scroll position and
 * visual selection.
 */
class Explorer {
public:
    /**
     * @brief Callback invoked after successful refresh-like state changes.
     */
    using RefreshCallback = std::function<void()>;

    /**
     * @brief Creates and initializes an explorer.
     * @param start_path Initial directory to browse.
     * @param services Service bundle used for filesystem, preview, and clipboard work.
     * @return Initialized explorer instance or an error if the initial refresh fails.
     */
    [[nodiscard]] static core::Result<std::shared_ptr<Explorer>> create(
        std::filesystem::path start_path, ExplorerServices services = make_default_explorer_services());
    ~Explorer();

    Explorer(Explorer&&) noexcept;
    Explorer& operator=(Explorer&&) noexcept;
    Explorer(const Explorer&) = delete;
    Explorer& operator=(const Explorer&) = delete;

    /**
     * @brief Returns the current explorer state snapshot.
     */
    [[nodiscard]] const ExplorerState& state() const noexcept;

    /**
     * @brief Returns the service bundle used by this explorer.
     */
    [[nodiscard]] const ExplorerServices& services() const noexcept;

    /**
     * @brief Navigates to an explicit directory path.
     * @return Error if the path cannot be accessed or is not a directory.
     */
    [[nodiscard]] core::VoidResult navigateTo(const std::filesystem::path& path);

    /**
     * @brief Navigates to the parent directory, preserving the previous child selection when possible.
     */
    [[nodiscard]] core::VoidResult goParent();

    /**
     * @brief Resolves the selected symlink target and enters it when it is a directory.
     */
    [[nodiscard]] core::VoidResult navigateToSelectedLinkTargetDirectory();

    /**
     * @brief Enters the selected directory or optionally opens the selected file.
     * @param open_file When true, non-directory entries are opened with the system default handler.
     */
    [[nodiscard]] core::VoidResult enterSelected(bool open_file = false);

    /**
     * @brief Moves the current selection downward by `count` rows.
     */
    void moveDown(int count = 1);

    /**
     * @brief Moves the current selection upward by `count` rows.
     */
    void moveUp(int count = 1);

    /**
     * @brief Moves the current selection to the first entry.
     */
    void goToTop();

    /**
     * @brief Moves the current selection to the last entry.
     */
    void goToBottom();

    /**
     * @brief Moves the current selection to the 1-based line number `line`.
     */
    void goToLine(int line);

    /**
     * @brief Sets the number of rows available to the current list viewport.
     *
     * The explorer clamps scroll and selection immediately after updating this value.
     */
    void setViewportRows(int rows);

    /**
     * @brief Creates a file or directory under the current directory.
     *
     * A trailing slash or backslash indicates directory creation.
     */
    [[nodiscard]] core::VoidResult create(const std::string& name);

    /**
     * @brief Renames the currently selected entry.
     */
    [[nodiscard]] core::VoidResult rename(const std::string& new_name);

    /**
     * @brief Permanently deletes the selected entry or visual selection.
     */
    [[nodiscard]] core::VoidResult deleteSelected();

    /**
     * @brief Moves the selected entry or visual selection to the platform trash.
     */
    [[nodiscard]] core::VoidResult trashSelected();

    /**
     * @brief Opens the selected non-directory entry with the system default application.
     */
    [[nodiscard]] core::VoidResult openSelected();

    /**
     * @brief Copies the selected entry or visual selection into explorer clipboard state.
     */
    [[nodiscard]] core::VoidResult yankSelected();

    /**
     * @brief Marks the selected entry or visual selection for move-on-paste.
     */
    [[nodiscard]] core::VoidResult cutSelected();

    /**
     * @brief Clears explorer clipboard state.
     */
    [[nodiscard]] core::VoidResult discardYank();

    /**
     * @brief Pastes the current explorer clipboard into the active directory.
     * @param overwrite Whether existing targets may be replaced.
     */
    [[nodiscard]] core::VoidResult pasteYanked(bool overwrite = false);

    /**
     * @brief Copies the selected entry path to the system clipboard.
     * @param absolute When false, the path is made relative to the explorer base directory when possible.
     */
    [[nodiscard]] core::VoidResult copySelectedPathToSystemClipboard(bool absolute = false);

    /**
     * @brief Copies the current directory path to the system clipboard.
     * @param absolute When false, the path is made relative to the explorer base directory when possible.
     */
    [[nodiscard]] core::VoidResult copyCurrentDirectoryPathToSystemClipboard(bool absolute = false);

    /**
     * @brief Copies the selected entry filename to the system clipboard.
     */
    [[nodiscard]] core::VoidResult copySelectedFileNameToSystemClipboard();

    /**
     * @brief Copies the selected entry stem to the system clipboard.
     */
    [[nodiscard]] core::VoidResult copySelectedNameWithoutExtensionToSystemClipboard();

    /**
     * @brief Starts visual range selection anchored at the current entry.
     */
    void enterVisualMode();

    /**
     * @brief Leaves visual mode and clears the current visual range.
     */
    void exitVisualMode();

    /**
     * @brief Returns the number of selected rows in visual mode.
     */
    [[nodiscard]] int visualSelectionCount() const noexcept;

    /**
     * @brief Applies a new sort order and preserves the current logical selection when possible.
     */
    void setSortOrder(SortOrder::Field field, SortOrder::Direction direction);

    /**
     * @brief Searches the current entry list using a filename substring match.
     */
    void search(const std::string& pattern);

    /**
     * @brief Advances to the next search match.
     */
    void nextMatch();

    /**
     * @brief Moves to the previous search match.
     */
    void prevMatch();

    /**
     * @brief Clears search highlighting and match navigation state.
     */
    void clearSearch();

    /**
     * @brief Reloads the current directory and parent panel state.
     */
    [[nodiscard]] core::VoidResult refresh();

    /**
     * @brief Registers a callback invoked after successful refresh-like updates.
     */
    void onRefresh(RefreshCallback callback);

    /**
     * @brief Toggles hidden-file visibility and refreshes the current directory.
     */
    [[nodiscard]] core::VoidResult toggleShowHidden();

private:
    explicit Explorer(std::filesystem::path start_path, ExplorerServices services);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_HPP
