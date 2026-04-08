#include "expp/app/explorer_command_dispatcher.hpp"

#include "expp/app/explorer_presenter.hpp"  // for kPageStep
#include "expp/app/navigation_utils.hpp"
#include "expp/core/config.hpp"

#include <algorithm>
#include <format>

namespace expp::app {

ExplorerCommandDispatcher::ExplorerCommandDispatcher(std::shared_ptr<Explorer> explorer,
                                                     NotificationCenter& notifications,
                                                     OverlayTriggerCallback overlay_trigger,
                                                     QuitTriggerCallback quit_trigger,
                                                     VisualModeObserver visual_mode_observer)
    : explorer_(std::move(explorer))
    , notifications_(notifications)
    , triggerOverlay_(std::move(overlay_trigger))
    , triggerQuit_(std::move(quit_trigger))
    , visualModeObserver_(std::move(visual_mode_observer)) {}

void ExplorerCommandDispatcher::execute(const ExplorerCommand command, const ui::ActionContext& ctx) {
    // 1. only for UI command that need overlay or quit
    switch (command) {
        case ExplorerCommand::Create:
        case ExplorerCommand::Rename:
        case ExplorerCommand::Search:
        case ExplorerCommand::Trash:
        case ExplorerCommand::Delete:
        case ExplorerCommand::PromptDirectoryJump:
        case ExplorerCommand::OpenHelp:
            triggerOverlay_(command);
            return;
        case ExplorerCommand::Quit:
            triggerQuit_();
            return;
        default:
            break;
    }

    handleNavigation(command, ctx);
    handleFileOperations(command, ctx);
    handleClipboard(command, ctx);
    handleSearchAndFilter(command, ctx);
    handleSorting(command);
}

void ExplorerCommandDispatcher::handleNavigation(const ExplorerCommand command, const ui::ActionContext& ctx) const {
    switch (command) {
        case ExplorerCommand::MoveDown:
            explorer_->moveDown(ctx.count);
            break;
        case ExplorerCommand::MoveUp:
            explorer_->moveUp(ctx.count);
            break;
        case ExplorerCommand::GoParent:
            (void)publish_if_error(notifications_, explorer_->goParent());
            break;
        case ExplorerCommand::EnterSelected:
            (void)publish_if_error(notifications_, explorer_->enterSelected(true));
            break;
        case ExplorerCommand::GoTop:
            explorer_->goToTop();
            break;
        case ExplorerCommand::GoBottom:
            if (!explorer_->state().entries.empty()) {
                if (ctx.count > 1) {
                    explorer_->goToLine(ctx.count);
                } else {
                    explorer_->goToBottom();
                }
            }
            break;
        case ExplorerCommand::PageDown:
            explorer_->moveDown(ExplorerPresenter::kPageStep * ctx.count);
            break;
        case ExplorerCommand::PageUp:
            explorer_->moveUp(ExplorerPresenter::kPageStep * ctx.count);
            break;
        case ExplorerCommand::GoHomeDirectory:
            (void)navigateToHomeDirectory();
            break;
        case ExplorerCommand::GoConfigDirectory:
            (void)navigateToConfigDirectory();
            break;
        case ExplorerCommand::GoLinkTargetDirectory:
            (void)publish_if_error(notifications_, explorer_->navigateToSelectedLinkTargetDirectory());
            break;
        default:
            break;
    }
}

void ExplorerCommandDispatcher::handleFileOperations(const ExplorerCommand command,
                                                     [[maybe_unused]] const ui::ActionContext& ctx) const {
    switch (command) {
        case ExplorerCommand::OpenFile: {
            const auto& state = explorer_->state();
            const bool can_open =
                !state.entries.empty() &&
                !state.entries[static_cast<std::size_t>(state.selection.currentSelected)].isDirectory();
            (void)publish_if_error(notifications_, explorer_->openSelected(),
                                   can_open ? "Opened with default application" : "");
            break;
        }
        case ExplorerCommand::EnterVisualMode:
            explorer_->enterVisualMode();
            if (visualModeObserver_ && explorer_->state().selection.visualModeActive) {
                visualModeObserver_(true);
            }
            break;
        case ExplorerCommand::ExitVisualMode:
            explorer_->exitVisualMode();
            if (visualModeObserver_) {
                visualModeObserver_(false);
            }
            break;
        default:
            break;
    }
}

void ExplorerCommandDispatcher::handleClipboard(const ExplorerCommand command,
                                                [[maybe_unused]] const ui::ActionContext& ctx) const {
    switch (command) {
        case ExplorerCommand::Yank: {
            const int count = selectedCount();
            (void)publish_if_error(notifications_, explorer_->yankSelected(),
                                   count > 0 ? std::format("Copied {}", nounWithCount(count, "item")) : "");
            break;
        }
        case ExplorerCommand::Cut: {
            const int count = selectedCount();
            (void)publish_if_error(notifications_, explorer_->cutSelected(),
                                   count > 0 ? std::format("Cut {}", nounWithCount(count, "item")) : "");
            break;
        }
        case ExplorerCommand::DiscardYank:
            (void)publish_if_error(notifications_, explorer_->discardYank(), "Clipboard cleared",
                                   ui::ToastSeverity::Info);
            break;
        case ExplorerCommand::Paste: {
            const int item_count = static_cast<int>(explorer_->state().clipboard.paths.size());
            (void)publish_if_error(notifications_, explorer_->pasteYanked(false),
                                   item_count > 0 ? std::format("Pasted {}", nounWithCount(item_count, "item")) : "");
            break;
        }
        case ExplorerCommand::PasteOverwrite: {
            const int item_count = static_cast<int>(explorer_->state().clipboard.paths.size());
            (void)publish_if_error(
                notifications_, explorer_->pasteYanked(true),
                item_count > 0 ? std::format("Pasted {} with overwrite", nounWithCount(item_count, "item")) : "");
            break;
        }
        case ExplorerCommand::CopyEntryPathRelative:
            (void)publish_if_error(notifications_, explorer_->copySelectedPathToSystemClipboard(false),
                                   "Copied relative path", ui::ToastSeverity::Info);
            break;
        case ExplorerCommand::CopyCurrentDirRelative:
            (void)publish_if_error(notifications_, explorer_->copyCurrentDirectoryPathToSystemClipboard(false),
                                   "Copied relative directory path", ui::ToastSeverity::Info);
            return;
        case ExplorerCommand::CopyEntryPathAbsolute:
            (void)publish_if_error(notifications_, explorer_->copySelectedPathToSystemClipboard(true),
                                   "Copied absolute path", ui::ToastSeverity::Info);
            return;
        case ExplorerCommand::CopyCurrentDirAbsolute:
            (void)publish_if_error(notifications_, explorer_->copyCurrentDirectoryPathToSystemClipboard(true),
                                   "Copied absolute directory path", ui::ToastSeverity::Info);
            return;
        case ExplorerCommand::CopyFileName:
            (void)publish_if_error(notifications_, explorer_->copySelectedFileNameToSystemClipboard(),
                                   "Copied file name", ui::ToastSeverity::Info);
            return;
        case ExplorerCommand::CopyNameWithoutExtension:
            (void)publish_if_error(notifications_, explorer_->copySelectedNameWithoutExtensionToSystemClipboard(),
                                   "Copied name without extension", ui::ToastSeverity::Info);
            return;
        default:
            break;
    }
}

void ExplorerCommandDispatcher::handleSearchAndFilter(const ExplorerCommand command,
                                                      const ui::ActionContext& ctx) const {
    switch (command) {
        case ExplorerCommand::NextMatch:
            for (int index = 0; index < ctx.count; ++index) {
                explorer_->nextMatch();
            }
            break;
        case ExplorerCommand::PrevMatch:
            for (int index = 0; index < ctx.count; ++index) {
                explorer_->prevMatch();
            }
            break;
        case ExplorerCommand::ClearSearch:
            explorer_->clearSearch();
            break;
        case ExplorerCommand::ToggleHidden:
            (void)publish_if_error(notifications_, explorer_->toggleShowHidden());
            break;
        default:
            break;
    }
}

void ExplorerCommandDispatcher::handleSorting(const ExplorerCommand command) const {
    for (const auto& spec : sort_specs()) {
        if (spec.ascendingCommand == command) {
            explorer_->setSortOrder(spec.field, SortOrder::Direction::Ascending);
            return;
        }
        if (spec.descendingCommand == command) {
            explorer_->setSortOrder(spec.field, SortOrder::Direction::Descending);
            return;
        }
    }
}

int ExplorerCommandDispatcher::selectedCount() const noexcept {
    const auto& state = explorer_->state();
    if (state.entries.empty()) {
        return 0;
    }
    return state.selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
}

std::string ExplorerCommandDispatcher::nounWithCount(int count, std::string_view singular) {
    return std::format("{} {}{}", count, singular, count == 1 ? "" : "s");
}

bool ExplorerCommandDispatcher::navigateToHomeDirectory() const {
    auto home_result = resolve_home_directory();
    if (!home_result) {
        notifications_.publish(severity_for_error(home_result.error()), home_result.error().message());
        return false;
    }
    return publish_if_error(notifications_, explorer_->navigateTo(*home_result));
}

bool ExplorerCommandDispatcher::navigateToConfigDirectory() const {
    const auto config_dir = core::ConfigManager::userConfigPath().parent_path();
    if (config_dir.empty()) {
        notifications_.publish(ui::ToastSeverity::Warning, "Config directory is not available");
        return false;
    }
    return publish_if_error(notifications_, explorer_->navigateTo(config_dir));
}

}  // namespace expp::app