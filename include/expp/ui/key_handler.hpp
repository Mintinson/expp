#ifndef EXPP_UI_KEY_HANDLER_HPP
#define EXPP_UI_KEY_HANDLER_HPP

#include "expp/core/error.hpp"


#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Forward declaration to avoid ftxui dependency in header
namespace ftxui {
    struct Event;
} // namespace ftxui


namespace expp::ui {

// ============================================================================
// Key Representation
// ============================================================================
enum class Modifier : std::uint8_t {
    None = 0,
    Ctrl = 1 << 0,
    Alt = 1 << 1,
    Shift = 1 << 2,
    Meta = 1 << 3,  // Windows key or Command key
};

[[nodiscard]] constexpr Modifier operator|(Modifier lhs, Modifier rhs) noexcept {
    return static_cast<Modifier>(std::to_underlying(lhs) | std::to_underlying(rhs));
}
[[nodiscard]] constexpr Modifier operator&(Modifier lhs, Modifier rhs) noexcept {
    return static_cast<Modifier>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

[[nodiscard]] constexpr bool has_modifier(Modifier set, Modifier flag) noexcept {
    return (std::to_underlying(set) & std::to_underlying(flag)) != 0;
}

#define EXPP_SPECIAL_KEYS(X) \
    X(ArrowUp,    "Up") \
    X(ArrowDown,  "Down") \
    X(ArrowLeft,  "ArrowLeft") \
    X(ArrowRight,  "ArrowRight") \
    X(Home,  "Home") \
    X(End,  "End") \
    X(PageUp,     "PageUp") \
    X(PageDown,     "PageDown") \
    X(Return,     "Return") \
    X(Backspace, "Backspace") \
    X(Delete, "Delete") \
    X(Escape, "Escape") \
    X(Tab, "Tab") \
    X(Insert, "Insert") \
    X(F1, "F1") \
    X(F2, "F2") \
    X(F3, "F3") \
    X(F4, "F4") \
    X(F5, "F5") \
    X(F6, "F6") \
    X(F7, "F7") \
    X(F8, "F8") \
    X(F9, "F9") \
    X(F10, "F10") \
    X(F11, "F11") \
    X(F12, "F12") \

// 2. Define Aliases separately (String, EnumName)
#define EXPP_KEY_ALIASES(X) \
    X("PgUp",     PageUp) \
    X("PgDn",     PageDown) \
    X("BS",     Backspace) \
    X("CR",       Return) \
    X("Esc",       Escape) \
    X("Enter",      Return) \


/**
 * @brief Special (non-character) key codes
 */
enum class SpecialKey : std::uint16_t {
    //// clang-format off
    None = 0,

    //// Navigation
    //ArrowUp, ArrowDown, ArrowLeft, ArrowRight,
    //Home, End, PageUp, PageDown,

    //// Editing
    //Backspace, Delete, Return,
    //Escape, Tab, Enter, Insert,

    //// Function keys
    //F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
    //// clang-format on

#define X(name, str) name,
    EXPP_SPECIAL_KEYS(X)
#undef X
};

struct Key {
    char character{'\0'};                  // character for printable keys, 0 for special keys
    SpecialKey special{SpecialKey::None};  // Special key code, None for character keys
    Modifier modifiers{Modifier::None};    // Active modifiers

    [[nodiscard]] constexpr bool isCharacter() const noexcept { return character != '\0'; }
    [[nodiscard]] constexpr bool isSpecial() const noexcept { return special != SpecialKey::None; }
    [[nodiscard]] constexpr bool hasModifiers() const noexcept { return modifiers != Modifier::None; }

    [[nodiscard]] constexpr bool operator==(const Key&) const noexcept = default;

    /**
     * @brief Creates a Key from a character
     */
    [[nodiscard]] static constexpr Key fromChar(char c, Modifier mods = Modifier::None) noexcept {
        return Key{.character = c, .special = SpecialKey::None, .modifiers = mods};
    }

    /**
     * @brief Creates a Key from a special key code
     */
    [[nodiscard]] static constexpr Key fromSpecial(SpecialKey sk, Modifier mods = Modifier::None) noexcept {
        return Key{.character = '\0', .special = sk, .modifiers = mods};
    }
};

/**
 * @brief Parses a key description string into a Key
 *
 * Formats supported:
 * - Single chars: "j", "k", "G"
 * - With modifiers: "C-d" (Ctrl+d), "M-x" (Alt+x), "S-Tab" (Shift+Tab)
 * - Special keys: "<Enter>", "<Esc>", "<Up>", "<F1>"
 * - Combined: "C-M-<Delete>" (Ctrl+Alt+Delete)
 *
 * @param desc Key description string
 * @return Parsed Key or error
 */
[[nodiscard]] core::Result<Key> parse_key(std::string_view desc);

/**
 * @brief Converts a Key back to its string representation
 */
[[nodiscard]] std::string key_to_string(const Key& key);

/**
 * @brief Converts an ftxui::Event to a Key
 */
[[nodiscard]] std::optional<Key> event_to_key(const ftxui::Event& event);

/**
 * @brief Editor modes (vim-like)
 */
enum class Mode : std::uint8_t {
    Normal,   // Navigation and commands
    Insert,   // Text input (bypass keybindings)
    Visual,   // Selection mode
    Command,  // Command-line input (e.g., ":wq")
};

[[nodiscard]] std::string_view mode_to_name(Mode mode) noexcept;
[[nodiscard]] std::optional<Mode> parse_mode(std::string_view name) noexcept;

// ============================================================================
// Action System
// ============================================================================
/**
 * @brief Context passed to action handlers
 */
struct ActionContext {
    int count{1};          // Numeric prefix (default: 1)
    Mode currentMode;      // Current editor mode
    std::string argument;  // Optional argument (for commands like :e <file>)
};

/**
 * @brief Action handler signature
 */
using ActionHandler = std::function<void(const ActionContext&)>;

/**
 * @brief Registered action with metadata
 */
struct Action {
    std::string name;         // Unique action name (e.g., "move_down", "delete_file")
    ActionHandler handler;    // Action implementation
    std::string description;  // Human-readable description
    std::string category;     // Category for grouping in help (e.g., "Navigation")
    bool repeatable{true};    // Whether numeric prefix applies
};

class ActionRegistry {
public:
    ActionRegistry();
    ~ActionRegistry();

    ActionRegistry(ActionRegistry&&) noexcept;
    ActionRegistry& operator=(ActionRegistry&&) noexcept;
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    /**
     * @brief Registers a new action
     * @param name Unique action name
     * @param handler Action implementation
     * @param description Human-readable description
     * @param category Category for grouping
     * @param repeatable Whether numeric prefix applies
     */
    void registerAction(std::string name,
                        ActionHandler handler,
                        std::string description = "",
                        std::string category = "General",
                        bool repeatable = true);

    /**
     * @brief Unregisters an action
     * @return True if action existed
     */
    bool unregisterAction(const std::string& name);

    /**
     * @brief Executes an action by name
     * @return True if action was found and executed
     */
    bool execute(const std::string& name, const ActionContext& context) const;

    /**
     * @brief Gets all registered actions
     * @return Vector of all actions
     */
    [[nodiscard]] const std::vector<Action>& actions() const;

    /**
     * @brief Gets an action by name
     */
    [[nodiscard]] const Action* find(const std::string& name) const;

    /**
     * @brief Gets actions filtered by category
     */
    [[nodiscard]] std::vector<const Action*> byCategory(const std::string& category) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Action handler type
 * @param count Repeat count from numeric prefix (default: 1)
 */
using KeyAction = std::function<void(int count)>;

/**
 * @brief A key sequence binding to an action
 */
struct KeyBinding {
    std::vector<Key> sequence;  ///< Key sequence (e.g., {g, g} for "gg")
    std::string actionName;     ///< Name of action to execute
    Mode mode{Mode::Normal};    ///< Mode this binding applies to
    std::string description;    ///< Optional override for help display
};

class KeyMap {
public:
    KeyMap();
    ~KeyMap();

    KeyMap(KeyMap&&) noexcept;
    KeyMap& operator=(KeyMap&&) noexcept;
    KeyMap(const KeyMap&) = delete;
    KeyMap& operator=(const KeyMap&) = delete;

    /**
     * @brief Adds a key binding
     * @param keys Key description string (e.g., "gg", "C-d", "<Enter>")
     * @param action_name Name of action to execute
     * @param mode Mode this binding applies to
     * @param description Optional description override
     * @return Error if key string is invalid
     */
    [[nodiscard]] core::VoidResult bind(std::string_view keys,
                                        std::string action_name,
                                        Mode mode = Mode::Normal,
                                        std::string description = "");

    /**
     * @brief Removes a binding
     * @return True if binding existed
     */
    bool unbind(std::string_view keys, Mode mode = Mode::Normal);

    /**
     * @brief Loads keybindings from a TOML config file
     *
     * Expected format:
     * ```toml
     * [keybindings.normal]
     * j = "move_down"
     * k = "move_up"
     * gg = "go_top"
     * "C-d" = "page_down"
     *
     * [keybindings.insert]
     * "<Esc>" = "enter_normal_mode"
     * ```
     *
     * @param path Path to config file
     * @return Error if file cannot be read or parsed
     */
    [[nodiscard]] core::VoidResult loadFromFile(const std::filesystem::path& path);

    /**
     * @brief Loads default vim-like keybindings
     */
    void loadDefaults();

    /**
     * @brief Gets all bindings
     */
    [[nodiscard]] const std::vector<KeyBinding>& bindings() const;

    /**
     * @brief Gets bindings for a specific mode
     */
    [[nodiscard]] std::vector<const KeyBinding*> bindingsForMode(Mode mode) const;

    /**
     * @brief Finds a binding by exact sequence match
     */
    [[nodiscard]] const KeyBinding* findExact(const std::vector<Key>& sequence, Mode mode) const;

    /**
     * @brief Checks if sequence is a valid prefix of any binding
     */
    [[nodiscard]] bool isPrefix(const std::vector<Key>& sequence, Mode mode) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Key Handler
// ============================================================================

/**
 * @brief Extensible vim-like key binding handler
 *
 * Combines ActionRegistry and Keymap to provide a complete input handling solution.
 *
 * Features:
 * - Single key bindings: "j", "k", "q"
 * - Multi-key sequences: "gg", "dd"
 * - Numeric prefixes: "5j" (repeat action 5 times)
 * - Modal editing: Normal, Insert, Visual, Command modes
 * - Special keys: arrows, F-keys, Enter, Escape
 * - Modifier keys: Ctrl, Alt, Shift
 * - Configuration from TOML files
 *
 * Thread-safety: Not thread-safe. Use from single thread only.
 *
 * Usage:
 * @code
 * KeyHandler handler;
 *
 * // Register actions
 * handler.actions().registerAction("move_down", [&](const ActionContext& ctx) {
 *     for (int i = 0; i < ctx.count; ++i) moveDown();
 * }, "Move cursor down");
 *
 * // Use default keymaps or load from config
 * handler.keymap().loadDefaults();
 * // Or: handler.keymap().loadFromFile("keybindings.toml");
 *
 * // In event loop:
 * if (handler.handle(event)) {
 *     // Event was consumed
 * }
 * @endcode
 */
class KeyHandler {
public:
    /**
     * @brief Default constructor with default timeout
     * @param timeout_ms Timeout for key sequences in milliseconds
     */
    explicit KeyHandler(int timeout_ms = kDefaultTimeoutMs);
    ~KeyHandler();

    // Movable but not copyable
    KeyHandler(KeyHandler&&) noexcept;
    KeyHandler& operator=(KeyHandler&&) noexcept;
    KeyHandler(const KeyHandler&) = delete;
    KeyHandler& operator=(const KeyHandler&) = delete;

    // --- Action and Keymap access ---

    /**
     * @brief Gets the action registry for registering handlers
     */
    [[nodiscard]] ActionRegistry& actions();
    [[nodiscard]] const ActionRegistry& actions() const;

    /**
     * @brief Gets the keymap for configuring bindings
     */
    [[nodiscard]] KeyMap& keymap();
    [[nodiscard]] const KeyMap& keymap() const;

    // --- Mode management ---

    /**
     * @brief Gets the current mode
     */
    [[nodiscard]] Mode mode() const;

    /**
     * @brief Sets the current mode
     */
    void setMode(Mode mode);

    /**
     * @brief Handles a key event
     * @param event FTXUI event to handle
     * @return True if event was consumed
     */
    bool handle(const ftxui::Event& event);

    /**
     * @brief Forces execution of pending exact match
     * @return True if there was a pending match to execute
     *
     * Useful for timeout handling or mode changes.
     */
    bool flush();
    /**
     * @brief Clears the current key buffer
     */
    void reset();

    /**
     * @brief Gets the current key buffer for display
     * @return Current buffer including numeric prefix
     */
    [[nodiscard]] std::string buffer() const;

    /**
     * @brief Gets the current numeric prefix
     */
    [[nodiscard]] int numericPrefix() const;

    /**
     * @brief Checks if buffer could match more bindings
     */
    [[nodiscard]] bool hasPendingSequence() const;

    /**
     * @brief Sets the sequence timeout
     * @param timeout_ms Timeout in milliseconds
     */
    void setTimeout(int timeout_ms);

    // --- Legacy API (for backward compatibility) ---

    /**
     * @brief Simple binding API (adds to Normal mode)
     * @deprecated Use actions().registerAction() and keymap().bind() instead
     */
    void bind(std::string sequence,
              std::function<void(int)> action,
              std::string description = "",
              std::string category = "General");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    constexpr static int kDefaultTimeoutMs = 1000;  // Default timeout for key sequences
};

// ============================================================================
// Convenience: Default Actions
// ============================================================================

/**
 * @brief Registers common file manager actions to a registry
 *
 * Actions registered:
 * - Navigation: move_down, move_up, page_down, page_up, go_top, go_bottom, go_line
 * - Directory: go_parent, enter_selected, open_selected
 * - File operations: create, rename, delete, trash
 * - Search: search, next_match, prev_match, clear_search
 * - Mode: enter_normal_mode, enter_insert_mode, quit
 *
 * @param registry Registry to populate
 * @param callbacks Struct containing callback implementations
 */
// EXTENSION POINT: Users can provide their own action callbacks
// struct DefaultActionCallbacks { ... };
// void registerDefaultActions(ActionRegistry& registry, const DefaultActionCallbacks& callbacks);

}  // namespace expp::ui

#endif  // EXPP_UI_KEY_HANDLER_HPP