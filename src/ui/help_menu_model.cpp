#include "expp/ui/help_menu_model.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace expp::ui {

namespace {

[[nodiscard]] std::string lowercase_copy(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const char ch : text) {
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    return normalized;
}

}  // namespace

void HelpMenuModel::setEntries(std::vector<HelpEntry> entries) {
    entries_ = std::move(entries);
    rebuildFilteredIndices();
}

void HelpMenuModel::setFilter(std::string filter) {
    filter_ = std::move(filter);
    rebuildFilteredIndices();
}

void HelpMenuModel::clear() {
    entries_.clear();
    filteredIndices_.clear();
    filter_.clear();
}

std::string_view HelpMenuModel::filter() const noexcept {
    return filter_;
}

std::span<const HelpEntry> HelpMenuModel::entries() const noexcept {
    return entries_;
}

std::size_t HelpMenuModel::filteredCount() const noexcept {
    return filteredIndices_.size();
}

const HelpEntry& HelpMenuModel::filteredEntry(std::size_t index) const {
    return entries_[filteredIndices_[index]];
}

std::size_t HelpMenuModel::filteredSourceIndex(std::size_t index) const {
    return filteredIndices_[index];
}

void HelpMenuModel::rebuildFilteredIndices() {
    filteredIndices_.clear();
    filteredIndices_.reserve(entries_.size());

    const std::string normalized_filter = lowercase_copy(filter_);
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (normalized_filter.empty()) {
            filteredIndices_.push_back(index);
            continue;
        }

        const HelpEntry& entry = entries_[index];
        const std::string normalized_shortcut = lowercase_copy(entry.shortcut);
        const std::string normalized_description = lowercase_copy(entry.description);
        if (normalized_shortcut.contains(normalized_filter) || normalized_description.contains(normalized_filter)) {
            filteredIndices_.push_back(index);
        }
    }
}

HelpViewport clamp_help_viewport(HelpViewport viewport, std::size_t entry_count) {
    viewport.viewportRows = std::max(1, viewport.viewportRows);
    if (entry_count == 0U) {
        viewport.selectedIndex = 0;
        viewport.scrollOffset = 0;
        return viewport;
    }

    const int last_index = static_cast<int>(entry_count) - 1;
    viewport.selectedIndex = std::clamp(viewport.selectedIndex, 0, last_index);

    const int max_offset = std::max(0, static_cast<int>(entry_count) - viewport.viewportRows);
    const int top_margin = viewport.viewportRows / 4;
    const int bottom_margin = (viewport.viewportRows * 3) / 4;

    if (viewport.selectedIndex < viewport.scrollOffset + top_margin) {
        viewport.scrollOffset = std::max(0, viewport.selectedIndex - top_margin);
    }
    if (viewport.selectedIndex >= viewport.scrollOffset + bottom_margin) {
        viewport.scrollOffset = viewport.selectedIndex - bottom_margin + 1;
    }

    viewport.scrollOffset = std::min(viewport.selectedIndex, viewport.scrollOffset);
    if (viewport.selectedIndex >= viewport.scrollOffset + viewport.viewportRows) {
        viewport.scrollOffset = viewport.selectedIndex - viewport.viewportRows + 1;
    }

    viewport.scrollOffset = std::clamp(viewport.scrollOffset, 0, max_offset);
    return viewport;
}

HelpViewport clamp_help_viewport(HelpViewport viewport, const HelpMenuModel& model) {
    viewport.viewportRows = std::max(1, viewport.viewportRows);
    if (model.filteredCount() == 0U) {
        return {.selectedIndex = 0, .scrollOffset = 0, .viewportRows = viewport.viewportRows};
    }

    // basic boundary clamping
    const int last_index = static_cast<int>(model.filteredCount()) - 1;
    viewport.selectedIndex = std::clamp(viewport.selectedIndex, 0, last_index);
    viewport.scrollOffset = std::clamp(viewport.scrollOffset, 0, viewport.selectedIndex);

    const auto visible_rows_to_selection = [&]() {
        int rows = 0;
        for (int index = viewport.scrollOffset; index <= viewport.selectedIndex; ++index) {
            const auto source_index = model.filteredSourceIndex(static_cast<std::size_t>(index));
            const bool has_header =
                index == viewport.scrollOffset || model.filteredEntry(static_cast<std::size_t>(index - 1)).category !=
                                                      model.entries()[source_index].category;
            rows += has_header ? 2 : 1;
        }
        return rows;
    };

    // visible boundary
    const int top_margin_rows = viewport.viewportRows / 4;
    const int bottom_margin_rows = (viewport.viewportRows * 3) / 4;

    if (viewport.scrollOffset < viewport.selectedIndex) {
        const int visual_rows = visible_rows_to_selection();
        if (visual_rows > bottom_margin_rows) {
            viewport.scrollOffset =
                std::min(viewport.selectedIndex - 1, viewport.scrollOffset + visual_rows - bottom_margin_rows);
        }
    }

    while (viewport.scrollOffset > 0 && (viewport.selectedIndex - viewport.scrollOffset) < top_margin_rows) {
        --viewport.scrollOffset;
    }

    while (viewport.scrollOffset < viewport.selectedIndex && visible_rows_to_selection() > viewport.viewportRows) {
        ++viewport.scrollOffset;
    }

    return viewport;
}

}  // namespace expp::ui
