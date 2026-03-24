/**
 * @file key_handler.cpp
 * @brief Implementation of the vim-like key binding system
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/ui/key_handler.hpp"

#include "expp/core/error.hpp"

#include <ftxui/component/event.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

namespace expp::ui {
namespace rng = std::ranges;

// ============================================================================
// Key Parsing
// ============================================================================
namespace {

const std::unordered_map<std::string_view, SpecialKey> kSpecialKeyNames = {
#define X(name, str) {str, SpecialKey::name},
    EXPP_SPECIAL_KEYS(X)
#undef X

#define X(str, name) {str, SpecialKey::name},
        EXPP_KEY_ALIASES(X)
#undef X
};
const std::unordered_map<SpecialKey, std::string_view> kSpecialKeyStrings = {

#define X(name, str) {SpecialKey::name, str},
    EXPP_SPECIAL_KEYS(X)
#undef X
};
// constexpr
}  // namespace

core::Result<Key> parse_key(std::string_view desc) {
    if (desc.empty()) {
        return core::make_error(core::ErrorCategory::InvalidArgument, "Key description cannot be empty");
    }

    Modifier mods = Modifier::None;

    // Parse modifier prefixes (C-, M-, S-, A-)
    while (desc.size() >= 2 && desc[1] == '-') {
        switch (desc[0]) {
            case 'C':
            case 'c':
                mods = mods | Modifier::Ctrl;
                break;
            case 'M':
            case 'm':
            case 'A':
            case 'a':
                mods = mods | Modifier::Alt;
                break;
            case 'W':
            case 'w':
                mods = mods | Modifier::Meta;
                break;
            case 'S':
            case 's':
                mods = mods | Modifier::Shift;
                break;
            default:
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        std::format("Unknown modifier '{}'", desc[0]));
        }
        desc.remove_prefix(2);
    }

    if (desc.empty()) {
        return core::make_error(core::ErrorCategory::InvalidArgument, "Key description ends with modifier prefix");
    }

    // Handle <Special> key notation
    if (desc.front() == '<' && desc.back() == '>') {
        std::string_view key_name = desc.substr(1, desc.size() - 2);

        if (auto it = kSpecialKeyNames.find(key_name); it != kSpecialKeyNames.end()) {
            return Key::fromSpecial(it->second, mods);
        } else {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    std::format("Unknown special key '<{}>'", key_name));
        }
    }

    // Single character
    if (desc.size() == 1) {
        return Key::fromChar(desc[0], mods);
    }

    // Multi-char without <> might be a special key name
    if (auto it = kSpecialKeyNames.find(desc); it != kSpecialKeyNames.end()) {
        return Key::fromSpecial(it->second, mods);
    }

    return core::make_error(core::ErrorCategory::InvalidArgument, std::format("Invalid key description '{}'", desc));
}

std::string key_to_string(const Key& key) {
    std::string result;

    if (has_modifier(key.modifiers, Modifier::Ctrl)) {
        result += "C-";
    }
    if (has_modifier(key.modifiers, Modifier::Alt)) {
        result += "M-";
    }
    if (has_modifier(key.modifiers, Modifier::Shift)) {
        result += "S-";
    }
    if (has_modifier(key.modifiers, Modifier::Meta)) {
        result += "W-";
    }

    if (key.isSpecial()) {
        if (auto it = kSpecialKeyStrings.find(key.special); it != kSpecialKeyStrings.end()) {
            result += '<';
            result += it->second;
            result += '>';
        } else {
            result += "<Unknown>";
        }
    } else if (key.isCharacter()) {
        result += key.character;
    }
    return result;
}

std::optional<Key> event_to_key(const ftxui::Event& event) {
    // Handle special keys first
#define X(name, str)                               \
    if (event == ftxui::Event::name) {             \
        return Key::fromSpecial(SpecialKey::name); \
    }
    EXPP_SPECIAL_KEYS(X)
#undef X

    if (event.is_character()) {
        const std::string& s = event.character();

        if (!s.empty()) {
            char c = s[0];

            // Check for Ctrl key (control characters are 1-26)
            if (c >= 1 && c <= 26) {
                return Key::fromChar(static_cast<char>('a' + c - 1), Modifier::Ctrl);
            }

            return Key::fromChar(c);
        }
    }

    return std::nullopt;
}

std::string_view mode_to_name(Mode mode) noexcept {
    switch (mode) {
        case expp::ui::Mode::Normal:
            return "normal";
        case expp::ui::Mode::Insert:
            return "insert";
        case expp::ui::Mode::Visual:
            return "visual";
        case expp::ui::Mode::Command:
            return "command";
        default:
            return "unknown";
    }
}

std::optional<Mode> parse_mode(std::string_view name) noexcept {
    if (name == "normal") {
        return Mode::Normal;
    }
    if (name == "insert") {
        return Mode::Insert;
    }
    if (name == "visual") {
        return Mode::Visual;
    }
    if (name == "command") {
        return Mode::Command;
    }
    return std::nullopt;
}

// ============================================================================
// ActionRegistry
// ============================================================================

struct ActionRegistry::Impl {
    std::vector<Action> actions;
    std::unordered_map<std::string, size_t> nameIndex;
};

ActionRegistry::ActionRegistry() : impl_(std::make_unique<Impl>()) {}
ActionRegistry::~ActionRegistry() = default;
ActionRegistry::ActionRegistry(ActionRegistry&&) noexcept = default;
ActionRegistry& ActionRegistry::operator=(ActionRegistry&&) noexcept = default;

void ActionRegistry::registerAction(
    std::string name, ActionHandler handler, std::string description, std::string category, bool repeatable) {
    // Remove existing if present
    unregisterAction(name);

    impl_->nameIndex[name] = impl_->actions.size();
    impl_->actions.push_back(Action{.name = std::move(name),
                                    .handler = std::move(handler),
                                    .description = std::move(description),
                                    .category = std::move(category),
                                    .repeatable = repeatable});
}

bool ActionRegistry::unregisterAction(const std::string& name) {
    auto it = impl_->nameIndex.find(name);
    if (it == impl_->nameIndex.end()) {
        return false;
    }

    size_t idx = it->second;
    impl_->actions.erase(impl_->actions.begin() + static_cast<ptrdiff_t>(idx));
    impl_->nameIndex.erase(it);

    // Rebuild index
    impl_->nameIndex.clear();
    for (size_t i = 0; i < impl_->actions.size(); ++i) {
        impl_->nameIndex[impl_->actions[i].name] = i;
    }

    return true;
}

bool ActionRegistry::execute(const std::string& name, const ActionContext& ctx) const {
    const Action* action = find(name);
    if (action == nullptr || !action->handler) {
        return false;
    }

    ActionContext execCtx = ctx;
    if (!action->repeatable) {
        execCtx.count = 1;
    }

    action->handler(execCtx);
    return true;
}

const Action* ActionRegistry::find(const std::string& name) const {
    auto it = impl_->nameIndex.find(name);
    if (it == impl_->nameIndex.end()) {
        return nullptr;
    }
    return &impl_->actions[it->second];
}

const std::vector<Action>& ActionRegistry::actions() const {
    return impl_->actions;
}

std::vector<const Action*> ActionRegistry::byCategory(const std::string& category) const {
    std::vector<const Action*> result;
    for (const auto& action : impl_->actions) {
        if (action.category == category) {
            result.push_back(&action);
        }
    }
    return result;
}

// ============================================================================
// Keymap
// ============================================================================
struct KeyMap::Impl {
    std::vector<KeyBinding> bindings;

    /**
     * @brief Helper function to parse key sequence string into Key vector
     * @param keys
     * @return
     */
    [[nodiscard]] core::Result<std::vector<Key>> parseKeySequence(std::string_view keys) {
        std::vector<Key> sequence;

        std::size_t pos = 0;
        while (pos < keys.size()) {
            // skip spaces
            while (pos < keys.size() && keys[pos] == ' ') {
                ++pos;
            }
            if (pos >= keys.size()) {
                break;
            }

            std::size_t end = pos;

            if (keys[pos] == '<') {
                end = keys.find('>', pos);
                if (end == std::string_view::npos) {
                    return core::make_error(core::ErrorCategory::InvalidArgument,
                                            std::format("Unmatched '<' in key sequence '{}'", keys));
                }
                ++end;  // include '>'
            } else {
                // Regular char or modifier prefix
                // Check for modifier (X-...)
                while (end < keys.size() && end + 1 < keys.size() && keys[end + 1] == '-' &&
                       (keys[end] == 'C' || keys[end] == 'c' || keys[end] == 'M' || keys[end] == 'm' ||
                        keys[end] == 'A' || keys[end] == 'a' || keys[end] == 'S' || keys[end] == 's')) {
                    end += 2;
                }

                // Now the actual key
                if (end < keys.size()) {
                    if (keys[end] == '<') {
                        // Special key with modifiers
                        size_t closePos = keys.find('>', end);
                        if (closePos == std::string_view::npos) {
                            return core::make_error(core::ErrorCategory::InvalidArgument,
                                                    "Unclosed '<' in key sequence");
                        }
                        end = closePos + 1;
                    } else {
                        ++end;  // Single char
                    }
                }
            }

            auto keyResult = parse_key(keys.substr(pos, end - pos));
            if (!keyResult) {
                return core::make_error(core::ErrorCategory::InvalidArgument, keyResult.error().message());
            }
            sequence.push_back(*keyResult);
            pos = end;
        }

        if (sequence.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument, "Empty key sequence");
        }

        return sequence;
    }
};

