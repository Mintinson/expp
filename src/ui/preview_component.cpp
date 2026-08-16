#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace expp::ui {

struct PreviewComponent::Impl {
    explicit Impl(PreviewRenderConfig in_config) : config(std::move(in_config)) {}

    PreviewRenderConfig config;

    [[nodiscard]] const Theme& theme() const noexcept {
        return config.theme != nullptr ? *config.theme : global_theme();
    }

    [[nodiscard]] int resolveMaxLines() const {
        int max_lines = config.maxRenderLines;
        if (max_lines < 0) {
            const auto terminal_size = ftxui::Terminal::Size();
            if (terminal_size.dimy > 0) {
                max_lines = terminal_size.dimy;
            }
        }
        return std::max(1, max_lines);
    }

    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines,
                                             int max_lines = -1) const {
        using namespace ftxui;
        if (lines.empty()) {
            return text(config.emptyMessage) | dim | center;
        }
        if (max_lines < 0) {
            max_lines = static_cast<int>(lines.size());
        }

        Elements elements;
        const int line_count = std::min(static_cast<int>(lines.size()), max_lines);
        for (int index = 0; index < line_count; ++index) {
            elements.push_back(text(lines[static_cast<std::size_t>(index)]));
        }
        if (lines.size() > static_cast<std::size_t>(max_lines)) {
            elements.push_back(
                text("... (" + std::to_string(lines.size() - static_cast<std::size_t>(max_lines)) +
                     " more lines)") |
                dim);
        }
        return vbox(std::move(elements));
    }

    [[nodiscard]] ftxui::Element renderFragment(const app::RichTextFragment& fragment) const {
        using namespace ftxui;
        auto element = text(fragment.text) | color(theme().getPreviewTextColor(fragment.role));
        switch (fragment.role) {
            case app::PreviewTextRole::Keyword:
                return element | bold;
            case app::PreviewTextRole::Comment:
                return element | dim;
            case app::PreviewTextRole::String:
            case app::PreviewTextRole::Number:
            case app::PreviewTextRole::Type:
            case app::PreviewTextRole::Diagnostic:
            case app::PreviewTextRole::Normal:
            default:
                return element;
        }
    }

    [[nodiscard]] ftxui::Element renderRichText(const app::RichTextPreview& preview,
                                                int max_lines) const {
        using namespace ftxui;
        Elements rendered;
        const auto line_count = std::min(preview.lines.size(), static_cast<std::size_t>(max_lines));
        rendered.reserve(line_count);
        for (std::size_t index = 0; index < line_count; ++index) {
            Elements fragments;
            fragments.reserve(preview.lines[index].size());
            for (const auto& fragment : preview.lines[index]) {
                fragments.push_back(renderFragment(fragment));
            }
            rendered.push_back(hbox(std::move(fragments)));
        }
        return rendered.empty() ? text(config.emptyMessage) | dim | center
                                : vbox(std::move(rendered));
    }

    [[nodiscard]] ftxui::Element renderContent(const app::PreviewReadyState& state,
                                               int max_lines) const {
        return std::visit(
            [&](const auto& content) -> ftxui::Element {
                using Content = std::decay_t<decltype(content)>;
                if constexpr (std::is_same_v<Content, app::PlainTextPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::RichTextPreview>) {
                    return renderRichText(content, max_lines);
                } else if constexpr (std::is_same_v<Content, app::ListingPreview>) {
                    return renderLines(content.entries.empty() ? state.lines : content.entries,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::HexDumpPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else if constexpr (std::is_same_v<Content, app::MetadataPreview>) {
                    return renderLines(content.lines.empty() ? state.lines : content.lines,
                                       max_lines);
                } else {
                    // FTXUI owns the terminal screen buffer, so raw image protocol
                    // escapes are carried in the data bus but represented by the
                    // provider's metadata lines during normal DOM rendering.
                    return renderLines(state.lines, max_lines);
                }
            },
            state.content);
    }

    [[nodiscard]] ftxui::Element render(const app::PreviewModel& model) const {
        using namespace ftxui;
        const int max_lines = resolveMaxLines();

        return std::visit(
            [&](const auto& state) -> Element {
                using State = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<State, app::PreviewIdleState>) {
                    return text(config.emptyMessage) | dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewLoadingState>) {
                    return text("[Loading...]") | dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewReadyState>) {
                    auto content = renderContent(state, max_lines);
                    if (state.diagnostic.empty()) {
                        return content;
                    }
                    return vbox({std::move(content), text("[" + state.diagnostic + "]") |
                                                         color(theme().getPreviewTextColor(
                                                             app::PreviewTextRole::Diagnostic)) |
                                                         dim});
                } else {
                    return text(config.errorPrefix + state.message + "]") |
                           color(theme().getPreviewTextColor(app::PreviewTextRole::Diagnostic)) |
                           dim;
                }
            },
            model);
    }
};

PreviewComponent::PreviewComponent(const PreviewRenderConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

PreviewComponent::~PreviewComponent() = default;
PreviewComponent::PreviewComponent(PreviewComponent&&) noexcept = default;
PreviewComponent& PreviewComponent::operator=(PreviewComponent&&) noexcept = default;

ftxui::Element PreviewComponent::render(const app::PreviewModel& model) const {
    return impl_->render(model);
}

ftxui::Element PreviewComponent::renderLines(const std::vector<std::string>& lines) const {
    return impl_->renderLines(lines, impl_->resolveMaxLines());
}

void PreviewComponent::setConfig(const PreviewRenderConfig& config) {
    impl_->config = config;
}

}  // namespace expp::ui
