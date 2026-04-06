#ifndef EXPP_APP_EXPLORER_STATE_HELPERS_HPP
#define EXPP_APP_EXPLORER_STATE_HELPERS_HPP

#include "expp/app/explorer.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace expp::app {

/**
 * @brief Clears the visual selection state.
 * @param selection A reference to the SelectionState object to be cleared.
 */
void clear_visual_selection(SelectionState& selection);

/**
 * @brief Updates the visual selection state based on a collection of file entries.
 * @param selection The selection state to be updated.
 * @param entries A span of file entries to process for the selection update.
 */
void update_visual_selection(SelectionState& selection, std::span<const core::filesystem::FileEntry> entries);

/**
 * @brief Updates the scroll position based on the current selection state.
 * @param selection The selection state to update scrolling for.
 * @param entry_count The total number of entries in the scrollable area.
 */
void update_scroll_for_selection(SelectionState& selection, int entry_count);

// A RAII helper for updating selection and scroll state together, since they are interdependent and often updated together.
struct SelectionUpdateHelper
{
    SelectionUpdateHelper(SelectionState& selection, std::span<const core::filesystem::FileEntry> entries, int entry_count)
        : selection_(selection), entries_(entries), entry_count_(entry_count) {}

    ~SelectionUpdateHelper() {
        update_scroll_for_selection(selection_, entry_count_);
        update_visual_selection(selection_, entries_);
    }

private:
    SelectionState& selection_;
    std::span<const core::filesystem::FileEntry> entries_;
    int entry_count_;
};

/**
 * @brief Retrieves the filesystem paths of all selected entries in the explorer state.
 * @param state The explorer state containing the selected entries.
 * @return A vector of filesystem paths representing the selected entries.
 */
[[nodiscard]] std::vector<std::filesystem::path> selected_entry_paths(const ExplorerState& state);


}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_STATE_HELPERS_HPP