KeyMap::KeyMap() : impl_{std::make_unique<Impl>()} {}
KeyMap::~KeyMap() = default;
KeyMap::KeyMap(KeyMap&&) noexcept = default;
KeyMap& KeyMap::operator=(KeyMap&&) noexcept = default;

core::VoidResult expp::ui::KeyMap::bind(std::string_view keys,
                                        std::string action_name,
                                        Mode mode,
                                        std::string description) {
    // Parse key sequence using helper
    auto sequenceResult = impl_->parseKeySequence(keys);

    if (!sequenceResult) {
        return std::unexpected(sequenceResult.error());
    }

    auto sequence = std::move(sequenceResult.value());

    // Remove existing binding for same sequence in same mode
    auto& bindings = impl_->bindings;
    auto removeIt =
        rng::remove_if(bindings, [&](const KeyBinding& b) { return b.mode == mode && b.sequence == sequence; });
    bindings.erase(removeIt.begin(), bindings.end());

    // Add new binding
    bindings.push_back(KeyBinding{.sequence = std::move(sequence),
                                  .actionName = std::move(action_name),
                                  .mode = mode,
                                  .description = std::move(description)});

    return {};
}

bool KeyMap::unbind(std::string_view keys, Mode mode) {
    auto sequenceResult = impl_->parseKeySequence(keys);

    if (!sequenceResult) {
        return false;
    }

    const auto& sequence = sequenceResult.value();

    // Find and remove matching binding
    auto& bindings = impl_->bindings;

    auto it = rng::find_if(bindings, [&](const KeyBinding& b) { return b.mode == mode && b.sequence == sequence; });

    if (it != bindings.end()) {
        bindings.erase(it);
        return true;
    }
    return false;
}

