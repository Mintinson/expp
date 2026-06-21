#include "expp/ui/components.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace expp::ui {

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
            columns.push_back(vbox({text(parent_title) | bold | center, separator(),
                                    std::move(parent_content) | frame | flex}) |
                              border | size(WIDTH, EQUAL, config.parentWidth));
        }

        columns.push_back(vbox({text(current_title) | bold | center, separator(),
                                std::move(current_content) | frame | flex}) |
                          border | flex);

        if (config.showPreview) {
            columns.push_back(vbox({text(preview_title) | bold | center, separator(),
                                    std::move(preview_content) | frame | flex}) |
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
    return impl_->render(parent_title, std::move(parent_content), current_title,
                         std::move(current_content), preview_title, std::move(preview_content));
}

void PanelComponent::setConfig(const PanelConfig& config) {
    impl_->config = config;
}

}  // namespace expp::ui
