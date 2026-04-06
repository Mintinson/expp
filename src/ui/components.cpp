#include "expp/ui/components.hpp"

#include "expp/ui/help_menu_model.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace expp::ui {
namespace {
/**
 * @brief Returns the properly formatted display name for a file entry and whether it should be highlighted as an error.
 *
 * @param entry The file entry for which to generate a display name.
 * @return std::pair<std::string, bool> The formatted display name and a boolean indicating whether it should be
 * highlighted as an error.
 */
[[nodiscard]] std::pair<std::string, bool> proper_display_name(const core::filesystem::FileEntry& entry) {
    std::string name = entry.filename();
    if (entry.isSymlink()) {
        if (!entry.symlinkTarget.empty()) {
            name += " -> " + entry.symlinkTarget.string();
        }
        if (entry.isRecursiveSymlink || entry.isBrokenSymlink) {
            name += entry.isRecursiveSymlink ? " [loop]" : " [broken]";
            return {name, true};
        }
    }
    return {name, false};
}
}  // namespace

struct FileListComponent::Impl {
    explicit Impl(FileListConfig file_config) : config(std::move(file_config)) {}

    FileListConfig config;

    [[nodiscard]] ftxui::Element render(std::span<const core::filesystem::FileEntry> entries,
                                        int selected,
                                        const std::vector<int>& search_matches,
                                        int current_match_index,
                                        const std::vector<int>& selected_indices) const {
        using namespace ftxui;

        Elements elements;
        std::unordered_set<int> search_match_set(search_matches.begin(), search_matches.end());
        std::unordered_set<int> selected_index_set(selected_indices.begin(), selected_indices.end());

        elements.reserve(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const bool is_selected = static_cast<int>(index) == selected;
            const bool is_visual_selected = selected_index_set.contains(static_cast<int>(index));
            const bool is_search_match = !search_matches.empty() && search_match_set.contains(static_cast<int>(index));
            const bool is_current_match =
                is_search_match && current_match_index >= 0 &&
                search_matches[static_cast<std::size_t>(current_match_index)] == static_cast<int>(index);

            std::string prefix = is_selected ? config.selectionPrefix : config.normalPrefix;
            const auto [display_name, should_highlight] = proper_display_name(entry);
            // std::string display_name = entry.filename();

            auto base_color = should_highlight ? Color::Red : config.theme->getFileEntryColor(entry);

            auto element =
                text(std::format("{}{} {}", prefix, config.showIcons ? config.theme->getFileTypeIcon(entry) : "",
                                 display_name)) |
                color(base_color);

            if (config.boldDirectories && entry.isDirectory()) {
                element |= bold;
            }
            if (config.enableHighlight && is_current_match) {
                element |= bgcolor(config.theme->getSearchHighlightColor()) | color(Color::Black);
            }
            if (is_selected) {
                element |= inverted;
            } else if (is_visual_selected) {
                element |= bgcolor(Color::Blue) | color(Color::White);
            }

            elements.push_back(std::move(element));
        }

        if (elements.empty()) {
            elements.push_back(text("[Empty]") | dim | center);
        }

        return vbox(std::move(elements));
    }
};

FileListComponent::FileListComponent(const FileListConfig& config) : impl_(std::make_unique<Impl>(config)) {}
FileListComponent::~FileListComponent() = default;
FileListComponent::FileListComponent(FileListComponent&&) noexcept = default;
FileListComponent& FileListComponent::operator=(FileListComponent&&) noexcept = default;

ftxui::Element FileListComponent::render(std::span<const core::filesystem::FileEntry> entries,
                                         int selected,
                                         const std::vector<int>& search_matches,
                                         int current_match_index,
                                         const std::vector<int>& selected_indices) const {
    return impl_->render(entries, selected, search_matches, current_match_index, selected_indices);
}

void FileListComponent::setConfig(FileListConfig config) {
    impl_->config = std::move(config);
}

// ============================================================================
// StatusBarComponent Implementation
// ============================================================================
struct StatusBarComponent::Impl {
    explicit Impl(const Theme* in_theme) : theme(in_theme) {}

    const Theme* theme;

    [[nodiscard]] ftxui::Element render(const StatusBarInfo& info) const {
        using namespace ftxui;

        Elements left_elements;
        Elements right_elements;

        // Left side: Path
        if (info.showPath && !info.currentPath.empty()) {
            left_elements.push_back(text(" Path: ") | bold);
            left_elements.push_back(text(info.currentPath) | color(theme->getForegroundColor()) | dim);
        }
        // Right side: Search status, key buffer, help text
        if (!info.searchStatus.empty()) {
            right_elements.push_back(text(info.searchStatus) | color(theme->getSearchHighlightColor()) | bold);
        }
        if (!info.keyBuffer.empty()) {
            right_elements.push_back(text(" [" + info.keyBuffer + "] ") | color(Color::Cyan) | bold);
        }
        if (info.showHelp && !info.helpText.empty()) {
            right_elements.push_back(text(info.helpText) | color(theme->getForegroundColor()) | dim);
        }

        return hbox({hbox(std::move(left_elements)), filler(), hbox(std::move(right_elements))}) |
               bgcolor(theme->getStatusBarColor());
    }
};

