
#ifndef EXPP_EXPLORER_RENDER_COMPOSER_HPP
#define EXPP_EXPLORER_RENDER_COMPOSER_HPP

#include "expp/app/explorer.hpp"
#include "expp/app/explorer_presenter.hpp"  // for ExplorerScreenModel
#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <optional>

namespace expp::app {

class ExplorerRenderComposer {
public:
    explicit ExplorerRenderComposer(const ui::Theme* theme);

    [[nodiscard]] ftxui::Element compose(const ExplorerState& state,
                                         const ExplorerScreenModel& screen_model,
                                         const ui::PreviewModel& preview_model,
                                         const ExplorerOverlayState& overlay_state,
                                         ftxui::Component active_input,
                                         const std::optional<ui::ToastInfo>& current_toast);

private:
    const ui::Theme* theme_;

    std::unique_ptr<ui::FileListComponent> fileList_;
    std::unique_ptr<ui::PanelComponent> panel_;
    std::unique_ptr<ui::PreviewComponent> preview_;
    std::unique_ptr<ui::StatusBarComponent> statusBar_;
    std::unique_ptr<ui::ToastComponent> toast_;
    std::unique_ptr<ui::HelpMenuComponent> helpMenu_;
    std::unique_ptr<ui::DialogComponent> dialog_;

    [[nodiscard]] ftxui::Element composeMainLayout(const ExplorerState& state,
                                                   const ExplorerScreenModel& screen_model,
                                                   const ui::PreviewModel& preview_model);
    [[nodiscard]] ftxui::Element composeOverlay(ftxui::Element base_content,
                                                const ExplorerOverlayState& state,
                                                ftxui::Component active_input);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_RENDER_COMPOSER_HPP
