#include "expp/app/explorer_overlay_controller.hpp"

#include "expp/app/navigation_utils.hpp"

#include <algorithm>
#include <format>

namespace expp::app {

ExplorerOverlayController::ExplorerOverlayController(std::shared_ptr<Explorer> explorer,
                                                     NotificationCenter& notifications)
    : explorer_(std::move(explorer))
    , notifications_(notifications) {}

void ExplorerOverlayController::openOverlayForCommand(const ExplorerCommand command) {
    switch (command) {
        case ExplorerCommand::Create:
            overlayState_ = CreateOverlayState{};
            break;
        case ExplorerCommand::Rename: {
            RenameOverlayState overlay;
            if (const auto& state = explorer_->state(); !state.entries.empty()) {
                overlay.input = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename();
            }
            overlayState_ = std::move(overlay);
            break;
        }
        case ExplorerCommand::Search:
            overlayState_ = SearchOverlayState{};
            break;
        case ExplorerCommand::PromptDirectoryJump:
            overlayState_ = DirectoryJumpOverlayState{};
            break;
        case ExplorerCommand::Delete: {
            const auto& state = explorer_->state();
            if (state.entries.empty()) {
                return;
            }
            // The logic for getting the selected count can be extracted into a helper function
            const int count = state.selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
            overlayState_ = DeleteConfirmOverlayState{
                .targetName = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename(),
                .selectionCount = count,
            };
            break;
        }
        case ExplorerCommand::Trash: {
            const auto& state = explorer_->state();
            if (state.entries.empty()) {
                return;
            }
            const int count = state.selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
            overlayState_ = TrashConfirmOverlayState{
                .targetName = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename(),
                .selectionCount = count,
            };
            break;
        }
        case ExplorerCommand::OpenHelp: {
            
            HelpOverlayState overlay;
            overlayState_ = std::move(overlay);
            break;
        }
        default:
            return;
    }

    // After the state switch, immediately rebuild and focus the input component
    rebuildInputComponents();
}

void ExplorerOverlayController::openHelpOverlay(const std::vector<ui::HelpEntry>& entries, int screen_rows) {
    HelpOverlayState overlay;
    overlay.model.setEntries(entries);
    overlay.viewport.viewportRows = ExplorerPresenter::helpViewportRows(screen_rows);
    overlay.viewport = ui::clamp_help_viewport(overlay.viewport, overlay.model);
    overlayState_ = std::move(overlay);
    rebuildInputComponents();
}

void ExplorerOverlayController::updateHelpEntries(const std::vector<ui::HelpEntry>& entries) {
    if (auto* help = std::get_if<HelpOverlayState>(&overlayState_)) {
        help->model.setEntries(entries);
        help->model.setFilter(help->filterText);
        help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
    }
}

void ExplorerOverlayController::closeOverlay() {
    overlayState_ = std::monostate{};
    activeInputComponent_ = nullptr;
}


void ExplorerOverlayController::rebuildInputComponents() {
    using namespace ftxui;

    const auto make_input_option = [](Color focused_color) {
        auto option = InputOption::Default();
        option.multiline = false;
        option.transform = [focused_color](InputState state) {
            if (state.is_placeholder) {
                state.element |= dim;
            }
            if (state.focused) {
                state.element |= color(focused_color);
            }
            return state.element;
        };
        return option;
    };

    activeInputComponent_ = nullptr;

    std::visit(
        [&]<typename T0>(T0& overlay) {
            using T = std::decay_t<T0>;

            if constexpr (std::is_same_v<T, CreateOverlayState>) {
                activeInputComponent_ =
                    Input(&overlay.input, "filename or path/to/dir/", make_input_option(Color::Cyan2));
                activeInputComponent_->TakeFocus();
            } else if constexpr (std::is_same_v<T, RenameOverlayState>) {
                activeInputComponent_ = Input(&overlay.input, "new name", make_input_option(Color::DarkCyan));
                activeInputComponent_->TakeFocus();
            } else if constexpr (std::is_same_v<T, SearchOverlayState>) {
                activeInputComponent_ = Input(&overlay.input, "pattern", make_input_option(Color::Yellow));
                activeInputComponent_->TakeFocus();
            } else if constexpr (std::is_same_v<T, DirectoryJumpOverlayState>) {
                activeInputComponent_ = Input(&overlay.input, "~/path/to/dir", make_input_option(Color::GreenLight));
                activeInputComponent_->TakeFocus();
            } else if constexpr (std::is_same_v<T, HelpOverlayState>) {
                activeInputComponent_ =
                    Input(&overlay.filterText, "shortcut or description", make_input_option(Color::Yellow));
                if (overlay.filterMode) {
                    activeInputComponent_->TakeFocus();
                }
            }
            // For states like Monostate, DeleteConfirm that don't require an input box, do nothing, keep activeInputComponent_ as nullptr
        },
        overlayState_);
}

bool ExplorerOverlayController::handleHelpEvent(HelpOverlayState& help, const ftxui::Event& event) {
    using namespace ftxui;
    if (event == Event::Custom) {
        return false;
    }

    if (help.filterMode) {
        if (event == Event::Return || event == Event::Escape) {
            help.filterMode = false;
            return true;
        }

        const bool handled = activeInputComponent_->OnEvent(event);
        if (handled) {
            // Filtering can change category headers and total visual rows, so
            // selection and scroll need to be re-clamped immediately.
            help.model.setFilter(help.filterText);
            help.viewport = ui::clamp_help_viewport(help.viewport, help.model);
        }
        return handled;
    }

    if (event == Event::Character('~') || event == Event::Escape) {
        closeOverlay();
        return true;
    }
    if (event == Event::Character('j') || event == Event::ArrowDown) {
        if (help.model.filteredCount() == 0U) {
            return true;
        }
        help.viewport.selectedIndex =
            std::clamp(help.viewport.selectedIndex + 1, 0, static_cast<int>(help.model.filteredCount()) - 1);
        help.viewport = ui::clamp_help_viewport(help.viewport, help.model);
        return true;
    }
    if (event == Event::Character('k') || event == Event::ArrowUp) {
        if (help.model.filteredCount() == 0U) {
            return true;
        }
        help.viewport.selectedIndex =
            std::clamp(help.viewport.selectedIndex - 1, 0, static_cast<int>(help.model.filteredCount()) - 1);
        help.viewport = ui::clamp_help_viewport(help.viewport, help.model);
        return true;
    }
    if (event == Event::Character('f')) {
        help.filterMode = true;
        rebuildInputComponents();
        return true;
    }
    return true;
}

// =========================================================================
// Core Event Routing Engine
// =========================================================================
bool ExplorerOverlayController::handleEvent(const ftxui::Event& event) {
    // Ignore custom events (usually redraw signals from timer refreshes)
    if (event == ftxui::Event::Custom) {
        return false;
    }

    // Use std::visit to precisely dispatch the event to the corresponding handler
    return std::visit(
        [&]<typename T0>(T0& overlay) -> bool {
            using T = std::decay_t<T0>;

            // if constexpr (std::is_same_v<T, std::monostate>) {
            //     return false;  // No modal, don't intercept events
            // } else
            if constexpr (std::is_same_v<T, CreateOverlayState>) {
                return handleCreateEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, RenameOverlayState>) {
                return handleRenameEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, SearchOverlayState>) {
                return handleSearchEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, DirectoryJumpOverlayState>) {
                return handleDirectoryJumpEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, HelpOverlayState>) {
                return handleHelpEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, DeleteConfirmOverlayState>) {
                return handleDeleteEvent(overlay, event);
            } else if constexpr (std::is_same_v<T, TrashConfirmOverlayState>) {
                return handleTrashEvent(overlay, event);
            } else {
                return false;
            }
        },
        overlayState_);
}

// ---------------------------------------------------------
// Specific modal event logic
// ---------------------------------------------------------

bool ExplorerOverlayController::handleDirectoryJumpEvent(DirectoryJumpOverlayState& overlay,
                                                         const ftxui::Event& event) {
    using namespace ftxui;
    // auto* overlay = std::get_if<DirectoryJumpOverlayState>(&overlayState_);
    // if (overlay == nullptr) {
    //     return false;
    // }
    auto navigate_to_type_directory = [this](const std::string& input) {
        auto resolved_path = resolve_directory_input(input, explorer_->state().currentDir);
        if (!resolved_path) {
            notifications_.publish(severity_for_error(resolved_path.error()), resolved_path.error().message());
            return false;
        }
        return publish_if_error(notifications_, explorer_->navigateTo(*resolved_path));
    };
    if (event == Event::Return) {
        if (std::ranges::all_of(overlay.input, [](unsigned char ch) {
                return std::isspace(ch) != 0;
            })) {  // NOLINT(bugprone-branch-clone) (it is intentional for clear motivation)
            closeOverlay();
        } else if (navigate_to_type_directory(overlay.input)) {
            closeOverlay();
        }
        return true;
    }
    if (event == Event::Escape) {
        closeOverlay();
        return true;
    }
    return activeInputComponent_->OnEvent(event);
}

bool ExplorerOverlayController::handleCreateEvent(CreateOverlayState& overlay, const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
        if (const std::string msg = overlay.input.empty() ? "" : std::format("Created '{}'", overlay.input);
            publish_if_error(notifications_, explorer_->create(overlay.input), msg)) {
            closeOverlay();
        }
        return true;
    }
    if (event == ftxui::Event::Escape) {
        closeOverlay();
        return true;
    }
    return activeInputComponent_->OnEvent(event);
}

