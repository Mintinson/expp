#include "expp/ui/components.hpp"
#include "expp/ui/help_menu_model.hpp"
#include "expp/ui/key_handler.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace expp::ui {
namespace {

/**
 * @brief Convert a key binding sequence to a human-readable string representation.
 *
 * @param binding
 * @return std::string
 */
[[nodiscard]] std::string key_sequence_to_string(const KeyBinding& binding) {
    return binding.sequence | std::views::transform(key_to_string) | std::views::join |
           std::ranges::to<std::string>();
}

}  // namespace

std::vector<HelpEntry> build_help_entries(std::span<const Action> actions,
                                          std::span<const KeyBinding> bindings) {
    std::unordered_map<CommandId, const Action*> actions_by_id;
    actions_by_id.reserve(actions.size());
    for (const auto& action : actions) {
        actions_by_id.emplace(action.commandId, std::addressof(action));
    }

    std::vector<HelpEntry> entries;
    entries.reserve(bindings.size());
    for (const auto& binding : bindings) {
        const auto action_it = actions_by_id.find(binding.commandId);
        if (action_it == actions_by_id.end()) {
            continue;
        }

        const auto& action = *action_it->second;
        entries.push_back(HelpEntry{
            .category = action.category,
            .shortcut = key_sequence_to_string(binding),
            .description = binding.description.empty() ? action.description : binding.description,
            .mode = binding.mode,
        });
    }

    // category > mode > shortcut > description
    std::ranges::sort(entries, [](const HelpEntry& lhs, const HelpEntry& rhs) {
        if (lhs.category != rhs.category) {
            return lhs.category < rhs.category;
        }
        if (lhs.mode != rhs.mode) {
            return static_cast<int>(lhs.mode) < static_cast<int>(rhs.mode);
        }
        if (lhs.shortcut != rhs.shortcut) {
            return lhs.shortcut < rhs.shortcut;
        }
        return lhs.description < rhs.description;
    });
    return entries;
}

struct HelpMenuComponent::Impl {
    static constexpr int kCursorWidth = 3;
    static constexpr int kCategoryWidth = 16;
    static constexpr int kShortcutWidth = 14;
    static constexpr int kModeWidth = 10;

    explicit Impl(const Theme* in_theme) : theme(in_theme) {}

    const Theme* theme;

    [[nodiscard]] static std::string modeLabel(Mode mode) {
        if (mode == Mode::Normal) {
            return {};
        }
        std::string label{mode_to_name(mode)};
        if (!label.empty()) {
            label.front() =
                static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
        }
        return std::format("[{}]", label);
    }

    [[nodiscard]] static ftxui::Element makeCell(ftxui::Element element, int width) {
        using namespace ftxui;
        return std::move(element) | size(WIDTH, EQUAL, width);
    }

    [[nodiscard]] ftxui::Element themedSeparator() const {
        return ftxui::separator() | ftxui::color(theme->getBorderColor());
    }

    [[nodiscard]] static std::pair<int, int> calculateVisibleRange(const HelpMenuModel& model,
                                                                   const HelpViewport& clamped) {
        int rows_remaining = clamped.viewportRows;
        int visible_end = clamped.scrollOffset;

        for (int index = clamped.scrollOffset;
             index < static_cast<int>(model.filteredCount()) && rows_remaining > 0; ++index) {
            const auto& entry = model.filteredEntry(static_cast<std::size_t>(index));
            const bool has_header =
                index == clamped.scrollOffset ||
                model.filteredEntry(static_cast<std::size_t>(index - 1)).category != entry.category;

            const int cost = has_header ? 2 : 1;
            if (rows_remaining < cost && visible_end > clamped.scrollOffset) {
                break;
            }
            rows_remaining -= cost;
            visible_end = index + 1;
        }
        return {clamped.scrollOffset, visible_end};
    }

    [[nodiscard]] ftxui::Element renderShortcutRow(const HelpEntry& entry,
                                                   bool show_category,
                                                   bool is_selected) const {
        using namespace ftxui;

        Elements row;

        row.push_back(
            makeCell(text(is_selected ? " > " : "   ") | bold | color(theme->getBorderColor()),
                     kCursorWidth));

        if (show_category) {
            row.push_back(
                makeCell(text(entry.category) | bold | color(theme->getSearchHighlightColor()),
                         kCategoryWidth));
        } else {
            row.push_back(makeCell(text(""), kCategoryWidth));
        }

        // shortcut
        row.push_back(makeCell(text(entry.shortcut) | bold | color(theme->getForegroundColor()),
                               kShortcutWidth));

        // mode
        const std::string mode_label = modeLabel(entry.mode);
        row.push_back(makeCell(text(mode_label) | (mode_label.empty() ? nothing : dim) |
                                   color(theme->getBorderColor()),
                               kModeWidth));

        // description
        row.push_back(text(" "));
        row.push_back(text(entry.description) | color(theme->getForegroundColor()) | flex);

        return hbox(std::move(row)) |
               bgcolor(is_selected ? theme->getSelectionColor() : theme->getBackgroundColor());
    }

