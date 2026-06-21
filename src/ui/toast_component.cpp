#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>

#include <format>
#include <memory>
#include <string_view>

namespace expp::ui {

struct ToastComponent::Impl {
    explicit Impl(const Theme* in_theme) : theme{ in_theme } {}

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
                   text(std::format(" {} ", severityLabel(toast.severity))) | bold |
                       color(Color::Black) | bgcolor(accent),
                   text(" "),
                   text(toast.message) | color(theme->getForegroundColor()),
               }) |
               bgcolor(theme->getStatusBarColor()) | borderRounded | clear_under;
    }
};

ToastComponent::ToastComponent(const Theme* theme)
    : impl_(std::make_unique<Impl>(theme ? theme : &global_theme())) {}

ToastComponent::~ToastComponent() = default;
ToastComponent::ToastComponent(ToastComponent&&) noexcept = default;
ToastComponent& ToastComponent::operator=(ToastComponent&&) noexcept = default;

ftxui::Element ToastComponent::render(const ToastInfo& toast) const {
    return impl_->render(toast);
}

void ToastComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

}  // namespace expp::ui
