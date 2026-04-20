#include "expp/app/explorer_overlay_controller.hpp"

#include <algorithm>

namespace expp::app {

// This module is the interaction half of overlay handling. The corresponding
// composer renders overlay visuals, while this controller owns overlay-local
// state, input widgets, and confirm/cancel behavior.

ExplorerOverlayController::ExplorerOverlayController(std::shared_ptr<Explorer> explorer,
                                                     NotificationCenter& notifications,
                                                     DirectoryJumpCallback directory_jump,
                                                     CreateCallback create,
                                                     RenameCallback rename,
                                                     DeleteCallback delete_selected,
                                                     TrashCallback trash_selected)
    : explorer_(std::move(explorer))
    , notifications_(notifications)
    , directoryJump_(std::move(directory_jump))
    , create_(std::move(create))
    , rename_(std::move(rename))
    , deleteSelected_(std::move(delete_selected))
    , trashSelected_(std::move(trash_selected)) {}

void ExplorerOverlayController::openOverlayForCommand(const ExplorerCommand command) {
    // Map command intent to one concrete overlay variant. Overlay-specific
    // initialization (input prefill, selection snapshots) happens per case.
    switch (command) {
        case ExplorerCommand::Create:
            overlayState_ = CreateOverlayState{};
            break;
        case ExplorerCommand::Rename: {
            // Seed the rename input with the current filename so the overlay is
            // immediately editable without another lookup in the view layer.
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
            // Confirmation overlays snapshot the current logical selection so the
            // dialog text stays stable even if the underlying explorer refreshes.
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
            // Help is usually opened through `openHelpOverlay`, which also seeds
            // the model and viewport. This fallback preserves the state shape.
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
    // Help overlay initialization depends on the current screen height, so this
    // logic lives here rather than in the generic command-to-overlay switch.
    HelpOverlayState overlay;
    overlay.model.setEntries(entries);
    overlay.viewport.viewportRows = ExplorerPresenter::helpViewportRows(screen_rows);
    overlay.viewport = ui::clamp_help_viewport(overlay.viewport, overlay.model);
    overlayState_ = std::move(overlay);
    rebuildInputComponents();
}

void ExplorerOverlayController::updateHelpEntries(const std::vector<ui::HelpEntry>& entries) {
    if (auto* help = std::get_if<HelpOverlayState>(&overlayState_)) {
        // Preserve the current filter text while replacing the backing entry set.
        help->model.setEntries(entries);
        help->model.setFilter(help->filterText);
        help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
    }
}

void ExplorerOverlayController::closeOverlay() {
    // Reset both the state variant and the input component to avoid stale
    // references to previous overlay storage.
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

    // Each overlay owns at most one active input widget. Rebuild from scratch
    // whenever the overlay variant changes to keep focus logic predictable.
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
            // Confirmation and empty states intentionally leave the active input
            // component unset because they only react to direct key presses.
        },
        overlayState_);
}

bool ExplorerOverlayController::handleHelpEvent(HelpOverlayState& help, const ftxui::Event& event) {
    using namespace ftxui;
    if (event == Event::Custom) {
        return false;
    }

    if (help.filterMode) {
        // Filter mode redirects keys to text input instead of list navigation.
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

bool ExplorerOverlayController::handleEvent(const ftxui::Event& event) {
    // Custom events are repaint ticks from the outer view loop and should not
    // mutate overlay state on their own.
    if (event == ftxui::Event::Custom) {
        return false;
    }

    // Variant dispatch keeps each overlay's event contract local and explicit.
    return std::visit(
        [&]<typename T0>(T0& overlay) -> bool {
            using T = std::decay_t<T0>;

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

bool ExplorerOverlayController::handleDirectoryJumpEvent(DirectoryJumpOverlayState& overlay,
                                                         const ftxui::Event& event) {
    using namespace ftxui;
    if (event == Event::Return) {
        // Blank input behaves like cancel instead of surfacing a validation error.
        if (std::ranges::all_of(overlay.input, [](unsigned char ch) {
                return std::isspace(ch) != 0;
            })) {  // NOLINT(bugprone-branch-clone) (it is intentional for clear motivation)
            closeOverlay();
        } else if (directoryJump_) {
            directoryJump_(overlay.input);
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
        if (!overlay.input.empty() && create_) {
            create_(overlay.input);
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
        if (!overlay.input.empty() && rename_) {
            rename_(overlay.input);
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
        // Search is side-effect free on filesystem state, so always close after
        // applying the new pattern.
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

bool ExplorerOverlayController::handleDeleteEvent([[maybe_unused]] DeleteConfirmOverlayState& overlay,
                                                  const ftxui::Event& event) {
    if (event == ftxui::Event::Character('y') || event == ftxui::Event::Character('Y')) {
        if (deleteSelected_) {
            deleteSelected_();
            closeOverlay();
        }
        return true;
    }
    if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N') ||
        event == ftxui::Event::Escape) {
        closeOverlay();
        return true;
    }
    // Confirmation overlays are modal, so unrecognized keys should not leak to
    // the main explorer keymap underneath.
    return true;
}

bool ExplorerOverlayController::handleTrashEvent([[maybe_unused]] TrashConfirmOverlayState& overlay,
                                                 const ftxui::Event& event) {
    using namespace ftxui;

    if (event == Event::Character('y') || event == Event::Character('Y')) {
        if (trashSelected_) {
            trashSelected_();
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