bool ExplorerOverlayController::handleRenameEvent(RenameOverlayState& overlay, const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
        if (const std::string msg = overlay.input.empty() ? "" : std::format("Renamed to '{}'", overlay.input);
            publish_if_error(notifications_, explorer_->rename(overlay.input), msg)) {
            closeOverlay();
        }
        return true;
    }
    if (event == ftxui::Event::Escape) {
        closeOverlay();
        return true;
    }
    return activeInputComponent_->OnEvent(event);
}

bool ExplorerOverlayController::handleSearchEvent(const SearchOverlayState& overlay, const ftxui::Event& event) {
    if (event == ftxui::Event::Return) {
        explorer_->search(overlay.input);
        closeOverlay();
        return true;
    }
    if (event == ftxui::Event::Escape) {
        closeOverlay();
        return true;
    }
    return activeInputComponent_->OnEvent(event);
}

bool ExplorerOverlayController::handleDeleteEvent(DeleteConfirmOverlayState& overlay, const ftxui::Event& event) {
    if (event == ftxui::Event::Character('y') || event == ftxui::Event::Character('Y')) {
        const std::string msg =
            overlay.selectionCount > 0 ? std::format("Deleted {} item(s)", overlay.selectionCount) : "";
        if (publish_if_error(notifications_, explorer_->deleteSelected(), msg)) {
            // Note: The logic for exiting visual mode is recommended to be coordinated by the upper-level business,
            // or provided as a callback in CommandDispatcher.
            // For simplicity, call the underlying exitVisualMode
            explorer_->exitVisualMode();
            closeOverlay();
        }
        return true;
    }
    if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N') ||
        event == ftxui::Event::Escape) {
        closeOverlay();
        return true;
    }
    return true;  // When the confirmation box is active, swallow all other buttons.
}

bool ExplorerOverlayController::handleTrashEvent(TrashConfirmOverlayState& overlay, const ftxui::Event& event) {
    using namespace ftxui;

    if (event == Event::Character('y') || event == Event::Character('Y')) {
        const std::string msg =
            overlay.selectionCount > 0 ? std::format("Move {} item(s) to trash", overlay.selectionCount) : "";
        if (publish_if_error(notifications_, explorer_->trashSelected(), msg)) {
            explorer_->exitVisualMode();
            closeOverlay();
        }
        return true;
    }
    if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
        closeOverlay();
        return true;
    }
    return true;
}

}  // namespace expp::app