core::VoidResult KeyMap::loadFromFile(const std::filesystem::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
        return core::make_error(core::ErrorCategory::Config, std::format("Failed to parse keybinding config '{}': {}",
                                                                         path.string(), err.description()));
    }

    auto *keys = tbl["keys"].as_table();
    if (!keys) {
        return {};  // No [keys] section — not an error, just use defaults
    }

    for (auto&& [modeName, modeTable] : *keys) {
        auto mode = parse_mode(modeName);
        if (!mode) {
            continue;  // Skip unknown mode names
        }

        auto* bindings = modeTable.as_table();
        if (!bindings) {
            continue;
        }

        for (auto&& [actionName, keyValue] : *bindings) {
            auto key_str = keyValue.value<std::string>();
            if (!key_str) {
                continue;  // Skip non-string values
            }

            auto result = bind(*key_str, std::string{actionName}, *mode);
            // Silently skip invalid bindings — they don't prevent other bindings from loading
            (void)result;
        }
    }

    return {};
}

void KeyMap::loadDefaults() {
    // Navigation
    (void)bind("j", "move_down", Mode::Normal);
    (void)bind("k", "move_up", Mode::Normal);
    (void)bind("h", "go_parent", Mode::Normal);
    (void)bind("l", "enter_selected", Mode::Normal);
    (void)bind("<Down>", "move_down", Mode::Normal);
    (void)bind("<Up>", "move_up", Mode::Normal);
    (void)bind("<Left>", "go_parent", Mode::Normal);
    (void)bind("<Right>", "enter_selected", Mode::Normal);

    // Jump
    (void)bind("gg", "go_top", Mode::Normal);
    (void)bind("G", "go_bottom", Mode::Normal);
    (void)bind("C-d", "page_down", Mode::Normal);
    (void)bind("C-u", "page_up", Mode::Normal);
    (void)bind("<PageDown>", "page_down", Mode::Normal);
    (void)bind("<PageUp>", "page_up", Mode::Normal);
    (void)bind("<Home>", "go_top", Mode::Normal);
    (void)bind("<End>", "go_bottom", Mode::Normal);

    // File operations
    (void)bind("o", "open_selected", Mode::Normal);
    (void)bind("<Enter>", "enter_selected", Mode::Normal);
    (void)bind("a", "create", Mode::Normal);
    (void)bind("r", "rename", Mode::Normal);
    (void)bind("d", "trash", Mode::Normal);
    (void)bind("D", "delete", Mode::Normal);

    // Search
    (void)bind("/", "search", Mode::Normal);
    (void)bind("n", "next_match", Mode::Normal);
    (void)bind("N", "prev_match", Mode::Normal);
    (void)bind("\\", "clear_search", Mode::Normal);

    // Mode switching
    (void)bind("<Esc>", "quit", Mode::Normal);
    (void)bind("q", "quit", Mode::Normal);

    // Insert mode
    (void)bind("<Esc>", "enter_normal_mode", Mode::Insert);
}

