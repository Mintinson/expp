/**
 * @file explorer.hpp
 * @brief Main file explorer application component
 *
 * This is the primary application component that ties together:
 * - Filesystem navigation
 * - UI rendering
 * - Key handling
 * - Preview system
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_APP_EXPLORER_HPP
#define EXPP_APP_EXPLORER_HPP

#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace expp::app {
/**
 * @brief Explorer state for UI rendering
 */
struct ExplorerState {
    /**
     * @brief Sort dimensions supported by the explorer list.
     *
     * Each field is applied with a stable tie-break chain so results are deterministic
     * across refreshes and key toggles.
     */
    enum class SortField : std::uint8_t {
        ModifiedTime,
        BirthTime,
        Extension,
        Alphabetical,
        Natural,
        Size,
    };

    /**
     * @brief Sort direction used by `sortField`.
     */
    enum class SortDirection : std::uint8_t {
        Ascending,
        Descending,
    };

    enum class ClipboardOperation : std::uint8_t {
        None,
        Copy,
        Cut,
    };

    std::filesystem::path currentDir;
    std::vector<core::filesystem::FileEntry> entries;
    std::vector<core::filesystem::FileEntry> parentEntries;
    int currentSelected{0};
    int currentScrollOffset{0};
    int currentViewportRows{1000};  // NOLINT
    int parentSelected{0};

    // Search state
    std::string searchPattern;
    std::vector<int> searchMatches;
    bool searchHighlightActive{false};
    int currentMatchIndex{-1};

    // Preview content
    std::vector<std::string> previewLines;

    // Dialog state
    bool showDeleteDialog{false};
    bool showTrashDialog{false};
    bool showCreateDialog{false};
    bool showRenameDialog{false};
    bool showSearchDialog{false};

    std::string inputBuffer;           // For dialogs
    std::filesystem::path trashDeletePath;  // For delete/trash operations

    // Can we remove the single clipboardPath and just use clipboardPaths for both normal and visual mode? 
    std::filesystem::path clipboardPath;
    std::vector<std::filesystem::path> clipboardPaths;
    ClipboardOperation clipboardOperation{ClipboardOperation::None};

    bool visualModeActive{false};
    int visualAnchor{-1};
    std::vector<int> visualSelectedIndices;

    SortField sortField{SortField::Natural};
    SortDirection sortDirection{SortDirection::Ascending};

};

class Explorer {
public:
    using RefreshCallback = std::function<void()>;

    /**
     * @brief Creates a new explorer instance
     * @param start_path 
     * @return 
     */
    [[nodiscard]] static core::Result<std::shared_ptr<Explorer>> create(std::filesystem::path start_path);
    ~Explorer();

    // Non-copyable, movable
    Explorer(Explorer&&) noexcept;
    Explorer& operator=(Explorer&&) noexcept;
    Explorer(const Explorer&) = delete;
    Explorer& operator=(const Explorer&) = delete;

    // ========== State Access ==========

    /**
     * @brief Gets the current state for rendering
     * @return Const reference to current state
     */
    [[nodiscard]] const ExplorerState& state() const noexcept;

    // ========== Navigation ==========

    /**
     * @brief Navigates to a directory
     * @param path Target directory
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult navigateTo(const std::filesystem::path& path);

    /**
     * @brief Goes to parent directory
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult goParent();

    /**
     * @brief Enters selected item (directory or opens file)
     * @param openFile If true, opens files with default app
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult enterSelected(bool open_file = false);

    /**
     * @brief Moves selection down
     * @param count Number of items to move
     */
    void moveDown(int count = 1);

    /**
     * @brief Moves selection up
     * @param count Number of items to move
     */
    void moveUp(int count = 1);

    /**
     * @brief Goes to first item
     */
    void goToTop();

    /**
     * @brief Goes to last item
     */
    void goToBottom();

    /**
     * @brief Goes to specific line (1-indexed)
     * @param line Line number
     */
    void goToLine(int line);

