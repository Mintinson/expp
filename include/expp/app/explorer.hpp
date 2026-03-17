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
#include <memory>
#include <string>
#include <vector>

namespace expp::app {
/**
 * @brief Explorer state for UI rendering
 */
struct ExplorerState {
    std::filesystem::path currentDir;
    std::vector<core::filesystem::FileEntry> entries;
    std::vector<core::filesystem::FileEntry> parentEntries;
    int currentSelected{0};
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
    std::filesystem::path targetPath;  // For delete/trash operations
};

class Explorer {
public:
    using RefreshCallback = std::function<void()>;

    explicit Explorer(std::filesystem::path start_path = std::filesystem::current_path());
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
    void refresh();

    /**
     * @brief Registers refresh callback
     * @param callback Called after refresh
     */
    void onRefresh(RefreshCallback callback);

    // ========== View ==========
    void toggleShowHidden();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_HPP