const std::vector<KeyBinding>& KeyMap::bindings() const {
    return impl_->bindings;
}

std::vector<const KeyBinding*> KeyMap::bindingsForMode(Mode mode) const {
    return impl_->bindings | rng::views::filter([mode](const KeyBinding& b) { return b.mode == mode; }) |
           rng::views::transform([](const KeyBinding& b) { return std::addressof(b); }) | rng::to<std::vector>();
}

const KeyBinding* KeyMap::findExact(const std::vector<Key>& sequence, Mode mode) const {
    auto it =
        rng::find_if(impl_->bindings, [&](const KeyBinding& b) { return b.mode == mode && b.sequence == sequence; });
    return it != impl_->bindings.end() ? &(*it) : nullptr;
}

bool KeyMap::isPrefix(const std::vector<Key>& sequence, Mode mode) const {
    return rng::any_of(impl_->bindings, [&](const KeyBinding& b) {
        if (b.mode != mode || b.sequence.size() <= sequence.size()) {
            return false;
        }
        return std::equal(sequence.begin(), sequence.end(), b.sequence.begin());
    });
}

// ============================================================================
// KeyHandler
// ============================================================================

struct KeyHandler::Impl {
    ActionRegistry actions;
    KeyMap keymap;

    std::vector<Key> buffer;

    int numericPrefix{0};
    bool hasNumericPrefix{false};
    Mode currentMode{Mode::Normal};
    int timeoutMs{1000};
    std::chrono::steady_clock::time_point lastKeyTime;

    struct _LegacyBinding {
        std::string sequence;
        std::function<void(int)> action;
    };

    // TODO: remove it when possible, this is just to keep backward compatibility with existing code that uses the old
    // bind() API
    std::vector<_LegacyBinding> legacyBindings;

    void reset() {
        buffer.clear();
        hasNumericPrefix = 0;
        hasNumericPrefix = false;
    }

    [[nodiscard]] std::string bufferToString() const {
        return std::accumulate(buffer.begin(), buffer.end(),
                               hasNumericPrefix ? std::to_string(numericPrefix) : std::string{},
                               [](const std::string& acc, const Key& key) { return acc + key_to_string(key); });
    }

