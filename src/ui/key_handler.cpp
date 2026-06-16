/**
 * @file key_handler.cpp
 * @brief Implementation of the vim-like key binding system
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/ui/key_handler.hpp"

#include <ftxui/component/event.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// #define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

namespace expp::ui {
namespace rng = std::ranges;

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

}  // namespace

core::Result<Key> parse_key(std::string_view desc) {
    if (desc.empty()) {
        return core::make_error(core::ErrorCategory::InvalidArgument, "Key description cannot be empty");
    }

    Modifier mods = Modifier::None;

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

    if (desc.front() == '<' && desc.back() == '>') {
        const std::string_view key_name = desc.substr(1, desc.size() - 2);
        if (auto it = kSpecialKeyNames.find(key_name); it != kSpecialKeyNames.end()) {
            return Key::fromSpecial(it->second, mods);
        }
        return core::make_error(core::ErrorCategory::InvalidArgument,
                                std::format("Unknown special key '<{}>'", key_name));
    }

    if (desc.size() == 1) {
        return Key::fromChar(desc[0], mods);
    }

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
#define X(name, str)                               \
    if (event == ftxui::Event::name) {             \
        return Key::fromSpecial(SpecialKey::name); \
    }
    EXPP_SPECIAL_KEYS(X)
#undef X

    if (event.is_character()) {
        const std::string& s = event.character();
        if (!s.empty()) {
            const char c = s[0];
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
        case Mode::Normal:
            return "normal";
        case Mode::Insert:
            return "insert";
        case Mode::Visual:
            return "visual";
        case Mode::Command:
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

struct ActionRegistry::Impl {
    std::vector<Action> actions;
    std::unordered_map<CommandId, std::size_t> indexById;
};

ActionRegistry::ActionRegistry() : impl_(std::make_unique<Impl>()) {}
ActionRegistry::~ActionRegistry() = default;
ActionRegistry::ActionRegistry(ActionRegistry&&) noexcept = default;
ActionRegistry& ActionRegistry::operator=(ActionRegistry&&) noexcept = default;

void ActionRegistry::registerAction(
    CommandId command_id, ActionHandler handler, std::string description, std::string category, bool repeatable) {
    unregisterAction(command_id);
    impl_->indexById[command_id] = impl_->actions.size();
    impl_->actions.push_back(Action{
        .commandId = command_id,
        .handler = std::move(handler),
        .description = std::move(description),
        .category = std::move(category),
        .repeatable = repeatable,
    });
}

bool ActionRegistry::unregisterAction(CommandId command_id) {
    const auto it = impl_->indexById.find(command_id);
    if (it == impl_->indexById.end()) {
        return false;
    }

    impl_->actions.erase(impl_->actions.begin() + static_cast<std::ptrdiff_t>(it->second));
    impl_->indexById.clear();
    for (std::size_t index = 0; index < impl_->actions.size(); ++index) {
        impl_->indexById[impl_->actions[index].commandId] = index;
    }
    return true;
}

bool ActionRegistry::execute(CommandId command_id, const ActionContext& ctx) const {
    const Action* action = find(command_id);
    if (action == nullptr || !action->handler) {
        return false;
    }

    ActionContext exec_ctx = ctx;
    if (!action->repeatable) {
        exec_ctx.count = 1;
    }

    action->handler(exec_ctx);
    return true;
}

const std::vector<Action>& ActionRegistry::actions() const {
    return impl_->actions;
}

const Action* ActionRegistry::find(CommandId command_id) const {
    const auto it = impl_->indexById.find(command_id);
    return it != impl_->indexById.end() ? &impl_->actions[it->second] : nullptr;
}

std::vector<const Action*> ActionRegistry::byCategory(const std::string& category) const {
    std::vector<const Action*> result;
    for (const auto& action : impl_->actions) {
        if (action.category == category) {
            result.push_back(std::addressof(action));
        }
    }
    return result;
}

struct KeyMap::Impl {
    std::vector<KeyBinding> bindings;

    // [[nodiscard]] static core::Result<std::vector<Key>> parseKeySequence(std::string_view keys) {
    //     std::vector<Key> sequence;
    //     std::size_t pos = 0;

    //     while (pos < keys.size()) {
    //         while (pos < keys.size() && keys[pos] == ' ') {
    //             ++pos;
    //         }
    //         if (pos >= keys.size()) {
    //             break;
    //         }

    //         std::size_t end = pos;
    //         if (keys[pos] == '<') {
    //             end = keys.find('>', pos);
    //             if (end == std::string_view::npos) {
    //                 return core::make_error(core::ErrorCategory::InvalidArgument,
    //                                         std::format("Unmatched '<' in key sequence '{}'", keys));
    //             }
    //             ++end;
    //         } else {
    //             while (end + 1 < keys.size() && keys[end + 1] == '-' &&
    //                    (keys[end] == 'C' || keys[end] == 'c' || keys[end] == 'M' || keys[end] == 'm' ||
    //                     keys[end] == 'A' || keys[end] == 'a' || keys[end] == 'S' || keys[end] == 's' ||
    //                     keys[end] == 'W' || keys[end] == 'w')) {
    //                 end += 2;
    //             }

    //             if (end < keys.size()) {
    //                 if (keys[end] == '<') {
    //                     const std::size_t close_pos = keys.find('>', end);
    //                     if (close_pos == std::string_view::npos) {
    //                         return core::make_error(core::ErrorCategory::InvalidArgument,
    //                                                 "Unclosed '<' in key sequence");
    //                     }
    //                     end = close_pos + 1;
    //                 } else {
    //                     ++end;
    //                 }
    //             }
    //         }

    //         auto key_result = parse_key(keys.substr(pos, end - pos));
    //         if (!key_result) {
    //             return std::unexpected(key_result.error());
    //         }
    //         sequence.push_back(*key_result);
    //         pos = end;
    //     }

    //     if (sequence.empty()) {
    //         return core::make_error(core::ErrorCategory::InvalidArgument, "Empty key sequence");
    //     }

    //     return sequence;
    // }
    [[nodiscard]] static core::Result<std::string_view> extractNextToken(std::string_view keys) {
        constexpr auto kIsModifier = [](char c) { return std::string_view("CcMmAaSsWw").contains(c); };
        std::size_t len = 0;

        if (keys.front() == '<') {
            len = keys.find('>');
            if (len == std::string_view::npos) {
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        std::format("Unmatched '<' in key sequence '{}'", keys));
            }
            return keys.substr(0, len + 1);
        }

        while (len + 1 < keys.size() && keys[len + 1] == '-' && kIsModifier(keys[len])) {
            len += 2;
        }

        if (len < keys.size()) {
            if (keys[len] == '<') {
                const std::size_t close_pos = keys.find('>', len);
                if (close_pos == std::string_view::npos) {
                    return core::make_error(core::ErrorCategory::InvalidArgument, "Unclosed '<' in key sequence");
                }
                len = close_pos + 1;
            } else {
                ++len;
            }
        }

        return keys.substr(0, len);
    }

    [[nodiscard]] static core::Result<std::vector<Key>> parseKeySequence(std::string_view keys) {
        std::vector<Key> sequence;

        while (true) {
            const std::size_t first_non_space = keys.find_first_not_of(' ');
            if (first_non_space == std::string_view::npos) {
                break;
            }
            keys.remove_prefix(first_non_space);

            auto token_result = Impl::extractNextToken(keys);
            if (!token_result) {
                return std::unexpected(token_result.error());
            }

            const std::string_view token = *token_result;
            keys.remove_prefix(token.size());

            auto key_result = parse_key(token);
            if (!key_result) {
                return std::unexpected(key_result.error());
            }
            sequence.push_back(*key_result);
        }
        if (sequence.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument, "Empty key sequence");
        }

        return sequence;
    }
};

KeyMap::KeyMap() : impl_(std::make_unique<Impl>()) {}
KeyMap::~KeyMap() = default;
KeyMap::KeyMap(KeyMap&&) noexcept = default;
KeyMap& KeyMap::operator=(KeyMap&&) noexcept = default;

core::VoidResult KeyMap::bind(std::string_view keys, CommandId command_id, Mode mode, std::string description) {
    auto sequence_result = KeyMap::Impl::parseKeySequence(keys);
    if (!sequence_result) {
        return std::unexpected(sequence_result.error());
    }

    auto sequence = std::move(*sequence_result);
    auto& bindings = impl_->bindings;
    auto remove_it = rng::remove_if(
        bindings, [&](const KeyBinding& binding) { return binding.mode == mode && binding.sequence == sequence; });
    bindings.erase(remove_it.begin(), bindings.end());

    bindings.push_back(KeyBinding{
        .sequence = std::move(sequence),
        .commandId = command_id,
        .mode = mode,
        .description = std::move(description),
    });
    return {};
}

bool KeyMap::unbind(std::string_view keys, Mode mode) {
    auto sequence_result = KeyMap::Impl::parseKeySequence(keys);
    if (!sequence_result) {
        return false;
    }

    auto& bindings = impl_->bindings;
    const auto it = rng::find_if(bindings, [&](const KeyBinding& binding) {
        return binding.mode == mode && binding.sequence == *sequence_result;
    });
    if (it == bindings.end()) {
        return false;
    }

    bindings.erase(it);
    return true;
}

core::Result<KeyLoadReport> KeyMap::loadFromFile(const std::filesystem::path& path,
                                                 const CommandResolver& resolve_command) {
    toml::parse_result result = toml::parse_file(path.string());

    if (!result) {
        return core::make_error(core::ErrorCategory::Config,
                                std::format("Failed to parse keybinding config '{}': {}", path.string(), result.error().description()));
    }
    toml::table table = std::move(result).table();
    // try {
    //     table = toml::parse_file(path.string());
    // } catch (const toml::parse_error& error) {
    //     return core::make_error(core::ErrorCategory::Config, std::format("Failed to parse keybinding config '{}': {}",
    //                                                                      path.string(), error.description()));
    // }

    KeyLoadReport report;
    auto* keys = table["keys"].as_table();
    if (keys == nullptr) {
        return report;
    }

    for (auto&& [mode_name, mode_value] : *keys) {
        const auto mode = parse_mode(mode_name);
        if (!mode) {
            report.warnings.push_back(
                KeyLoadWarning{std::format("Skipped unknown keybinding mode '{}'", std::string{mode_name})});
            continue;
        }

        auto* bindings = mode_value.as_table();
        if (bindings == nullptr) {
            report.warnings.push_back(
                KeyLoadWarning{std::format("Skipped non-table keybinding mode '{}'", std::string{mode_name})});
            continue;
        }

        for (auto&& [command_name, key_value] : *bindings) {
            const auto key_string = key_value.value<std::string>();
            if (!key_string) {
                report.warnings.push_back(
                    KeyLoadWarning{std::format("Skipped binding '{}' in mode '{}' because the key is not a string",
                                               std::string{command_name}, std::string{mode_name})});
                continue;
            }

            const auto command_id = resolve_command(command_name);
            if (!command_id.has_value()) {
                report.warnings.push_back(KeyLoadWarning{std::format("Skipped binding '{}' -> '{}': unknown command",
                                                                     *key_string, std::string{command_name})});
                continue;
            }

            KeyLoadEntry entry{
                .keys = *key_string,
                .commandName = std::string{command_name},
                .mode = *mode,
                .description = {},
            };
            auto bind_result = bind(entry.keys, *command_id, entry.mode, entry.description);
            if (!bind_result) {
                report.warnings.push_back(
                    KeyLoadWarning{std::format("Skipped binding '{}' -> '{}' in mode '{}': {}", entry.keys,
                                               entry.commandName, mode_to_name(*mode), bind_result.error().message())});
                continue;
            }

            report.loadedBindings.push_back(std::move(entry));
        }
    }

    return report;
}

const std::vector<KeyBinding>& KeyMap::bindings() const {
    return impl_->bindings;
}

std::vector<const KeyBinding*> KeyMap::bindingsForMode(Mode mode) const {
    std::vector<const KeyBinding*> result;
    for (const auto& binding : impl_->bindings) {
        if (binding.mode == mode) {
            result.push_back(std::addressof(binding));
        }
    }
    return result;
}

const KeyBinding* KeyMap::findExact(const std::vector<Key>& sequence, Mode mode) const {
    const auto it = rng::find_if(impl_->bindings, [&](const KeyBinding& binding) {
        return binding.mode == mode && binding.sequence == sequence;
    });
    return it != impl_->bindings.end() ? std::addressof(*it) : nullptr;
}

bool KeyMap::isPrefix(const std::vector<Key>& sequence, Mode mode) const {
    return rng::any_of(impl_->bindings, [&](const KeyBinding& binding) {
        if (binding.mode != mode || binding.sequence.size() <= sequence.size()) {
            return false;
        }
        return std::equal(sequence.begin(), sequence.end(), binding.sequence.begin());
    });
}

void KeyMap::clear() {
    impl_->bindings.clear();
}

struct KeyHandler::Impl {
    ActionRegistry actions;
    KeyMap keymap;
    std::vector<Key> buffer;
    int numericPrefix{0};
    bool hasNumericPrefix{false};
    Mode currentMode{Mode::Normal};
    int timeoutMs{1000};
    std::chrono::steady_clock::time_point lastKeyTime;

    void reset() {
        buffer.clear();
        numericPrefix = 0;
        hasNumericPrefix = false;
    }

    [[nodiscard]] std::string bufferToString() const {
        return std::accumulate(buffer.begin(), buffer.end(),
                               hasNumericPrefix ? std::to_string(numericPrefix) : std::string{},
                               [](const std::string& acc, const Key& key) { return acc + key_to_string(key); });
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
    const auto key = event_to_key(event);
    if (!key) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - impl_->lastKeyTime).count();
    if (elapsed > impl_->timeoutMs && !impl_->buffer.empty()) {
        impl_->reset();
    }
    impl_->lastKeyTime = now;

    if (key->isCharacter() && key->modifiers == Modifier::None && std::isdigit(key->character) != 0 &&
        impl_->buffer.empty()) {
        if (key->character != '0' || impl_->hasNumericPrefix) {
            impl_->numericPrefix = (impl_->numericPrefix *
                                    10) +  // NOLINT(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
                                   (key->character - '0');
            impl_->hasNumericPrefix = true;
            return true;
        }
    }

    impl_->buffer.push_back(*key);
    if (const auto* binding = impl_->keymap.findExact(impl_->buffer, impl_->currentMode)) {
        const int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
        const bool executed = impl_->actions.execute(
            binding->commandId, ActionContext{.count = count, .currentMode = impl_->currentMode, .argument = {}});
        impl_->reset();
        return executed;
    }

    if (impl_->keymap.isPrefix(impl_->buffer, impl_->currentMode)) {
        return true;
    }

    impl_->reset();
    return false;
}

bool KeyHandler::flush() {
    if (const auto* binding = impl_->keymap.findExact(impl_->buffer, impl_->currentMode)) {
        const int count = impl_->hasNumericPrefix ? impl_->numericPrefix : 1;
        const bool executed = impl_->actions.execute(
            binding->commandId, ActionContext{.count = count, .currentMode = impl_->currentMode, .argument = {}});
        impl_->reset();
        return executed;
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

void KeyHandler::setTimeout(int timeout_ms) {
    impl_->timeoutMs = timeout_ms;
}

}  // namespace expp::ui
