

#ifndef EXPP_EXPLORER_COMMAND_DISPATCHER_HPP
#define EXPP_EXPLORER_COMMAND_DISPATCHER_HPP

#include "expp/app/explorer.hpp"
#include "expp/app/explorer_commands.hpp"
#include "expp/app/explorer_presenter.hpp"
#include "expp/app/notification_center.hpp"

#include <memory>
#include <string>

namespace expp::app {

class ExplorerCommandDispatcher {
public:
    using OverlayTriggerCallback = std::function<void(ExplorerCommand)>;
    using QuitTriggerCallback = std::function<void()>;
    using VisualModeObserver = std::function<void(bool active)>;

    ExplorerCommandDispatcher(std::shared_ptr<Explorer> explorer,
                              NotificationCenter& notifications,
                              OverlayTriggerCallback overlay_trigger,
                              QuitTriggerCallback quit_trigger,
                              VisualModeObserver visual_mode_observer = nullptr);

    void execute(ExplorerCommand command, const ui::ActionContext& ctx);

private:
    std::shared_ptr<Explorer> explorer_;
    NotificationCenter& notifications_;

    OverlayTriggerCallback triggerOverlay_;
    QuitTriggerCallback triggerQuit_;
    VisualModeObserver visualModeObserver_;

    void handleNavigation(ExplorerCommand command, const ui::ActionContext& ctx) const;
    void handleFileOperations(ExplorerCommand command, const ui::ActionContext& ctx) const;
    void handleClipboard(ExplorerCommand command, const ui::ActionContext& ctx) const;
    void handleSearchAndFilter(ExplorerCommand command, const ui::ActionContext& ctx) const;
    void handleSorting(ExplorerCommand command) const;

    [[nodiscard]] bool navigateToHomeDirectory() const;
    [[nodiscard]] bool navigateToConfigDirectory() const;
    [[nodiscard]] int selectedCount() const noexcept;
    static std::string nounWithCount(int count, std::string_view singular);

    // [[nodiscard]] bool handleResult(core::VoidResult result,
    //                                 std::string success_message = {},
    //                                 ui::ToastSeverity success_severity = ui::ToastSeverity::Success);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_COMMAND_DISPATCHER_HPP
