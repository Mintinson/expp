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

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace expp::app {

class ExplorerView::Impl {
public:
    explicit Impl(std::shared_ptr<Explorer> explorer)
        : explorer_{std::move(explorer)}
        , screen_{ftxui::ScreenInteractive::Fullscreen()}
        , theme_{&ui::globalTheme()}
        , keyHandler_{1000}  // 1000 ms timeout for key sequences
    {
        setupComponents();
        setupActions();
        setupKeyBindings();
        setupInputComponents();
    }

    int run() {
        using namespace ftxui;

        auto component = Renderer([this] { return render(); });

        component = CatchEvent(component, [this](Event event) { return handleEvent(event); });

        screen_.Loop(component);
        return 0;
    }

    void requestExit() { screen_.Exit(); }

private:
    /**
     * @brief Setup reusable UI components
     */
    void setupComponents() {
        ui::FileListConfig file_list_config{};

        file_list_config.theme = theme_;

        fileList_ = std::make_unique<ui::FileListComponent>(file_list_config);

        ui::PreviewConfig preview_config{};
        preview_config.theme = theme_;
        preview_ = std::make_unique<ui::PreviewComponent>(preview_config);

        statusBar_ = std::make_unique<ui::StatusBarComponent>(theme_);

        dialog_ = std::make_unique<ui::DialogComponent>();
        dialog_->setTheme(theme_);

        ui::PanelConfig panel_config{};
        panel_config.theme = theme_;
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
            "go_parent", [this]([[maybe_unused]] const ui::ActionContext& ctx) { (void)explorer_->goParent(); },
            "Move cursor up", "Navigation", false);
        actions.registerAction(
            "enter_selected",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) { (void)explorer_->enterSelected(true); },
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
            "page_down", [this](const ui::ActionContext& ctx) { explorer_->moveDown(10 * ctx.count); },
            "Scroll down half page", "Navigation", true);
        actions.registerAction(
            "page_up", [this](const ui::ActionContext& ctx) { explorer_->moveUp(10 * ctx.count); },
            "Scroll up half page", "Navigation", true);

        // File operations
        actions.registerAction(
            "open_file", [this]([[maybe_unused]] const ui::ActionContext& ctx) { (void)explorer_->openSelected(); },
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
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->yankSelected();
            },
            "Yank current item", "File Operations", false
        );

        actions.registerAction(
            "cut",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->cutSelected();
            },
            "Cut current item", "File Operations", false
        );
        
        actions.registerAction(
            "discard_yank",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->discardYank();
            },
            "Clear clipboard", "File Operations", false
        );

        actions.registerAction(
            "paste", [this]([[maybe_unused]] const ui::ActionContext& ctx) { (void)explorer_->pasteYanked(false); },
            "Paste yanked item into current directory", "File Operations", false);

        actions.registerAction(
            "paste_overwrite",
            [this]([[maybe_unused]] const ui::ActionContext& ctx) { (void)explorer_->pasteYanked(true); },
            "Paste into current directory with overwrite", "File Operations", false);

        actions.registerAction(
            "copy_entry_path_relative",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copySelectedPathToSystemClipboard(false);
            },
            "Copy selected entry relative path to system clipboard", "File Operations", false
        );

        actions.registerAction(
            "copy_current_dir_relative",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copyCurrentDirectoryPathToSystemClipboard(false);
            },
            "Copy current directory relative path to system clipboard", "File Operations", false
        );

        actions.registerAction(
            "copy_entry_path_absolute",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copySelectedPathToSystemClipboard(true);
            },
            "Copy selected entry absolute path to system clipboard", "File Operations", false
        );

        actions.registerAction(
            "copy_current_dir_absolute",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copyCurrentDirectoryPathToSystemClipboard(true);
            },
            "Copy current directory absolute path to system clipboard", "File Operations", false
        );

        actions.registerAction(
            "copy_file_name",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copySelectedFileNameToSystemClipboard();
            },
            "Copy selected file name to system clipboard", "File Operations", false
        );

        actions.registerAction(
            "copy_name_without_extension",
            [this]([[maybe_unused]] const ui::ActionContext& ctx)
            {
                (void)explorer_->copySelectedNameWithoutExtensionToSystemClipboard();
            },
            "Copy selected name without extension to system clipboard", "File Operations", false
        );

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
            "toggle_hidden", [this]([[maybe_unused]] const ui::ActionContext& ctx) { explorer_->toggleShowHidden(); },
            "Toggle the visibility of hidden files", "View", false);

        // Application control
        actions.registerAction(
            "quit", [this]([[maybe_unused]] const ui::ActionContext& ctx) { screen_.Exit(); }, "Quit application",
            "Application", false);
    }

    void setupKeyBindings() {
        // Use the Keymap to bind keys to actions
        auto& keymap = keyHandler_.keymap();

        // TODO: Better Error handling for invalid bindings (e.g. duplicate keys, unknown actions)
        // Navigation bindings
        (void)keymap.bind("j", "move_down", ui::Mode::Normal, "Move down");
        (void)keymap.bind("k", "move_up", ui::Mode::Normal, "Move up");
        (void)keymap.bind("h", "go_parent", ui::Mode::Normal, "Go to parent");
        (void)keymap.bind("l", "enter_selected", ui::Mode::Normal, "Enter/open");
        (void)keymap.bind("gg", "go_top", ui::Mode::Normal, "Go to top");
        (void)keymap.bind("G", "go_bottom", ui::Mode::Normal, "Go to bottom");

        // Page navigation
        (void)keymap.bind("C-d", "page_down", ui::Mode::Normal, "Page down");
        (void)keymap.bind("C-u", "page_up", ui::Mode::Normal, "Page up");

        // File operations
        (void)keymap.bind("o", "open_file", ui::Mode::Normal, "Open file");
        (void)keymap.bind("a", "create", ui::Mode::Normal, "Create new");
        (void)keymap.bind("r", "rename", ui::Mode::Normal, "Rename");
        (void)keymap.bind("d", "trash", ui::Mode::Normal, "Trash");
        (void)keymap.bind("D", "delete", ui::Mode::Normal, "Delete");
        (void)keymap.bind("y", "yank", ui::Mode::Normal, "Yank (copy)");
        (void)keymap.bind("x", "cut", ui::Mode::Normal, "Cut");
        (void)keymap.bind("Y", "discard_yank", ui::Mode::Normal, "Discard Yank (copy)");
        (void)keymap.bind("X", "discard_yank", ui::Mode::Normal, "Discard cut/copy");
        (void)keymap.bind("p", "paste", ui::Mode::Normal, "Paste yanked item");
        (void)keymap.bind("P", "paste_overwrite", ui::Mode::Normal, "Paste with overwrite");
        (void)keymap.bind("cc", "copy_entry_path_relative", ui::Mode::Normal, "Copy entry path (relative)");
        (void)keymap.bind("cd", "copy_current_dir_relative", ui::Mode::Normal, "Copy current path (relative)");
        (void)keymap.bind("cC", "copy_entry_path_absolute", ui::Mode::Normal, "Copy entry path (absolute)");
        (void)keymap.bind("cD", "copy_current_dir_absolute", ui::Mode::Normal, "Copy current path (absolute)");
        (void)keymap.bind("cf", "copy_file_name", ui::Mode::Normal, "Copy file name");
        (void)keymap.bind("cn", "copy_name_without_extension", ui::Mode::Normal, "Copy name without extension");

        // Search
        (void)keymap.bind("/", "search", ui::Mode::Normal, "Search");
        (void)keymap.bind("n", "next_match", ui::Mode::Normal, "Next match");
        (void)keymap.bind("N", "prev_match", ui::Mode::Normal, "Prev match");
        (void)keymap.bind("\\", "clear_search", ui::Mode::Normal, "Clear search");

        // View
        (void)keymap.bind(".", "toggle_hidden", ui::Mode::Normal, "Toggle the visibility of hidden files");

        // Quit
        (void)keymap.bind("q", "quit", ui::Mode::Normal, "Quit");
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

        const auto& state = explorer_->state();

        // Render parent directory list using FileListComponent
        auto parent_content = fileList_->render(state.parentEntries, state.parentSelected, {}, -1);

        // render current directory list with search highlighting
        auto current_content =
            fileList_->render(state.entries, state.currentSelected, state.searchMatches, state.currentMatchIndex);

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

        // Build status bar using StatusBarComponent

        ui::StatusBarInfo status_info;

        status_info.currentPath = state.currentDir.string();
        status_info.keyBuffer = keyHandler_.buffer();
        status_info.searchStatus = std::move(search_status);
        // TODO: Dynamic help text based on mode and available actions
        status_info.helpText = "j/k move, h/l nav, y/x copy-cut, p/P paste, cc/cd/cC/cD/cf/cn clip, q quit";

        auto status_bar_elem = statusBar_->render(status_info);

        // Compose main content
        Element main_content = vbox({
            std::move(panels) | flex,
            separator(),
            std::move(status_bar_elem),
        });

        // Overlay dialogs if active
        main_content = renderDialogs(std::move(main_content), state);

        return main_content;
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
                "Trash Confirmation", "Are you sure you want to move to trash:", state.trashDeletePath.filename().string(),
                Color::Red);

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

    bool handleEvent(ftxui::Event event) {
        using namespace ftxui;
        const auto& state = explorer_->state();

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
                (void)explorer_->create(newNameInput_);
                explorer_->hideAllDialogs();
                newNameInput_.clear();
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
                (void)explorer_->rename(renameInput_);
                explorer_->hideAllDialogs();
                renameInput_.clear();
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
                (void)explorer_->deleteSelected();
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
                (void)explorer_->trashSelected();
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
            (void)explorer_->goParent();
            return true;
        }
        if (event == Event::ArrowRight || event == Event::Return) {
            (void)explorer_->enterSelected(false);
            return true;
        }

        // Use KeyHandler for all character-based bindings
        return keyHandler_.handle(event);
    }

    std::shared_ptr<Explorer> explorer_;
    ftxui::ScreenInteractive screen_;
    const ui::Theme* theme_;
    ui::KeyHandler keyHandler_;

    // UI Components
    std::unique_ptr<ui::FileListComponent> fileList_;
    std::unique_ptr<ui::PanelComponent> panel_;
    std::unique_ptr<ui::PreviewComponent> preview_;
    std::unique_ptr<ui::StatusBarComponent> statusBar_;
    std::unique_ptr<ui::DialogComponent> dialog_;

    // Input components and their buffers
    std::string newNameInput_;
    std::string renameInput_;
    std::string searchInput_;
    ftxui::Component createInputComponent_;
    ftxui::Component renameInputComponent_;
    ftxui::Component searchInputComponent_;
};

ExplorerView::ExplorerView(std::shared_ptr<Explorer> explorer) : impl_(std::make_unique<Impl>(explorer)) {}
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