StatusBarComponent::StatusBarComponent(const Theme* theme) : impl_(std::make_unique<Impl>(theme)) {}
StatusBarComponent::~StatusBarComponent() = default;
StatusBarComponent::StatusBarComponent(StatusBarComponent&&) noexcept = default;
StatusBarComponent& StatusBarComponent::operator=(StatusBarComponent&&) noexcept = default;

ftxui::Element StatusBarComponent::render(const StatusBarInfo& info) const {
    return impl_->render(info);
}

void StatusBarComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// ToastComponent Implementation
// ============================================================================
struct ToastComponent::Impl {
    explicit Impl(const Theme* in_theme) : theme(in_theme) {}

    const Theme* theme;

    [[nodiscard]] static ftxui::Color severityColor(ToastSeverity severity) {
        using ftxui::Color;
        switch (severity) {
            case ToastSeverity::Info:
                return Color::BlueLight;
            case ToastSeverity::Success:
                return Color::GreenLight;
            case ToastSeverity::Warning:
                return Color::YellowLight;
            case ToastSeverity::Error:
                return Color::RedLight;
            default:
                return Color::White;
        }
    }

    [[nodiscard]] static std::string_view severityLabel(ToastSeverity severity) {
        switch (severity) {
            case ToastSeverity::Info:
                return "INFO";
            case ToastSeverity::Success:
                return "OK";
            case ToastSeverity::Warning:
                return "WARN";
            case ToastSeverity::Error:
                return "ERR";
            default:
                return "LOG";
        }
    }

    [[nodiscard]] ftxui::Element render(const ToastInfo& toast) const {
        using namespace ftxui;

        const auto accent = severityColor(toast.severity);
        return hbox({
                   text(std::format(" {} ", severityLabel(toast.severity))) | bold | color(Color::Black) |
                       bgcolor(accent),
                   text(" "),
                   text(toast.message) | color(theme->getForegroundColor()),
               }) |
               bgcolor(theme->getStatusBarColor()) | borderRounded | clear_under;
    }
};

ToastComponent::ToastComponent(const Theme* theme) : impl_(std::make_unique<Impl>(theme)) {}
ToastComponent::~ToastComponent() = default;
ToastComponent::ToastComponent(ToastComponent&&) noexcept = default;
ToastComponent& ToastComponent::operator=(ToastComponent&&) noexcept = default;

ftxui::Element ToastComponent::render(const ToastInfo& toast) const {
    return impl_->render(toast);
}

void ToastComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// HelpMenu helpers and component
// ============================================================================
namespace {

/**
 * @brief Convert a key binding sequence to a human-readable string representation.
 *
 * @param binding
 * @return std::string
 */
[[nodiscard]] std::string key_sequence_to_string(const KeyBinding& binding) {
    return binding.sequence | std::views::transform(key_to_string) | std::views::join | std::ranges::to<std::string>();
}

}  // namespace

