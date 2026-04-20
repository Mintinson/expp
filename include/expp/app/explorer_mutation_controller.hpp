#ifndef EXPP_APP_EXPLORER_MUTATION_CONTROLLER_HPP
#define EXPP_APP_EXPLORER_MUTATION_CONTROLLER_HPP

#include "expp/app/explorer.hpp"
#include "expp/app/explorer_directory_controller.hpp"
#include "expp/app/notification_center.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace expp::app {

/**
 * @brief Async coordinator for mutating filesystem state and handling clipboard/yank operations.
 *
 * This controller orchestrates write-heavy operations (creation, deletion, moving, pasting)
 * and interactions with the OS clipboard. It ensures that all blocking I/O calls are dispatched
 * to background executors, maintaining a responsive UI, and automatically triggers directory
 * reloads upon successful mutations.
 */
class ExplorerMutationController {
public:
    ExplorerMutationController(std::shared_ptr<Explorer> explorer,
                               NotificationCenter& notifications,
                               ExplorerDirectoryController& directory_controller);
    /**
     * @brief Asynchronously creates a new file or directory.
     * @param name The name of the entity to create. If it ends with '/' or '\',
     *             a directory is created; otherwise, a file is created.
     */
    void create(std::string name);

    /**
     * @brief Asynchronously renames the currently selected file or directory.
     * @param new_name The target name to rename to.
     */
    void rename(std::string new_name);

    /**
     * @brief Permanently deletes the currently selected item(s) asynchronously.
     */
    void deleteSelected();

    /**
     * @brief Moves the currently selected item(s) to the system trash/recycle bin asynchronously.
     */
    void trashSelected();

    /**
     * @brief Opens the currently selected item using the OS default application.
     *        If the item is a directory, it navigates into it instead.
     */
    void openSelected();

    /**
     * @brief Asynchronously executes a paste operation using the internally yanked/cut items.
     * @param overwrite If true, existing files at the destination will be overwritten.
     */
    void pasteYanked(bool overwrite);

    /**
     * @brief Copies the selected item(s) into the internal application clipboard (Vim-style Yank).
     */
    void yankSelected();

    /**
     * @brief Marks the selected item(s) for a move operation in the internal clipboard (Vim-style Cut).
     */
    void cutSelected();

    /**
     * @brief Clears the internal application clipboard.
     */
    void discardYank();
    /**
     * @brief Asynchronously pushes the path of the selected item to the OS clipboard.
     * @param absolute If true, copies the absolute path; otherwise, the relative path.
     */
    void copySelectedPath(bool absolute);

    /**
     * @brief Asynchronously pushes the current directory's path to the OS clipboard.
     * @param absolute If true, copies the absolute path; otherwise, the relative path.
     */
    void copyCurrentDirectoryPath(bool absolute);

    /**
     * @brief Asynchronously pushes the selected file's full name (including extension) to the OS clipboard.
     */
    void copySelectedFileName();

    /**
     * @brief Asynchronously pushes the selected file's stem (name without extension) to the OS clipboard.
     */
    void copySelectedStem();

private:
    std::shared_ptr<Explorer> explorer_;
    NotificationCenter& notifications_;
    ExplorerDirectoryController& directoryController_;

    /**
     * @brief Checks if the source directory is an ancestor of the destination directory.
     *
     * Used to prevent infinite recursion and filesystem corruption when a user attempts
     * to copy or move a directory into one of its own subdirectories.
     *
     * @param source The source path.
     * @param destination The target path.
     * @return true if destination is inside source; false otherwise.
     */
    static bool isSourceParentOfDestination(const std::filesystem::path& source,
                                            const std::filesystem::path& destination);
};

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_MUTATION_CONTROLLER_HPP
