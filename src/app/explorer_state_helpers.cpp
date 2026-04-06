#include "expp/app/explorer_state_helpers.hpp"

#include <algorithm>
#include <numeric>

namespace expp::app {

void clear_visual_selection(SelectionState& selection) {
    selection.visualModeActive = false;
    selection.visualAnchor = -1;
    selection.visualSelectedIndices.clear();
}

void update_visual_selection(SelectionState& selection, std::span<const core::filesystem::FileEntry> entries) {
    if (!selection.visualModeActive || entries.empty()) {
        selection.visualSelectedIndices.clear();
        return;
    }

    selection.visualAnchor = std::clamp(selection.visualAnchor, 0, static_cast<int>(entries.size()) - 1);
    const int left = std::min(selection.visualAnchor, selection.currentSelected);
    const int right = std::max(selection.visualAnchor, selection.currentSelected);

    selection.visualSelectedIndices.clear();
    selection.visualSelectedIndices.resize(static_cast<std::size_t>(std::max(0, right - left + 1)));
    std::ranges::iota(selection.visualSelectedIndices, left);
    // for (int index = left; index <= right; ++index) {
    //     selection.visualSelectedIndices.push_back(index);
    // }
}

void update_scroll_for_selection(SelectionState& selection, int entry_count) {
    if (entry_count <= 0) {
        selection.currentSelected = 0;
        selection.currentScrollOffset = 0;
        return;
    }

    selection.currentSelected = std::clamp(selection.currentSelected, 0, entry_count - 1);

    const int viewport_rows = std::max(1, selection.currentViewportRows);
    const int max_offset = std::max(0, entry_count - viewport_rows);
    selection.currentScrollOffset = std::clamp(selection.currentScrollOffset, 0, max_offset);

    const int top_anchor = std::max(0, viewport_rows / 4);
    const int bottom_anchor = std::max(0, (viewport_rows * 3) / 4);
    const int top_threshold = selection.currentScrollOffset + top_anchor;
    const int bottom_threshold = selection.currentScrollOffset + bottom_anchor;

    if (selection.currentSelected < top_threshold) {
        selection.currentScrollOffset = std::max(0, selection.currentSelected - top_anchor);
    } else if (selection.currentSelected > bottom_threshold) {
        selection.currentScrollOffset = std::min(max_offset, std::max(0, selection.currentSelected - bottom_anchor));
    }

    selection.currentScrollOffset = std::min(selection.currentScrollOffset, selection.currentSelected);

    const int visible_last = selection.currentScrollOffset + viewport_rows - 1;
    if (selection.currentSelected > visible_last) {
        selection.currentScrollOffset = selection.currentSelected - viewport_rows + 1;
    }

    selection.currentScrollOffset = std::clamp(selection.currentScrollOffset, 0, max_offset);
}

std::vector<std::filesystem::path> selected_entry_paths(const ExplorerState& state) {
    std::vector<std::filesystem::path> selected_paths;
    if (state.entries.empty()) {
        return selected_paths;
    }

    if (!state.selection.visualModeActive || state.selection.visualSelectedIndices.empty()) {
        selected_paths.push_back(state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path);
        return selected_paths;
    }

    selected_paths.reserve(state.selection.visualSelectedIndices.size());
    for (const int index : state.selection.visualSelectedIndices) {
        if (index >= 0 && index < static_cast<int>(state.entries.size())) {
            selected_paths.push_back(state.entries[static_cast<std::size_t>(index)].path);
        }
    }
    return selected_paths;
}

}  // namespace expp::app
