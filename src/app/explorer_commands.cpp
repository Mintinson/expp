#include "expp/app/explorer_commands.hpp"

#include <array>
#include <vector>

namespace expp::app {

namespace {

constexpr std::array kBaseCommands = {
    CommandSpec{.command = ExplorerCommand::MoveDown,
                .name = "move_down",
                .description = "Move cursor down",
                .category = "Navigation",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::MoveUp,
                .name = "move_up",
                .description = "Move cursor up",
                .category = "Navigation",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::GoParent,
                .name = "go_parent",
                .description = "Go to parent directory",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::EnterSelected,
                .name = "enter_selected",
                .description = "Enter directory or open file",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::GoTop,
                .name = "go_top",
                .description = "Go to first item",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::GoBottom,
                .name = "go_bottom",
                .description = "Go to last item or line N",
                .category = "Navigation",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::PageDown,
                .name = "page_down",
                .description = "Scroll down half page",
                .category = "Navigation",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::PageUp,
                .name = "page_up",
                .description = "Scroll up half page",
                .category = "Navigation",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::GoHomeDirectory,
                .name = "go_home_directory",
                .description = "Navigate to home directory",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::GoConfigDirectory,
                .name = "go_config_directory",
                .description = "Navigate to expp config directory",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::GoLinkTargetDirectory,
                .name = "go_link_target_directory",
                .description = "Navigate to selected link target directory",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::PromptDirectoryJump,
                .name = "prompt_directory_jump",
                .description = "Open directory jump prompt",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::EnterVisualMode,
                .name = "enter_visual_mode",
                .description = "Enter visual mode",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::ExitVisualMode,
                .name = "exit_visual_mode",
                .description = "Exit visual mode",
                .category = "Navigation",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::OpenFile,
                .name = "open_file",
                .description = "Open file with default application",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Create,
                .name = "create",
                .description = "Create new file or directory",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Rename,
                .name = "rename",
                .description = "Rename current item",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Yank,
                .name = "yank",
                .description = "Yank current item",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::Cut,
                .name = "cut",
                .description = "Cut current item",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::DiscardYank,
                .name = "discard_yank",
                .description = "Clear clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Paste,
                .name = "paste",
                .description = "Paste yanked item into current directory",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::PasteOverwrite,
                .name = "paste_overwrite",
                .description = "Paste into current directory with overwrite",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyEntryPathRelative,
                .name = "copy_entry_path_relative",
                .description = "Copy selected entry relative path to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyCurrentDirRelative,
                .name = "copy_current_dir_relative",
                .description = "Copy current directory relative path to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyEntryPathAbsolute,
                .name = "copy_entry_path_absolute",
                .description = "Copy selected entry absolute path to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyCurrentDirAbsolute,
                .name = "copy_current_dir_absolute",
                .description = "Copy current directory absolute path to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyFileName,
                .name = "copy_file_name",
                .description = "Copy selected file name to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::CopyNameWithoutExtension,
                .name = "copy_name_without_extension",
                .description = "Copy selected name without extension to system clipboard",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Trash,
                .name = "trash",
                .description = "Move to trash",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::Delete,
                .name = "delete",
                .description = "Delete permanently",
                .category = "File Operations",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Exit    },
    CommandSpec{.command = ExplorerCommand::Search,
                .name = "search",
                .description = "Open search dialog",
                .category = "Search",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::NextMatch,
                .name = "next_match",
                .description = "Jump to next search match",
                .category = "Search",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::PrevMatch,
                .name = "prev_match",
                .description = "Jump to previous search match",
                .category = "Search",
                .repeatable = true,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::ClearSearch,
                .name = "clear_search",
                .description = "Clear search highlighting",
                .category = "Search",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::ToggleHidden,
                .name = "toggle_hidden",
                .description = "Toggle the visibility of hidden files",
                .category = "View",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::OpenHelp,
                .name = "open_help",
                .description = "Open help menu",
                .category = "Help",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
    CommandSpec{.command = ExplorerCommand::Quit,
                .name = "quit",
                .description = "Quit application",
                .category = "Application",
                .repeatable = false,
                .visualModePolicy = VisualModePolicy::Preserve},
};

constexpr std::array kSortSpecs = {
    SortSpec{.ascendingCommand = ExplorerCommand::SortModified,
             .descendingCommand = ExplorerCommand::SortModifiedDesc,
             .field = SortOrder::Field::ModifiedTime,
             .shortName = "modified",
             .category = "View",
             .ascendingName = "sort_modified",
             .descendingName = "sort_modified_desc",
             .ascendingDescription = "Sort by modified time",
             .descendingDescription = "Sort by modified time (desc)",
             .ascendingBinding = ",m",
             .descendingBinding = ",M"},
    SortSpec{.ascendingCommand = ExplorerCommand::SortBirth,
             .descendingCommand = ExplorerCommand::SortBirthDesc,
             .field = SortOrder::Field::BirthTime,
             .shortName = "birth",
             .category = "View",
             .ascendingName = "sort_birth",
             .descendingName = "sort_birth_desc",
             .ascendingDescription = "Sort by birth time",
             .descendingDescription = "Sort by birth time (desc)",
             .ascendingBinding = ",b",
             .descendingBinding = ",B"},
    SortSpec{.ascendingCommand = ExplorerCommand::SortExtension,
             .descendingCommand = ExplorerCommand::SortExtensionDesc,
             .field = SortOrder::Field::Extension,
             .shortName = "extension",
             .category = "View",
             .ascendingName = "sort_extension",
             .descendingName = "sort_extension_desc",
             .ascendingDescription = "Sort by extension",
             .descendingDescription = "Sort by extension (desc)",
             .ascendingBinding = ",e",
             .descendingBinding = ",E"},
    SortSpec{.ascendingCommand = ExplorerCommand::SortAlpha,
             .descendingCommand = ExplorerCommand::SortAlphaDesc,
             .field = SortOrder::Field::Alphabetical,
             .shortName = "alpha",
             .category = "View",
             .ascendingName = "sort_alpha",
             .descendingName = "sort_alpha_desc",
             .ascendingDescription = "Sort alphabetically",
             .descendingDescription = "Sort alphabetically (desc)",
             .ascendingBinding = ",a",
             .descendingBinding = ",A"},
    SortSpec{.ascendingCommand = ExplorerCommand::SortNatural,
             .descendingCommand = ExplorerCommand::SortNaturalDesc,
             .field = SortOrder::Field::Natural,
             .shortName = "natural",
             .category = "View",
             .ascendingName = "sort_natural",
             .descendingName = "sort_natural_desc",
             .ascendingDescription = "Sort naturally",
             .descendingDescription = "Sort naturally (desc)",
             .ascendingBinding = ",n",
             .descendingBinding = ",N"},
    SortSpec{.ascendingCommand = ExplorerCommand::SortSize,
             .descendingCommand = ExplorerCommand::SortSizeDesc,
             .field = SortOrder::Field::Size,
             .shortName = "size",
             .category = "View",
             .ascendingName = "sort_size",
             .descendingName = "sort_size_desc",
             .ascendingDescription = "Sort by size",
             .descendingDescription = "Sort by size (desc)",
             .ascendingBinding = ",s",
             .descendingBinding = ",S"},
};

constexpr std::array kBaseBindings = {
    BindingSpec{
                .keys = "j",          .command = ExplorerCommand::MoveDown,      .mode = ui::Mode::Normal, .description = "Move down"         },
    BindingSpec{.keys = "k",          .command = ExplorerCommand::MoveUp,        .mode = ui::Mode::Normal, .description = "Move up"           },
    BindingSpec{
                .keys = "h",          .command = ExplorerCommand::GoParent,      .mode = ui::Mode::Normal, .description = "Go to parent"      },
    BindingSpec{
                .keys = "l",          .command = ExplorerCommand::EnterSelected, .mode = ui::Mode::Normal, .description = "Enter/open"        },
    BindingSpec{
                .keys = "<Down>",     .command = ExplorerCommand::MoveDown,      .mode = ui::Mode::Normal, .description = "Move down"         },
    BindingSpec{.keys = "<Up>",       .command = ExplorerCommand::MoveUp,        .mode = ui::Mode::Normal, .description = "Move up"           },
    BindingSpec{.keys = "<Left>",
                .command = ExplorerCommand::GoParent,
                .mode = ui::Mode::Normal,
                .description = "Go to parent"                                                                                                 },
    BindingSpec{.keys = "<Right>",
                .command = ExplorerCommand::EnterSelected,
                .mode = ui::Mode::Normal,
                .description = "Enter/open"                                                                                                   },
    BindingSpec{.keys = "gg",         .command = ExplorerCommand::GoTop,         .mode = ui::Mode::Normal, .description = "Go to top"         },
    BindingSpec{.keys = "gh",
                .command = ExplorerCommand::GoHomeDirectory,
                .mode = ui::Mode::Normal,
                .description = "Go to home directory"                                                                                         },
    BindingSpec{.keys = "gc",
                .command = ExplorerCommand::GoConfigDirectory,
                .mode = ui::Mode::Normal,
                .description = "Go to config directory"                                                                                       },
    BindingSpec{.keys = "gl",
                .command = ExplorerCommand::GoLinkTargetDirectory,
                .mode = ui::Mode::Normal,
                .description = "Go to link target directory"                                                                                  },
    BindingSpec{.keys = "g:",
                .command = ExplorerCommand::PromptDirectoryJump,
                .mode = ui::Mode::Normal,
                .description = "Jump to directory"                                                                                            },
    BindingSpec{
                .keys = "G",          .command = ExplorerCommand::GoBottom,      .mode = ui::Mode::Normal, .description = "Go to bottom"      },
    BindingSpec{.keys = "v",
                .command = ExplorerCommand::EnterVisualMode,
                .mode = ui::Mode::Normal,
                .description = "Enter visual mode"                                                                                            },
    BindingSpec{
                .keys = "C-d",        .command = ExplorerCommand::PageDown,      .mode = ui::Mode::Normal, .description = "Page down"         },
    BindingSpec{.keys = "C-u",        .command = ExplorerCommand::PageUp,        .mode = ui::Mode::Normal, .description = "Page up"           },
    BindingSpec{.keys = "<PageDown>",
                .command = ExplorerCommand::PageDown,
                .mode = ui::Mode::Normal,
                .description = "Page down"                                                                                                    },
    BindingSpec{
                .keys = "<PageUp>",   .command = ExplorerCommand::PageUp,        .mode = ui::Mode::Normal, .description = "Page up"           },
    BindingSpec{
                .keys = "<Home>",     .command = ExplorerCommand::GoTop,         .mode = ui::Mode::Normal, .description = "Go to top"         },
    BindingSpec{
                .keys = "<End>",      .command = ExplorerCommand::GoBottom,      .mode = ui::Mode::Normal, .description = "Go to bottom"      },
    BindingSpec{
                .keys = "o",          .command = ExplorerCommand::OpenFile,      .mode = ui::Mode::Normal, .description = "Open file"         },
    BindingSpec{.keys = "<Enter>",
                .command = ExplorerCommand::EnterSelected,
                .mode = ui::Mode::Normal,
                .description = "Enter/open"                                                                                                   },
    BindingSpec{.keys = "a",          .command = ExplorerCommand::Create,        .mode = ui::Mode::Normal, .description = "Create new"        },
    BindingSpec{.keys = "r",          .command = ExplorerCommand::Rename,        .mode = ui::Mode::Normal, .description = "Rename"            },
    BindingSpec{.keys = "d",          .command = ExplorerCommand::Trash,         .mode = ui::Mode::Normal, .description = "Trash"             },
    BindingSpec{.keys = "D",          .command = ExplorerCommand::Delete,        .mode = ui::Mode::Normal, .description = "Delete"            },
    BindingSpec{.keys = "y",          .command = ExplorerCommand::Yank,          .mode = ui::Mode::Normal, .description = "Yank (copy)"       },
    BindingSpec{.keys = "x",          .command = ExplorerCommand::Cut,           .mode = ui::Mode::Normal, .description = "Cut"               },
    BindingSpec{
                .keys = "Y",          .command = ExplorerCommand::DiscardYank,   .mode = ui::Mode::Normal, .description = "Discard yank"      },
    BindingSpec{.keys = "X",
                .command = ExplorerCommand::DiscardYank,
                .mode = ui::Mode::Normal,
                .description = "Discard cut/copy"                                                                                             },
    BindingSpec{
                .keys = "p",          .command = ExplorerCommand::Paste,         .mode = ui::Mode::Normal, .description = "Paste yanked item" },
    BindingSpec{.keys = "P",
                .command = ExplorerCommand::PasteOverwrite,
                .mode = ui::Mode::Normal,
                .description = "Paste with overwrite"                                                                                         },
    BindingSpec{.keys = "cc",
                .command = ExplorerCommand::CopyEntryPathRelative,
                .mode = ui::Mode::Normal,
                .description = "Copy entry path (relative)"                                                                                   },
    BindingSpec{.keys = "cd",
                .command = ExplorerCommand::CopyCurrentDirRelative,
                .mode = ui::Mode::Normal,
                .description = "Copy current path (relative)"                                                                                 },
    BindingSpec{.keys = "cC",
                .command = ExplorerCommand::CopyEntryPathAbsolute,
                .mode = ui::Mode::Normal,
                .description = "Copy entry path (absolute)"                                                                                   },
    BindingSpec{.keys = "cD",
                .command = ExplorerCommand::CopyCurrentDirAbsolute,
                .mode = ui::Mode::Normal,
                .description = "Copy current path (absolute)"                                                                                 },
    BindingSpec{.keys = "cf",
                .command = ExplorerCommand::CopyFileName,
                .mode = ui::Mode::Normal,
                .description = "Copy file name"                                                                                               },
    BindingSpec{.keys = "cn",
                .command = ExplorerCommand::CopyNameWithoutExtension,
                .mode = ui::Mode::Normal,
                .description = "Copy name without extension"                                                                                  },
    BindingSpec{.keys = "/",          .command = ExplorerCommand::Search,        .mode = ui::Mode::Normal, .description = "Search"            },
    BindingSpec{
                .keys = "n",          .command = ExplorerCommand::NextMatch,     .mode = ui::Mode::Normal, .description = "Next match"        },
    BindingSpec{
                .keys = "N",          .command = ExplorerCommand::PrevMatch,     .mode = ui::Mode::Normal, .description = "Prev match"        },
    BindingSpec{
                .keys = "\\",         .command = ExplorerCommand::ClearSearch,   .mode = ui::Mode::Normal, .description = "Clear search"      },
    BindingSpec{.keys = ".",
                .command = ExplorerCommand::ToggleHidden,
                .mode = ui::Mode::Normal,
                .description = "Toggle hidden files"                                                                                          },
    BindingSpec{.keys = "j",
                .command = ExplorerCommand::MoveDown,
                .mode = ui::Mode::Visual,
                .description = "Move down (visual)"                                                                                           },
    BindingSpec{
                .keys = "k",          .command = ExplorerCommand::MoveUp,        .mode = ui::Mode::Visual, .description = "Move up (visual)"  },
    BindingSpec{
                .keys = "gg",         .command = ExplorerCommand::GoTop,         .mode = ui::Mode::Visual, .description = "Go to top (visual)"},
    BindingSpec{.keys = "G",
                .command = ExplorerCommand::GoBottom,
                .mode = ui::Mode::Visual,
                .description = "Go to bottom (visual)"                                                                                        },
    BindingSpec{.keys = "C-d",
                .command = ExplorerCommand::PageDown,
                .mode = ui::Mode::Visual,
                .description = "Page down (visual)"                                                                                           },
    BindingSpec{
                .keys = "C-u",        .command = ExplorerCommand::PageUp,        .mode = ui::Mode::Visual, .description = "Page up (visual)"  },
    BindingSpec{
                .keys = "y",          .command = ExplorerCommand::Yank,          .mode = ui::Mode::Visual, .description = "Yank selection"    },
    BindingSpec{.keys = "x",          .command = ExplorerCommand::Cut,           .mode = ui::Mode::Visual, .description = "Cut selection"     },
    BindingSpec{
                .keys = "d",          .command = ExplorerCommand::Trash,         .mode = ui::Mode::Visual, .description = "Trash selection"   },
    BindingSpec{
                .keys = "D",          .command = ExplorerCommand::Delete,        .mode = ui::Mode::Visual, .description = "Delete selection"  },
    BindingSpec{.keys = "v",
                .command = ExplorerCommand::ExitVisualMode,
                .mode = ui::Mode::Visual,
                .description = "Exit visual mode"                                                                                             },
    BindingSpec{.keys = "<Esc>",
                .command = ExplorerCommand::ExitVisualMode,
                .mode = ui::Mode::Visual,
                .description = "Exit visual mode"                                                                                             },
    BindingSpec{
                .keys = "~",          .command = ExplorerCommand::OpenHelp,      .mode = ui::Mode::Normal, .description = "Open help"         },
    BindingSpec{.keys = "<Esc>",      .command = ExplorerCommand::Quit,          .mode = ui::Mode::Normal, .description = "Quit"              },
    BindingSpec{.keys = "q",          .command = ExplorerCommand::Quit,          .mode = ui::Mode::Normal, .description = "Quit"              },
};

}  // namespace

std::optional<ExplorerCommand> command_from_name(std::string_view name) noexcept {
    if (const auto it = std::ranges::find(kBaseCommands, name, &CommandSpec::name); it != kBaseCommands.end()) {
        return it->command;
    }

    for (const auto& spec : kSortSpecs) {
        if (spec.ascendingName == name) {
            return spec.ascendingCommand;
        }
        if (spec.descendingName == name) {
            return spec.descendingCommand;
        }
    }

    return std::nullopt;
}

std::optional<ExplorerCommand> command_from_id(ui::CommandId id) noexcept {
    const auto command = static_cast<ExplorerCommand>(id);
    const auto* spec = find_command_spec(command_specs(), command);
    return spec != nullptr ? std::optional<ExplorerCommand>{command} : std::nullopt;
}

std::string_view command_name(ExplorerCommand command) noexcept {
    return command_spec(command).name;
}

const CommandSpec& command_spec(ExplorerCommand command) noexcept {
    if (const auto* spec = find_command_spec(command_specs(), command); spec != nullptr) {
        return *spec;
    }
    static constexpr CommandSpec kUnknown{.command = ExplorerCommand::Quit,
                                          .name = "unknown",
                                          .description = "unknown",
                                          .category = "unknown",
                                          .repeatable = false};
    return kUnknown;
}

bool exits_visual_mode(ExplorerCommand command) noexcept {
    return command_spec(command).visualModePolicy == VisualModePolicy::Exit;
}

std::span<const CommandSpec> command_specs() noexcept {
    static std::vector<CommandSpec> specs;
    if (specs.empty()) {
        specs.assign(kBaseCommands.begin(), kBaseCommands.end());
        for (const auto& spec : kSortSpecs) {
            specs.push_back(CommandSpec{.command = spec.ascendingCommand,
                                        .name = spec.ascendingName,
                                        .description = spec.ascendingDescription,
                                        .category = spec.category,
                                        .repeatable = false,
                                        .visualModePolicy = VisualModePolicy::Exit});
            specs.push_back(CommandSpec{.command = spec.descendingCommand,
                                        .name = spec.descendingName,
                                        .description = spec.descendingDescription,
                                        .category = spec.category,
                                        .repeatable = false,
                                        .visualModePolicy = VisualModePolicy::Exit});
        }
    }
    return specs;
}

std::span<const BindingSpec> default_bindings() noexcept {
    static std::vector<BindingSpec> bindings;
    if (bindings.empty()) {
        bindings.assign(kBaseBindings.begin(), kBaseBindings.end());
        for (const auto& spec : kSortSpecs) {
            bindings.push_back(BindingSpec{.keys = spec.ascendingBinding,
                                           .command = spec.ascendingCommand,
                                           .mode = ui::Mode::Normal,
                                           .description = spec.ascendingDescription});
            bindings.push_back(BindingSpec{.keys = spec.descendingBinding,
                                           .command = spec.descendingCommand,
                                           .mode = ui::Mode::Normal,
                                           .description = spec.descendingDescription});
        }
    }
    return bindings;
}

std::span<const SortSpec> sort_specs() noexcept {
    return kSortSpecs;
}

std::string_view sort_field_short_name(SortOrder::Field field) noexcept {
    for (const auto& spec : kSortSpecs) {
        if (spec.field == field) {
            return spec.shortName;
        }
    }
    return "unknown";
}

}  // namespace expp::app