    [[nodiscard]] ftxui::Element renderBody(const HelpMenuModel& model,
                                            int visible_begin,
                                            int visible_end,
                                            int selected_index) const {
        using namespace ftxui;

        if (model.filteredCount() == 0U) {
            return text("[No matching shortcuts]") | dim | center |
                   color(theme->getForegroundColor());
        }

        Elements body;
        for (int index = visible_begin; index < visible_end; ++index) {
            const auto& entry = model.filteredEntry(static_cast<std::size_t>(index));
            const bool show_category =
                index == visible_begin ||
                model.filteredEntry(static_cast<std::size_t>(index - 1)).category != entry.category;

            if (show_category) {
                body.push_back(separatorLight() | color(theme->getBorderColor()));
            }
            body.push_back(renderShortcutRow(entry, show_category, index == selected_index));
        }
        return vbox(std::move(body));
    }

    [[nodiscard]] ftxui::Element renderFilterBar(const HelpMenuModel& model,
                                                 bool filter_mode,
                                                 int selected_index) const {
        using namespace ftxui;

        const std::string filter_label =
            model.filter().empty() ? "<none>" : std::string{model.filter()};
        const std::string selection_label =
            model.filteredCount() == 0U
                ? "0/0"
                : std::format("{}/{}", selected_index + 1, model.filteredCount());

        auto filter_value =
            text(filter_label) | (filter_mode ? (color(theme->getBorderColor()) | bold)
                                              : (color(theme->getForegroundColor()) | dim));

        return hbox({
                   text(" Filter: ") | bold | color(theme->getForegroundColor()),
                   std::move(filter_value),
                   filler(),
                   text(selection_label) | color(theme->getForegroundColor()) | dim,
               }) |
               bgcolor(theme->getStatusBarColor());
    }

    [[nodiscard]] ftxui::Element renderHeaderRow() const {
        using namespace ftxui;
        return hbox({
                   makeCell(text(""), kCursorWidth),
                   makeCell(text("Category") | bold, kCategoryWidth),
                   makeCell(text("Shortcut") | bold, kShortcutWidth),
                   makeCell(text("Mode") | bold, kModeWidth),
                   text(" Description") | bold,
               }) |
               color(theme->getForegroundColor()) | bgcolor(theme->getStatusBarColor());
    }

    [[nodiscard]] ftxui::Element render(const HelpMenuModel& model,
                                        bool filter_mode,
                                        HelpViewport viewport) const {
        using namespace ftxui;

        const HelpViewport clamped = clamp_help_viewport(viewport, model);
        const auto [visible_begin, visible_end] =
            calculateVisibleRange(model, clamped);  // C++17 structured binding

        static constexpr int kChromeRows = 8;

        // combine all the pieces together
        return vbox({
                   // title
                   text("Keyboard Shortcuts") | bold | center | color(theme->getForegroundColor()) |
                       bgcolor(theme->getStatusBarColor()),
                   themedSeparator(),

                   // header
                   renderHeaderRow(),
                   themedSeparator(),

                   // filter bar
                   renderFilterBar(model, filter_mode, clamped.selectedIndex),
                   themedSeparator(),

                   // help menu body
                   renderBody(model, visible_begin, visible_end, clamped.selectedIndex) | flex,
                   themedSeparator(),

                   // footer
                   text("[j/k] Move  [f] Filter  [Enter] Done  [Esc/~] Close") | dim |
                       color(theme->getForegroundColor()) | bgcolor(theme->getStatusBarColor()) |
                       center,
               }) |
               borderRounded | color(theme->getBorderColor()) |
               bgcolor(theme->getBackgroundColor()) | size(WIDTH, EQUAL, 104) |
               size(HEIGHT, EQUAL, clamped.viewportRows + kChromeRows);
    }
};

HelpMenuComponent::HelpMenuComponent(const Theme* theme)
    : impl_(std::make_unique<Impl>(theme ? theme : &global_theme())) {}

HelpMenuComponent::~HelpMenuComponent() = default;
HelpMenuComponent::HelpMenuComponent(HelpMenuComponent&&) noexcept = default;
HelpMenuComponent& HelpMenuComponent::operator=(HelpMenuComponent&&) noexcept = default;

ftxui::Element HelpMenuComponent::render(const HelpMenuModel& model,
                                         bool filter_mode,
                                         HelpViewport viewport) const {
    return impl_->render(model, filter_mode, viewport);
}

void HelpMenuComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

}  // namespace expp::ui