    bool tryLegacyBindings() {
        std::string seq = bufferToString();
        // Strip numeric prefix for matching
        size_t start = 0;
        while (start < seq.size() && std::isdigit(seq[start]) != 0) {
            ++start;
        }
        std::string key_seq = seq.substr(start);

        for (const auto& binding : legacyBindings) {
            if (binding.sequence == key_seq) {
                int count = hasNumericPrefix ? numericPrefix : 1;
                binding.action(count);
                reset();
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool isLegacyPrefix() const {
        std::string seq;
        for (const auto& key : buffer) {
            seq += key_to_string(key);
        }

        return rng::any_of(legacyBindings, [&seq](const _LegacyBinding& b) {
            return b.sequence.starts_with(seq) && b.sequence != seq;
        });
    }
};

KeyHandler::KeyHandler(int timeout_ms) : impl_(std::make_unique<Impl>()) {
    impl_->timeoutMs = timeout_ms;
}

KeyHandler::~KeyHandler() = default;

KeyHandler::KeyHandler(KeyHandler&&) noexcept = default;
KeyHandler& KeyHandler::operator=(KeyHandler&&) noexcept = default;

ActionRegistry& KeyHandler::actions() {
    return impl_->actions;
}
const ActionRegistry& KeyHandler::actions() const {
    return impl_->actions;
}
KeyMap& KeyHandler::keymap() {
    return impl_->keymap;
}

const KeyMap& KeyHandler::keymap() const {
    return impl_->keymap;
}

Mode KeyHandler::mode() const {
    return impl_->currentMode;
}

void KeyHandler::setMode(Mode mode) {
    impl_->currentMode = mode;
    impl_->reset();
}

bool KeyHandler::handle(const ftxui::Event& event) {
    auto keyOpt = event_to_key(event);
    if (!keyOpt) {
        return false;
    }

    Key key = keyOpt.value();

    auto now = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->lastKeyTime).count();

    if (elapsed > impl_->timeoutMs && !impl_->buffer.empty()) {
        impl_->reset();
    }

    impl_->lastKeyTime = now;

    // Handle numeric prefix (digits without modifiers)
    if (key.isCharacter() && key.modifiers == Modifier::None && std::isdigit(key.character) != 0 &&
        impl_->buffer.empty()) {
        // '0' is only a prefix if we already have digits
        if (key.character != '0' || impl_->hasNumericPrefix) {
            impl_->numericPrefix = (impl_->numericPrefix * 10) + (key.character - '0');  // NOLINT
            impl_->hasNumericPrefix = true;
            return true;  // Consume numeric prefix input
        }
    }

    // add key to buffer
    impl_->buffer.push_back(key);

    // try new keymap system first
    if (const auto* binding = impl_->keymap.findExact(impl_->buffer, impl_->currentMode)) {
        int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
        bool executed = impl_->actions.execute(
            binding->actionName, ActionContext{.count = count, .currentMode = impl_->currentMode, .argument = {}});
        impl_->reset();
        return executed;
    }

    // Check if it's a valid prefix in new system
    if (impl_->keymap.isPrefix(impl_->buffer, impl_->currentMode)) {
        return true;  // Wait for more keys
    }

    // Try legacy bindings
    if (impl_->tryLegacyBindings()) {
        return true;
    }

    // Check legacy prefix
    if (impl_->isLegacyPrefix()) {
        return true;
    }

    // No match - reset
    impl_->reset();
    return false;
}

bool KeyHandler::flush() {
    if (const auto* binding = impl_->keymap.findExact(impl_->buffer, impl_->currentMode)) {
        int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
        bool executed = impl_->actions.execute(
            binding->actionName, ActionContext{.count = count, .currentMode = impl_->currentMode, .argument = {}});
        impl_->reset();
        return executed;
    }

    // Try legacy
    if (impl_->tryLegacyBindings()) {
        return true;
    }

    impl_->reset();
    return false;
}

void KeyHandler::reset() {
    impl_->reset();
}

std::string KeyHandler::buffer() const {
    return impl_->bufferToString();
}

int KeyHandler::numericPrefix() const {
    return impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
}

bool KeyHandler::hasPendingSequence() const {
    return !impl_->buffer.empty() || impl_->hasNumericPrefix;
}

void KeyHandler::setTimeout(int timeoutMs) {
    impl_->timeoutMs = timeoutMs;
}

void KeyHandler::bind(std::string sequence,
                      std::function<void(int)> action,
                      [[maybe_unused]] std::string description,
                      [[maybe_unused]] std::string category) {
    impl_->legacyBindings.push_back({.sequence = std::move(sequence), .action = std::move(action)});
}

// void KeyHandler::bind(std::string sequence, KeyAction action, std::string description, std::string category) {
//     impl_->bindings.push_back({.sequence = std::move(sequence),
//                                .action = std::move(action),
//                                .description = std::move(description),
//                                .category = std::move(category)});
// }
//
// bool KeyHandler::unbind(const std::string& sequence) {
//     auto it = rng::find_if(impl_->bindings, [&sequence](const KeyBinding& bind) { return bind.sequence == sequence;
//     }); if (it != impl_->bindings.end()) {
//         impl_->bindings.erase(it);
//         return true;
//     }
//
//     return false;
// }
//
// bool KeyHandler::handle(const ftxui::Event& event) {
//     // Only handle single character events
//     if (!event.is_character()) {
//         return false;
//     }
//
//     auto now = std::chrono::steady_clock::now();
//     auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->lastKeyTime).count();
//
//     // reset buffer on timeout
//     if (elapsed > impl_->timeoutMs && !impl_->buffer.empty()) {
//         impl_->reset();
//     }
//     impl_->lastKeyTime = now;
//
//     char c = event.character()[0];
//
//     // Handle numeric prefix (but not if buffer already has non-digit chars)
//     // Special case: '0' is only a prefix if we already have digits
//     if (std::isdigit(c) != 0 && impl_->buffer.empty()) {
//         if (c != '0' || impl_->hasNumericPrefix) {
//             impl_->numericPrefix = (impl_->numericPrefix * 10) + (c - '0');  // NOLINT
//             impl_->hasNumericPrefix = true;
//             return true;  // Consume numeric prefix input
//         }
//     }
//
//     // Add character to buffer
//     impl_->buffer += c;
//
//     // Check for exact match
//     if (const auto* binding = impl_->findExactMatch()) {
//         // If this could also be a prefix, execute the exact match anyway
//         // (unlike some vim modes, we don't wait for timeouts on exact matches)
//         int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
//         binding->action(count);
//         impl_->reset();
//         return true;
//     }
//
//     // Check if buffer is a valid prefix
//     if (impl_->isPrefixOfAny()) {
//         return true;  // Wait for more keys
//     }
//
//     // No match and not a valid prefix - reset
//     impl_->reset();
//     return false;
// }
//
// bool KeyHandler::flush() {
//     if (const auto* binding = impl_->findExactMatch()) {
//         int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
//         binding->action(count);
//         impl_->reset();
//         return true;
//     }
//     impl_->reset();
//     return false;
// }
//
// void KeyHandler::reset() {
//     impl_->reset();
// }
//
// std::string KeyHandler::buffer() const {
//     std::string result;
//     if (impl_->hasNumericPrefix) {
//         result = std::format("{}{} ", impl_->numericPrefix, impl_->buffer);
//     } else {
//         result = impl_->buffer;
//     }
//     return result;
// }
// const std::vector<KeyBinding>& KeyHandler::bindings() const {
//     return impl_->bindings;
// }
// std::vector<KeyBinding> KeyHandler::bindingsByCategory(const std::string& category) const {
//     std::vector<KeyBinding> result;
//     for (const auto& bind : impl_->bindings) {
//         if (bind.category == category) {
//             result.push_back(bind);
//         }
//     }
//     return result;
// }
//
// void KeyHandler::setTimeout(int timeout_ms) {
//     impl_->timeoutMs = timeout_ms;
// }
//
// bool KeyHandler::hasPendingSequence() const {
//     return !impl_->buffer.empty() || impl_->hasNumericPrefix;
// }

}  // namespace expp::ui