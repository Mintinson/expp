#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <utility>

namespace expp::ui {

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
            left_elements.push_back(text(info.currentPath) | color(theme->getForegroundColor()) |
                                    dim);
        }
        // Right side: Search status, key buffer, help text
        if (!info.searchStatus.empty()) {
            right_elements.push_back(text(info.searchStatus) |
                                     color(theme->getSearchHighlightColor()) | bold);
        }
        if (!info.keyBuffer.empty()) {
            right_elements.push_back(text(" [" + info.keyBuffer + "] ") | color(Color::Cyan) |
                                     bold);
        }
        if (info.showHelp && !info.helpText.empty()) {
            right_elements.push_back(text(info.helpText) | color(theme->getForegroundColor()) |
                                     dim);
        }

        return hbox({hbox(std::move(left_elements)), filler(), hbox(std::move(right_elements))}) |
               bgcolor(theme->getStatusBarColor());
    }
};

StatusBarComponent::StatusBarComponent(const Theme* theme)
    : impl_(std::make_unique<Impl>(theme ? theme : &global_theme())) {}

StatusBarComponent::~StatusBarComponent() = default;
StatusBarComponent::StatusBarComponent(StatusBarComponent&&) noexcept = default;
StatusBarComponent& StatusBarComponent::operator=(StatusBarComponent&&) noexcept = default;

ftxui::Element StatusBarComponent::render(const StatusBarInfo& info) const {
    return impl_->render(info);
}

void StatusBarComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

}  // namespace expp::ui
