#include "expp/ui/components.hpp"

#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <string>
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
                                        int current_match_index) const {
        using namespace ftxui;

        Elements elements;

        std::unordered_set<int> search_match_set(search_matches.begin(), search_matches.end());

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];
            bool is_selected = static_cast<int>(i) == selected;
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
                                         int current_match_index) const {
    return impl_->render(entries, selected, search_matches, current_match_index);
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
        Elements combined;
        for (auto& elem : left_elements) {
            combined.push_back(std::move(elem));
        }
        combined.push_back(filler());
        for (auto& elem : right_elements) {
            combined.push_back(std::move(elem));
        }

        return hbox(std::move(combined));
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

    [[nodiscard]] ftxui::Element render(const core::filesystem::FileEntry& entry) const {
        using namespace ftxui;
        auto preview_result = core::filesystem::read_preview(entry.path);

        if (preview_result) {
            return renderLines(*preview_result);
        }
        // Show error message
        std::string err_msg = config.errorPrefix + preview_result.error().message() + "]";
        return text(err_msg) | color(Color::Red) | dim;
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines) const {
        using namespace ftxui;
        if (lines.empty()) {
            return text(config.emptyMessage) | dim | center;
        }

        ftxui::Elements elements;
        int line_count = std::min(static_cast<int>(lines.size()), config.maxLines);

        for (int i = 0; i < line_count; ++i) {
            elements.push_back(text(lines[static_cast<size_t>(i)]));
        }

        if (lines.size() > static_cast<size_t>(config.maxLines)) {
            elements.push_back(
                text("... (" + std::to_string(lines.size() - static_cast<size_t>(config.maxLines)) + " more lines)") |
                dim);
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