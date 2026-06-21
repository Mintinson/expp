#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <utility>

namespace expp::ui {

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
        elements.reserve(7);
        elements.push_back(text(title) | bold | center);
        elements.push_back(separator());
        elements.push_back(text(message) | center);
        elements.push_back(separator());
        elements.push_back(hbox({text(" > "), std::move(input_component) | flex}) | border);
        elements.push_back(separator());
        elements.push_back(text("[Enter] Confirm  [Esc] Cancel") | dim | center);
        return vbox(std::move(elements)) | border | size(WIDTH, EQUAL, 55) | center;
    }

    [[nodiscard]] static ftxui::Element renderMessage(const std::string& title,
                                                      const std::string& message) {
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

ftxui::Element DialogComponent::renderMessage(const std::string& title,
                                              const std::string& message) const {
    return Impl::renderMessage(title, message);
}

void DialogComponent::setTheme(const Theme* theme) {
    impl_->theme = theme;
}

}  // namespace expp::ui
