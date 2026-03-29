#include "expp/ui/components.hpp"

#include <algorithm>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <expp/core/filesystem.hpp>
#include <expp/ui/theme.hpp>


namespace expp::ui {

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

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            bool is_selected = static_cast<int>(i) == selected;
            bool is_visual_selected = selected_index_set.contains(static_cast<int>(i));
            bool is_search_match = !search_matches.empty() && search_match_set.contains(static_cast<int>(i));
            bool is_current_match = is_search_match && (current_match_index != -1) &&
                                    (search_matches[static_cast<size_t>(current_match_index)] == static_cast<int>(i));

            auto base_color = config.theme->getFileEntryColor(entry);

            // Build display text with prefix
            std::string prefix = (static_cast<int>(i) == selected) ? config.selectionPrefix : config.normalPrefix;
            std::string display_name = entry.filename();
            if (entry.isSymlink()) {
                if (!entry.symlinkTarget.empty()) {
                    display_name += " -> " + entry.symlinkTarget.string();
                }
                if (entry.isRecursiveSymlink) {
                    display_name += " [loop]";
                    base_color = ftxui::Color::Red;
                } else if (entry.isBrokenSymlink) {
                    display_name += " [broken]";
                    base_color = ftxui::Color::Red;
                }
            }

            // Create basic element
            auto element =
                text(std::format("{}{} {}", prefix, config.showIcons ? config.theme->getFileTypeIcon(entry) : "",
                                 display_name)) |
                color(base_color);

            // Apply directory bold styling
            if (config.boldDirectories && entry.isDirectory()) {
                element |= bold;
            }

            // Highlight search matches
            if (config.enableHighlight && is_current_match) {
                element |= bgcolor(config.theme->getSearchHighlightColor()) | color(Color::Black);
            }

