/**
 * @file explorer_view.cpp
 * @brief TUI view implementation for the file explorer
 *
 * Refactored to use modular UI components:
 * - FileListComponent for file display
 * - PanelComponent for three-column layout
 * - PreviewComponent for file preview
 * - StatusBarComponent for status display
 * - DialogComponent for modals
 * - ui::KeyHandler with ActionRegistry for key bindings
 * - Theme system for consistent colors
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/app/explorer_view.hpp"

#include "expp/app/notification_center.hpp"
#include "expp/core/config.hpp"
#include "expp/ui/components.hpp"
#include "expp/ui/key_handler.hpp"
#include "expp/ui/theme.hpp"

// don't change this include order, otherwise it causes weird compilation errors
// on MSVC
// clang-format off
#include <ftxui/component/event.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
// clang-format on

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace expp::app {

class ExplorerView::Impl {
public:
    static constexpr int kPageStep = 10;
    static constexpr int kRootLayoutRows = 2;  // Main separator + status bar
    static constexpr int kPanelDecorRows = 4;  // Border(top/bottom) + title + separator
    static constexpr auto kRefreshInterval = std::chrono::milliseconds(120);
    constexpr static int kKeyTimeoutMs = 1000;

    explicit Impl(std::shared_ptr<Explorer> explorer)
        : explorer_{std::move(explorer)}
        , screen_{ftxui::ScreenInteractive::Fullscreen()}
        , theme_{&ui::global_theme()}
        , keyHandler_{core::global_config().config().behavior.keyTimeoutMs}
        , notifications_{make_notification_options(core::global_config().config().notifications)} {
        setupComponents();
        setupActions();

        // Load default key bindings first, then override with user config if available
        setupKeyBindings();
        auto config_path = core::ConfigManager::userConfigPath();
        if (std::filesystem::exists(config_path)) {
            auto result = keyHandler_.keymap().loadFromFile(config_path);
            if (!result) {
                initError_ = result.error();
                return;
            }
            applyUserKeyBindings(*result);
        }

        setupInputComponents();
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
        refreshTicker_ = std::jthread([this](std::stop_token stop_token) {
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
     * @brief Handle the result of an operation, publishing notifications as appropriate
     * @param result The result of the operation
     * @param success_message The message to display on success
     * @param success_severity The severity of the success notification
     * @return True if the operation was successful, false otherwise
     */
    [[nodiscard]] bool handleResult(core::VoidResult result,
                                    std::string success_message = {},
                                    ui::ToastSeverity success_severity = ui::ToastSeverity::Success) {
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
     * @return The number of selected items (1 if visual mode is inactive, otherwise the count of visually selected items)
     */
    [[nodiscard]] int selectedCount() const noexcept {
        const auto& state = explorer_->state();
        if (state.entries.empty()) {
            return 0;
        }
        if (state.visualModeActive) {
            return std::max(1, explorer_->visualSelectionCount());
        }
        return 1;
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

    /**
     * @brief Publish startup warnings to the notification center
     */
    void publishStartupWarnings() {
        if (startupWarnings_.empty()) {
            return;
        }
        notifications_.publish(ui::ToastSeverity::Warning, summarizeWarnings(startupWarnings_));
        startupWarnings_.clear();
    }

    /// Apply user-loaded key bindings and collect any warnings for display
    void applyUserKeyBindings(const ui::KeyLoadReport& report) {
        auto& keymap = keyHandler_.keymap();
        keymap.clear();
        setupKeyBindings();

        for (const auto& warning : report.warnings) {
            startupWarnings_.push_back(warning.message);
        }

        for (const auto& binding : report.loadedBindings) {
            if (keyHandler_.actions().find(binding.actionName) == nullptr) {
                startupWarnings_.push_back(
                    std::format("Skipped binding '{}' -> '{}': unknown action", binding.keys, binding.actionName));
                continue;
            }

            auto bind_result = keymap.bind(binding.keys, binding.actionName, binding.mode, binding.description);
            if (!bind_result) {
                startupWarnings_.push_back(std::format("Skipped binding '{}' -> '{}': {}", binding.keys,
                                                       binding.actionName, bind_result.error().message()));
            }
        }
    }

    /**
     * @brief Summarize a list of warnings into a single string for display
     * @param warnings
     * @return
     */
    [[nodiscard]] static std::string summarizeWarnings(const std::vector<std::string>& warnings) {
        if (warnings.empty()) {
            return {};
        }
        if (warnings.size() == 1) {
            return warnings.front();
        }

        constexpr size_t kPreviewCount = 2;
        // std::string summary = std::format("Skipped {} key bindings: ", warnings.size());

        using namespace std::string_view_literals;
        auto preview =
            warnings | std::views::take(kPreviewCount) | std::views::join_with("; "sv) | std::ranges::to<std::string>();
        if (warnings.size() > kPreviewCount) {
            return std::format("Skipped {} key bindings: {}; +{} more", warnings.size(), preview,
                               warnings.size() - kPreviewCount);
        }
        return std::format("Skipped {} key bindings: {}", warnings.size(), preview);
    }

    /**
     * @brief Setup reusable UI components
     */
    void setupComponents() {
        const auto& cfg = core::global_config().config();

        ui::FileListConfig file_list_config{};
        file_list_config.theme = theme_;

        fileList_ = std::make_unique<ui::FileListComponent>(file_list_config);

        ui::PreviewConfig preview_config{};
        preview_config.theme = theme_;
        preview_config.maxLines = cfg.preview.maxLines;
        preview_ = std::make_unique<ui::PreviewComponent>(preview_config);

        statusBar_ = std::make_unique<ui::StatusBarComponent>(theme_);
        toast_ = std::make_unique<ui::ToastComponent>(theme_);

        dialog_ = std::make_unique<ui::DialogComponent>();
        dialog_->setTheme(theme_);

        ui::PanelConfig panel_config{};
        panel_config.theme = theme_;
        panel_config.showParent = cfg.layout.showParentPanel;
        panel_config.showPreview = cfg.layout.showPreviewPanel;
        panel_config.parentWidth = cfg.layout.parentPanelWidth;
        panel_config.previewWidth = cfg.layout.previewPanelWidth;
        panel_ = std::make_unique<ui::PanelComponent>(panel_config);
    }

    /**
     * @brief Register all actions using the ActionRegistry
     */
    void setupActions() {
        auto& actions = keyHandler_.actions();

        // Navigation actions
        actions.registerAction(
            "move_down", [this](const ui::ActionContext& ctx) { explorer_->moveDown(ctx.count); }, "Move cursor down",
            "Navigation", true);
        actions.registerAction(
            "move_up", [this](const ui::ActionContext& ctx) { explorer_->moveUp(ctx.count); }, "Move cursor up",
            "Navigation", true);
        actions.registerAction(
            "go_parent",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->goParent());
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
            },
            "Move cursor up", "Navigation", false);
        actions.registerAction(
            "enter_selected",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->enterSelected(true));
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
            },
            "Enter directory or open file", "Navigation", false);
        actions.registerAction(
            "go_top", [this]([[maybe_unused]] const ui::ActionContext& ctx) { explorer_->goToTop(); },
            "Go to first item", "Navigation", false);
        actions.registerAction(
            "go_bottom",
            [this](const ui::ActionContext& ctx) {
                const auto& st = explorer_->state();
                if (!st.entries.empty()) {
                    if (ctx.count > 1) {
                        explorer_->goToLine(ctx.count);
                    } else {
                        explorer_->goToBottom();
                    }
                }
            },
            "Go to last item or line N", "Navigation", true);
        actions.registerAction(
            "page_down", [this](const ui::ActionContext& ctx) { explorer_->moveDown(kPageStep * ctx.count); },
            "Scroll down half page", "Navigation", true);
        actions.registerAction(
            "page_up", [this](const ui::ActionContext& ctx) { explorer_->moveUp(kPageStep * ctx.count); },
            "Scroll up half page", "Navigation", true);

        actions.registerAction(
            "enter_visual_mode",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->enterVisualMode();
                if (explorer_->state().visualModeActive) {
                    keyHandler_.setMode(ui::Mode::Visual);
                }
            },
            "Enter visual mode", "Navigation", false);

        actions.registerAction(
            "exit_visual_mode",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->exitVisualMode();
                keyHandler_.setMode(ui::Mode::Normal);
            },
            "Exit visual mode", "Navigation", false);

        // File operations
        actions.registerAction(
            "open_file",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                const auto& state = explorer_->state();
                const bool can_open_file =
                    !state.entries.empty() && !state.entries[static_cast<size_t>(state.currentSelected)].isDirectory();
                (void)handleResult(explorer_->openSelected(), can_open_file ? "Opened with default application" : "");
            },
            "Open file with default application", "File Operations", false);

        actions.registerAction(
            "create",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->showCreateDialog();
                newNameInput_.clear();
            },
            "Create new file or directory", "File Operations", false);

        actions.registerAction(
            "rename",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->showRenameDialog();
                const auto& st = explorer_->state();
                if (!st.entries.empty()) {
                    renameInput_ = st.entries[static_cast<size_t>(st.currentSelected)].filename();
                }
            },
            "Rename current item", "File Operations", false);
        actions.registerAction(
            "yank",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                const int count = selectedCount();
                (void)handleResult(explorer_->yankSelected(),
                                   count > 0 ? std::format("Copied {}", nounWithCount(count, "item")) : "");
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
            },
            "Yank current item", "File Operations", false);

        actions.registerAction(
            "cut",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                const int count = selectedCount();
                (void)handleResult(explorer_->cutSelected(),
                                   count > 0 ? std::format("Cut {}", nounWithCount(count, "item")) : "");
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
            },
            "Cut current item", "File Operations", false);

        actions.registerAction(
            "discard_yank",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->discardYank(), "Clipboard cleared", ui::ToastSeverity::Info);
            },
            "Clear clipboard", "File Operations", false);

        actions.registerAction(
            "paste",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                const auto item_count = static_cast<int>(explorer_->state().clipboardPaths.size());
                (void)handleResult(explorer_->pasteYanked(false),
                                   item_count > 0 ? std::format("Pasted {}", nounWithCount(item_count, "item")) : "");
            },
            "Paste yanked item into current directory", "File Operations", false);

        actions.registerAction(
            "paste_overwrite",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                const auto item_count = static_cast<int>(explorer_->state().clipboardPaths.size());
                (void)handleResult(
                    explorer_->pasteYanked(true),
                    item_count > 0 ? std::format("Pasted {} with overwrite", nounWithCount(item_count, "item")) : "");
            },
            "Paste into current directory with overwrite", "File Operations", false);

        actions.registerAction(
            "copy_entry_path_relative",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copySelectedPathToSystemClipboard(false), "Copied relative path",
                                   ui::ToastSeverity::Info);
            },
            "Copy selected entry relative path to system clipboard", "File Operations", false);

        actions.registerAction(
            "copy_current_dir_relative",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copyCurrentDirectoryPathToSystemClipboard(false),
                                   "Copied relative directory path", ui::ToastSeverity::Info);
            },
            "Copy current directory relative path to system clipboard", "File Operations", false);

        actions.registerAction(
            "copy_entry_path_absolute",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copySelectedPathToSystemClipboard(true), "Copied absolute path",
                                   ui::ToastSeverity::Info);
            },
            "Copy selected entry absolute path to system clipboard", "File Operations", false);

        actions.registerAction(
            "copy_current_dir_absolute",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copyCurrentDirectoryPathToSystemClipboard(true),
                                   "Copied absolute directory path", ui::ToastSeverity::Info);
            },
            "Copy current directory absolute path to system clipboard", "File Operations", false);

        actions.registerAction(
            "copy_file_name",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copySelectedFileNameToSystemClipboard(), "Copied file name",
                                   ui::ToastSeverity::Info);
            },
            "Copy selected file name to system clipboard", "File Operations", false);

        actions.registerAction(
            "copy_name_without_extension",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->copySelectedNameWithoutExtensionToSystemClipboard(),
                                   "Copied name without extension", ui::ToastSeverity::Info);
            },
            "Copy selected name without extension to system clipboard", "File Operations", false);

        actions.registerAction(
            "trash", [this]([[maybe_unused]] const ui::ActionContext& ctx) { explorer_->showTrashDialog(); },
            "Move to trash", "File Operations", false);

        actions.registerAction(
            "delete", [this]([[maybe_unused]] const ui::ActionContext& ctx) { explorer_->showDeleteDialog(); },
            "Delete permanently", "File Operations", false);

        // Search actions
        actions.registerAction(
            "search",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->showSearchDialog();
                searchInput_.clear();
            },
            "Open search dialog", "Search", false);

        actions.registerAction(
            "next_match",
            [this](const ui::ActionContext& ctx) {
                for (int i = 0; i < ctx.count; ++i) {
                    explorer_->nextMatch();
                }
            },
            "Jump to next search match", "Search", true);

        actions.registerAction(
            "prev_match",
            [this](const ui::ActionContext& ctx) {
                for (int i = 0; i < ctx.count; ++i) {
                    explorer_->prevMatch();
                }
            },
            "Jump to previous search match", "Search", true);

        actions.registerAction(
            "clear_search", [this]([[maybe_unused]] const ui::ActionContext& ctx) { explorer_->clearSearch(); },
            "Clear search highlighting", "Search", false);

        // View actions
        actions.registerAction(
            "toggle_hidden",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) {
                (void)handleResult(explorer_->toggleShowHidden());
            },
            "Toggle the visibility of hidden files", "View", false);

        registerSortAction(actions, "sort_modified", "Sort by modified time", ExplorerState::SortField::ModifiedTime,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_modified_desc", "Sort by modified time (desc)",
                           ExplorerState::SortField::ModifiedTime, ExplorerState::SortDirection::Descending);
        registerSortAction(actions, "sort_birth", "Sort by birth time", ExplorerState::SortField::BirthTime,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_birth_desc", "Sort by birth time (desc)", ExplorerState::SortField::BirthTime,
                           ExplorerState::SortDirection::Descending);
        registerSortAction(actions, "sort_extension", "Sort by extension", ExplorerState::SortField::Extension,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_extension_desc", "Sort by extension (desc)",
                           ExplorerState::SortField::Extension, ExplorerState::SortDirection::Descending);
        registerSortAction(actions, "sort_alpha", "Sort alphabetically", ExplorerState::SortField::Alphabetical,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_alpha_desc", "Sort alphabetically (desc)",
                           ExplorerState::SortField::Alphabetical, ExplorerState::SortDirection::Descending);
        registerSortAction(actions, "sort_natural", "Sort naturally", ExplorerState::SortField::Natural,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_natural_desc", "Sort naturally (desc)", ExplorerState::SortField::Natural,
                           ExplorerState::SortDirection::Descending);
        registerSortAction(actions, "sort_size", "Sort by size", ExplorerState::SortField::Size,
                           ExplorerState::SortDirection::Ascending);
        registerSortAction(actions, "sort_size_desc", "Sort by size (desc)", ExplorerState::SortField::Size,
                           ExplorerState::SortDirection::Descending);

        // Application control
        actions.registerAction(
            "quit", [this]([[maybe_unused]] const ui::ActionContext& ctx) { screen_.Exit(); }, "Quit application",
            "Application", false);
    }

    void setupKeyBindings() {
        // Use the Keymap to bind keys to actions
        auto& keymap = keyHandler_.keymap();
        // Lambda to simplify binding keys and handling errors during setup
        const auto bind_or_fail = [this, &keymap](std::string_view keys, std::string action, ui::Mode mode,
                                                  std::string description) {
            if (initError_.has_value()) {
                return;
            }
            auto result = keymap.bind(keys, std::move(action), mode, std::move(description));
            if (!result) {
                initError_ = result.error();
            }
        };

        // Navigation bindings
        bind_or_fail("j", "move_down", ui::Mode::Normal, "Move down");
        bind_or_fail("k", "move_up", ui::Mode::Normal, "Move up");
        bind_or_fail("h", "go_parent", ui::Mode::Normal, "Go to parent");
        bind_or_fail("l", "enter_selected", ui::Mode::Normal, "Enter/open");
        bind_or_fail("gg", "go_top", ui::Mode::Normal, "Go to top");
        bind_or_fail("G", "go_bottom", ui::Mode::Normal, "Go to bottom");
        bind_or_fail("v", "enter_visual_mode", ui::Mode::Normal, "Enter visual mode");

        // Page navigation
        bind_or_fail("C-d", "page_down", ui::Mode::Normal, "Page down");
        bind_or_fail("C-u", "page_up", ui::Mode::Normal, "Page up");

        // File operations
        bind_or_fail("o", "open_file", ui::Mode::Normal, "Open file");
        bind_or_fail("a", "create", ui::Mode::Normal, "Create new");
        bind_or_fail("r", "rename", ui::Mode::Normal, "Rename");
        bind_or_fail("d", "trash", ui::Mode::Normal, "Trash");
        bind_or_fail("D", "delete", ui::Mode::Normal, "Delete");
        bind_or_fail("y", "yank", ui::Mode::Normal, "Yank (copy)");
        bind_or_fail("x", "cut", ui::Mode::Normal, "Cut");
        bind_or_fail("Y", "discard_yank", ui::Mode::Normal, "Discard Yank (copy)");
        bind_or_fail("X", "discard_yank", ui::Mode::Normal, "Discard cut/copy");
        bind_or_fail("p", "paste", ui::Mode::Normal, "Paste yanked item");
        bind_or_fail("P", "paste_overwrite", ui::Mode::Normal, "Paste with overwrite");
        bind_or_fail("cc", "copy_entry_path_relative", ui::Mode::Normal, "Copy entry path (relative)");
        bind_or_fail("cd", "copy_current_dir_relative", ui::Mode::Normal, "Copy current path (relative)");
        bind_or_fail("cC", "copy_entry_path_absolute", ui::Mode::Normal, "Copy entry path (absolute)");
        bind_or_fail("cD", "copy_current_dir_absolute", ui::Mode::Normal, "Copy current path (absolute)");
        bind_or_fail("cf", "copy_file_name", ui::Mode::Normal, "Copy file name");
        bind_or_fail("cn", "copy_name_without_extension", ui::Mode::Normal, "Copy name without extension");

        // Search
        bind_or_fail("/", "search", ui::Mode::Normal, "Search");
        bind_or_fail("n", "next_match", ui::Mode::Normal, "Next match");
        bind_or_fail("N", "prev_match", ui::Mode::Normal, "Prev match");
        bind_or_fail("\\", "clear_search", ui::Mode::Normal, "Clear search");

        // View
        bind_or_fail(".", "toggle_hidden", ui::Mode::Normal, "Toggle the visibility of hidden files");
        bind_or_fail(",m", "sort_modified", ui::Mode::Normal, "Sort by modified time");
        bind_or_fail(",M", "sort_modified_desc", ui::Mode::Normal, "Sort by modified time (desc)");
        bind_or_fail(",b", "sort_birth", ui::Mode::Normal, "Sort by birth time");
        bind_or_fail(",B", "sort_birth_desc", ui::Mode::Normal, "Sort by birth time (desc)");
        bind_or_fail(",e", "sort_extension", ui::Mode::Normal, "Sort by extension");
        bind_or_fail(",E", "sort_extension_desc", ui::Mode::Normal, "Sort by extension (desc)");
        bind_or_fail(",a", "sort_alpha", ui::Mode::Normal, "Sort alphabetically");
        bind_or_fail(",A", "sort_alpha_desc", ui::Mode::Normal, "Sort alphabetically (desc)");
        bind_or_fail(",n", "sort_natural", ui::Mode::Normal, "Sort naturally");
        bind_or_fail(",N", "sort_natural_desc", ui::Mode::Normal, "Sort naturally (desc)");
        bind_or_fail(",s", "sort_size", ui::Mode::Normal, "Sort by size");
        bind_or_fail(",S", "sort_size_desc", ui::Mode::Normal, "Sort by size (desc)");

        // Visual mode
        bind_or_fail("j", "move_down", ui::Mode::Visual, "Move down (visual)");
        bind_or_fail("k", "move_up", ui::Mode::Visual, "Move up (visual)");
        bind_or_fail("gg", "go_top", ui::Mode::Visual, "Go to top (visual)");
        bind_or_fail("G", "go_bottom", ui::Mode::Visual, "Go to bottom (visual)");
        bind_or_fail("C-d", "page_down", ui::Mode::Visual, "Page down (visual)");
        bind_or_fail("C-u", "page_up", ui::Mode::Visual, "Page up (visual)");
        bind_or_fail("y", "yank", ui::Mode::Visual, "Yank selection");
        bind_or_fail("x", "cut", ui::Mode::Visual, "Cut selection");
        bind_or_fail("d", "trash", ui::Mode::Visual, "Trash selection");
        bind_or_fail("D", "delete", ui::Mode::Visual, "Delete selection");
        bind_or_fail("v", "exit_visual_mode", ui::Mode::Visual, "Exit visual mode");
        bind_or_fail("<Esc>", "exit_visual_mode", ui::Mode::Visual, "Exit visual mode");

        // Quit
        bind_or_fail("q", "quit", ui::Mode::Normal, "Quit");
    }

    void setupInputComponents() {
        using namespace ftxui;

        // Input for create dialog
        auto create_input_option = InputOption::Default();

        create_input_option.multiline = false;

        create_input_option.transform = [](InputState state) {
            if (state.is_placeholder) {
                state.element |= dim;
            }
            if (state.focused) {
                state.element |= color(Color::Cyan2);  // TODO: customizable
            }
            return state.element;
        };
        createInputComponent_ = Input(&newNameInput_, "filename or path/to/dir/", create_input_option);

        // Input for rename dialog
        auto rename_input_option = InputOption::Default();
        rename_input_option.multiline = false;
        rename_input_option.transform = [](InputState state) {
            if (state.is_placeholder) {
                state.element |= dim;
            }
            if (state.focused) {
                state.element |= color(Color::DarkCyan);
            }
            return state.element;
        };
        renameInputComponent_ = Input(&renameInput_, "new name", rename_input_option);

        // Input for search dialog
        auto search_input_option = InputOption::Default();
        search_input_option.multiline = false;
        search_input_option.transform = [](InputState state) {
            if (state.is_placeholder) {
                state.element |= dim;
            }
            if (state.focused) {
                state.element |= color(Color::Yellow);
            }
            return state.element;
        };
        searchInputComponent_ = Input(&searchInput_, "pattern", search_input_option);
    }

    ftxui::Element render() {
        using namespace ftxui;

        notifications_.expire();

        // Keep list viewport in sync with terminal height for threshold scrolling.
        // On first render some terminals report incomplete geometry; ignore tiny values.
        const int measured_rows = screen_.dimy() - (kRootLayoutRows + kPanelDecorRows);
        if (measured_rows > 1) {
            explorer_->setViewportRows(measured_rows);
        }

        const auto& state = explorer_->state();

        // Render parent directory list using FileListComponent
        auto parent_content = fileList_->render(state.parentEntries, state.parentSelected, {}, -1);

        const int total_entries = static_cast<int>(state.entries.size());
        const int visible_rows = std::max(1, state.currentViewportRows);
        // For example, if there are 100 files in total, and 20 are displayed on the screen,
        // then one can only scroll down to the 80th file at most (i.e., offset=80, displaying 80~99).
        const int max_offset = std::max(0, total_entries - visible_rows);
        const int offset = std::clamp(state.currentScrollOffset, 0, max_offset);
        const int visible_end = std::min(total_entries, offset + visible_rows);

        auto visible_entries =
            std::ranges::subrange(state.entries.begin() + offset, state.entries.begin() + visible_end);

        std::vector<int> visible_search_matches;
        int visible_current_match = -1;  // store the index of the current match within the visible matches
        visible_search_matches.reserve(state.searchMatches.size());

        for (size_t i = 0; i < state.searchMatches.size(); ++i) {
            const int match = state.searchMatches[i];

            if (match >= offset && match < visible_end) {
                visible_search_matches.push_back(match - offset);
                if (static_cast<int>(i) == state.currentMatchIndex) {
                    visible_current_match = static_cast<int>(visible_search_matches.size()) - 1;
                }
            }
        }
        // combine the below two loops and track the current match index at the same time to avoid iterating twice

        // for (int match : state.searchMatches) {
        //     if (match >= offset && match < visible_end) {
        //         visible_search_matches.push_back(match - offset);
        //     }
        // }

        // if (state.currentMatchIndex >= 0 && state.currentMatchIndex < static_cast<int>(state.searchMatches.size())) {
        //     const int absolute_current_match = state.searchMatches[static_cast<size_t>(state.currentMatchIndex)];
        //     if (absolute_current_match >= offset && absolute_current_match < visible_end) {
        //         for (size_t i = 0; i < visible_search_matches.size(); ++i) {
        //             if (visible_search_matches[i] == (absolute_current_match - offset)) {
        //                 visible_current_match = static_cast<int>(i);
        //                 break;
        //             }
        //         }
        //     }
        // }

        const int visible_selected = state.currentSelected - offset;
        std::vector<int> visible_visual_selected_indices;
        if (state.visualModeActive) {
            visible_visual_selected_indices.reserve(state.visualSelectedIndices.size());
            for (int absolute_index : state.visualSelectedIndices) {
                if (absolute_index >= offset && absolute_index < visible_end) {
                    visible_visual_selected_indices.push_back(absolute_index - offset);
                }
            }
        }

        // render current directory list with search highlighting
        auto current_content = fileList_->render(visible_entries, visible_selected, visible_search_matches,
                                                 visible_current_match, visible_visual_selected_indices);

        // render preview using previewComponent
        Element preview_content;
        if (!state.entries.empty()) {
            const auto& selected_entry = state.entries[static_cast<size_t>(state.currentSelected)];
            preview_content = preview_->render(selected_entry);
        } else {
            preview_content = text("[Empty directory]") | dim | center;
        }

        // Determine titles
        std::string parent_title = state.currentDir.has_parent_path() ? state.currentDir.parent_path().string() : "/";
        if (parent_title.empty()) {
            parent_title = "/";
        }

        std::string current_title = state.currentDir.filename().string();

        if (current_title.empty()) {
            current_title = state.currentDir.string();
        }

        // build panels using PanelComponent
        auto panels = panel_->render(parent_title, std::move(parent_content), current_title, std::move(current_content),
                                     "Preview", std::move(preview_content));

        // build search status text
        std::string search_status;

        if (state.searchHighlightActive && !state.searchPattern.empty()) {
            search_status = std::format("[/{}] {} matches", state.searchPattern, state.searchMatches.size());

            if (state.currentMatchIndex >= 0) {
                search_status += std::format(" ({}/{})", state.currentMatchIndex + 1, state.searchMatches.size());
            }
        }

        const std::string sort_status =
            std::format("[sort:{}{}]", sortFieldToShortName(state.sortField),
                        state.sortDirection == ExplorerState::SortDirection::Descending ? ":desc" : ":asc");
        if (search_status.empty()) {
            search_status = sort_status;
        } else {
            search_status = sort_status + " " + search_status;
        }

        // Build status bar using StatusBarComponent

        ui::StatusBarInfo status_info;

        status_info.currentPath = state.currentDir.string();
        status_info.keyBuffer = keyHandler_.buffer();
        status_info.searchStatus = std::move(search_status);
        // TODO: Dynamic help text based on mode and available actions
        if (keyHandler_.mode() == ui::Mode::Visual) {
            status_info.helpText = "VISUAL: j/k range, y/x copy-cut, d/D trash-delete, v/Esc exit";
        } else {
            status_info.helpText = "j/k move, h/l nav, v visual, ,m|,b|,e|,a|,n|,s sort, Y/X cancel, q quit";
        }

        if (state.visualModeActive) {
            status_info.searchStatus += std::format(" [visual:{}]", explorer_->visualSelectionCount());
        }

        auto status_bar_elem = statusBar_->render(status_info);

        // Compose main content
        Element main_content = vbox({
            std::move(panels) | flex,
            separator(),
            std::move(status_bar_elem),
        });

        // Overlay dialogs if active
        main_content = renderDialogs(std::move(main_content), state);

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

    void registerSortAction(ui::ActionRegistry& actions,
                            std::string name,
                            std::string description,
                            ExplorerState::SortField field,
                            ExplorerState::SortDirection direction) {
        actions.registerAction(
            std::move(name),
            [this, field, direction]([[maybe_unused]] const ui::ActionContext& ctx) {
                explorer_->setSortOrder(field, direction);
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
            },
            std::move(description), "View", false);
    }

    [[nodiscard]] static std::string_view sortFieldToShortName(ExplorerState::SortField field) {
        switch (field) {
            case ExplorerState::SortField::ModifiedTime:
                return "modified";
            case ExplorerState::SortField::BirthTime:
                return "birth";
            case ExplorerState::SortField::Extension:
                return "extension";
            case ExplorerState::SortField::Alphabetical:
                return "alpha";
            case ExplorerState::SortField::Natural:
                return "natural";
            case ExplorerState::SortField::Size:
                return "size";
            default:
                return "unknown";
        }
    }

    ftxui::Element renderDialogs(ftxui::Element base_content, const ExplorerState& state) {
        using namespace ftxui;

        // Show delete confirmation dialog
        if (state.showDeleteDialog) {
            auto dialog_elem = dialog_->renderConfirmation("Delete Confirmation", "Are you sure you want to delete:",
                                                           state.trashDeletePath.filename().string(), Color::Red);

            return dbox({
                std::move(base_content) | dim,
                std::move(dialog_elem) | clear_under | center,
            });
        }

        // Show trash confirmation dialog
        if (state.showTrashDialog) {
            auto dialog_elem = dialog_->renderConfirmation(
                "Trash Confirmation",
                "Are you sure you want to move to trash:", state.trashDeletePath.filename().string(), Color::Red);

            return dbox({
                std::move(base_content) | dim,
                std::move(dialog_elem) | clear_under | center,
            });
        }

        // Show create file/directory dialog
        if (state.showCreateDialog) {
            auto dialog_elem = dialog_->renderInput("Create New File/Directory",
                                                    "Enter name (end with / for directory):\ne.g., "
                                                    "foo/bar/baz/ creates nested dirs",
                                                    createInputComponent_->Render());

            return dbox({
                std::move(base_content) | dim,
                std::move(dialog_elem) | clear_under | center,
            });
        }

        // Show rename dialog
        if (state.showRenameDialog) {
            auto dialog_elem = dialog_->renderInput("Rename Current File/Directory",
                                                    "Enter the new name:", renameInputComponent_->Render());

            return dbox({
                std::move(base_content) | dim,
                std::move(dialog_elem) | clear_under | center,
            });
        }

        // Show search dialog
        if (state.showSearchDialog) {
            auto dialog_elem = dialog_->renderInput("Search (case-sensitive)", "",
                                                    hbox({
                                                        text(" / "),
                                                        searchInputComponent_->Render() | flex,
                                                    }));

            return dbox({
                std::move(base_content) | dim,
                std::move(dialog_elem) | clear_under | center,
            });
        }

        return base_content;
    }

    bool handleEvent(const ftxui::Event& event) {
        using namespace ftxui;
        const auto& state = explorer_->state();

        // if (event == Event::Special({0})) {
        //     return true;
        // }

        // Handle search dialog events
        if (state.showSearchDialog) {
            if (event == Event::Return) {
                explorer_->search(searchInput_);
                explorer_->hideAllDialogs();
                return true;
            }
            if (event == Event::Escape) {
                explorer_->hideAllDialogs();
                searchInput_.clear();
                return true;
            }
            return searchInputComponent_->OnEvent(event);
        }

        // Handle create dialog events
        if (state.showCreateDialog) {
            if (event == Event::Return) {
                const std::string success_message =
                    newNameInput_.empty() ? std::string{} : std::format("Created '{}'", newNameInput_);
                if (handleResult(explorer_->create(newNameInput_), success_message)) {
                    explorer_->hideAllDialogs();
                    newNameInput_.clear();
                }
                return true;
            }
            if (event == Event::Escape) {
                explorer_->hideAllDialogs();
                newNameInput_.clear();
                return true;
            }
            return createInputComponent_->OnEvent(event);
        }

        // Handle rename dialog events
        if (state.showRenameDialog) {
            if (event == Event::Return) {
                const std::string success_message =
                    renameInput_.empty() ? std::string{} : std::format("Renamed to '{}'", renameInput_);
                if (handleResult(explorer_->rename(renameInput_), success_message)) {
                    explorer_->hideAllDialogs();
                    renameInput_.clear();
                }
                return true;
            }
            if (event == Event::Escape) {
                explorer_->hideAllDialogs();
                renameInput_.clear();
                return true;
            }
            return renameInputComponent_->OnEvent(event);
        }

        // Handle delete dialog events
        if (state.showDeleteDialog) {
            if (event == Event::Character('y') || event == Event::Character('Y')) {
                const int count = selectedCount();
                (void)handleResult(explorer_->deleteSelected(),
                                   count > 0 ? std::format("Deleted {}", nounWithCount(count, "item")) : "");
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
                explorer_->hideAllDialogs();
                return true;
            }
            if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
                explorer_->hideAllDialogs();
                return true;
            }
            return true;  // Block other events
        }

        // Handle trash dialog events
        if (state.showTrashDialog) {
            if (event == Event::Character('y') || event == Event::Character('Y')) {
                const int count = selectedCount();
                (void)handleResult(explorer_->trashSelected(),
                                   count > 0 ? std::format("Moved {} to trash", nounWithCount(count, "item")) : "");
                if (keyHandler_.mode() == ui::Mode::Visual) {
                    keyHandler_.setMode(ui::Mode::Normal);
                }
                explorer_->hideAllDialogs();
                return true;
            }
            if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
                explorer_->hideAllDialogs();
                return true;
            }
            return true;  // Block other events
        }

        // Handle Escape separately (quit)
        if (event == Event::Escape) {
            if (keyHandler_.mode() == ui::Mode::Visual) {
                explorer_->exitVisualMode();
                keyHandler_.setMode(ui::Mode::Normal);
                return true;
            }
            screen_.Exit();
            return true;
        }

        // Handle arrow keys (map to vim keys)
        if (event == Event::ArrowDown) {
            explorer_->moveDown();
            return true;
        }
        if (event == Event::ArrowUp) {
            explorer_->moveUp();
            return true;
        }
        if (event == Event::ArrowLeft) {
            (void)handleResult(explorer_->goParent());
            if (keyHandler_.mode() == ui::Mode::Visual) {
                keyHandler_.setMode(ui::Mode::Normal);
            }
            return true;
        }
        if (event == Event::ArrowRight || event == Event::Return) {
            (void)handleResult(explorer_->enterSelected(false));
            if (keyHandler_.mode() == ui::Mode::Visual) {
                keyHandler_.setMode(ui::Mode::Normal);
            }
            return true;
        }

        // Use KeyHandler for all character-based bindings
        return keyHandler_.handle(event);
    }

    std::shared_ptr<Explorer> explorer_;
    ftxui::ScreenInteractive screen_;
    const ui::Theme* theme_;
    ui::KeyHandler keyHandler_;
    NotificationCenter notifications_;

    // UI Components
    std::unique_ptr<ui::FileListComponent> fileList_;
    std::unique_ptr<ui::PanelComponent> panel_;
    std::unique_ptr<ui::PreviewComponent> preview_;
    std::unique_ptr<ui::StatusBarComponent> statusBar_;
    std::unique_ptr<ui::ToastComponent> toast_;
    std::unique_ptr<ui::DialogComponent> dialog_;

    // Input components and their buffers
    std::string newNameInput_;
    std::string renameInput_;
    std::string searchInput_;
    ftxui::Component createInputComponent_;
    ftxui::Component renameInputComponent_;
    ftxui::Component searchInputComponent_;

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
