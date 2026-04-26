#include "expp/app/explorer_presenter.hpp"

#include "expp/app/explorer_commands.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace expp::app {

int ExplorerPresenter::resolveScreenRows(int screen_rows, int fallback_rows) noexcept {
    return std::max({1, screen_rows, fallback_rows});
}

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
 * @brief Computes visible list bounds and selected row within the visible slice.
 * @param model Screen model being populated.
 * @param state Explorer domain state.
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
 * @param model Screen model being populated.
 * @param state Explorer domain state.
 */
void map_viewport_indices(ExplorerScreenModel& model, const ExplorerState& state) {
    const int offset = model.currentList.offset;
    const int visible_end = model.currentList.visibleEnd;

    // Filter and map search matches to the currently visible list slice.
    for (std::size_t i = 0; i < state.search.matches.size(); ++i) {
        const int match = state.search.matches[i];
        if (match >= offset && match < visible_end) {
            model.currentList.searchMatches.push_back(match - offset);
            // Preserve which visible match is the active one.
            if (static_cast<int>(i) == state.search.currentMatchIndex) {
                model.currentList.currentMatchIndex = static_cast<int>(model.currentList.searchMatches.size()) - 1;
            }
        }
    }

    // Filter and map visual selection indices to visible coordinates.
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
 * @param model Screen model being populated.
 * @param dir Current explorer directory.
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
 * @param state Current explorer state.
 * @return Status string containing sort/search/visual counters.
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

    if (state.listing.scanInProgress || state.listing.loading) {
        status += std::format(" [loading:{}/{}]", state.listing.loadedEntries,
                              std::max(state.listing.totalEntries, state.listing.loadedEntries));
    }

    return status;
}

/**
 * @brief Determines the appropriate help text to display based on the current overlay state and UI mode.
 * @param overlay The current explorer overlay state (e.g., help overlay or directory jump overlay).
 * @param mode The current UI mode (e.g., Visual mode).
 * @return Context-specific help text for the status line.
 */
[[nodiscard]] std::string determine_help_text(const ExplorerOverlayState& overlay, ui::Mode mode) {
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

    return text;
}

/**
 * @brief Copies the raw key buffer into the screen model.
 */
void store_key_buffer(ExplorerScreenModel& model, std::string_view key_buffer) {
    model.keyBuffer = key_buffer;
}

}  // namespace

ExplorerScreenModel ExplorerPresenter::present(const ExplorerState& state,
                                               const ExplorerOverlayState& overlay,
                                               std::string_view key_buffer,
                                               ui::Mode mode) const {
    ExplorerScreenModel model;

    // 1) Geometry projection: convert absolute domain coordinates into the
    // viewport-local list model consumed by the renderer.
    setup_viewport(model, state);
    map_viewport_indices(model, state);

    // 2) Text assembly: derive titles and key-buffer echoes.
    setup_titles(model, state.currentDir);
    store_key_buffer(model, key_buffer);

    // 3) Status/help synthesis: summarize search/sort/visual state and mode.
    model.searchStatus = build_status_bar(state);
    model.helpText = determine_help_text(overlay, mode);

    return model;
}

}  // namespace expp::app
