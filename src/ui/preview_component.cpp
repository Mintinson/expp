#include "expp/ui/components.hpp"

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

    [[nodiscard]] ftxui::Element render(const app::PreviewModel& model) const {
        using namespace ftxui;
        const int max_lines = resolveMaxLines();

        return std::visit(
            [&](const auto& state) -> Element {
                using State = std::decay_t<decltype(state)>;
                if constexpr (std::is_same_v<State, app::PreviewIdleState>) {
                    return text(config.emptyMessage) | dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewLoadingState>) {
                    return text(std::format("[Loading: {}]", state.target.filename().string())) |
                           dim | center;
                } else if constexpr (std::is_same_v<State, app::PreviewReadyState>) {
                    return renderLines(state.lines, max_lines);
                } else {
                    return text(config.errorPrefix + state.message + "]") | color(Color::Red) | dim;
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
