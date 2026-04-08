
#ifndef EXPP_EXPLORER_OVERLAY_CONTROLLER_HPP
#define EXPP_EXPLORER_OVERLAY_CONTROLLER_HPP

#include "expp/app/explorer.hpp"
#include "expp/app/explorer_commands.hpp"
#include "expp/app/explorer_presenter.hpp"
#include "expp/app/notification_center.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include <memory>
#include <string>

namespace expp::app {

class ExplorerOverlayController {
public:
    ExplorerOverlayController(std::shared_ptr<Explorer> explorer, NotificationCenter& notifications);

    [[nodiscard]] const ExplorerOverlayState& state() const { return overlayState_; }

    [[nodiscard]] ExplorerOverlayState& state() { return overlayState_; }

    [[nodiscard]] ftxui::Component activeInputComponent() const { return activeInputComponent_; }

    void openOverlayForCommand(ExplorerCommand command);

    void openHelpOverlay(const std::vector<ui::HelpEntry>& entries, int screen_rows);

    void updateHelpEntries(const std::vector<ui::HelpEntry>& entries);

    void closeOverlay();

    [[nodiscard]] bool handleEvent(const ftxui::Event& event);

private:
    std::shared_ptr<Explorer> explorer_;
    NotificationCenter& notifications_;

    ExplorerOverlayState overlayState_;

    ftxui::Component activeInputComponent_;

    void rebuildInputComponents();

    bool handleHelpEvent(HelpOverlayState& help, const ftxui::Event& event);
    bool handleDirectoryJumpEvent(DirectoryJumpOverlayState& overlay, const ftxui::Event& event);
    bool handleCreateEvent(CreateOverlayState& overlay, const ftxui::Event& event);
    bool handleRenameEvent(RenameOverlayState& overlay, const ftxui::Event& event);
    bool handleSearchEvent(const SearchOverlayState& overlay, const ftxui::Event& event);
    bool handleDeleteEvent(DeleteConfirmOverlayState& overlay, const ftxui::Event& event);
    bool handleTrashEvent(TrashConfirmOverlayState& overlay, const ftxui::Event& event);
};
}  // namespace expp::app
#endif  // EXPP_EXPLORER_OVERLAY_CONTROLLER_HPP