std::vector<HelpEntry> build_help_entries(std::span<const Action> actions, std::span<const KeyBinding> bindings) {
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
            label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
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

        for (int index = clamped.scrollOffset; index < static_cast<int>(model.filteredCount()) && rows_remaining > 0;
             ++index) {
            const auto& entry = model.filteredEntry(static_cast<std::size_t>(index));
            const bool has_header = index == clamped.scrollOffset ||
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

    [[nodiscard]] ftxui::Element renderShortcutRow(const HelpEntry& entry, bool show_category, bool is_selected) const {
        using namespace ftxui;

        Elements row;

        row.push_back(
            makeCell(text(is_selected ? " > " : "   ") | bold | color(theme->getBorderColor()), kCursorWidth));

        if (show_category) {
            row.push_back(
                makeCell(text(entry.category) | bold | color(theme->getSearchHighlightColor()), kCategoryWidth));
        } else {
            row.push_back(makeCell(text(""), kCategoryWidth));
        }

        // shortcut
        row.push_back(makeCell(text(entry.shortcut) | bold | color(theme->getForegroundColor()), kShortcutWidth));

        // mode
        const std::string mode_label = modeLabel(entry.mode);
        row.push_back(makeCell(text(mode_label) | (mode_label.empty() ? nothing : dim) | color(theme->getBorderColor()),
                               kModeWidth));

        // description
        row.push_back(text(" "));
        row.push_back(text(entry.description) | color(theme->getForegroundColor()) | flex);

        return hbox(std::move(row)) | bgcolor(is_selected ? theme->getSelectionColor() : theme->getBackgroundColor());
    }

    [[nodiscard]] ftxui::Element renderBody(const HelpMenuModel& model,
                                            int visible_begin,
                                            int visible_end,
                                            int selected_index) const {
        using namespace ftxui;

        if (model.filteredCount() == 0U) {
            return text("[No matching shortcuts]") | dim | center | color(theme->getForegroundColor());
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

        const std::string filter_label = model.filter().empty() ? "<none>" : std::string{model.filter()};
        const std::string selection_label =
            model.filteredCount() == 0U ? "0/0" : std::format("{}/{}", selected_index + 1, model.filteredCount());

        auto filter_value = text(filter_label) | (filter_mode ? (color(theme->getBorderColor()) | bold)
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

    [[nodiscard]] ftxui::Element render(const HelpMenuModel& model, bool filter_mode, HelpViewport viewport) const {
        using namespace ftxui;

        const HelpViewport clamped = clamp_help_viewport(viewport, model);
        const auto [visible_begin, visible_end] = calculateVisibleRange(model, clamped);  // C++17 结构化绑定

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
                       color(theme->getForegroundColor()) | bgcolor(theme->getStatusBarColor()) | center,
               }) |
               borderRounded | color(theme->getBorderColor()) | bgcolor(theme->getBackgroundColor()) |
               size(WIDTH, EQUAL, 104) | size(HEIGHT, EQUAL, clamped.viewportRows + kChromeRows);
    }
};

HelpMenuComponent::HelpMenuComponent(const Theme* theme) : impl_(std::make_unique<Impl>(theme)) {}
HelpMenuComponent::~HelpMenuComponent() = default;
HelpMenuComponent::HelpMenuComponent(HelpMenuComponent&&) noexcept = default;
HelpMenuComponent& HelpMenuComponent::operator=(HelpMenuComponent&&) noexcept = default;

ftxui::Element HelpMenuComponent::render(const HelpMenuModel& model, bool filter_mode, HelpViewport viewport) const {
    return impl_->render(model, filter_mode, viewport);
}

void HelpMenuComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// DialogComponent Implementation
// ============================================================================
struct DialogComponent::Impl {
    const Theme* theme = &global_theme();

    [[nodiscard]] static ftxui::Element renderConfirmation(const std::string& title,
                                                           const std::string& message,
                                                           const std::string& target_name,
                                                           ftxui::Color target_color) {
        using namespace ftxui;
        return vbox({
                   text(title) | bold | center,
                   separator(),
                   text(message) | center,
                   text(target_name) | bold | center | color(target_color),
                   separator(),
                   text("[y] Yes  [n] No  [Esc] Cancel") | dim | center,
               }) |
               border | size(WIDTH, EQUAL, 50) | center;
    }

    [[nodiscard]] static ftxui::Element renderInput(const std::string& title,
                                                    const std::string& message,
                                                    ftxui::Element input_component) {
        using namespace ftxui;
        Elements elements;
        elements.push_back(text(title) | bold | center);
        elements.push_back(separator());
        elements.push_back(text(message) | center);
        elements.push_back(separator());
        elements.push_back(hbox({text(" > "), std::move(input_component) | flex}) | border);
        elements.push_back(separator());
        elements.push_back(text("[Enter] Confirm  [Esc] Cancel") | dim | center);
        return vbox(std::move(elements)) | border | size(WIDTH, EQUAL, 55) | center;
    }

    [[nodiscard]] static ftxui::Element renderMessage(const std::string& title, const std::string& message) {
        using namespace ftxui;
        return vbox({
                   text(title) | bold | center,
                   separator(),
                   text(message) | center,
                   separator(),
                   text("[Enter] OK") | dim | center,
               }) |
               border | size(WIDTH, EQUAL, 50) | center;
    }
};

DialogComponent::DialogComponent() : impl_(std::make_unique<Impl>()) {}
DialogComponent::~DialogComponent() = default;
DialogComponent::DialogComponent(DialogComponent&&) noexcept = default;
DialogComponent& DialogComponent::operator=(DialogComponent&&) noexcept = default;

ftxui::Element DialogComponent::renderConfirmation(const std::string& title,
                                                   const std::string& message,
                                                   const std::string& target_name,
                                                   ftxui::Color target_color) const {
    return Impl::renderConfirmation(title, message, target_name, target_color);
}

ftxui::Element DialogComponent::renderInput(const std::string& title,
                                            const std::string& message,
                                            ftxui::Element input_component) const {
    return Impl::renderInput(title, message, std::move(input_component));
}

ftxui::Element DialogComponent::renderMessage(const std::string& title, const std::string& message) const {
    return Impl::renderMessage(title, message);
}

void DialogComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// PanelComponent Implementation
// ============================================================================
struct PanelComponent::Impl {
    explicit Impl(const PanelConfig& in_config) : config(in_config) {}

    PanelConfig config;

    [[nodiscard]] ftxui::Element render(const std::string& parent_title,
                                        ftxui::Element parent_content,
                                        const std::string& current_title,
                                        ftxui::Element current_content,
                                        const std::string& preview_title,
                                        ftxui::Element preview_content) const {
        using namespace ftxui;

        Elements columns;
        if (config.showParent) {
            columns.push_back(
                vbox({text(parent_title) | bold | center, separator(), std::move(parent_content) | frame | flex}) |
                border | size(WIDTH, EQUAL, config.parentWidth));
        }

        columns.push_back(
            vbox({text(current_title) | bold | center, separator(), std::move(current_content) | frame | flex}) |
            border | flex);

        if (config.showPreview) {
            columns.push_back(
                vbox({text(preview_title) | bold | center, separator(), std::move(preview_content) | frame | flex}) |
                border | size(WIDTH, EQUAL, config.previewWidth));
        }

        return hbox(std::move(columns));
    }
};

PanelComponent::PanelComponent(const PanelConfig& config) : impl_(std::make_unique<Impl>(config)) {}
PanelComponent::~PanelComponent() = default;
PanelComponent::PanelComponent(PanelComponent&&) noexcept = default;
PanelComponent& PanelComponent::operator=(PanelComponent&&) noexcept = default;

ftxui::Element PanelComponent::render(const std::string& parent_title,
                                      ftxui::Element parent_content,
                                      const std::string& current_title,
                                      ftxui::Element current_content,
                                      const std::string& preview_title,
                                      ftxui::Element preview_content) const {
    return impl_->render(parent_title, std::move(parent_content), current_title, std::move(current_content),
                         preview_title, std::move(preview_content));
}

void PanelComponent::setConfig(const PanelConfig& config) {
    impl_->config = config;
}

// ============================================================================
// PreviewComponent Implementation
// ============================================================================
struct PreviewComponent::Impl {
    explicit Impl(PreviewConfig in_config) : config(std::move(in_config)) {}

    PreviewConfig config;

    [[nodiscard]] int resolveMaxLines() const {
        int max_lines = config.maxLines;
        if (max_lines < 0) {
            const auto terminal_size = ftxui::Terminal::Size();
            if (terminal_size.dimy > 0) {
                max_lines = terminal_size.dimy;
            }
        }
        return std::max(1, max_lines);
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines, int max_lines) const {
        using namespace ftxui;
        if (lines.empty()) {
            return text(config.emptyMessage) | dim | center;
        }

        Elements elements;
        const int line_count = std::min(static_cast<int>(lines.size()), max_lines);
        for (int index = 0; index < line_count; ++index) {
            elements.push_back(text(lines[static_cast<std::size_t>(index)]));
        }
        if (lines.size() > static_cast<std::size_t>(max_lines)) {
            elements.push_back(
                text("... (" + std::to_string(lines.size() - static_cast<std::size_t>(max_lines)) + " more lines)") |
                dim);
        }
        return vbox(std::move(elements));
    }

    [[nodiscard]] ftxui::Element render(const PreviewModel& model) const {
        using namespace ftxui;
        const int max_lines = resolveMaxLines();

        return std::visit(
            [&](const auto& state) -> Element {
                using State = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<State, PreviewIdleState>) {
                    return text(config.emptyMessage) | dim | center;
                } else if constexpr (std::is_same_v<State, PreviewLoadingState>) {
                    return text(std::format("[Loading: {}]", state.target.filename().string())) | dim | center;
                } else if constexpr (std::is_same_v<State, PreviewReadyState>) {
                    return renderLines(state.lines, max_lines);
                } else {
                    return text(config.errorPrefix + state.message + "]") | color(Color::Red) | dim;
                }
            },
            model);
    }
};

PreviewComponent::PreviewComponent(const PreviewConfig& config) : impl_(std::make_unique<Impl>(config)) {}
PreviewComponent::~PreviewComponent() = default;
PreviewComponent::PreviewComponent(PreviewComponent&&) noexcept = default;
PreviewComponent& PreviewComponent::operator=(PreviewComponent&&) noexcept = default;

ftxui::Element PreviewComponent::render(const PreviewModel& model) const {
    return impl_->render(model);
}

ftxui::Element PreviewComponent::renderLines(const std::vector<std::string>& lines) const {
    return impl_->renderLines(lines, impl_->resolveMaxLines());
}

void PreviewComponent::setConfig(const PreviewConfig& config) {
    impl_->config = config;
}

}  // namespace expp::ui
