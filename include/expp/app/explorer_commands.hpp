#ifndef EXPP_APP_EXPLORER_COMMANDS_HPP
#define EXPP_APP_EXPLORER_COMMANDS_HPP

/**
 * @file explorer_commands.hpp
 * @brief Compile-time command and binding catalogs for explorer actions.
 *
 * The explorer uses these catalogs as the single source of truth for command
 * names, help text, repeatability, visual-mode policy, and default bindings.
 */

#include "expp/app/explorer.hpp"
#include "expp/ui/key_handler.hpp"

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace expp::app {

/**
 * @brief Strongly typed command identifiers for explorer actions.
 */
enum class ExplorerCommand : ui::CommandId {
    MoveDown,
    MoveUp,
    GoParent,
    EnterSelected,
    GoTop,
    GoBottom,
    PageDown,
    PageUp,
    GoHomeDirectory,
    GoConfigDirectory,
    GoLinkTargetDirectory,
    PromptDirectoryJump,
    EnterVisualMode,
    ExitVisualMode,
    OpenFile,
    Create,
    Rename,
    Yank,
    Cut,
    DiscardYank,
    Paste,
    PasteOverwrite,
    CopyEntryPathRelative,
    CopyCurrentDirRelative,
    CopyEntryPathAbsolute,
    CopyCurrentDirAbsolute,
    CopyFileName,
    CopyNameWithoutExtension,
    Trash,
    Delete,
    Search,
    NextMatch,
    PrevMatch,
    ClearSearch,
    ToggleHidden,
    SortModified,
    SortModifiedDesc,
    SortBirth,
    SortBirthDesc,
    SortExtension,
    SortExtensionDesc,
    SortAlpha,
    SortAlphaDesc,
    SortNatural,
    SortNaturalDesc,
    SortSize,
    SortSizeDesc,
    OpenHelp,
    Quit,
};

/**
 * @brief Visual-mode behavior after a command executes.
 */
enum class VisualModePolicy : std::uint8_t {
    Preserve,
    Exit,
};

/**
 * @brief Declarative metadata for a command.
 */
struct CommandSpec {
    ExplorerCommand command;
    std::string_view name;
    std::string_view description;
    std::string_view category;
    bool repeatable{true};
    VisualModePolicy visualModePolicy{VisualModePolicy::Preserve};
};

/**
 * @brief Declarative default key binding for a command.
 */
struct BindingSpec {
    std::string_view keys;
    ExplorerCommand command;
    ui::Mode mode{ui::Mode::Normal};
    std::string_view description;
};

/**
 * @brief Declarative description of a sort command pair.
 *
 * Sort commands are generated from these specs instead of being registered and
 * documented in multiple imperative locations.
 */
struct SortSpec {
    ExplorerCommand ascendingCommand;
    ExplorerCommand descendingCommand;
    SortOrder::Field field;
    std::string_view shortName;
    std::string_view category;
    std::string_view ascendingName;
    std::string_view descendingName;
    std::string_view ascendingDescription;
    std::string_view descendingDescription;
    std::string_view ascendingBinding;
    std::string_view descendingBinding;
};

/**
 * @brief Concept for catalog entries that identify a command.
 */
template <typename Spec>
concept CommandSpecCnc = requires(const Spec& spec) {
    { spec.command } -> std::convertible_to<ExplorerCommand>;
};

/**
 * @brief Finds a command-like spec in a catalog.
 */
template <CommandSpecCnc Spec>
[[nodiscard]] constexpr const Spec* find_command_spec(std::span<const Spec> specs, ExplorerCommand command) noexcept {
    for (const auto& spec : specs) {
        if (spec.command == command) {
            return &spec;
        }
    }
    return nullptr;
}

/**
 * @brief Converts a strongly typed explorer command to the generic key-handler id.
 */
[[nodiscard]] constexpr ui::CommandId to_command_id(ExplorerCommand command) noexcept {
    return static_cast<ui::CommandId>(command);
}

/**
 * @brief Resolves a config-facing command name to a typed command.
 */
[[nodiscard]] std::optional<ExplorerCommand> command_from_name(std::string_view name) noexcept;
/**
 * @brief Resolves a generic key-handler id back to a typed command.
 */
[[nodiscard]] std::optional<ExplorerCommand> command_from_id(ui::CommandId id) noexcept;
/**
 * @brief Returns the config/help name of a command.
 */
[[nodiscard]] std::string_view command_name(ExplorerCommand command) noexcept;
/**
 * @brief Returns the full metadata entry for a command.
 */
[[nodiscard]] const CommandSpec& command_spec(ExplorerCommand command) noexcept;
/**
 * @brief Returns whether a command should leave visual mode after execution.
 */
[[nodiscard]] bool exits_visual_mode(ExplorerCommand command) noexcept;
/**
 * @brief Returns the complete command catalog.
 */
[[nodiscard]] std::span<const CommandSpec> command_specs() noexcept;
/**
 * @brief Returns the complete default key binding catalog.
 */
[[nodiscard]] std::span<const BindingSpec> default_bindings() noexcept;
/**
 * @brief Returns the sort-command catalog used to derive sort actions.
 */
[[nodiscard]] std::span<const SortSpec> sort_specs() noexcept;
/**
 * @brief Returns the short status-line label for a sort field.
 */
[[nodiscard]] std::string_view sort_field_short_name(SortOrder::Field field) noexcept;

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_COMMANDS_HPP
