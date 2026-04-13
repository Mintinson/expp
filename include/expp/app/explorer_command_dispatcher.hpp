

#ifndef EXPP_EXPLORER_COMMAND_DISPATCHER_HPP
#define EXPP_EXPLORER_COMMAND_DISPATCHER_HPP

/**
 * @file explorer_command_dispatcher.hpp
 * @brief Command-to-behavior routing for explorer actions.
 *
 * The dispatcher decouples key/action resolution from concrete side effects.
 * It maps typed commands to domain operations, overlay triggers, and user
 * notifications, while keeping `ExplorerView` focused on input/render plumbing.
 */

#include "expp/app/explorer.hpp"
#include "expp/app/explorer_commands.hpp"
#include "expp/app/explorer_presenter.hpp"
#include "expp/app/notification_center.hpp"

#include <memory>
#include <string>

namespace expp::app {

/**
 * @brief Routes high-level explorer commands to domain and UI side effects.
 */
class ExplorerCommandDispatcher {
public:
    /// Callback type used to open command-driven overlays (create, rename, help, etc.).
    using OverlayTriggerCallback = std::function<void(ExplorerCommand)>;
    /// Callback type used to request application shutdown.
    using QuitTriggerCallback = std::function<void()>;
    /// Callback type used to synchronize key-handler mode with explorer visual mode.
    using VisualModeObserver = std::function<void(bool active)>;

    /**
     * @brief Constructs a command dispatcher.
     * @param explorer Domain controller used for command execution.
     * @param notifications Notification center used for user-facing success/error feedback.
     * @param overlay_trigger Callback to open overlays for commands that require modal UI.
     * @param quit_trigger Callback to terminate the main loop.
     * @param visual_mode_observer Optional callback notified when visual mode toggles.
     */
    ExplorerCommandDispatcher(std::shared_ptr<Explorer> explorer,
                              NotificationCenter& notifications,
                              OverlayTriggerCallback overlay_trigger,
                              QuitTriggerCallback quit_trigger,
                              VisualModeObserver visual_mode_observer = nullptr);

    /**
     * @brief Executes one command with an optional numeric prefix context.
     */
    void execute(ExplorerCommand command, const ui::ActionContext& ctx);

private:
    std::shared_ptr<Explorer> explorer_;
    NotificationCenter& notifications_;

    OverlayTriggerCallback triggerOverlay_;
    QuitTriggerCallback triggerQuit_;
    VisualModeObserver visualModeObserver_;

    /// Executes navigation-related commands.
    void handleNavigation(ExplorerCommand command, const ui::ActionContext& ctx) const;
    /// Executes file-operation and visual-mode commands.
    void handleFileOperations(ExplorerCommand command, const ui::ActionContext& ctx) const;
    /// Executes clipboard and path-copy commands.
    void handleClipboard(ExplorerCommand command, const ui::ActionContext& ctx) const;
    /// Executes search and visibility/filter toggles.
    void handleSearchAndFilter(ExplorerCommand command, const ui::ActionContext& ctx) const;
    /// Executes sort-order commands generated from sort specs.
    void handleSorting(ExplorerCommand command) const;

    /// Resolves and navigates to the platform home directory.
    [[nodiscard]] bool navigateToHomeDirectory() const;
    /// Navigates to the configuration directory.
    [[nodiscard]] bool navigateToConfigDirectory() const;
    /// Returns active selection count (visual selection aware).
    [[nodiscard]] int selectedCount() const noexcept;
    /// Builds a simple count-aware noun phrase ("1 item", "2 items").
    static std::string nounWithCount(int count, std::string_view singular);

    // [[nodiscard]] bool handleResult(core::VoidResult result,
    //                                 std::string success_message = {},
    //                                 ui::ToastSeverity success_severity = ui::ToastSeverity::Success);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_COMMAND_DISPATCHER_HPP