            // Apply selection inversion
            if (is_selected) {
                element |= inverted;
            } else if (is_visual_selected) {
                // TODO: the color may be configurable, and should also consider theme contrast
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
public:
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

        // combine left and right with filler in between
        return hbox({hbox(std::move(left_elements)), filler(), hbox(std::move(right_elements))}) |
               bgcolor(theme->getStatusBarColor());

        // TODO: which one is better.
        // Elements combined;
        // for (auto& elem : left_elements) {
        //     combined.push_back(std::move(elem));
        // }
        // combined.push_back(filler());
        // for (auto& elem : right_elements) {
        //     combined.push_back(std::move(elem));
        // }

        // return hbox(std::move(combined));
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
public:
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

[[nodiscard]] std::string key_sequence_to_string(const expp::ui::KeyBinding& binding) {
    std::string rendered;
    for (const auto& key : binding.sequence) {
        rendered += expp::ui::key_to_string(key);
    }
    return rendered;
}

/**
 * @brief Converts a string to lowercase
 *
 * @param text Input string
 * @return std::string Lowercase copy of the input string
 */
[[nodiscard]] std::string lowercase_copy(std::string_view text) {
    return text | std::views::transform([](auto ch) { return static_cast<char>(std::tolower(ch)); }) |
           std::ranges::to<std::string>();
}

}  // namespace

std::vector<HelpEntry> build_help_entries(std::span<const Action> actions, std::span<const KeyBinding> bindings) {
    std::unordered_map<std::string_view, const Action*> action_index;
    action_index.reserve(actions.size());
    for (const auto& action : actions) {
        action_index.emplace(action.name, std::addressof(action));
    }

    std::vector<HelpEntry> entries;
    entries.reserve(bindings.size());

    for (const auto& binding : bindings) {
        const auto action_it = action_index.find(binding.actionName);
        if (action_it == action_index.end()) {
            continue;
        }

        const auto& action = *action_it->second;
        entries.push_back(HelpEntry{
            .category = action.category,
            .shortcut = key_sequence_to_string(binding),
            .description = binding.description.empty()
                               ? action.description
                               : binding.description,  // override with binding description if provided
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

std::vector<HelpEntry> filter_help_entries(std::span<const HelpEntry> entries, std::string_view filter) {
    // TODO: it is necessary to have entries copy? Can we do lazy filtering with views instead?
    const std::string normalized_filter = lowercase_copy(filter);
    if (normalized_filter.empty()) {
        return {entries.begin(), entries.end()};
    }
    auto filter_pred = [&normalized_filter](const HelpEntry& entry) {
        const std::string normalized_shortcut = lowercase_copy(entry.shortcut);
        const std::string normalized_description = lowercase_copy(entry.description);
        return normalized_shortcut.contains(normalized_filter) || normalized_description.contains(normalized_filter);
    };
    return entries | std::views::filter(filter_pred) | std::ranges::to<std::vector>();

    // std::vector<HelpEntry> filtered;
    // filtered.reserve(entries.size());
    // for (const auto& entry : entries) {
    //     const std::string normalized_shortcut = lowercase_copy(entry.shortcut);
    //     const std::string normalized_description = lowercase_copy(entry.description);
    //     if (normalized_shortcut.contains(normalized_filter) || normalized_description.contains(normalized_filter)) {
    //         filtered.push_back(entry);
    //     }
    // }
    // return filtered;
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

    // Keep selection within the visible window using 25/75 thresholds
    const int top_margin = viewport.viewportRows / 4;
    const int bottom_margin = (viewport.viewportRows * 3) / 4;

    // If selection is above the visible top margin, scroll up
    if (viewport.selectedIndex < viewport.scrollOffset + top_margin) {
        viewport.scrollOffset = std::max(0, viewport.selectedIndex - top_margin);
    }

    // If selection is below the visible bottom margin, scroll down
    if (viewport.selectedIndex >= viewport.scrollOffset + bottom_margin) {
        viewport.scrollOffset = viewport.selectedIndex - bottom_margin + 1;
    }

    // Hard guarantee: selected index must be within [scrollOffset, scrollOffset + viewportRows)
    viewport.scrollOffset = std::min(viewport.selectedIndex, viewport.scrollOffset);
    if (viewport.selectedIndex >= viewport.scrollOffset + viewport.viewportRows) {
        viewport.scrollOffset = viewport.selectedIndex - viewport.viewportRows + 1;
    }

    viewport.scrollOffset = std::clamp(viewport.scrollOffset, 0, max_offset);
    return viewport;
}

struct HelpMenuComponent::Impl {
public:
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

    [[nodiscard]] ftxui::Element render(std::span<const HelpEntry> entries,
                                        std::string_view filter_text,
                                        bool filter_mode,
                                        HelpViewport viewport) const {
        using namespace ftxui;

        const HelpViewport clamped = clamp_help_viewport(viewport, entries.size());
        const int visible_begin = clamped.scrollOffset;

        // Compute visible_end dynamically: category headers consume a row each
        int rows_remaining = clamped.viewportRows;
        int visible_end = visible_begin;
        for (int i = visible_begin; i < static_cast<int>(entries.size()) && rows_remaining > 0; ++i) {
            bool has_header =
                (i == visible_begin) || (entries[static_cast<size_t>(i - 1)].category != entries[static_cast<size_t>(i)].category);
            int cost = has_header ? 2 : 1;
            if (rows_remaining < cost && visible_end > visible_begin) {
                break;
            }
            rows_remaining -= cost;
            visible_end = i + 1;
        }

        // Count actual visual rows for sizing
        int body_rows = clamped.viewportRows - rows_remaining;

        Elements body;
        if (entries.empty()) {
            body.push_back(text("[No matching shortcuts]") | dim | center | color(theme->getForegroundColor()));
        } else {
            for (int index = visible_begin; index < visible_end; ++index) {
                const auto& entry = entries[static_cast<size_t>(index)];
                const bool show_category =
                    index == visible_begin || entries[static_cast<size_t>(index - 1)].category != entry.category;
                const bool is_selected = index == clamped.selectedIndex;

                if (show_category) {
                    body.push_back(separatorLight() | color(theme->getBorderColor()));
                    // Category label overlaid on the separator line — not an extra row
                }

                Elements row;
                row.push_back(
                    makeCell(text(is_selected ? " > " : "   ") | bold | color(theme->getBorderColor()), kCursorWidth));

                // Show category badge on first entry of each group
                if (show_category) {
                    row.push_back(
                        makeCell(text(entry.category) | bold | color(theme->getSearchHighlightColor()), kCategoryWidth));
                } else {
                    row.push_back(makeCell(text(""), kCategoryWidth));
                }

                row.push_back(
                    makeCell(text(entry.shortcut) | bold | color(theme->getForegroundColor()), kShortcutWidth));

                const std::string mode_label = modeLabel(entry.mode);
                if (!mode_label.empty()) {
                    row.push_back(makeCell(text(mode_label) | dim | color(theme->getBorderColor()), kModeWidth));
                } else {
                    row.push_back(makeCell(text(""), kModeWidth));
                }

                row.push_back(text(" "));
                row.push_back(text(entry.description) | color(theme->getForegroundColor()) | flex);

                auto row_element = hbox(std::move(row));
                if (is_selected) {
                    row_element |= bgcolor(theme->getSelectionColor());
                } else {
                    row_element |= bgcolor(theme->getBackgroundColor());
                }

                body.push_back(std::move(row_element));
            }
        }

        const std::string filter_label = filter_text.empty() ? "<none>" : std::string{filter_text};
        const std::string selection_label =
            entries.empty() ? "0/0" : std::format("{}/{}", clamped.selectedIndex + 1, entries.size());
        auto filter_value = text(filter_label);
        if (filter_mode) {
            filter_value = std::move(filter_value) | color(theme->getBorderColor()) | bold;
        } else {
            filter_value = std::move(filter_value) | color(theme->getForegroundColor()) | dim;
        }

        auto header_row = hbox({
                              makeCell(text(""), kCursorWidth),
                              makeCell(text("Category") | bold | color(theme->getForegroundColor()), kCategoryWidth),
                              makeCell(text("Shortcut") | bold | color(theme->getForegroundColor()), kShortcutWidth),
                              makeCell(text("Mode") | bold | color(theme->getForegroundColor()), kModeWidth),
                              text(" Description") | bold | color(theme->getForegroundColor()),
                          }) |
                          bgcolor(theme->getStatusBarColor());

        static constexpr int kChromeRows = 8;
        return vbox({
                   text("Keyboard Shortcuts") | bold | center | color(theme->getForegroundColor()) |
                       bgcolor(theme->getStatusBarColor()),
                   themedSeparator(),
                   std::move(header_row),
                   themedSeparator(),
                   hbox({
                       text(" Filter: ") | bold | color(theme->getForegroundColor()) |
                           bgcolor(theme->getStatusBarColor()),
                       std::move(filter_value),
                       filler(),
                       text(selection_label) | color(theme->getForegroundColor()) | dim |
                           bgcolor(theme->getStatusBarColor()),
                   }) | bgcolor(theme->getStatusBarColor()),
                   themedSeparator(),
                   vbox(std::move(body)) | flex | bgcolor(theme->getBackgroundColor()),
                   themedSeparator(),
                   text("[j/k] Move  [f] Filter  [Enter] Done  [Esc/~] Close") | dim |
                       color(theme->getForegroundColor()) | bgcolor(theme->getStatusBarColor()) | center,
               }) |
               borderRounded | color(theme->getBorderColor()) | bgcolor(theme->getBackgroundColor()) |
               size(WIDTH, EQUAL, 104) | size(HEIGHT, EQUAL, body_rows + kChromeRows);
    }
};

HelpMenuComponent::HelpMenuComponent(const Theme* theme) : impl_(std::make_unique<Impl>(theme)) {}

HelpMenuComponent::~HelpMenuComponent() = default;

HelpMenuComponent::HelpMenuComponent(HelpMenuComponent&&) noexcept = default;
HelpMenuComponent& HelpMenuComponent::operator=(HelpMenuComponent&&) noexcept = default;

ftxui::Element HelpMenuComponent::render(std::span<const HelpEntry> entries,
                                         std::string_view filter_text,
                                         bool filter_mode,
                                         HelpViewport viewport) const {
    return impl_->render(entries, filter_text, filter_mode, viewport);
}

void HelpMenuComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// DialogComponent Implementation
// ============================================================================
struct DialogComponent::Impl {
public:
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

        // Add message lines
        elements.push_back(text(message) | center);
        elements.push_back(separator());

        // Add input field
        elements.push_back(hbox({
                               text(" > "),
                               std::move(input_component) | flex,
                           }) |
                           border);

        elements.push_back(separator());
        elements.push_back(text("[Enter] Confirm  [Esc] Cancel") | dim | center);

        return vbox(std::move(elements)) | border | size(WIDTH, EQUAL, 55) | center;
    }

    [[nodiscard]] ftxui::Element renderMessage(const std::string& title, const std::string& message) const {
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
    return expp::ui::DialogComponent::Impl::renderConfirmation(title, message, target_name, target_color);
}

ftxui::Element DialogComponent::renderInput(const std::string& title,
                                            const std::string& message,
                                            ftxui::Element input_component) const {
    return impl_->renderInput(title, message, std::move(input_component));
}

ftxui::Element DialogComponent::renderMessage(const std::string& title, const std::string& message) const {
    return impl_->renderMessage(title, message);
}

void DialogComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

// ============================================================================
// PanelComponent Implementation
// ============================================================================
struct PanelComponent::Impl {
public:
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

        // Parent column (optional)
        if (config.showParent) {
            columns.push_back(
                vbox({text(parent_title) | bold | center, separator(), std::move(parent_content) | frame | flex}) |
                border | size(WIDTH, EQUAL, config.parentWidth));
        }

        // Current directory column (always shown)
        columns.push_back(
            vbox({text(current_title) | bold | center, separator(), std::move(current_content) | frame | flex}) |
            border | flex);

        // Preview column (optional)
        if (config.showPreview) {
            columns.push_back(
                vbox({text(preview_title) | bold | center, separator(), std::move(preview_content) | frame | flex}) |
                border | size(WIDTH, EQUAL, config.previewWidth));
        }

        // return hbox(std::move(columns)) | border | color(config.theme->getBorderColor());
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
public:
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

    [[nodiscard]] ftxui::Element render(const core::filesystem::FileEntry& entry) const {
        using namespace ftxui;
        const int max_lines = resolveMaxLines();

        auto preview_result = core::filesystem::read_preview(entry.path, max_lines);

        if (preview_result) {
            return renderLines(*preview_result, max_lines);
        }
        // Show error message
        std::string err_msg = config.errorPrefix + preview_result.error().message() + "]";
        return text(err_msg) | color(Color::Red) | dim;
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines) const {
        return renderLines(lines, resolveMaxLines());
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines, int max_lines) const {
        using namespace ftxui;
        if (lines.empty()) {
            return text(config.emptyMessage) | dim | center;
        }

        ftxui::Elements elements;
        const int line_count = std::min(static_cast<int>(lines.size()), max_lines);

        for (int i = 0; i < line_count; ++i) {
            elements.push_back(text(lines[static_cast<size_t>(i)]));
        }

        if (lines.size() > static_cast<size_t>(max_lines)) {
            elements.push_back(
                text("... (" + std::to_string(lines.size() - static_cast<size_t>(max_lines)) + " more lines)") | dim);
        }

        return vbox(std::move(elements));
    }
};

PreviewComponent::PreviewComponent(const PreviewConfig& config) : impl_(std::make_unique<Impl>(config)) {}

PreviewComponent::~PreviewComponent() = default;

PreviewComponent::PreviewComponent(PreviewComponent&&) noexcept = default;
PreviewComponent& PreviewComponent::operator=(PreviewComponent&&) noexcept = default;

ftxui::Element PreviewComponent::render(const core::filesystem::FileEntry& entry) const {
    return impl_->render(entry);
}

ftxui::Element PreviewComponent::renderLines(const std::vector<std::string>& lines) const {
    return impl_->renderLines(lines);
}

void PreviewComponent::setConfig(const PreviewConfig& config) {
    impl_->config = config;
}
}  // namespace expp::ui
