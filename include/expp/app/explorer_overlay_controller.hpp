
/**
 * @file explorer_overlay_controller.hpp
 * @brief Overlay state machine for explorer modal interactions.
 *
 * This module owns the transient UI state for explorer overlays: help,
 * directory jump, create, rename, search, and destructive confirmations. It
 * does not render those overlays and it does not dispatch normal explorer
 * commands. Its responsibilities are to:
 * - choose which overlay should be active,
 * - keep overlay-local input state alive,
 * - route events while an overlay is open,
 * - translate confirmed overlay actions into explorer mutations.
 */

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

/**
 * @brief Controller for all modal explorer overlays.
 *
 * `ExplorerOverlayController` is the ownership boundary for overlay-specific
 * state. `ExplorerView` delegates modal event handling here so the main view
 * loop stays focused on high-level orchestration instead of per-dialog logic.
 */
class ExplorerOverlayController {
public:
    /**
     * @brief Creates an overlay controller bound to one explorer instance.
     * @param explorer Explorer domain object mutated by confirmed overlay actions.
     * @param notifications Notification sink used for user-visible failures.
     */
    ExplorerOverlayController(std::shared_ptr<Explorer> explorer, NotificationCenter& notifications);

    /**
     * @brief Returns the active overlay state.
     */
    [[nodiscard]] const ExplorerOverlayState& state() const { return overlayState_; }

    /**
     * @brief Returns the mutable active overlay state.
     *
     * This is primarily used by the outer view layer for presenter-driven
     * viewport updates.
     */
    [[nodiscard]] ExplorerOverlayState& state() { return overlayState_; }

    /**
     * @brief Returns the currently active input component, if the overlay needs one.
     */
    [[nodiscard]] ftxui::Component activeInputComponent() const { return activeInputComponent_; }

    /**
     * @brief Opens the overlay corresponding to a command, when one exists.
     *
     * Commands without overlay semantics are ignored.
     */
    void openOverlayForCommand(ExplorerCommand command);

    /**
     * @brief Opens the help overlay with a prebuilt help-entry list.
     * @param entries Help entries derived from the current command and key catalogs.
     * @param screen_rows Current terminal height used to initialize the help viewport.
     */
    void openHelpOverlay(const std::vector<ui::HelpEntry>& entries, int screen_rows);

    /**
     * @brief Refreshes help-overlay data after bindings or action metadata change.
     */
    void updateHelpEntries(const std::vector<ui::HelpEntry>& entries);

    /**
     * @brief Closes the current overlay and clears any active input component.
     */
    void closeOverlay();

    /**
     * @brief Routes one event to the active overlay, if any.
     * @return True when the event was consumed by overlay logic.
     */
    [[nodiscard]] bool handleEvent(const ftxui::Event& event);

private:
    /// Explorer domain instance mutated by confirmed overlay actions.
    std::shared_ptr<Explorer> explorer_;
    /// Notification channel used to surface recoverable UI errors.
    NotificationCenter& notifications_;

    /// Active overlay state variant.
    ExplorerOverlayState overlayState_;

    /// Focusable input widget owned by the currently active overlay, if any.
    ftxui::Component activeInputComponent_;

    /**
     * @brief Rebuilds the active input component after an overlay-state change.
        *
        * This ensures placeholder text, focus state, and bound storage references
        * always match the currently active overlay variant.
     */
    void rebuildInputComponents();

        /// Overlay-specific handlers used by the generic `std::visit` dispatcher.
        /// Each handler returns true when the event is consumed.
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
