/**
 * @file explorer_view.cpp
 * @brief TUI view implementation for the file explorer
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/app/explorer_view.hpp"

#include "expp/app/explorer_command_dispatcher.hpp"
#include "expp/app/explorer_commands.hpp"
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
        , notifications_(make_notification_options(core::global_config().config().notifications))
        , overlayController_(explorer_, notifications_)
        , previewController_(explorer_)
        , composer_(theme_) {
        dispatcher_ = std::make_unique<ExplorerCommandDispatcher>(
            explorer_, notifications_,
            [this](ExplorerCommand cmd) {
                if (cmd == ExplorerCommand::OpenHelp) {
                    overlayController_.openHelpOverlay(helpEntries_, screen_.dimy());
                } else {
                    overlayController_.openOverlayForCommand(cmd);
                }
            },
            [this]() { requestExit(); },
            [this](bool active) { keyHandler_.setMode(active ? ui::Mode::Visual : ui::Mode::Normal); });

        // setupComponents();
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

    void setupActions() {
        auto& actions = keyHandler_.actions();
        std::ranges::for_each(command_specs(), [this, &actions](const auto& spec) {
            // Register every command from the declarative catalog so help text,
            // repeatability, and runtime dispatch stay in lockstep.
            actions.registerAction(
                to_command_id(spec.command),
                [this, command = spec.command](const ui::ActionContext& ctx) {
                    // executeCommand(command, ctx);
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

    void rebuildHelpEntries() {
        helpEntries_ = ui::build_help_entries(keyHandler_.actions().actions(), keyHandler_.keymap().bindings());
        overlayController_.updateHelpEntries(helpEntries_);
    }

    [[nodiscard]] std::optional<std::filesystem::path> currentPreviewTarget() const {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return std::nullopt;
        }
        return state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path;
    }

    void syncDerivedState(bool force_preview = false) {
        if (const int measured_rows = ExplorerPresenter::listViewportRows(screen_.dimy()); measured_rows > 0) {
            explorer_->setViewportRows(measured_rows);
        }

        previewController_.sync(currentPreviewTarget(), force_preview);

        if (auto* help = std::get_if<HelpOverlayState>(&overlayController_.state())) {
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
        notifications_.expire();
        syncDerivedState();

        const auto& state = explorer_->state();
        const auto screen_model =
            presenter_.present(state, overlayController_.state(), keyHandler_.buffer(), keyHandler_.mode());

        return composer_.compose(state, screen_model, previewController_.model(), overlayController_.state(),
                                 overlayController_.activeInputComponent(), notifications_.current());
    }

    [[nodiscard]] bool handleEvent(const ftxui::Event& event) {
        if (event == ftxui::Event::Custom) {
            return false;
        }

        if (overlayController_.handleEvent(event)) {
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

    std::unique_ptr<ExplorerCommandDispatcher> dispatcher_;
    ExplorerOverlayController overlayController_;
    ExplorerPreviewController previewController_;
    ExplorerRenderComposer composer_;

    std::vector<ui::HelpEntry> helpEntries_;
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