    /**
     * @brief Updates the visible row capacity for threshold-based scrolling
     * @param rows Number of visible rows in the current file list panel
     */
    void setViewportRows(int rows);

    // ========== File Operations ==========

    /**
     * @brief Creates a file or directory
     * @param name Name or path (ending with / creates directory)
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult create(const std::string& name);

    /**
     * @brief Renames the selected entry
     * @param newName New name
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult rename(const std::string& new_name);

    /**
     * @brief Deletes the selected entry permanently
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult deleteSelected();

    /**
     * @brief Moves the selected entry to trash
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult trashSelected();

    /**
     * @brief Opens the selected file with default application
     * @return Success or Error
     */
    [[nodiscard]] core::VoidResult openSelected();

    /**
     * @brief Yanks (copies) the currently selected text or content.
     * @return A result object indicating whether the yank operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult yankSelected();

    /**
     * @brief Cuts the currently selected content.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult cutSelected();

    /**
     * @brief Discards the current yank operation.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult discardYank();

    /**
     * @brief Pastes previously yanked (copied) content.
     * @param overwrite If true, overwrites existing content; if false, skip the existing destination.
     * @return A result indicating whether the paste operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult pasteYanked(bool overwrite = false);

    /**
     * @brief Copies the selected entry path to the system clipboard.
     * @param absolute If true, copy as absolute path; otherwise copy relative to explorer start directory.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult copySelectedPathToSystemClipboard(bool absolute = false);

    /**
     * @brief Copies the current directory path to the system clipboard.
     * @param absolute If true, copy as absolute path; otherwise copy relative to explorer start directory.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult copyCurrentDirectoryPathToSystemClipboard(bool absolute = false);

    /**
     * @brief Copies the selected entry file name (with extension) to the system clipboard.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult copySelectedFileNameToSystemClipboard();

    /**
     * @brief Copies the selected entry file name without extension to the system clipboard.
     * @return A result indicating whether the operation succeeded or failed.
     */
    [[nodiscard]] core::VoidResult copySelectedNameWithoutExtensionToSystemClipboard();

    /**
     * @brief Enters visual mode and starts range selection from current cursor row.
     */
    void enterVisualMode();

    /**
     * @brief Exits visual mode and clears active multi-selection.
     */
    void exitVisualMode();

    /**
     * @brief Returns how many entries are currently selected in visual mode.
     * @return Number of selected entries (0 if visual mode is inactive).
     */
    [[nodiscard]] int visualSelectionCount() const noexcept;

    /**
     * @brief Changes sort strategy for current and parent lists.
     *
     * This operation is lightweight: it reorders cached entries and preserves
     * selection where possible instead of forcing a full filesystem re-scan.
     *
     * @param field Sort field to apply (name, natural, size, timestamps, extension).
     * @param direction Sort direction (`Ascending` or `Descending`).
     */
    void setSortOrder(ExplorerState::SortField field, ExplorerState::SortDirection direction);

    // ========== Search ==========

    /**
     * @brief Starts a search with the given pattern
     * @param pattern Search pattern
     */
    void search(const std::string& pattern);

    /**
     * @brief Jumps to next search match
     */
    void nextMatch();

    /**
     * @brief Jumps to previous search match
     */
    void prevMatch();

    /**
     * @brief Clears search state
     */
    void clearSearch();

    // ========== Dialog Control ==========

    void showDeleteDialog();
    void showTrashDialog();
    void showCreateDialog();
    void showRenameDialog();
    void showSearchDialog();
    void hideAllDialogs();

    /**
     * @brief Sets dialog input buffer
     * @param input Input text
     */
    void setInput(const std::string& input);

    // ========== Refresh ==========

    /**
     * @brief Refreshes directory contents
     */
    [[nodiscard]] core::VoidResult refresh();

    /**
     * @brief Registers refresh callback
     * @param callback Called after refresh
     */
    void onRefresh(RefreshCallback callback);

    // ========== View ==========
    [[nodiscard]] core::VoidResult toggleShowHidden();

private:
    explicit Explorer(std::filesystem::path start_path);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_HPP
