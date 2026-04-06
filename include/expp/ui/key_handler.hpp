#ifndef EXPP_UI_KEY_HANDLER_HPP
#define EXPP_UI_KEY_HANDLER_HPP

/**
 * @file key_handler.hpp
 * @brief Vim-like key parsing, binding, and dispatch infrastructure.
 *
 * The key system is intentionally generic: explorer commands are registered as
 * strongly typed command ids, but the key layer itself remains reusable for
 * other interactive components.
 */

#include "expp/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ftxui {
struct Event;
}  // namespace ftxui

namespace expp::ui {

/**
 * @brief Modifier bitflags attached to a key.
 */
enum class Modifier : std::uint8_t {
    None = 0,
    Ctrl = 1 << 0,
    Alt = 1 << 1,
    Shift = 1 << 2,
    Meta = 1 << 3,
};

/**
 * @brief Combines modifier flags.
 */
[[nodiscard]] constexpr Modifier operator|(Modifier lhs, Modifier rhs) noexcept {
    return static_cast<Modifier>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

/**
 * @brief Intersects modifier flags.
 */
[[nodiscard]] constexpr Modifier operator&(Modifier lhs, Modifier rhs) noexcept {
    return static_cast<Modifier>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

/**
 * @brief Returns whether `set` contains `flag`.
 */
[[nodiscard]] constexpr bool has_modifier(Modifier set, Modifier flag) noexcept {
    return (std::to_underlying(set) & std::to_underlying(flag)) != 0;
}

// Canonical special-key spellings used when parsing config strings and
// rendering key sequences back to text.
#define EXPP_SPECIAL_KEYS(X)       \
    X(ArrowUp, "Up")               \
    X(ArrowDown, "Down")           \
    X(ArrowLeft, "Left")           \
    X(ArrowRight, "Right")         \
    X(Home, "Home")                \
    X(End, "End")                  \
    X(PageUp, "PageUp")            \
    X(PageDown, "PageDown")        \
    X(Return, "Return")            \
    X(Backspace, "Backspace")      \
    X(Delete, "Delete")            \
    X(Escape, "Escape")            \
    X(Tab, "Tab")                  \
    X(Insert, "Insert")            \
    X(F1, "F1")                    \
    X(F2, "F2")                    \
    X(F3, "F3")                    \
    X(F4, "F4")                    \
    X(F5, "F5")                    \
    X(F6, "F6")                    \
    X(F7, "F7")                    \
    X(F8, "F8")                    \
    X(F9, "F9")                    \
    X(F10, "F10")                  \
    X(F11, "F11")                  \
    X(F12, "F12")

#define EXPP_KEY_ALIASES(X) \
    X("ArrowUp", ArrowUp)   \
    X("ArrowDown", ArrowDown) \
    X("ArrowLeft", ArrowLeft) \
    X("ArrowRight", ArrowRight) \
    X("PgUp", PageUp)       \
    X("PgDn", PageDown)     \
    X("BS", Backspace)      \
    X("CR", Return)         \
    X("Esc", Escape)        \
    X("Enter", Return)

/**
 * @brief Non-character keys recognized by the binding system.
 */
enum class SpecialKey : std::uint8_t {
    None = 0,

#define X(name, str) name,
    EXPP_SPECIAL_KEYS(X)
#undef X
};

/**
 * @brief Parsed key token used in a binding sequence.
 */
struct Key {
    /// Non-zero when the key is a printable character.
    char character{'\0'};
    /// Non-`None` when the key is a named special key.
    SpecialKey special{SpecialKey::None};
    /// Modifier flags held when the key was pressed.
    Modifier modifiers{Modifier::None};

    [[nodiscard]] constexpr bool isCharacter() const noexcept { return character != '\0'; }
    [[nodiscard]] constexpr bool isSpecial() const noexcept { return special != SpecialKey::None; }
    [[nodiscard]] constexpr bool hasModifiers() const noexcept { return modifiers != Modifier::None; }

    [[nodiscard]] constexpr bool operator==(const Key&) const noexcept = default;

    [[nodiscard]] static constexpr Key fromChar(char c, Modifier mods = Modifier::None) noexcept {
        return Key{.character = c, .special = SpecialKey::None, .modifiers = mods};
    }

    [[nodiscard]] static constexpr Key fromSpecial(SpecialKey sk, Modifier mods = Modifier::None) noexcept {
        return Key{.character = '\0', .special = sk, .modifiers = mods};
    }
};

/**
 * @brief Parses a textual key description such as `j`, `C-d`, or `<Left>`.
 */
[[nodiscard]] core::Result<Key> parse_key(std::string_view desc);
/**
 * @brief Converts a key back to its textual description.
 */
[[nodiscard]] std::string key_to_string(const Key& key);
/**
 * @brief Converts an FTXUI event into a key, when representable.
 */
[[nodiscard]] std::optional<Key> event_to_key(const ftxui::Event& event);

/**
 * @brief Key-handling mode for bindings and dispatch.
 */
enum class Mode : std::uint8_t {
    Normal,
    Insert,
    Visual,
    Command,
};

/**
 * @brief Returns the stable lowercase name for a mode.
 */
[[nodiscard]] std::string_view mode_to_name(Mode mode) noexcept;
/**
 * @brief Parses a stable lowercase mode name.
 */
[[nodiscard]] std::optional<Mode> parse_mode(std::string_view name) noexcept;

/**
 * @brief Execution context passed to a bound action.
 */
struct ActionContext {
    /// Numeric prefix supplied before the action, defaulting to 1.
    int count{1};
    /// Mode active when the action executed.
    Mode currentMode;
    /// Reserved for future argument-carrying commands.
    std::string argument;
};

using ActionHandler = std::function<void(const ActionContext&)>;
using CommandId = std::uint32_t;
constexpr CommandId kInvalidCommandId = std::numeric_limits<CommandId>::max();

/**
 * @brief Registered action metadata and callback.
 */
struct Action {
    CommandId commandId{kInvalidCommandId};
    ActionHandler handler;
    std::string description;
    std::string category;
    bool repeatable{true};
};

/**
 * @brief Registry mapping command ids to executable actions.
 */
class ActionRegistry {
public:
    ActionRegistry();
    ~ActionRegistry();

    ActionRegistry(ActionRegistry&&) noexcept;
    ActionRegistry& operator=(ActionRegistry&&) noexcept;
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    /**
     * @brief Registers or replaces an action for `command_id`.
     */
    void registerAction(CommandId command_id,
                        ActionHandler handler,
                        std::string description = "",
                        std::string category = "General",
                        bool repeatable = true);
    /**
     * @brief Removes a registered action.
     */
    bool unregisterAction(CommandId command_id);
    /**
     * @brief Executes the action bound to `command_id`.
     *
     * Non-repeatable actions force `ctx.count` back to 1.
     */
    bool execute(CommandId command_id, const ActionContext& ctx) const;
    /**
     * @brief Returns all registered actions.
     */
    [[nodiscard]] const std::vector<Action>& actions() const;
    /**
     * @brief Returns a registered action by id.
     */
    [[nodiscard]] const Action* find(CommandId command_id) const;
    /**
     * @brief Returns actions grouped under a category label.
     */
    [[nodiscard]] std::vector<const Action*> byCategory(const std::string& category) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief One concrete key sequence bound to a command.
 */
struct KeyBinding {
    std::vector<Key> sequence;
    CommandId commandId{kInvalidCommandId};
    Mode mode{Mode::Normal};
    std::string description;
};

/**
 * @brief Successfully loaded binding entry from a TOML file.
 */
struct KeyLoadEntry {
    std::string keys;
    std::string commandName;
    Mode mode{Mode::Normal};
    std::string description;
};

/**
 * @brief Non-fatal issue encountered while loading key bindings.
 */
struct KeyLoadWarning {
    std::string message;
};

/**
 * @brief Summary of a keybinding config load.
 */
struct KeyLoadReport {
    std::vector<KeyLoadEntry> loadedBindings;
    std::vector<KeyLoadWarning> warnings;
};

/**
 * @brief Resolves a config-facing command name to a command id.
 */
using CommandResolver = std::function<std::optional<CommandId>(std::string_view)>;

/**
 * @brief Stores parsed key bindings for all modes.
 */
class KeyMap {
public:
    KeyMap();
    ~KeyMap();

    KeyMap(KeyMap&&) noexcept;
    KeyMap& operator=(KeyMap&&) noexcept;
    KeyMap(const KeyMap&) = delete;
    KeyMap& operator=(const KeyMap&) = delete;

    /**
     * @brief Binds a textual key sequence to a command.
     */
    [[nodiscard]] core::VoidResult bind(std::string_view keys,
                                        CommandId command_id,
                                        Mode mode = Mode::Normal,
                                        std::string description = "");
    /**
     * @brief Removes a binding if it exists.
     */
    bool unbind(std::string_view keys, Mode mode = Mode::Normal);
    /**
     * @brief Loads bindings from TOML and resolves command names through `resolve_command`.
     */
    [[nodiscard]] core::Result<KeyLoadReport> loadFromFile(const std::filesystem::path& path,
                                                           const CommandResolver& resolve_command);
    /**
     * @brief Returns all bindings.
     */
    [[nodiscard]] const std::vector<KeyBinding>& bindings() const;
    /**
     * @brief Returns bindings active in a specific mode.
     */
    [[nodiscard]] std::vector<const KeyBinding*> bindingsForMode(Mode mode) const;
    /**
     * @brief Returns the exact binding for a parsed sequence, if any.
     */
    [[nodiscard]] const KeyBinding* findExact(const std::vector<Key>& sequence, Mode mode) const;
    /**
     * @brief Returns whether `sequence` is a prefix of any longer binding.
     */
    [[nodiscard]] bool isPrefix(const std::vector<Key>& sequence, Mode mode) const;
    /**
     * @brief Removes all bindings.
     */
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Stateful key-sequence interpreter with vim-like prefix behavior.
 *
 * `KeyHandler` tracks the current mode, numeric prefixes, and partially entered
 * multi-key sequences, then dispatches to the registered action registry.
 */
class KeyHandler {
public:
    explicit KeyHandler(int timeout_ms = kDefaultTimeoutMs);
    ~KeyHandler();

    KeyHandler(KeyHandler&&) noexcept;
    KeyHandler& operator=(KeyHandler&&) noexcept;
    KeyHandler(const KeyHandler&) = delete;
    KeyHandler& operator=(const KeyHandler&) = delete;

    /// Returns the mutable action registry.
    [[nodiscard]] ActionRegistry& actions();
    /// Returns the read-only action registry.
    [[nodiscard]] const ActionRegistry& actions() const;
    /// Returns the mutable keymap.
    [[nodiscard]] KeyMap& keymap();
    /// Returns the read-only keymap.
    [[nodiscard]] const KeyMap& keymap() const;

    /// Returns the current key-handling mode.
    [[nodiscard]] Mode mode() const;
    /// Changes mode and clears any pending sequence state.
    void setMode(Mode mode);
    /**
     * @brief Processes one FTXUI event.
     *
     * Returns true when the event was consumed either as a complete binding, a
     * pending prefix, or a numeric prefix component.
     */
    bool handle(const ftxui::Event& event);
    /// Forces execution or reset of the current pending sequence.
    bool flush();
    /// Clears buffered sequence and numeric-prefix state.
    void reset();
    /// Returns the textual form of the pending key buffer.
    [[nodiscard]] std::string buffer() const;
    /// Returns the current numeric prefix, defaulting to 1.
    [[nodiscard]] int numericPrefix() const;
    /// Returns whether a partial sequence or numeric prefix is buffered.
    [[nodiscard]] bool hasPendingSequence() const;
    /// Sets the timeout for multi-key sequence buffering.
    void setTimeout(int timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    constexpr static int kDefaultTimeoutMs = 1000;
};

}  // namespace expp::ui

#endif  // EXPP_UI_KEY_HANDLER_HPP
