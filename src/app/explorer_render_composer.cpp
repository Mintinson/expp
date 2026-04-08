#include "expp/app/explorer_render_composer.hpp"

#include "expp/core/config.hpp"

#include <algorithm>
#include <utility>

namespace expp::app {

ExplorerRenderComposer::ExplorerRenderComposer(const ui::Theme* theme) : theme_(theme) {
    const auto& cfg = core::global_config().config();

    // Initialize all basic rendering components
    fileList_ = std::make_unique<ui::FileListComponent>(ui::FileListConfig{.theme = theme_});
    preview_ = std::make_unique<ui::PreviewComponent>(ui::PreviewConfig{
        .theme = theme_,
        .maxLines = cfg.preview.maxLines,
    });
    statusBar_ = std::make_unique<ui::StatusBarComponent>(theme_);
    toast_ = std::make_unique<ui::ToastComponent>(theme_);
    helpMenu_ = std::make_unique<ui::HelpMenuComponent>(theme_);

    dialog_ = std::make_unique<ui::DialogComponent>();
    dialog_->setTheme(theme_);

    panel_ = std::make_unique<ui::PanelComponent>(ui::PanelConfig{
        .showParent = cfg.layout.showParentPanel,
        .showPreview = cfg.layout.showPreviewPanel,
        .parentWidth = cfg.layout.parentPanelWidth,
        .previewWidth = cfg.layout.previewPanelWidth,
        .theme = theme_,
    });
}

ftxui::Element ExplorerRenderComposer::compose(const ExplorerState& state,
                                               const ExplorerScreenModel& screen_model,
                                               const ui::PreviewModel& preview_model,
                                               const ExplorerOverlayState& overlay_state,
                                               ftxui::Component active_input,
                                               const std::optional<ui::ToastInfo>& current_toast) {
    using namespace ftxui;

    // 1. Assemble the underlying main interface (three-column layout + status bar)
    Element main_content = composeMainLayout(state, screen_model, preview_model);

    // 2. Add a pop-up window (if present).
    main_content = composeOverlay(std::move(main_content), overlay_state, std::move(active_input));

    // 3. Add notification Toast (if any)
    if (current_toast.has_value()) {
        auto toast_layer = vbox({
            filler(),
            hbox({
                filler(),
                toast_->render(*current_toast),
            }),
        });
        main_content = dbox({
            std::move(main_content),
            std::move(toast_layer),
        });
    }

    return main_content;
}

ftxui::Element ExplorerRenderComposer::composeMainLayout(const ExplorerState& state,
                                                         const ExplorerScreenModel& screen_model,
                                                         const ui::PreviewModel& preview_model) {
    using namespace ftxui;

    // auto scree_model = pr

    // Render parent directory list
    auto parent_content = fileList_->render(state.parentEntries, state.selection.parentSelected, {}, -1, {});

    // Render current directory list (handling visible area slicing)
    std::span<const core::filesystem::FileEntry> visible_entries{state.entries};
    visible_entries = visible_entries.subspan(
        static_cast<std::size_t>(screen_model.currentList.offset),
        static_cast<std::size_t>(std::max(0, screen_model.currentList.visibleEnd - screen_model.currentList.offset)));
    auto current_content = fileList_->render(
        visible_entries, screen_model.currentList.selectedIndex, screen_model.currentList.searchMatches,
        screen_model.currentList.currentMatchIndex, screen_model.currentList.visualSelectedIndices);

    // Render preview area
    auto preview_content = preview_->render(preview_model);

    // Assemble the three columns into a Panel
    auto panels = panel_->render(screen_model.parentTitle, std::move(parent_content), screen_model.currentTitle,
                                 std::move(current_content), "Preview", std::move(preview_content));
    // Render status bar
    const ui::StatusBarInfo status_info{.currentPath = screen_model.statusPath,
                                        .keyBuffer = screen_model.keyBuffer,
                                        .searchStatus = screen_model.searchStatus,
                                        .helpText = screen_model.helpText};
    auto status_bar = statusBar_->render(status_info);

    // Final vertical composition
    return vbox({
        std::move(panels) | flex,
        separator(),
        std::move(status_bar),
    });
}

ftxui::Element ExplorerRenderComposer::composeOverlay(ftxui::Element base_content,
                                                      const ExplorerOverlayState& state,
                                                      ftxui::Component active_input) {
    using namespace ftxui;

    return std::visit(
        [&]<typename T0>(const T0& overlay) -> Element {
            using Overlay = std::decay_t<T0>;

            if constexpr (std::is_same_v<Overlay, HelpOverlayState>) {
                // Note: Do not modify the internal data of the overlay (such as the viewport) here.
                // Data computation (clamping) should be completed in the Controller or Presenter phase.
                // Composer is only responsible for "drawing".
                auto help_elem = helpMenu_->render(overlay.model, overlay.filterMode, overlay.viewport);
                return dbox({std::move(base_content) | dim, std::move(help_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, DirectoryJumpOverlayState>) {
                auto dialog_elem = dialog_->renderInput("Jump To Directory",
                                                        "Enter a path, or use ~ for home:", active_input->Render());
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, CreateOverlayState>) {
                auto dialog_elem = dialog_->renderInput(
                    "Create New File/Directory",
                    "Enter name (end with / for directory):\ne.g., foo/bar/baz/ creates nested dirs",
                    active_input->Render());
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, RenameOverlayState>) {
                auto dialog_elem = dialog_->renderInput("Rename Current File/Directory",
                                                        "Enter the new name:", active_input->Render());
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, SearchOverlayState>) {
                auto dialog_elem = dialog_->renderInput("Search (case-sensitive)", "",
                                                        hbox({text(" / "), active_input->Render() | flex}));
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, DeleteConfirmOverlayState>) {
                auto dialog_elem = dialog_->renderConfirmation(
                    "Delete Confirmation", "Are you sure you want to delete:", overlay.targetName, Color::Red);
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else if constexpr (std::is_same_v<Overlay, TrashConfirmOverlayState>) {
                auto dialog_elem = dialog_->renderConfirmation(
                    "Trash Confirmation", "Are you sure you want to move to trash:", overlay.targetName, Color::Red);
                return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
            } else {
                return base_content;
            }
        },
        state);
}

}  // namespace expp::app