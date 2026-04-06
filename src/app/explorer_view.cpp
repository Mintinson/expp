/**
 * @file explorer_view.cpp
 * @brief TUI view implementation for the file explorer
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/app/explorer_view.hpp"

#include "expp/app/explorer_commands.hpp"
#include "expp/app/explorer_presenter.hpp"
#include "expp/app/navigation_utils.hpp"
#include "expp/app/notification_center.hpp"
#include "expp/core/config.hpp"
#include "expp/ui/components.hpp"
#include "expp/ui/key_handler.hpp"
#include "expp/ui/theme.hpp"

// don't change this include order, otherwise it causes weird compilation errors
// on MSVC
// clang-format off
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
// clang-format on

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace expp::app {

class ExplorerView::Impl {
public:
    static constexpr auto kRefreshInterval = std::chrono::milliseconds(120);

    explicit Impl(std::shared_ptr<Explorer> explorer)
        : explorer_(std::move(explorer))
        , screen_(ftxui::ScreenInteractive::Fullscreen())
        , theme_(&ui::global_theme())
        , keyHandler_(core::global_config().config().behavior.keyTimeoutMs)
        , notifications_(make_notification_options(core::global_config().config().notifications)) {
        setupComponents();
        setupActions();
        installDefaultBindings();
        loadUserBindings();
        rebuildHelpEntries();
        syncDerivedState(true);
    }

    int run() {
        using namespace ftxui;

        if (initError_.has_value()) {
            std::println(stderr, "Fatal: {}", initError_->message());
            return 1;
        }

        auto component = Renderer([this] { return render(); });
        component = CatchEvent(component, [this](const Event& event) { return handleEvent(event); });

        publishStartupWarnings();
        running_.store(true);
        refreshTicker_ = std::jthread([this](const std::stop_token& stop_token) {
            while (!stop_token.stop_requested()) {
                std::this_thread::sleep_for(kRefreshInterval);
                if (!running_.load()) {
                    break;
                }
                screen_.PostEvent(Event::Custom);
            }
        });

        screen_.Loop(component);
        running_.store(false);
        if (refreshTicker_.joinable()) {
            refreshTicker_.request_stop();
        }
        return 0;
    }

    void requestExit() {
        running_.store(false);
        if (refreshTicker_.joinable()) {
            refreshTicker_.request_stop();
        }
        screen_.Exit();
    }

private:
    [[nodiscard]] bool handleResult(core::VoidResult result,
                                    std::string success_message = {},
                                    const ui::ToastSeverity success_severity = ui::ToastSeverity::Success) {
        if (!result) {
            notifications_.publish(severity_for_error(result.error()), result.error().message());
            return false;
        }

        if (!success_message.empty()) {
            notifications_.publish(success_severity, std::move(success_message));
        }
        return true;
    }

    /**
     * @brief Get the number of selected items, accounting for visual mode if active
     * @return The number of selected items (1 if visual mode is inactive, otherwise the count of visually selected
     * items)
     */
    [[nodiscard]] int selectedCount() const noexcept {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return 0;
        }
        return state.selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
    }

    /**
     * @brief Format a noun with a count, handling singular and plural forms
     * @param count The number of items
     * @param singular The singular form of the noun
     * @return The formatted string (e.g., "1 file" or "3 files")
     */
    [[nodiscard]] static std::string nounWithCount(int count, std::string_view singular) {
        return std::format("{} {}{}", count, singular, count == 1 ? "" : "s");
    }

    [[nodiscard]] static bool isBlank(std::string_view text) {
        return std::ranges::all_of(text, [](unsigned char ch) { return std::isspace(ch) != 0; });
    }

    void setupComponents() {
        const auto& cfg = core::global_config().config();

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

    void setupActions() {
        auto& actions = keyHandler_.actions();
        for (const auto& spec : command_specs()) {
            // Register every command from the declarative catalog so help text,
            // repeatability, and runtime dispatch stay in lockstep.
            actions.registerAction(
                to_command_id(spec.command),
                [this, command = spec.command](const ui::ActionContext& ctx) {
                    executeCommand(command, ctx);
                    if (exits_visual_mode(command) && keyHandler_.mode() == ui::Mode::Visual) {
                        explorer_->exitVisualMode();
                        keyHandler_.setMode(ui::Mode::Normal);
                    }
                    syncDerivedState();
                },
                std::string{spec.description}, std::string{spec.category}, spec.repeatable);
        }
    }

    void installDefaultBindings() {
        auto& keymap = keyHandler_.keymap();
        for (const auto& [keys, command, mode, description] : default_bindings()) {
            auto result = keymap.bind(keys, to_command_id(command), mode, std::string{description});
            if (!result && !initError_.has_value()) {
                initError_ = result.error();
                return;
            }
        }
    }

    void loadUserBindings() {
        const auto config_path = core::ConfigManager::userConfigPath();
        if (!std::filesystem::exists(config_path)) {
            return;
        }

        // User bindings are layered on top of catalog defaults. Rebuild from the
        // default set first so unknown or partial user config files do not leave
        // the keymap half-empty.
        keyHandler_.keymap().clear();
        installDefaultBindings();
        if (initError_.has_value()) {
            return;
        }

        auto result =
            keyHandler_.keymap().loadFromFile(config_path, [](std::string_view name) -> std::optional<ui::CommandId> {
                if (const auto command = command_from_name(name)) {
                    return to_command_id(*command);
                }
                return std::nullopt;
            });
        if (!result) {
            initError_ = result.error();
            return;
        }

        for (const auto& warning : result->warnings) {
            startupWarnings_.push_back(warning.message);
        }
    }

    void executeCommand(ExplorerCommand command, const ui::ActionContext& ctx) {
        switch (command) {
            case ExplorerCommand::MoveDown:
                explorer_->moveDown(ctx.count);
                return;
            case ExplorerCommand::MoveUp:
                explorer_->moveUp(ctx.count);
                return;
            case ExplorerCommand::GoParent:
                (void)handleResult(explorer_->goParent());
                return;
            case ExplorerCommand::EnterSelected:
                (void)handleResult(explorer_->enterSelected(true));
                return;
            case ExplorerCommand::GoTop:
                explorer_->goToTop();
                return;
            case ExplorerCommand::GoBottom:
                if (!explorer_->state().entries.empty()) {
                    if (ctx.count > 1) {
                        explorer_->goToLine(ctx.count);
                    } else {
                        explorer_->goToBottom();
                    }
                }
                return;
            case ExplorerCommand::PageDown:
                explorer_->moveDown(ExplorerPresenter::kPageStep * ctx.count);
                return;
            case ExplorerCommand::PageUp:
                explorer_->moveUp(ExplorerPresenter::kPageStep * ctx.count);
                return;
            case ExplorerCommand::GoHomeDirectory:
                (void)navigateToHomeDirectory();
                return;
            case ExplorerCommand::GoConfigDirectory:
                (void)navigateToConfigDirectory();
                return;
            case ExplorerCommand::GoLinkTargetDirectory:
                (void)handleResult(explorer_->navigateToSelectedLinkTargetDirectory());
                return;
            case ExplorerCommand::PromptDirectoryJump:
                openDirectoryJumpOverlay();
                return;
            case ExplorerCommand::EnterVisualMode:
                explorer_->enterVisualMode();
                if (explorer_->state().selection.visualModeActive) {
                    keyHandler_.setMode(ui::Mode::Visual);
                }
                return;
            case ExplorerCommand::ExitVisualMode:
                explorer_->exitVisualMode();
                keyHandler_.setMode(ui::Mode::Normal);
                return;
            case ExplorerCommand::OpenFile: {
                const auto& state = explorer_->state();
                const bool can_open =
                    !state.entries.empty() &&
                    !state.entries[static_cast<std::size_t>(state.selection.currentSelected)].isDirectory();
                (void)handleResult(explorer_->openSelected(), can_open ? "Opened with default application" : "");
                return;
            }
            case ExplorerCommand::Create:
                openCreateOverlay();
                return;
            case ExplorerCommand::Rename:
                openRenameOverlay();
                return;
            case ExplorerCommand::Yank: {
                const int count = selectedCount();
                (void)handleResult(explorer_->yankSelected(),
                                   count > 0 ? std::format("Copied {}", nounWithCount(count, "item")) : "");
                return;
            }
            case ExplorerCommand::Cut: {
                const int count = selectedCount();
                (void)handleResult(explorer_->cutSelected(),
                                   count > 0 ? std::format("Cut {}", nounWithCount(count, "item")) : "");
                return;
            }
            case ExplorerCommand::DiscardYank:
                (void)handleResult(explorer_->discardYank(), "Clipboard cleared", ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::Paste: {
                const int item_count = static_cast<int>(explorer_->state().clipboard.paths.size());
                (void)handleResult(explorer_->pasteYanked(false),
                                   item_count > 0 ? std::format("Pasted {}", nounWithCount(item_count, "item")) : "");
                return;
            }
            case ExplorerCommand::PasteOverwrite: {
                const int item_count = static_cast<int>(explorer_->state().clipboard.paths.size());
                (void)handleResult(
                    explorer_->pasteYanked(true),
                    item_count > 0 ? std::format("Pasted {} with overwrite", nounWithCount(item_count, "item")) : "");
                return;
            }
            case ExplorerCommand::CopyEntryPathRelative:
                (void)handleResult(explorer_->copySelectedPathToSystemClipboard(false), "Copied relative path",
                                   ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::CopyCurrentDirRelative:
                (void)handleResult(explorer_->copyCurrentDirectoryPathToSystemClipboard(false),
                                   "Copied relative directory path", ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::CopyEntryPathAbsolute:
                (void)handleResult(explorer_->copySelectedPathToSystemClipboard(true), "Copied absolute path",
                                   ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::CopyCurrentDirAbsolute:
                (void)handleResult(explorer_->copyCurrentDirectoryPathToSystemClipboard(true),
                                   "Copied absolute directory path", ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::CopyFileName:
                (void)handleResult(explorer_->copySelectedFileNameToSystemClipboard(), "Copied file name",
                                   ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::CopyNameWithoutExtension:
                (void)handleResult(explorer_->copySelectedNameWithoutExtensionToSystemClipboard(),
                                   "Copied name without extension", ui::ToastSeverity::Info);
                return;
            case ExplorerCommand::Trash:
                openTrashOverlay();
                return;
            case ExplorerCommand::Delete:
                openDeleteOverlay();
                return;
            case ExplorerCommand::Search:
                openSearchOverlay();
                return;
            case ExplorerCommand::NextMatch:
                for (int index = 0; index < ctx.count; ++index) {
                    explorer_->nextMatch();
                }
                return;
            case ExplorerCommand::PrevMatch:
                for (int index = 0; index < ctx.count; ++index) {
                    explorer_->prevMatch();
                }
                return;
            case ExplorerCommand::ClearSearch:
                explorer_->clearSearch();
                return;
            case ExplorerCommand::ToggleHidden:
                (void)handleResult(explorer_->toggleShowHidden());
                return;
            case ExplorerCommand::SortModified:
            case ExplorerCommand::SortModifiedDesc:
            case ExplorerCommand::SortBirth:
            case ExplorerCommand::SortBirthDesc:
            case ExplorerCommand::SortExtension:
            case ExplorerCommand::SortExtensionDesc:
            case ExplorerCommand::SortAlpha:
            case ExplorerCommand::SortAlphaDesc:
            case ExplorerCommand::SortNatural:
            case ExplorerCommand::SortNaturalDesc:
            case ExplorerCommand::SortSize:
            case ExplorerCommand::SortSizeDesc:
                applySortCommand(command);
                return;
            case ExplorerCommand::OpenHelp:
                openHelpOverlay();
                return;
            case ExplorerCommand::Quit:
                requestExit();
                return;
            default:
                return;
        }
    }

    void applySortCommand(ExplorerCommand command) {
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

    void rebuildHelpEntries() {
        helpEntries_ = ui::build_help_entries(keyHandler_.actions().actions(), keyHandler_.keymap().bindings());
        if (auto* help = std::get_if<HelpOverlayState>(&overlayState_)) {
            // Keep an already-open help overlay live-updated if bindings change.
            help->model.setEntries(helpEntries_);
            help->model.setFilter(help->filterText);
            help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
        }
    }

    void rebuildInputComponents() {
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

        // createInputComponent_ = {};
        // renameInputComponent_ = {};
        // searchInputComponent_ = {};
        // directoryJumpInputComponent_ = {};
        // helpFilterInputComponent_ = {};

        // if (auto* overlay = std::get_if<CreateOverlayState>(&overlayState_)) {
        //     createInputComponent_ = Input(&overlay->input, "filename or path/to/dir/",
        //     make_input_option(Color::Cyan2)); createInputComponent_->TakeFocus();
        // }
        // if (auto* overlay = std::get_if<RenameOverlayState>(&overlayState_)) {
        //     renameInputComponent_ = Input(&overlay->input, "new name", make_input_option(Color::DarkCyan));
        //     renameInputComponent_->TakeFocus();
        // }
        // if (auto* overlay = std::get_if<SearchOverlayState>(&overlayState_)) {
        //     searchInputComponent_ = Input(&overlay->input, "pattern", make_input_option(Color::Yellow));
        //     searchInputComponent_->TakeFocus();
        // }
        // if (auto* overlay = std::get_if<DirectoryJumpOverlayState>(&overlayState_)) {
        //     directoryJumpInputComponent_ =
        //         Input(&overlay->input, "~/path/to/dir", make_input_option(Color::GreenLight));
        //     directoryJumpInputComponent_->TakeFocus();
        // }
        // if (auto* overlay = std::get_if<HelpOverlayState>(&overlayState_)) {
        //     helpFilterInputComponent_ =
        //         Input(&overlay->filterText, "shortcut or description", make_input_option(Color::Yellow));
        //     if (overlay->filterMode) {
        //         helpFilterInputComponent_->TakeFocus();
        //     }
        // }

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
                    activeInputComponent_ =
                        Input(&overlay.input, "~/path/to/dir", make_input_option(Color::GreenLight));
                    activeInputComponent_->TakeFocus();
                } else if constexpr (std::is_same_v<T, HelpOverlayState>) {
                    activeInputComponent_ =
                        Input(&overlay.filterText, "shortcut or description", make_input_option(Color::Yellow));
                    if (overlay.filterMode) {
                        activeInputComponent_->TakeFocus();
                    }
                }
            },
            overlayState_);
    }

    [[nodiscard]] bool navigateToHomeDirectory() {
        auto home_result = resolve_home_directory();
        if (!home_result) {
            notifications_.publish(severity_for_error(home_result.error()), home_result.error().message());
            return false;
        }
        return handleResult(explorer_->navigateTo(*home_result));
    }

    [[nodiscard]] bool navigateToConfigDirectory() {
        const auto config_dir = core::ConfigManager::userConfigPath().parent_path();
        if (config_dir.empty()) {
            notifications_.publish(ui::ToastSeverity::Warning, "Config directory is not available");
            return false;
        }
        return handleResult(explorer_->navigateTo(config_dir));
    }

    [[nodiscard]] bool navigateToTypedDirectory(const std::string& input) {
        auto resolved_path = resolve_directory_input(input, explorer_->state().currentDir);
        if (!resolved_path) {
            notifications_.publish(severity_for_error(resolved_path.error()), resolved_path.error().message());
            return false;
        }
        return handleResult(explorer_->navigateTo(*resolved_path));
    }

    void openHelpOverlay() {
        HelpOverlayState overlay;
        overlay.model.setEntries(helpEntries_);
        overlay.viewport.viewportRows = ExplorerPresenter::helpViewportRows(screen_.dimy());
        overlay.viewport = ui::clamp_help_viewport(overlay.viewport, overlay.model);
        overlayState_ = std::move(overlay);
        rebuildInputComponents();
    }

    void openDirectoryJumpOverlay() {
        overlayState_ = DirectoryJumpOverlayState{};
        rebuildInputComponents();
    }

    void openCreateOverlay() {
        overlayState_ = CreateOverlayState{};
        rebuildInputComponents();
    }

    void openRenameOverlay() {
        RenameOverlayState overlay;
        const auto& state = explorer_->state();
        if (!state.entries.empty()) {
            overlay.input = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename();
        }
        overlayState_ = std::move(overlay);
        rebuildInputComponents();
    }

    void openSearchOverlay() {
        overlayState_ = SearchOverlayState{};
        rebuildInputComponents();
    }

    void openDeleteOverlay() {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return;
        }
        overlayState_ = DeleteConfirmOverlayState{
            .targetName = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename(),
            .selectionCount = selectedCount(),
        };
    }

    void openTrashOverlay() {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return;
        }
        overlayState_ = TrashConfirmOverlayState{
            .targetName = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].filename(),
            .selectionCount = selectedCount(),
        };
    }

    void closeOverlay() {
        overlayState_ = std::monostate{};
        rebuildInputComponents();
    }

    [[nodiscard]] std::optional<std::filesystem::path> currentPreviewTarget() const {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return std::nullopt;
        }
        return state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path;
    }

    void refreshPreview() {
        previewCancellation_.cancel();
        previewCancellation_.reset();

        const auto target = currentPreviewTarget();
        if (!target.has_value()) {
            previewModel_ = ui::PreviewIdleState{};
            previewTarget_.reset();
            return;
        }

        previewTarget_ = target;
        previewModel_ = ui::PreviewLoadingState{.target = *target};

        const int max_lines = std::max(1, core::global_config().config().preview.maxLines);
        // The scheduler is inline today, but this call boundary is where future
        // background preview loading and stale-result dropping will plug in.
        const auto result = scheduler_.execute(
            core::TaskContext{
                .name = "preview.load",
                .priority = core::TaskPriority::UserVisible,
                .taskClass = core::TaskClass::Micro,
                .cancellation = previewCancellation_.token(),
            },
            [&](const core::TaskContext& context) {
                return explorer_->services().preview->loadPreview({
                    .target = *target,
                    .maxLines = max_lines,
                    .cancellation = context.cancellation,
                });
            });

        if (previewCancellation_.token().isCancellationRequested()) {
            return;
        }

        if (!result) {
            previewModel_ = ui::PreviewErrorState{
                .target = *target,
                .message = result.error().message(),
            };
            return;
        }

        previewModel_ = ui::PreviewReadyState{
            .target = *target,
            .lines = result->lines,
        };
    }

    void syncDerivedState(bool force_preview = false) {
        if (const int measured_rows = ExplorerPresenter::listViewportRows(screen_.dimy()); measured_rows > 0) {
            explorer_->setViewportRows(measured_rows);
        }

        const auto next_target = currentPreviewTarget();
        if (force_preview || next_target != previewTarget_) {
            refreshPreview();
        }

        if (auto* help = std::get_if<HelpOverlayState>(&overlayState_)) {
            help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
        }
    }

    void publishStartupWarnings() {
        if (startupWarnings_.empty()) {
            return;
        }
        notifications_.publish(ui::ToastSeverity::Warning, summarizeWarnings(startupWarnings_));
        startupWarnings_.clear();
    }

    [[nodiscard]] static std::string summarizeWarnings(const std::vector<std::string>& warnings) {
        if (warnings.empty()) {
            return {};
        }
        if (warnings.size() == 1) {
            return warnings.front();
        }

        constexpr std::size_t kPreviewCount = 2;
        std::string preview;
        for (std::size_t index = 0; index < std::min(kPreviewCount, warnings.size()); ++index) {
            if (!preview.empty()) {
                preview += "; ";
            }
            preview += warnings[index];
        }
        if (warnings.size() > kPreviewCount) {
            return std::format("Skipped {} key bindings: {}; +{} more", warnings.size(), preview,
                               warnings.size() - kPreviewCount);
        }
        return std::format("Skipped {} key bindings: {}", warnings.size(), preview);
    }

    [[nodiscard]] ftxui::Element render() {
        using namespace ftxui;

        notifications_.expire();
        syncDerivedState();

        const auto& state = explorer_->state();
        // The presenter converts absolute explorer state into render-local
        // offsets so the view only deals with the currently visible slice.
        const auto screen_model = presenter_.present(state, overlayState_, keyHandler_.buffer(), keyHandler_.mode());

        auto parent_content = fileList_->render(state.parentEntries, state.selection.parentSelected, {}, -1, {});

        std::span<const core::filesystem::FileEntry> visible_entries{state.entries};
        visible_entries =
            visible_entries.subspan(static_cast<std::size_t>(screen_model.currentList.offset),
                                    static_cast<std::size_t>(std::max(0, screen_model.currentList.visibleEnd -
                                                                             screen_model.currentList.offset)));
        auto current_content = fileList_->render(
            visible_entries, screen_model.currentList.selectedIndex, screen_model.currentList.searchMatches,
            screen_model.currentList.currentMatchIndex, screen_model.currentList.visualSelectedIndices);

        auto preview_content = preview_->render(previewModel_);
        auto panels = panel_->render(screen_model.parentTitle, std::move(parent_content), screen_model.currentTitle,
                                     std::move(current_content), "Preview", std::move(preview_content));

        ui::StatusBarInfo status_info;
        status_info.currentPath = screen_model.statusPath;
        status_info.keyBuffer = keyHandler_.buffer();
        status_info.searchStatus = screen_model.searchStatus;
        status_info.helpText = screen_model.helpText;

        auto status_bar = statusBar_->render(status_info);
        ftxui::Element main_content = vbox({
            std::move(panels) | flex,
            separator(),
            std::move(status_bar),
        });

        main_content = renderOverlay(std::move(main_content));
        if (const auto& toast = notifications_.current(); toast.has_value()) {
            auto toast_layer = vbox({
                filler(),
                hbox({
                    filler(),
                    toast_->render(*toast),
                }),
            });
            main_content = dbox({
                std::move(main_content),
                std::move(toast_layer),
            });
        }

        return main_content;
    }

    [[nodiscard]] ftxui::Element renderOverlay(ftxui::Element base_content) {
        using namespace ftxui;

        return std::visit(
            [&]<typename T0>(T0& overlay) -> Element {
                using Overlay = std::decay_t<T0>;
                if constexpr (std::is_same_v<Overlay, std::monostate>) {
                    return base_content;
                } else if constexpr (std::is_same_v<Overlay, HelpOverlayState>) {
                    overlay.viewport.viewportRows = ExplorerPresenter::helpViewportRows(screen_.dimy());
                    overlay.viewport = ui::clamp_help_viewport(overlay.viewport, overlay.model);
                    auto help_elem = helpMenu_->render(overlay.model, overlay.filterMode, overlay.viewport);
                    return dbox({std::move(base_content) | dim, std::move(help_elem) | clear_under | center});
                } else if constexpr (std::is_same_v<Overlay, DirectoryJumpOverlayState>) {
                    auto dialog_elem = dialog_->renderInput(
                        "Jump To Directory", "Enter a path, or use ~ for home:", activeInputComponent_->Render());
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                } else if constexpr (std::is_same_v<Overlay, CreateOverlayState>) {
                    auto dialog_elem = dialog_->renderInput(
                        "Create New File/Directory",
                        "Enter name (end with / for directory):\ne.g., foo/bar/baz/ creates nested dirs",
                        activeInputComponent_->Render());
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                } else if constexpr (std::is_same_v<Overlay, RenameOverlayState>) {
                    auto dialog_elem = dialog_->renderInput("Rename Current File/Directory",
                                                            "Enter the new name:", activeInputComponent_->Render());
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                } else if constexpr (std::is_same_v<Overlay, SearchOverlayState>) {
                    auto dialog_elem = dialog_->renderInput("Search (case-sensitive)", "",
                                                            hbox({
                                                                text(" / "),
                                                                activeInputComponent_->Render() | flex,
                                                            }));
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                } else if constexpr (std::is_same_v<Overlay, DeleteConfirmOverlayState>) {
                    auto dialog_elem = dialog_->renderConfirmation(
                        "Delete Confirmation", "Are you sure you want to delete:", overlay.targetName, Color::Red);
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                } else {
                    auto dialog_elem = dialog_->renderConfirmation(
                        "Trash Confirmation", "Are you sure you want to move to trash:", overlay.targetName,
                        Color::Red);
                    return dbox({std::move(base_content) | dim, std::move(dialog_elem) | clear_under | center});
                }
            },
            overlayState_);
    }

    [[nodiscard]] bool handleHelpOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* help = std::get_if<HelpOverlayState>(&overlayState_);
        if (help == nullptr) {
            return false;
        }

        if (event == Event::Custom) {
            return false;
        }

        if (help->filterMode) {
            if (event == Event::Return || event == Event::Escape) {
                help->filterMode = false;
                return true;
            }

            const bool handled = activeInputComponent_->OnEvent(event);
            if (handled) {
                // Filtering can change category headers and total visual rows, so
                // selection and scroll need to be re-clamped immediately.
                help->model.setFilter(help->filterText);
                help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
            }
            return handled;
        }

        if (event == Event::Character('~') || event == Event::Escape) {
            closeOverlay();
            return true;
        }
        if (event == Event::Character('j') || event == Event::ArrowDown) {
            if (help->model.filteredCount() == 0U) {
                return true;
            }
            help->viewport.selectedIndex =
                std::clamp(help->viewport.selectedIndex + 1, 0, static_cast<int>(help->model.filteredCount()) - 1);
            help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
            return true;
        }
        if (event == Event::Character('k') || event == Event::ArrowUp) {
            if (help->model.filteredCount() == 0U) {
                return true;
            }
            help->viewport.selectedIndex =
                std::clamp(help->viewport.selectedIndex - 1, 0, static_cast<int>(help->model.filteredCount()) - 1);
            help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
            return true;
        }
        if (event == Event::Character('f')) {
            help->filterMode = true;
            rebuildInputComponents();
            return true;
        }
        return true;
    }

    [[nodiscard]] bool handleDirectoryJumpOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<DirectoryJumpOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Return) {
            if (isBlank(overlay->input)) {  // NOLINT(bugprone-branch-clone) (it is intentional for clear motivation)
                closeOverlay();
            } else if (navigateToTypedDirectory(overlay->input)) {
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

    [[nodiscard]] bool handleCreateOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<CreateOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Return) {
            const std::string success_message =
                overlay->input.empty() ? std::string{} : std::format("Created '{}'", overlay->input);
            if (handleResult(explorer_->create(overlay->input), success_message)) {
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

    [[nodiscard]] bool handleRenameOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<RenameOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Return) {
            const std::string success_message =
                overlay->input.empty() ? std::string{} : std::format("Renamed to '{}'", overlay->input);
            if (handleResult(explorer_->rename(overlay->input), success_message)) {
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

    [[nodiscard]] bool handleSearchOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<SearchOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Return) {
            explorer_->search(overlay->input);
            closeOverlay();
            return true;
        }
        if (event == Event::Escape) {
            closeOverlay();
            return true;
        }
        return activeInputComponent_->OnEvent(event);
    }

    [[nodiscard]] bool handleDeleteOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<DeleteConfirmOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Character('y') || event == Event::Character('Y')) {
            const int count = overlay->selectionCount;
            (void)handleResult(explorer_->deleteSelected(),
                               count > 0 ? std::format("Deleted {}", nounWithCount(count, "item")) : "");
            if (keyHandler_.mode() == ui::Mode::Visual) {
                keyHandler_.setMode(ui::Mode::Normal);
            }
            closeOverlay();
            return true;
        }
        if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
            closeOverlay();
            return true;
        }
        return true;
    }

    [[nodiscard]] bool handleTrashOverlayEvent(const ftxui::Event& event) {
        using namespace ftxui;
        auto* overlay = std::get_if<TrashConfirmOverlayState>(&overlayState_);
        if (overlay == nullptr) {
            return false;
        }

        if (event == Event::Character('y') || event == Event::Character('Y')) {
            const int count = overlay->selectionCount;
            (void)handleResult(explorer_->trashSelected(),
                               count > 0 ? std::format("Moved {} to trash", nounWithCount(count, "item")) : "");
            if (keyHandler_.mode() == ui::Mode::Visual) {
                keyHandler_.setMode(ui::Mode::Normal);
            }
            closeOverlay();
            return true;
        }
        if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
            closeOverlay();
            return true;
        }
        return true;
    }

    [[nodiscard]] bool handleEvent(const ftxui::Event& event) {
        if (std::holds_alternative<HelpOverlayState>(overlayState_)) {
            return handleHelpOverlayEvent(event);
        }
        if (std::holds_alternative<DirectoryJumpOverlayState>(overlayState_)) {
            return handleDirectoryJumpOverlayEvent(event);
        }
        if (std::holds_alternative<CreateOverlayState>(overlayState_)) {
            return handleCreateOverlayEvent(event);
        }
        if (std::holds_alternative<RenameOverlayState>(overlayState_)) {
            return handleRenameOverlayEvent(event);
        }
        if (std::holds_alternative<SearchOverlayState>(overlayState_)) {
            return handleSearchOverlayEvent(event);
        }
        if (std::holds_alternative<DeleteConfirmOverlayState>(overlayState_)) {
            return handleDeleteOverlayEvent(event);
        }
        if (std::holds_alternative<TrashConfirmOverlayState>(overlayState_)) {
            return handleTrashOverlayEvent(event);
        }

        if (event == ftxui::Event::Custom) {
            return false;
        }

        const bool handled = keyHandler_.handle(event);
        if (handled) {
            syncDerivedState();
        }
        return handled;
    }

    std::shared_ptr<Explorer> explorer_;
    ftxui::ScreenInteractive screen_;
    const ui::Theme* theme_;
    ui::KeyHandler keyHandler_;
    NotificationCenter notifications_;
    ExplorerPresenter presenter_;
    core::InlineScheduler scheduler_;

    std::unique_ptr<ui::FileListComponent> fileList_;
    std::unique_ptr<ui::PanelComponent> panel_;
    std::unique_ptr<ui::PreviewComponent> preview_;
    std::unique_ptr<ui::StatusBarComponent> statusBar_;
    std::unique_ptr<ui::ToastComponent> toast_;
    std::unique_ptr<ui::HelpMenuComponent> helpMenu_;
    std::unique_ptr<ui::DialogComponent> dialog_;

    // ftxui::Component createInputComponent_;
    // ftxui::Component renameInputComponent_;
    // ftxui::Component searchInputComponent_;
    // ftxui::Component directoryJumpInputComponent_;
    // ftxui::Component helpFilterInputComponent_;
    ftxui::Component activeInputComponent_;

    ExplorerOverlayState overlayState_;
    std::vector<ui::HelpEntry> helpEntries_;
    ui::PreviewModel previewModel_{ui::PreviewIdleState{}};
    std::optional<std::filesystem::path> previewTarget_;
    core::CancellationSource previewCancellation_;
    std::vector<std::string> startupWarnings_;
    std::optional<core::Error> initError_;
    std::atomic_bool running_{false};
    std::jthread refreshTicker_;
};

ExplorerView::ExplorerView(std::shared_ptr<Explorer> explorer) : impl_(std::make_unique<Impl>(std::move(explorer))) {}

ExplorerView::~ExplorerView() = default;
ExplorerView::ExplorerView(ExplorerView&&) noexcept = default;
ExplorerView& ExplorerView::operator=(ExplorerView&&) noexcept = default;

int ExplorerView::run() {
    return impl_->run();
}

void ExplorerView::requestExit() {
    impl_->requestExit();
}

}  // namespace expp::app
