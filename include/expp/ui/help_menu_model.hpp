#ifndef EXPP_UI_HELP_MENU_MODEL_HPP
#define EXPP_UI_HELP_MENU_MODEL_HPP

/**
 * @file help_menu_model.hpp
 * @brief Filtered help-menu projection helpers.
 */

#include "expp/ui/key_handler.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace expp::ui {

/**
 * @brief Help entry rendered in the shortcut overlay.
 */
struct HelpEntry {
    std::string category;
    std::string shortcut;
    std::string description;
    Mode mode{Mode::Normal};
};

/**
 * @brief Selection and scroll state for the help overlay.
 */
struct HelpViewport {
    int selectedIndex{0};
    int scrollOffset{0};
    int viewportRows{12};
};

/**
 * @brief Filter-aware help entry model.
 *
 * The model preserves the original backing storage and tracks filtered source
 * indices, which avoids repeatedly copying `HelpEntry` objects while filtering.
 */
class HelpMenuModel {
public:
    /**
     * @brief Replaces the full help entry set.
     */
    void setEntries(std::vector<HelpEntry> entries);

    /**
     * @brief Applies a case-insensitive filter over shortcuts and descriptions.
     */
    void setFilter(std::string filter);

    /**
     * @brief Clears entries and filter state.
     */
    void clear();

    /**
     * @brief Returns the active filter text.
     */
    [[nodiscard]] std::string_view filter() const noexcept;

    /**
     * @brief Returns the unfiltered entry storage.
     */
    [[nodiscard]] std::span<const HelpEntry> entries() const noexcept;

    /**
     * @brief Returns the number of visible entries after filtering.
     */
    [[nodiscard]] std::size_t filteredCount() const noexcept;

    /**
     * @brief Returns the filtered entry at `index`.
     */
    [[nodiscard]] const HelpEntry& filteredEntry(std::size_t index) const;

    /**
     * @brief Returns the backing-source index for a filtered entry.
     */
    [[nodiscard]] std::size_t filteredSourceIndex(std::size_t index) const;

private:
    void rebuildFilteredIndices();

    std::vector<HelpEntry> entries_;
    std::vector<std::size_t> filteredIndices_;
    std::string filter_;
};

/**
 * @brief Clamps a viewport against a flat entry count.
 */
[[nodiscard]] HelpViewport clamp_help_viewport(HelpViewport viewport, std::size_t entry_count);
/**
 * @brief Clamps a viewport while accounting for category headers in the model.
 */
[[nodiscard]] HelpViewport clamp_help_viewport(HelpViewport viewport, const HelpMenuModel& model);

}  // namespace expp::ui

#endif  // EXPP_UI_HELP_MENU_MODEL_HPP
