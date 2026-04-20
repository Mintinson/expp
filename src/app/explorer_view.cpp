/**
 * @file explorer_view.cpp
 * @brief TUI view implementation for the file explorer
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/app/explorer_view.hpp"

#include "expp/app/explorer_command_dispatcher.hpp"
#include "expp/app/explorer_commands.hpp"
#include "expp/app/explorer_directory_controller.hpp"
#include "expp/app/explorer_mutation_controller.hpp"
#include "expp/app/explorer_overlay_controller.hpp"
#include "expp/app/explorer_presenter.hpp"
#include "expp/app/explorer_preview_controller.hpp"
#include "expp/app/explorer_render_composer.hpp"
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
#include <chrono>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace expp::app {

class ExplorerView::Impl {
public:
    explicit Impl(std::shared_ptr<Explorer> explorer)
        : explorer_(std::move(explorer))
        , screen_(ftxui::ScreenInteractive::Fullscreen())
        , theme_(&ui::global_theme())
        , keyHandler_(core::global_config().config().behavior.keyTimeoutMs)
        , notifications_(make_notification_options(core::global_config().config().notifications))
        , previewController_(explorer_)
        , directoryController_(explorer_, notifications_)
        , mutationController_(explorer_, notifications_, directoryController_)
        , composer_(theme_) {
        explorer_->services().runtime->mailbox().setWakeCallback([this] { screen_.PostEvent(ftxui::Event::Custom); });
        notifications_.setPublishObserver([this](NotificationCenter::Clock::time_point expires_at) {
            scheduleToastExpiry(expires_at);
        });

        overlayController_ = std::make_unique<ExplorerOverlayController>(
            explorer_, notifications_,
            [this](std::string input) { directoryController_.navigateToInput(std::move(input)); },
            [this](std::string name) { mutationController_.create(std::move(name)); },
            [this](std::string new_name) { mutationController_.rename(std::move(new_name)); },
            [this]() { mutationController_.deleteSelected(); },
            [this]() { mutationController_.trashSelected(); });

        dispatcher_ = std::make_unique<ExplorerCommandDispatcher>(
            explorer_, notifications_,
            [this](ExplorerCommand cmd) {
                if (cmd == ExplorerCommand::OpenHelp) {
                    overlayController_->openHelpOverlay(helpEntries_, screen_.dimy());
                } else {
                    overlayController_->openOverlayForCommand(cmd);
                }
            },
            [this]() { requestExit(); },
            [this](bool active) { keyHandler_.setMode(active ? ui::Mode::Visual : ui::Mode::Normal); },
            [this](ExplorerCommand command, const ui::ActionContext& ctx) { handleAsyncCommand(command, ctx); });

        setupActions();
        installDefaultBindings();
        loadUserBindings();
        rebuildHelpEntries();
        syncDerivedState(true);
    }

    ~Impl() {
        explorer_->services().runtime->mailbox().setWakeCallback({});
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
        screen_.Loop(component);
        return 0;
    }

    void requestExit() {
        screen_.Exit();
    }

private:
    void handleAsyncCommand(ExplorerCommand command, const ui::ActionContext& ctx) {
        (void)ctx;
        switch (command) {
            case ExplorerCommand::GoParent:
                directoryController_.goParent();
                break;
            case ExplorerCommand::EnterSelected: {
                const auto selected = explorer_->selectedPath();
                if (!selected.has_value()) {
                    break;
                }
                const auto& state = explorer_->state();
                const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
                if (entry.isDirectory()) {
                    directoryController_.navigateTo(entry.path);
                } else {
                    mutationController_.openSelected();
                }
                break;
            }
            case ExplorerCommand::GoHomeDirectory:
                directoryController_.navigateToHomeDirectory();
                break;
            case ExplorerCommand::GoConfigDirectory:
                directoryController_.navigateToConfigDirectory();
                break;
            case ExplorerCommand::GoLinkTargetDirectory:
                directoryController_.navigateToSelectedLinkTargetDirectory();
                break;
            case ExplorerCommand::OpenFile:
                mutationController_.openSelected();
                break;
            case ExplorerCommand::Yank:
                mutationController_.yankSelected();
                break;
            case ExplorerCommand::Cut:
                mutationController_.cutSelected();
                break;
            case ExplorerCommand::DiscardYank:
                mutationController_.discardYank();
                break;
            case ExplorerCommand::Paste:
                mutationController_.pasteYanked(false);
                break;
            case ExplorerCommand::PasteOverwrite:
                mutationController_.pasteYanked(true);
                break;
            case ExplorerCommand::CopyEntryPathRelative:
                mutationController_.copySelectedPath(false);
                break;
            case ExplorerCommand::CopyCurrentDirRelative:
                mutationController_.copyCurrentDirectoryPath(false);
                break;
            case ExplorerCommand::CopyEntryPathAbsolute:
                mutationController_.copySelectedPath(true);
                break;
            case ExplorerCommand::CopyCurrentDirAbsolute:
                mutationController_.copyCurrentDirectoryPath(true);
                break;
            case ExplorerCommand::CopyFileName:
                mutationController_.copySelectedFileName();
                break;
            case ExplorerCommand::CopyNameWithoutExtension:
                mutationController_.copySelectedStem();
                break;
            case ExplorerCommand::ToggleHidden:
                directoryController_.toggleHidden();
                break;
            default:
                break;
        }
    }

    void setupActions() {
        auto& actions = keyHandler_.actions();
        std::ranges::for_each(command_specs(), [this, &actions](const auto& spec) {
            actions.registerAction(
                to_command_id(spec.command),
                [this, command = spec.command](const ui::ActionContext& ctx) {
                    dispatcher_->execute(command, ctx);
                    if (exits_visual_mode(command) && keyHandler_.mode() == ui::Mode::Visual) {
                        explorer_->exitVisualMode();
                        keyHandler_.setMode(ui::Mode::Normal);
                    }
                    syncDerivedState();
                },
                std::string{spec.description}, std::string{spec.category}, spec.repeatable);
        });
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

    void rebuildHelpEntries() {
        helpEntries_ = ui::build_help_entries(keyHandler_.actions().actions(), keyHandler_.keymap().bindings());
        overlayController_->updateHelpEntries(helpEntries_);
    }

    [[nodiscard]] std::optional<std::filesystem::path> currentPreviewTarget() const {
        return explorer_->selectedPath();
    }

    void syncDerivedState(bool force_preview = false) {
        if (const int measured_rows = ExplorerPresenter::listViewportRows(screen_.dimy()); measured_rows > 0) {
            explorer_->setViewportRows(measured_rows);
        }

        previewController_.sync(currentPreviewTarget(), force_preview);
        directoryController_.updateViewportInterest();

        if (auto* help = std::get_if<HelpOverlayState>(&overlayController_->state())) {
            help->viewport.viewportRows = ExplorerPresenter::helpViewportRows(screen_.dimy());
            help->viewport = ui::clamp_help_viewport(help->viewport, help->model);
        }
    }

    void scheduleToastExpiry(NotificationCenter::Clock::time_point expires_at) {
        const auto generation = ++toastGeneration_;
        const auto now = NotificationCenter::Clock::now();
        const auto delay = expires_at > now ? expires_at - now : std::chrono::milliseconds(0);
        explorer_->services().runtime->scheduleAfter(delay, [this, generation] {
            if (generation != toastGeneration_) {
                return;
            }
            notifications_.expire();
        });
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
        const auto& state = explorer_->state();
        const auto screen_model =
            presenter_.present(state, overlayController_->state(), keyHandler_.buffer(), keyHandler_.mode());

        return composer_.compose(state, screen_model, previewController_.model(), overlayController_->state(),
                                 overlayController_->activeInputComponent(), notifications_.current());
    }

    [[nodiscard]] bool handleEvent(const ftxui::Event& event) {
        if (event == ftxui::Event::Custom) {
            const bool drained = explorer_->services().runtime->mailbox().drain();
            syncDerivedState();
            return drained;
        }

        if (overlayController_->handleEvent(event)) {
            syncDerivedState();
            return true;
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
    ExplorerPreviewController previewController_;
    ExplorerDirectoryController directoryController_;
    ExplorerMutationController mutationController_;
    ExplorerRenderComposer composer_;

    std::unique_ptr<ExplorerCommandDispatcher> dispatcher_;
    std::unique_ptr<ExplorerOverlayController> overlayController_;

    std::vector<ui::HelpEntry> helpEntries_;
    std::vector<std::string> startupWarnings_;
    std::optional<core::Error> initError_;
    std::uint64_t toastGeneration_{0};
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
