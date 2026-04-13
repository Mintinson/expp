
/**
 * @file explorer_render_composer.hpp
 * @brief Pure rendering assembly for explorer view models.
 *
 * This module is responsible only for turning already-prepared state into FTXUI
 * elements. It does not own business logic, overlay transitions, preview
 * loading, or command dispatch.
 */

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

/**
 * @brief Composes the explorer screen from prepared render models.
 *
 * `ExplorerRenderComposer` is the drawing boundary for the explorer view split.
 * Its inputs are already-decided state objects: the explorer domain snapshot,
 * presenter output, preview model, overlay state, and optional toast.
 */
class ExplorerRenderComposer {
public:
    /**
     * @brief Creates a composer with UI components configured from the active theme/config.
     */
    explicit ExplorerRenderComposer(const ui::Theme* theme);

    /**
     * @brief Composes the full explorer frame.
     * @param state Explorer domain snapshot.
     * @param screen_model Presenter output for the visible frame.
     * @param preview_model Current preview state.
     * @param overlay_state Active overlay variant.
     * @param active_input Input widget owned by the overlay controller, if any.
     * @param current_toast Optional notification toast to render above the layout.
     * @return Top-level FTXUI element for the current frame.
     */
    [[nodiscard]] ftxui::Element compose(const ExplorerState& state,
                                         const ExplorerScreenModel& screen_model,
                                         const ui::PreviewModel& preview_model,
                                         const ExplorerOverlayState& overlay_state,
                                         ftxui::Component active_input,
                                         const std::optional<ui::ToastInfo>& current_toast);

private:
    /// Theme shared by the owned rendering components.
    const ui::Theme* theme_;

    /// Reusable leaf components used to assemble the final layout.
    std::unique_ptr<ui::FileListComponent> fileList_;
    std::unique_ptr<ui::PanelComponent> panel_;
    std::unique_ptr<ui::PreviewComponent> preview_;
    std::unique_ptr<ui::StatusBarComponent> statusBar_;
    std::unique_ptr<ui::ToastComponent> toast_;
    std::unique_ptr<ui::HelpMenuComponent> helpMenu_;
    std::unique_ptr<ui::DialogComponent> dialog_;

    /**
     * @brief Composes the non-modal explorer layout.
     */
    [[nodiscard]] ftxui::Element composeMainLayout(const ExplorerState& state,
                                                   const ExplorerScreenModel& screen_model,
                                                   const ui::PreviewModel& preview_model);
    /**
     * @brief Layers the active overlay over the base layout.
     */
    [[nodiscard]] ftxui::Element composeOverlay(ftxui::Element base_content,
                                                const ExplorerOverlayState& state,
                                                ftxui::Component active_input);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_RENDER_COMPOSER_HPP
