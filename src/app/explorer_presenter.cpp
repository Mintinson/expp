#include "expp/app/explorer_presenter.hpp"

#include "expp/app/explorer_commands.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace expp::app {

int ExplorerPresenter::listViewportRows(int screen_rows) noexcept {
    constexpr int kRootLayoutRows = 2;
    constexpr int kPanelDecorRows = 4;
    return std::max(1, screen_rows - (kRootLayoutRows + kPanelDecorRows));
}

int ExplorerPresenter::helpViewportRows(int screen_rows) noexcept {
    constexpr int kHelpViewportPaddingRows = 10;
    // TODO consider avoid the magic numbers by introducing a more explicit layout model and deriving these values from
    // it
    return std::clamp(screen_rows - kHelpViewportPaddingRows, 8, 28);
}

namespace {
/**
 * @brief Set the up viewport object
 *
 * @param model (in/out) the screen model to update with viewport information
 * @param state the current explorer state containing selection and entry information
 */
void setup_viewport(ExplorerScreenModel& model, const ExplorerState& state) {
    const int total_entries = static_cast<int>(state.entries.size());
    const int visible_rows = std::max(1, state.selection.currentViewportRows);
    const int max_offset = std::max(0, total_entries - visible_rows);

    model.currentList.offset = std::clamp(state.selection.currentScrollOffset, 0, max_offset);
    model.currentList.visibleEnd = std::min(total_entries, model.currentList.offset + visible_rows);
    model.currentList.selectedIndex = state.selection.currentSelected - model.currentList.offset;
}

/**
 * @brief Maps absolute indices to viewport-relative indices for search matches and visual selections.
 *
 * @param model (in/out) the screen model to update with mapped indices
 * @param state the current explorer state containing selection and entry information
 */
void map_viewport_indices(ExplorerScreenModel& model, const ExplorerState& state) {
    const int offset = model.currentList.offset;
    const int visible_end = model.currentList.visibleEnd;

    // filter and map search matches to visible range
    for (std::size_t i = 0; i < state.search.matches.size(); ++i) {
        const int match = state.search.matches[i];
        if (match >= offset && match < visible_end) {
            model.currentList.searchMatches.push_back(match - offset);
            // locate current match index in the filtered list
            if (static_cast<int>(i) == state.search.currentMatchIndex) {
                model.currentList.currentMatchIndex = static_cast<int>(model.currentList.searchMatches.size()) - 1;
            }
        }
    }

    // filter and map visual selection indices to visible range
    if (state.selection.visualModeActive) {
        for (int abs_index : state.selection.visualSelectedIndices) {
            if (abs_index >= offset && abs_index < visible_end) {
                model.currentList.visualSelectedIndices.push_back(abs_index - offset);
            }
        }
    }
}

/**
 * @brief Sets up the parent, current, and status path titles in an ExplorerScreenModel based on a directory path.
 * @param model The ExplorerScreenModel to populate with title information.
 * @param dir The filesystem path to extract title information from.
 */
void setup_titles(ExplorerScreenModel& model, const std::filesystem::path& dir) {
    model.parentTitle = dir.has_parent_path() ? dir.parent_path().string() : "/";
    if (model.parentTitle.empty()) {
        model.parentTitle = "/";
    }

    model.currentTitle = dir.filename().string();
    if (model.currentTitle.empty()) {
        model.currentTitle = dir.string();
    }

    model.statusPath = dir.string();
}

/**
 * @brief Builds a formatted status bar string based on the current explorer state.
 * @param state The current state of the explorer, containing sort order, search information, and selection details.
 * @return A formatted string containing status information including sort order, active search matches, and visual
 * selection count.
 */
[[nodiscard]] std::string build_status_bar(const ExplorerState& state) {
    std::string status = std::format("[sort:{}{}]", sort_field_short_name(state.sortOrder.field),
                                     state.sortOrder.direction == SortOrder::Direction::Descending ? ":desc" : ":asc");

    if (state.search.highlightActive && !state.search.pattern.empty()) {
        if (state.search.currentMatchIndex >= 0) {
            status += std::format(" [/{}] {} matches ({}/{})", state.search.pattern, state.search.matches.size(),
                                  state.search.currentMatchIndex + 1, state.search.matches.size());
        } else {
            status += std::format(" [/{}] {} matches", state.search.pattern, state.search.matches.size());
        }
    }

    if (state.selection.visualModeActive) {
        status += std::format(" [visual:{}]", state.selection.visualSelectedIndices.size());
    }

    return status;
}

/**
 * @brief Determines the appropriate help text to display based on the current overlay state and UI mode.
 * @param overlay The current explorer overlay state (e.g., help overlay or directory jump overlay).
 * @param mode The current UI mode (e.g., Visual mode).
 * @param key_buffer A view of the current key buffer containing pending keystrokes.
 * @return A string containing context-appropriate help text, with any buffered keystrokes appended in brackets if
 * present.
 */
[[nodiscard]] std::string determine_help_text(const ExplorerOverlayState& overlay, ui::Mode mode
                                              // std::string_view key_buffer
) {
    std::string text;
    if (std::holds_alternative<HelpOverlayState>(overlay)) {
        text = "HELP: j/k move, f filter, Enter done, Esc/~ close";
    } else if (std::holds_alternative<DirectoryJumpOverlayState>(overlay)) {
        text = "JUMP: Enter confirm, Esc cancel";
    } else if (mode == ui::Mode::Visual) {
        text = "VISUAL: j/k range, y/x copy-cut, d/D trash-delete, v/Esc exit";
    } else {
        text = "j/k move, h/l nav, q quit, ~ help";
    }

    // if (!key_buffer.empty()) {
    //     text += std::format("  [{}]", key_buffer);
    // }

    return text;
}

// for now just copy it
void store_key_buffer(ExplorerScreenModel& model, std::string_view key_buffer) {
    model.keyBuffer = key_buffer;
}

}  // namespace

ExplorerScreenModel ExplorerPresenter::present(const ExplorerState& state,
                                               const ExplorerOverlayState& overlay,
                                               std::string_view key_buffer,
                                               ui::Mode mode) const {
    ExplorerScreenModel model;

    // 1. 几何计算
    setup_viewport(model, state);
    map_viewport_indices(model, state);

    // 2. 文本解析
    setup_titles(model, state.currentDir);
    store_key_buffer(model, key_buffer);


    // 3. 状态组装
    model.searchStatus = build_status_bar(state);
    model.helpText = determine_help_text(overlay, mode);


    return model;
}

}  // namespace expp::app
