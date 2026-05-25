/**
 * @file cli.hpp
 * @brief Single-header, modern C++23 command-line argument parser.
 *
 * ## Design Overview
 *
 * The library follows a layered architecture with clear separation between
 * tokenization, parsing, validation, and rendering. Each layer operates on
 * well-defined data structures so every stage can be tested independently.
 *
 * ### Architecture Layers
 *
 * ```
 *   Raw argv ──► Tokenizer ──► TokenStream ──► Parser ──► ParsedCommand
 *                                                    │
 *                                                    ▼
 *                                          Validator::validate()
 *                                                    │
 *                                                    ▼
 *                                          Command::execute()
 * ```
 *
 *   - **Tokenizer** (lexer): Converts raw `argv` strings into typed `Token`
 *     objects. Handles `--long`, `-s`, `-abc` combined shorts, `--option=value`,
 *     `-o=value`, `--` double-dash terminator, negative numbers, and help flags.
 *   - **TokenStream**: Provides sequential iteration over tokens with
 *     peek/consume/backtrack semantics. Keeps ownership of the token buffer.
 *   - **Parser**: Walks the token stream against a `Command` definition tree,
 *     resolving long/short option names into `ParsedOptions` (flags, values,
 *     positionals) and navigating subcommands. Applies default values after
 *     parsing is complete.
 *   - **Validator**: Post-parse structural checks: required options present,
 *     required positional arguments count satisfied, no unexpected arguments.
 *   - **HelpGenerator**: Renders formatted help text from a `Command`
 *     definition, with optional ANSI color support.
 *   - **CLI**: Top-level orchestrator that wires parsing, validation, help/version
 *     handling, and command execution into a single `run()` entry point.
 *
 * ### Data Flow
 *
 * | Step | Input | Output |
 * |------|-------|--------|
 * | Tokenize | `span<string_view>` | `Result<vector<Token>>` |
 * | Parse | `vector<Token>`, `Command` | `Result<ParsedCommand>` |
 * | Validate | `ParsedCommand`, `Command` | `VoidResult` |
 * | Execute | `ParsedCommand` | `VoidResult` (handler return) |
 *
 * ### Error Handling
 *
 * All fallible operations return `Result<T>` (alias for `std::expected<T,
 *ParseError>`). The `ParseError` type carries an `ErrorCode`, a human-readable
 * message, a severity level, and `std::source_location` for diagnostics. Errors
 * are propagated via `std::unexpected` and never thrown as exceptions.
 *
 * ### Fluent Builder Pattern
 *
 * `Option`, `Argument`, `Command`, and `CLI` all use a fluent builder API
 * powered by C++23 deducing `this`. This allows chaining configuration
 * calls in a natural, readable order:
 *
 * @code
 * auto cmd = Command::create("build")
 *     .description("Build the project")
 *     .option(Option::withName("jobs", 'j')
 *         .setTakesValue()
 *         .defaultValue(1)
 *         .description("Number of parallel jobs"))
 *     .argument(Argument::create("target").description("Build target"))
 *     .handler([](const ParsedCommand& cmd) -> VoidResult {
 *         // handle the command
 *         return ok();
 *     });
 * @endcode
 *
 * ### Usage Example — Simple CLI
 *
 * @code
 * auto cli = CLI::create("myapp")
 *     .description("Sample application")
 *     .option(Option::withLong("verbose").description("Verbose output"))
 *     .handler([](const ParsedCommand& cmd) -> VoidResult {
 *         if (cmd.options.isFlag("verbose")) {
 *             std::println("Verbose mode enabled");
 *         }
 *         return ok();
 *     });
 *
 * int main(int argc, char* argv[]) {
 *     std::vector<std::string_view> args(argv, argv + argc);
 *     return cli.run(args);
 * }
 * @endcode
 *
 * ### Usage Example — Subcommands
 *
 * @code
 * auto cli = CLI::create("tool")
 *     .description("Development tool")
 *     .command(Command::create("build")
 *         .description("Build targets")
 *         .option(Option::withName("release", 'r').description("Release mode"))
 *         .handler([](const ParsedCommand& cmd) -> VoidResult {
 *             // build logic
 *             return ok();
 *         }))
 *     .command(Command::create("test")
 *         .description("Run tests")
 *         .handler([](const ParsedCommand& cmd) -> VoidResult {
 *             // test logic
 *             return ok();
 *         }));
 *
 * // $ tool build --release
 * // $ tool test
 * // $ tool --help   (auto-generated help)
 * @endcode
 *
 * ### Extension Points
 *
 * - Custom option validation via `Option::validator()` for domain-specific
 *   checks beyond the built-in `choices()` constraint.
 * - Custom type parsing via specialization or the `>>` stream operator fallback
 *   in `ValueParser<T>`.
 * - Help output customization via `HelpConfig` (colors, widths, indentation).
 * - Use concept to formulate constraints on handler signatures.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_APP_CLI_HPP
#define EXPP_APP_CLI_HPP

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
    #define isatty        _isatty
    #define STDOUT_FILENO _fileno(stdout)
#else
    #include <unistd.h>
#endif

namespace expp::app::cli {
namespace rng = std::ranges;
namespace views = std::views;

/**
 * @brief Internal implementation details. Not part of the public API.
 */
namespace details {
/**
 * @brief Transparent string hash functor enabling heterogeneous lookup in
 * unordered containers with `std::string_view` keys.
 */
struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
};
}  // namespace details

/**
 * @brief Error types and result aliases used throughout the CLI library.
 *
 * All fallible operations return `Result<T>` (a `std::expected<T, ParseError>`)
 * so that errors propagate without exceptions. The `ok()` and `err()` helpers
 * reduce boilerplate when constructing return values.
 */
namespace cli_error {

/**
 * @brief Severity level attached to a `ParseError`.
 */
enum class ErrorSeverity : std::uint8_t {
    Warning,  ///< Non-fatal; the operation may still succeed.
    Error,    ///< Standard failure; the operation cannot continue.
    Fatal     ///< Unrecoverable; the entire program should exit.
};

/**
 * @brief Machine-readable error category for diagnostics and testing.
 *
 * Codes are grouped by processing stage:
 *   - **Tokenization** (`UnknownOption`, `MissingOptionArgument`,
 *     `InvalidOptionFormat`) — detected during lexing.
 *   - **Parsing** (`UnexpectedArgument`, `MissingRequiredOption`,
 *     `MissingRequiredArgument`, `UnknownCommand`, `AmbiguousCommand`)
 *     — detected while walking the command tree.
 *   - **Validation** (`InvalidValue`, `ValueOutOfRange`,
 *     `ConstraintViolation`) — value-level checks (choices, ranges,
 *     custom validators).
 *   - **Command dispatch** (`CommandNotFound`, `NoCommandProvided`).
 *   - **Internal** (`InternalError`) — logic errors or unexpected states.
 */
enum class ErrorCode : std::uint8_t {
    None = 0,

    // Tokenization errors
    UnknownOption,          ///< An option name was not found in the command definition.
    MissingOptionArgument,  ///< An option that requires a value was not given one.
    InvalidOptionFormat,    ///< Malformed option string (e.g. bare `-`).

    // Parsing errors
    UnexpectedArgument,       ///< More positional arguments than the command accepts.
    MissingRequiredOption,    ///< A required option was not provided.
    MissingRequiredArgument,  ///< A required positional argument was not provided.
    UnknownCommand,           ///< The requested subcommand does not exist.
    AmbiguousCommand,         ///< The subcommand name matches more than one command.

    // Validation errors
    InvalidValue,         ///< A value failed to parse or is not in the allowed choices.
    ValueOutOfRange,      ///< A numeric value is outside the allowed range.
    ConstraintViolation,  ///< A custom validator rejected the value.

    // Command errors
    CommandNotFound,    ///< No command matched the given name.
    NoCommandProvided,  ///< A subcommand was required but none was given.

    // Internal errors
    InternalError  ///< Logic error or unexpected internal state.
};

/**
 * @brief Structured parsing error carrying code, message, severity, and source
 * location.
 *
 * Designed to be returned via `std::expected` (`Result<T>`) so that callers
 * can inspect and format errors without exception overhead.
 */
class ParseError {
public:
    /**
     * @brief Constructs a ParseError.
     * @param code Machine-readable error category.
     * @param message Human-readable description of the failure.
     * @param severity Severity level (default: Error).
     * @param location Source location captured at the call site.
     */
    constexpr ParseError(ErrorCode code,
                         std::string message,
                         ErrorSeverity severity = ErrorSeverity::Error,
                         std::source_location location = std::source_location::current())
        : code_(code)
        , message_(std::move(message))
        , severity_(severity)
        , location_(location) {}

    /// @return The error category code.
    [[nodiscard]] constexpr ErrorCode code() const noexcept { return code_; }

    /// @return The human-readable error description.
    [[nodiscard]] constexpr const std::string& message() const noexcept { return message_; }

    /// @return The severity level of this error.
    [[nodiscard]] constexpr ErrorSeverity severity() const noexcept { return severity_; }

    /// @return The source location where this error was constructed.
    [[nodiscard]] constexpr const std::source_location& location() const noexcept {
        return location_;
    }

    /**
     * @brief Formats the error as: `Severity: message (at file:line)`.
     * @return Formatted diagnostic string suitable for stderr output.
     */
    [[nodiscard]] std::string format() const {
        return std::format("{}: {} (at {}:{})", severityString(), message_, location_.file_name(),
                           location_.line());
    }

private:
    [[nodiscard]] constexpr std::string_view severityString() const noexcept {
        switch (severity_) {
            case ErrorSeverity::Warning:
                return "Warning";
            case ErrorSeverity::Error:
                return "Error";
            case ErrorSeverity::Fatal:
                return "Fatal";
            default:
                return "Unknown";
        }
    }

    ErrorCode code_;
    std::string message_;
    ErrorSeverity severity_;
    std::source_location location_;
};

/**
 * @brief Generic result type for operations that can fail.
 * @tparam T The success value type.
 */
template <typename T>
using Result = std::expected<T, ParseError>;

/**
 * @brief Result type for operations with no return value on success.
 */
using VoidResult = Result<void>;

/**
 * @brief Creates a successful `Result<T>` containing the given value.
 * @tparam T The value type (deduced from the argument).
 * @param value The success value to wrap.
 * @return A `Result` in the success state.
 */
template <typename T>
[[nodiscard]] Result<std::remove_cvref_t<T>> ok(T&& value) {
    return std::expected<std::remove_cvref_t<T>, ParseError>(std::in_place, std::forward<T>(value));
}

/**
 * @brief Creates a successful `VoidResult` (no value).
 */
[[nodiscard]] inline VoidResult ok() {
    return {};
}

/**
 * @brief Creates an error result from the given code and message.
 * @param code Machine-readable error category.
 * @param msg Human-readable error description.
 * @param severity Severity level (default: Error).
 * @param location Source location captured at the call site.
 * @return An `std::unexpected<ParseError>` suitable for `Result<T>`.
 */
[[nodiscard]] inline auto err(ErrorCode code,
                              std::string msg,
                              ErrorSeverity severity = ErrorSeverity::Error,
                              std::source_location location = std::source_location::current()) {
    return std::unexpected(ParseError(code, std::move(msg), severity, location));
}
}  // namespace cli_error

/**
 * @brief Defines a command-line option (flag or value-taking).
 *
 * Supports both long (`--option`) and short (`-o`) names, aliases, default
 * values, required/optional semantics, constrained choices, and arbitrary
 * custom validators.
 *
 * Instances are created through static factory methods and configured through
 * a fluent builder API powered by C++23 deducing `this`.
 */
class Option {
public:
    /**
     * @brief Validator function type: receives the option value as a string
     * and returns `VoidResult` (success) or a `ParseError`.
     */
    using ValueValidator = std::function<cli_error::VoidResult(std::string_view)>;

    /**
     * @brief Creates an option identified by a long name only.
     * @param long_name Long option name without the `--` prefix (e.g. `"verbose"`).
     */
    constexpr static Option withLong(std::string long_name) noexcept {
        Option opt;
        opt.longName_ = std::move(long_name);
        return opt;
    }

    /**
     * @brief Creates an option identified by a short character only.
     * @param short_name Short option character (e.g. `'v'` for `-v`).
     */
    static constexpr Option withShort(char short_name) noexcept {
        Option opt;
        opt.shortName_ = short_name;
        return opt;
    }

    /**
     * @brief Creates an option with both long and short identifiers.
     * @param long_name Long option name (e.g. `"verbose"` for `--verbose`).
     * @param short_name Short option character (e.g. `'v'` for `-v`).
     */
    static constexpr Option withName(std::string long_name, char short_name) noexcept {
        Option opt;
        opt.longName_ = std::move(long_name);
        opt.shortName_ = short_name;
        return opt;
    }

    // --- Configuration (fluent builder) ---

    /**
     * @brief Sets the human-readable description shown in help output.
     * @param desc Description text.
     */
    template <typename Self>
    constexpr Self&& description(this Self&& self, std::string desc) noexcept {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets a default value applied when the option is not given on the
     * command line.
     * @tparam T Value type (must be formattable via `std::format`).
     * @param value The default value.
     */
    template <typename Self, typename T>
    constexpr Self&& defaultValue(this Self&& self, T value) noexcept {
        self.defaultValueStr_ = std::format("{}", value);
        self.hasDefault_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Marks this option as required. Validation fails if it is not
     * present on the command line.
     */
    template <typename Self>
    constexpr Self&& required(this Self&& self) noexcept {
        self.required_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Marks this option as taking a value argument (as opposed to a
     * boolean flag).
     */
    template <typename Self>
    constexpr Self&& setTakesValue(this Self&& self) noexcept {
        self.takesValue_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets the placeholder name for the option's value in help output.
     * @param name Placeholder text (e.g. `"FILE"` produces `--output <FILE>`).
     */
    template <typename Self>
    constexpr Self&& valueName(this Self&& self, std::string name) noexcept {
        self.valueName_ = std::move(name);
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds an alternative long-form name for this option.
     * @param name Alias name (e.g. `"out"` so `--out` works the same as
     * `--output`).
     *
     * @note Multiple aliases can be added by chaining calls to `alias()`.
     */
    template <typename Self>
    constexpr Self&& alias(this Self&& self, std::string name) noexcept {
        self.aliases_.push_back(std::move(name));
        return std::forward<Self>(self);
    }

    /**
     * @brief Restricts the allowed values to a fixed set.
     * @param valid_values The set of acceptable values.
     *
     * Validation during parsing rejects any value not in this list.
     */
    template <typename Self>
    constexpr Self&& choices(this Self&& self, std::vector<std::string> valid_values) noexcept {
        self.choices_ = std::move(valid_values);
        self.hasChoices_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Registers a custom validation function.
     * @param fn A callable taking `std::string_view` and returning `VoidResult`.
     *
     * EXTENSION POINT: add domain-specific validation (e.g. file existence,
     * URL format) without changing the parser core. Multiple validators can
     * be stacked; all must pass for the value to be accepted.
     */
    template <typename Self>
    constexpr Self&& validator(this Self&& self, ValueValidator fn) noexcept {
        self.validators_.push_back(std::move(fn));
        return std::forward<Self>(self);
    }

    // --- Accessors ---

    /// @return The long option name (empty if only a short name exists).
    [[nodiscard]] constexpr const std::string& longName() const noexcept { return longName_; }

    /// @return The short option character, if set.
    [[nodiscard]] constexpr std::optional<char> shortName() const noexcept { return shortName_; }

    /// @return The description text for help output.
    [[nodiscard]] constexpr const std::string& description() const noexcept { return description_; }

    /// @return `true` if this option expects a value argument.
    [[nodiscard]] constexpr bool needsValue() const noexcept { return takesValue_; }

    /// @return `true` if this option must be present on the command line.
    [[nodiscard]] constexpr bool isRequired() const noexcept { return required_; }

    /// @return `true` if a default value has been set.
    [[nodiscard]] constexpr bool hasDefault() const noexcept { return hasDefault_; }

    /// @return The default value as a string (empty if no default).
    [[nodiscard]] constexpr const std::string& defaultValueStr() const noexcept {
        return defaultValueStr_;
    }

    /// @return The value placeholder name for help output.
    [[nodiscard]] constexpr const std::string& valueName() const noexcept { return valueName_; }

    /// @return The list of alias names for this option.
    [[nodiscard]] constexpr const std::vector<std::string>& aliases() const noexcept {
        return aliases_;
    }

    /// @return `true` if this option has a constrained set of valid values.
    [[nodiscard]] constexpr bool hasChoices() const noexcept { return hasChoices_; }

    /// @return The list of allowed values (empty if no constraint).
    [[nodiscard]] constexpr const std::vector<std::string>& choices() const noexcept {
        return choices_;
    }

    /// @return The list of registered custom validator functions.
    [[nodiscard]] constexpr const std::vector<ValueValidator>& validators() const noexcept {
        return validators_;
    }

    /**
     * @brief Checks whether the given long-form name matches this option.
     * @param name Long option name (without `--` prefix).
     * @return `true` if the name matches either the primary long name or any
     * alias.
     */
    [[nodiscard]] constexpr bool matches(std::string_view name) const noexcept {
        return name == longName_ ||
               rng::any_of(aliases_, [&](auto& alias) { return alias == name; });
    }

    /**
     * @brief Checks whether the given short character matches this option.
     * @param short_name Short option character.
     * @return `true` if this option has a short name and it matches.
     */
    [[nodiscard]] constexpr bool matchesShort(char short_name) const noexcept {
        return shortName_ && *shortName_ == short_name;
    }

    /**
     * @brief Builds a human-readable display name for use in help output.
     * @return String like `"-v, --verbose"`, `"--verbose"`, or `"-v"`.
     */
    [[nodiscard]] constexpr std::string displayName() const noexcept {
        std::string result;
        if (shortName_) {
            result = std::format("-{}", *shortName_);
            if (!longName_.empty()) {
                result += std::format(", --{}", longName_);
            }
        } else if (!longName_.empty()) {
            result = std::format("--{}", longName_);
        }
        return result;
    }

    /**
     * @brief Builds a usage placeholder for help output, including the value
     * placeholder if applicable.
     * @return String like `"--output <FILE>"` or `"--verbose"`.
     */
    [[nodiscard]] constexpr std::string usageString() const noexcept {
        std::string result = displayName();
        if (takesValue_) {
            result += std::format(" <{}>", valueName_.empty() ? "value" : valueName_);
        }
        return result;
    }

private:
    Option() = default;

    std::string longName_;
    std::optional<char> shortName_;
    std::string description_;
    std::string valueName_;
    std::string defaultValueStr_;
    std::vector<std::string> aliases_;
    std::vector<std::string> choices_;
    std::vector<ValueValidator> validators_;
    bool takesValue_ = false;
    bool required_ = false;
    bool hasDefault_ = false;
    bool hasChoices_ = false;
};

/**
 * @brief Defines a positional command-line argument.
 *
 * Positional arguments are consumed in the order they are defined on a
 * `Command`. They can be required, optional, or variadic (accepting zero
 * or more values). At most one variadic argument is allowed per command,
 * and it must be the last argument defined.
 */
class Argument {
public:
    /**
     * @brief Creates a new positional argument with the given name.
     * @param name Argument name shown in help/usage output (e.g. `"file"`).
     */
    static constexpr Argument create(std::string name) noexcept {
        Argument arg;
        arg.name_ = std::move(name);
        return arg;
    }

    /**
     * @brief Sets the description text for help output.
     */
    template <typename Self>
    constexpr Self&& description(this Self&& self, std::string desc) noexcept {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    /**
     * @brief Marks this argument as required (the default).
     */
    template <typename Self>
    constexpr Self&& required(this Self&& self) noexcept {
        self.required_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Marks this argument as optional.
     */
    template <typename Self>
    constexpr Self&& optional(this Self&& self) noexcept {
        self.required_ = false;
        return std::forward<Self>(self);
    }

    /**
     * @brief Marks this argument as variadic — it consumes all remaining
     * positional arguments. Only one variadic argument is allowed per
     * command, and it must be the last argument in the definition.
     */
    template <typename Self>
    constexpr Self&& variadic(this Self&& self) noexcept {
        self.variadic_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets a default value applied when no positional value is provided.
     */
    template <typename Self, typename T>
    constexpr Self&& defaultValue(this Self&& self, T value) noexcept {
        self.defaultValueStr_ = std::format("{}", value);
        self.hasDefault_ = true;
        return std::forward<Self>(self);
    }

    /// @return The argument name.
    [[nodiscard]] constexpr const std::string& name() const noexcept { return name_; }

    /// @return The description text.
    [[nodiscard]] constexpr const std::string& description() const noexcept { return description_; }

    /// @return `true` if this argument is required.
    [[nodiscard]] constexpr bool isRequired() const noexcept { return required_; }

    /// @return `true` if this argument is variadic.
    [[nodiscard]] constexpr bool isVariadic() const noexcept { return variadic_; }

    /// @return `true` if a default value is set.
    [[nodiscard]] constexpr bool hasDefault() const noexcept { return hasDefault_; }

    /// @return The default value as a string.
    [[nodiscard]] constexpr const std::string& defaultValueStr() const noexcept {
        return defaultValueStr_;
    }

    /**
     * @brief Builds a usage placeholder string.
     * @return `<name>` for required, `[<name>]` for optional,
     * `[<name>...]` for variadic.
     */
    [[nodiscard]] constexpr std::string usageString() const noexcept {
        if (variadic_) {
            return std::format("[<{}>...]", name_);
        }
        if (required_) {
            return std::format("<{}>", name_);
        }
        return std::format("[<{}>]", name_);
    }

private:
    Argument() = default;

    std::string name_;
    std::string description_;
    std::string defaultValueStr_;
    bool required_ = true;
    bool variadic_ = false;
    bool hasDefault_ = false;
};

/**
 * @brief Compile-time value parser dispatching on the target type.
 *
 * Provides `static parse(string_view) -> Result<T>` with built-in support for:
 *   - `std::string` (identity)
 *   - `bool` (`true`/`false`, `1`/`0`, `yes`/`no`, `on`/`off`)
 *   - Integral types via `std::from_chars`
 *   - Floating-point types via `std::from_chars`
 *   - Custom types via `operator>>(istream&, T&)` fallback
 *
 * The dispatch uses `if constexpr` so only the matching branch is instantiated.
 *
 * @tparam T The target value type.
 */
template <typename T>
struct ValueParser {
    /**
     * @brief Parses a string into a value of type T.
     * @param str The raw string to parse.
     * @return The parsed value on success, or a `ParseError` on failure.
     */
    static constexpr cli_error::Result<T> parse(std::string_view str) {
        if constexpr (std::is_same_v<T, std::string>) {
            return cli_error::ok(std::string(str));
        } else if constexpr (std::is_same_v<T, bool>) {
            if (str == "true" || str == "1" || str == "yes" || str == "on") {
                return cli_error::ok(true);
            }
            if (str == "false" || str == "0" || str == "no" || str == "off") {
                return cli_error::ok(false);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Invalid boolean value: '{}'", str));
        } else if constexpr (std::is_integral_v<T>) {
            T value{};
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);  // NOLINT
            if (ec == std::errc() && ptr == str.data() + str.size()) {                     // NOLINT
                return cli_error::ok(value);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Invalid integer value: '{}'", str));
        } else if constexpr (std::is_floating_point_v<T>) {
            T value{};
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);  // NOLINT
            if (ec == std::errc() && ptr == str.data() + str.size()) {                     // NOLINT
                return cli_error::ok(value);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Invalid floating point value: '{}'", str));
        } else {
            // Fallback: try the stream extraction operator
            std::string tmp(str);
            std::istringstream iss(tmp);
            T value;
            if (iss >> value && iss.eof()) {
                return cli_error::ok(value);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Cannot parse value: '{}'", str));
        }
    }
};

/**
 * @brief Container for parsed option values, flags, and positional arguments.
 *
 * Stores all option values as raw strings and converts on demand via
 * `get<T>()` / `getOr<T>()`, which delegate to `ValueParser<T>::parse()`.
 * Flags (options without values) are tracked separately in a set.
 *
 * Uses unordered containers with transparent hashing so that lookups by
 * `std::string_view` avoid constructing temporary `std::string` objects.
 */
class ParsedOptions {
public:
    /**
     * @brief Checks whether an option with the given name is present.
     * @param name The option's canonical long name.
     */
    [[nodiscard]] constexpr bool has(std::string_view name) const noexcept {
        return options_.contains(name);
    }

    /**
     * @brief Retrieves a typed value for the given option.
     * @tparam T The desired type (must be supported by `ValueParser<T>`).
     * @param name The option's canonical name.
     * @return The converted value, or `std::nullopt` if the option is not
     * present or the value fails to parse.
     */
    template <typename T>
    [[nodiscard]] constexpr std::optional<T> get(std::string_view name) const noexcept {
        auto it = options_.find(name);
        if (it == options_.end()) {
            return std::nullopt;
        }

        auto result = ValueParser<T>::parse(it->second);
        if (result) {
            return *result;
        }
        return std::nullopt;
    }

    /**
     * @brief Retrieves a typed value, falling back to a default if absent.
     * @tparam T The desired type.
     * @param name The option's canonical name.
     * @param default_value The fallback value.
     * @return The parsed value or `default_value`.
     */
    template <typename T>
    [[nodiscard]] constexpr T getOr(std::string_view name, T default_value) const noexcept {
        auto value = get<T>(name);
        return value.value_or(std::move(default_value));
    }

    /**
     * @brief Stores a named option value (overwrites if already present).
     */
    void set(std::string name, std::string value) { options_[std::move(name)] = std::move(value); }

    /**
     * @brief Records a flag option as present.
     */
    void setFlag(std::string name) { flags_.insert(std::move(name)); }

    /**
     * @brief Checks whether a flag option is set.
     */
    [[nodiscard]] constexpr bool isFlag(std::string_view name) const noexcept {
        return flags_.contains(name);
    }

    /**
     * @brief Returns the list of positional arguments in order.
     */
    [[nodiscard]] constexpr const std::vector<std::string>& positional() const noexcept {
        return positional_;
    }

    /**
     * @brief Appends a positional argument.
     */
    void addPositional(std::string value) { positional_.push_back(std::move(value)); }

    /**
     * @brief Retrieves the raw string value of an option, without type parsing.
     * @return The raw string or `std::nullopt` if not found.
     */
    [[nodiscard]] constexpr std::optional<std::string> rawOption(
        std::string_view name) const noexcept {
        auto it = options_.find(name);
        if (it == options_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    using MapType =
        std::unordered_map<std::string, std::string, details::StringHash, std::equal_to<>>;
    using SetType = std::unordered_set<std::string, details::StringHash, std::equal_to<>>;

    MapType options_;                      ///< Named option values (keyed by canonical name).
    SetType flags_;                        ///< Set of flag options that are present.
    std::vector<std::string> positional_;  ///< Positional arguments in order.
};

/**
 * @brief The result of parsing a command line against a `Command` tree.
 *
 * Contains the resolved leaf command name, the navigation path through nested
 * subcommands, all parsed options (flags and values), and any positional
 * arguments. Also tracks whether `--help` was requested.
 *
 * This is the primary data structure passed to `Command::execute()` handlers.
 */
struct ParsedCommand {
    /// Leaf command name (e.g. `"set"` when the path is `"tool"` -> `"config"` -> `"set"`).
    std::string name;
    /// Parsed options (flags, values) accessible by canonical name.
    ParsedOptions options;
    /// Positional arguments consumed by the leaf command.
    std::vector<std::string> positionalArgs;
    /// Ordered list of subcommand names from root to leaf (e.g. `["tool", "config", "set"]`).
    std::vector<std::string> commandPath;
    /// `true` if `--help` or `-h` was encountered during parsing.
    bool helpRequested = false;

    /**
     * @brief Builds a space-separated string of the fully qualified command path.
     * @return e.g. `"tool config set"` or just the leaf name if there is no path.
     */
    [[nodiscard]] std::string pathString() const {
        if (commandPath.empty()) {
            return name;
        }
        auto result = rng::fold_left(commandPath, std::string(),
                                     [](std::string acc, const std::string& part) {
                                         return std::move(acc) + (acc.empty() ? "" : " ") + part;
                                     });
        return result;
    }

    /**
     * @brief Appends a positional argument value.
     */
    void addPositional(std::string value) { positionalArgs.push_back(std::move(value)); }
};

/**
 * @brief Signature for command handler callbacks.
 *
 * Receives the full `ParsedCommand` (options, positionals, subcommand path)
 * and returns `VoidResult` to report success or failure.
 */
using CommandFn = std::function<cli_error::VoidResult(const ParsedCommand&)>;

/**
 * @brief Defines a command with its options, arguments, subcommands, and handler.
 *
 * `Command` is the central definition type in the CLI library. It forms a tree:
 * the root command holds global options and top-level subcommands; each
 * subcommand can define its own options, arguments, and nested subcommands.
 *
 * ### Option Registration
 *
 * When an option is added via `option()`, it is registered in both a linear
 * vector (for iteration/help) and in hash maps keyed by long name, aliases, and
 * short character (for O(1) lookup during parsing). Duplicate option names are
 * silently ignored to preserve the first definition.
 *
 * ### Argument Constraints
 *
 * At most one variadic argument is allowed per command, and it must be the last
 * argument. Attempting to add another argument after a variadic one is silently
 * ignored.
 *
 * ### Subcommand Ownership
 *
 * Subcommands are stored as `shared_ptr<Command>` in a hash map keyed by name.
 * This allows the root `CLI` object to own the entire command tree while
 * individual commands can be looked up by the parser.
 */
class Command {
public:
    /**
     * @brief Creates a new command with the given primary name.
     * @param name The command name used for invocation and help output.
     */
    static constexpr Command create(std::string name) noexcept {
        Command cmd;
        cmd.name_ = std::move(name);
        return cmd;
    }

    // --- Configuration (fluent builder) ---

    /**
     * @brief Sets a short description shown in help command listings.
     */
    template <typename Self>
    constexpr Self&& description(this Self&& self, std::string desc) noexcept {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets a longer, multi-line description shown after the short
     * description in full help output.
     */
    template <typename Self>
    constexpr Self&& longDescription(this Self&& self, std::string desc) noexcept {
        self.detailedDescription_ = std::move(desc);
        return std::forward<Self>(self);
    }

    /**
     * @brief Registers an option on this command.
     * @param opt An `Option` instance (typically built via the fluent API).
     *
     * Duplicate option names (long name, aliases, or short character) are
     * silently ignored to keep the first definition and avoid ambiguity.
     */
    template <typename Self>
    constexpr Self&& option(this Self&& self, Option opt) noexcept {
        if (opt.longName().empty() && !opt.shortName()) {
            return std::forward<Self>(self);
        }

        bool has_conflict =
            (!opt.longName().empty() && self.longOptionMap_.contains(opt.longName()));
        has_conflict = has_conflict || rng::any_of(opt.aliases(), [&self](std::string_view alias) {
                           return self.longOptionMap_.contains(alias);
                       });
        has_conflict =
            has_conflict || (opt.shortName() && self.shortOptionMap_.contains(*opt.shortName()));
        if (has_conflict) {
            return std::forward<Self>(self);
        }

        if (!opt.longName().empty()) {
            self.longOptionMap_[opt.longName()] = self.options_.size();
            for (const auto& alias : opt.aliases()) {
                self.longOptionMap_[alias] = self.options_.size();
            }
        }
        if (opt.shortName()) {
            self.shortOptionMap_[*opt.shortName()] = self.options_.size();
        }

        self.options_.push_back(std::move(opt));
        return std::forward<Self>(self);
    }

    /**
     * @brief Registers a positional argument on this command.
     * @param arg An `Argument` instance.
     *
     * Arguments are consumed in definition order. Adding after a variadic
     * argument is silently ignored.
     */
    template <typename Self>
    constexpr Self&& argument(this Self&& self, Argument arg) noexcept {
        if (!self.arguments_.empty() && self.arguments_.back().isVariadic()) {
            return std::forward<Self>(self);
        }

        self.arguments_.push_back(std::move(arg));
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds a nested subcommand.
     * @param sub A `Command` instance representing the subcommand.
     *
     * Subcommands are stored by name. Adding a subcommand with a name that
     * already exists will replace the previous one.
     */
    template <typename Self>
    constexpr Self&& subcommand(this Self&& self, Command sub) noexcept {
        std::string name = sub.name();
        self.subcommands_[name] = std::make_shared<Command>(std::move(sub));
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets the handler function invoked when this command is the
     * parsing target.
     * @param fn Callable matching the `CommandFn` signature.
     */
    template <typename Self>
    constexpr Self&& handler(this Self&& self, CommandFn fn) noexcept {
        self.handler_ = std::move(fn);
        return std::forward<Self>(self);
    }

    /**
     * @brief Convenience method to add a boolean flag option.
     * @param long_name Long option name (e.g. `"verbose"`).
     * @param short_name Short option character (e.g. `'v'`).
     * @param desc Description text for help output.
     */
    template <typename Self>
    constexpr Self&& flag(this Self&& self,
                          std::string long_name,
                          char short_name,
                          std::string desc) noexcept {
        return std::forward<Self>(self).option(
            Option::withName(std::move(long_name), short_name).description(std::move(desc)));
    }

    /**
     * @brief Marks this command as hidden (omitted from help listings).
     */
    template <typename Self>
    constexpr Self&& hidden(this Self&& self) noexcept {
        self.hidden_ = true;
        return std::forward<Self>(self);
    }

    /**
     * @brief Overrides the auto-generated usage string.
     * @param usage_str The full usage string.
     */
    template <typename Self>
    constexpr Self&& usage(this Self&& self, std::string usage_str) noexcept {
        self.usageOverride_ = std::move(usage_str);
        return std::forward<Self>(self);
    }

    // --- Accessors ---

    /// @return The command's primary name.
    [[nodiscard]] constexpr const std::string& name() const noexcept { return name_; }

    /// @return The short description.
    [[nodiscard]] constexpr const std::string& description() const noexcept { return description_; }

    /// @return The long description (may be empty).
    [[nodiscard]] constexpr const std::string& longDescription() const noexcept {
        return detailedDescription_;
    }

    /// @return The registered options.
    [[nodiscard]] constexpr const std::vector<Option>& options() const noexcept { return options_; }

    /// @return The registered positional arguments.
    [[nodiscard]] constexpr const std::vector<Argument>& arguments() const noexcept {
        return arguments_;
    }

    /// @return The map of subcommands (keyed by name).
    [[nodiscard]] constexpr const auto& subcommands() const noexcept { return subcommands_; }

    /// @return `true` if this command is hidden from help.
    [[nodiscard]] constexpr bool isHidden() const noexcept { return hidden_; }

    /// @return The usage override string (empty if using auto-generated).
    [[nodiscard]] constexpr const std::string& usageOverride() const noexcept {
        return usageOverride_;
    }

    /**
     * @brief Looks up an option by its long name or alias.
     * @param name Long name or alias (without `--` prefix).
     * @return Pointer to the `Option`, or `nullptr` if not found.
     */
    [[nodiscard]] constexpr const Option* findOption(std::string_view name) const noexcept {
        auto it = longOptionMap_.find(name);
        if (it != longOptionMap_.end() && it->second < options_.size()) {
            return &options_[it->second];
        }
        return nullptr;
    }

    /**
     * @brief Looks up an option by its short character.
     * @param short_name Short option character.
     * @return Pointer to the `Option`, or `nullptr` if not found.
     */
    [[nodiscard]] constexpr const Option* findOption(char short_name) const noexcept {
        auto it = shortOptionMap_.find(short_name);
        if (it != shortOptionMap_.end() && it->second < options_.size()) {
            return &options_[it->second];
        }
        return nullptr;
    }

    /**
     * @brief Looks up a subcommand by name.
     * @param name The subcommand name.
     * @return Pointer to the `Command`, or `nullptr` if not found.
     */
    [[nodiscard]] constexpr const Command* findSubcommand(std::string_view name) const noexcept {
        auto it = subcommands_.find(name);
        if (it != subcommands_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    /// @return `true` if this command has any subcommands.
    [[nodiscard]] constexpr bool hasSubcommands() const noexcept { return !subcommands_.empty(); }

    /**
     * @brief Executes this command's handler with the given parsed result.
     * @param parsed The parsed command data.
     * @return `VoidResult` from the handler, or success if no handler is set.
     */
    [[nodiscard]] cli_error::VoidResult execute(const ParsedCommand& parsed) const {
        if (handler_) {
            return handler_(parsed);
        }
        return cli_error::ok();
    }

    /**
     * @brief Returns non-hidden subcommands for help rendering.
     */
    [[nodiscard]] std::vector<const Command*> visibleSubcommands() const {
        std::vector<const Command*> result;
        // or filter | transform | to (which is efficient?)
        for (const auto& [name, cmd] : subcommands_) {
            if (!cmd->isHidden()) {
                result.push_back(cmd.get());
            }
        }
        return result;
    }

    /**
     * @brief Generates the usage line for this command.
     * @param program_name The executable name to use.
     * @return Usage string like `"myapp build [options] <target>"`.
     */
    [[nodiscard]] std::string usageString(std::string_view program_name) const {
        if (!usageOverride_.empty()) {
            return usageOverride_;
        }
        std::string result = std::string(program_name);

        if (!name_.empty() && name_ != program_name) {
            result += " " + name_;
        }

        if (!options_.empty()) {
            result += " [options]";
        }

        if (hasSubcommands()) {
            result += " <command>";
        }

        result +=
            rng::fold_left(arguments_, std::string(""), [](std::string acc, const Argument& arg) {
                return std::move(acc) + " " + arg.usageString();
            });
        return result;
    }

private:
    template <typename ValueType>
    using MapType =
        std::unordered_map<std::string, ValueType, details::StringHash, std::equal_to<>>;

    Command() = default;
    std::string name_;
    std::string description_;
    std::string detailedDescription_;
    std::string usageOverride_;

    std::vector<Option> options_;      ///< Registered options in definition order.
    std::vector<Argument> arguments_;  ///< Registered positional arguments.

    MapType<std::shared_ptr<Command>> subcommands_;  ///< Subcommands keyed by name.
    MapType<std::size_t> longOptionMap_;             ///< Long name/alias → index into options_.
    std::unordered_map<char, std::size_t> shortOptionMap_;  ///< Short char → index into options_.

    CommandFn handler_;    ///< Callback invoked on execute.
    bool hidden_ = false;  ///< If true, excluded from help listings.
};

/**
 * @brief Mutable parsing state tracking the current position in the command
 * tree.
 *
 * Starts at the root command and is updated as the parser descends into
 * subcommands. The command path is recorded for use in error messages,
 * default-value layering, and the final `ParsedCommand::commandPath`.
 */
class ParseContext {
public:
    /**
     * @brief Initializes the context at the root command.
     */
    constexpr explicit ParseContext(const Command& root) : root_(&root), current_(&root) {}

    /**
     * @brief Returns the top-level root command (never changes).
     */
    [[nodiscard]] constexpr const Command& root() const noexcept { return *root_; }

    /**
     * @brief Returns the command currently in scope during parsing.
     */
    [[nodiscard]] constexpr const Command& current() const noexcept { return *current_; }

    /**
     * @brief Descends into a subcommand, recording the name in the path.
     */
    constexpr void navigateToSubcommand(const Command* subcmd) {
        if (subcmd) {
            current_ = subcmd;
            commandPath_.push_back(subcmd->name());
        }
    }

    /**
     * @brief Returns the ordered list of subcommand names from root to current.
     */
    [[nodiscard]] constexpr const std::vector<std::string>& commandPath() const noexcept {
        return commandPath_;
    }

private:
    const Command* root_;
    const Command* current_;
    std::vector<std::string> commandPath_;
};

/**
 * @brief ANSI SGR (Select Graphic Rendition) escape codes for terminal output.
 *
 * Used by `HelpGenerator` when `HelpConfig::useColors` is enabled. All codes
 * are `constexpr` to allow zero-cost elision when colors are disabled at
 * compile time.
 */
namespace color {
constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBold = "\033[1m";
constexpr std::string_view kDim = "\033[2m";
constexpr std::string_view kItalic = "\033[3m";
constexpr std::string_view kUnderline = "\033[4m";

constexpr std::string_view kBlack = "\033[30m";
constexpr std::string_view kRed = "\033[31m";
constexpr std::string_view kGreen = "\033[32m";
constexpr std::string_view kYellow = "\033[33m";
constexpr std::string_view kBlue = "\033[34m";
constexpr std::string_view kMagenta = "\033[35m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kWhite = "\033[37m";

constexpr std::string_view kBrightBlack = "\033[90m";
constexpr std::string_view kBrightRed = "\033[91m";
constexpr std::string_view kBrightGreen = "\033[92m";
constexpr std::string_view kBrightYellow = "\033[93m";
constexpr std::string_view kBrightBlue = "\033[94m";
constexpr std::string_view kBrightMagenta = "\033[95m";
constexpr std::string_view kBrightCyan = "\033[96m";
constexpr std::string_view kBrightWhite = "\033[97m";
}  // namespace color

/**
 * @brief Configuration for help text generation.
 *
 * Controls layout (indentation, max width), whether ANSI colors are used,
 * and whether default values / required markers are shown.
 */
struct HelpConfig {
    bool useColors = true;     ///< If true, ANSI escape codes are emitted.
    bool showDefaults = true;  ///< If true, default values are shown in help.
    bool showRequired = true;  ///< If true, `[required]` tag is shown.

    std::size_t maxDescriptionWidth = 80;  ///< Maximum line width before wrapping.
    std::size_t optionIndent = 4;          ///< Left indent for option/argument lines.
    std::size_t descriptionIndent = 24;    ///< Column where description text starts.
    std::string programName;               ///< Override program name in help output.

    /**
     * @brief Creates a default configuration with colors auto-detected.
     */
    static constexpr HelpConfig defaults() noexcept {
        HelpConfig config;
        config.useColors = isTerminal();
        return config;
    }

    /**
     * @brief Detects whether stdout is a terminal (TTY).
     *
     * When output is piped to a file or another command, colors are
     * automatically disabled to avoid escape-code noise.
     */
    static constexpr bool isTerminal() noexcept { return isatty(STDOUT_FILENO) != 0; }
};

/**
 * @brief Renders formatted help text from a `Command` definition.
 *
 * Help output follows a conventional structure: Usage, Description, Arguments,
 * Options, and Commands sections. ANSI colors are applied per `HelpConfig`
 * and automatically disabled when stdout is not a terminal.
 *
 * Two generation methods are provided:
 *   - `generate()`: Imperative loop-based rendering.
 *   - `generate2()`: Range-pipeline-based rendering using `views::transform`
 *     and `views::join`. Produces identical output; kept for benchmarking
 *     comparison until one approach is chosen for removal.
 */
class HelpGenerator {
public:
    /**
     * @brief Constructs a help generator with the given configuration.
     */
    explicit HelpGenerator(HelpConfig config = HelpConfig::defaults())
        : config_(std::move(config)) {}

    /**
     * @brief Generates full help text using range-based pipelines.
     * @param cmd The command to generate help for.
     * @param program_name The executable name for the usage line.
     * @return Formatted help text.
     */
    [[nodiscard]] std::string generate2(const Command& cmd, std::string_view program_name) const {
        std::string result =
            std::format("{}\n{}{}\n\n", formatSectionHeader("Usage"),
                        formatIndent(config_.optionIndent), cmd.usageString(program_name));

        result.reserve(2048);  // NOLINT
        auto out = std::back_inserter(result);
        if (!cmd.description().empty()) {
            std::format_to(
                out, "{}\n{}\n\n", formatSectionHeader("Description"),
                wrapText(cmd.description(), config_.maxDescriptionWidth, config_.optionIndent));
        }
        if (!cmd.longDescription().empty()) {
            std::format_to(
                out, "{}\n\n",
                wrapText(cmd.longDescription(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        // clang-format off
        if (!cmd.arguments().empty()) {
            std::format_to(out, "{}\n{}\n",
                        formatSectionHeader("Arguments"),
                        cmd.arguments()
                            | views::transform([this](const Argument& arg) {return
                            formatArgument(arg);}) | views::join | rng::to<std::string>());
        }
        if (!cmd.options().empty())
        {
            std::format_to(out, "{}\n{}\n",
                        formatSectionHeader("Options"),
                        cmd.options()
                            | views::transform([this](const Option& opt) {return
                            formatOption(opt);}) | views::join | rng::to<std::string>()
                    );
        }
        // clang-format on

        auto visible_subcmds = cmd.visibleSubcommands();
        if (!visible_subcmds.empty()) {
            std::size_t max_name_width = rng::fold_left(
                visible_subcmds | views::transform(&Command::name) |
                    views::transform(&std::string::size),
                0U, [](std::size_t acc, std::size_t size) { return std::max(acc, size); });

            std::format_to(out, "{}\n{}\n", formatSectionHeader("Commands"),
                           visible_subcmds |
                               views::transform([this, max_name_width](const Command* subcmd) {
                                   return formatSubCommand(*subcmd, max_name_width);
                               }) |
                               views::join | rng::to<std::string>());
        }

        return result;
    }

    /**
     * @brief Generates full help text using imperative loops.
     * @param cmd The command to generate help for.
     * @param program_name The executable name for the usage line.
     * @return Formatted help text.
     */
    [[nodiscard]] std::string generate(const Command& cmd, std::string_view program_name) const {
        std::string result;

        result.reserve(2048);  // NOLINT

        auto out = std::back_inserter(result);

        std::format_to(out, "{}\n{}{}\n\n", formatSectionHeader("Usage"),
                       formatIndent(config_.optionIndent), cmd.usageString(program_name));

        if (!cmd.description().empty()) {
            std::format_to(
                out, "{}\n{}\n\n", formatSectionHeader("Description"),
                wrapText(cmd.description(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        if (!cmd.longDescription().empty()) {
            std::format_to(
                out, "{}\n\n",
                wrapText(cmd.longDescription(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        if (!cmd.arguments().empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Arguments"));
            for (const auto& arg : cmd.arguments()) {
                result += formatArgument(arg);
            }
            result += '\n';
        }

        if (!cmd.options().empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Options"));
            for (const auto& opt : cmd.options()) {
                result += formatOption(opt);
            }
            result += '\n';
        }

        auto visible_subcmds = cmd.visibleSubcommands();
        if (!visible_subcmds.empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Commands"));

            std::size_t max_name_width = 0;
            for (const auto* subcmd : visible_subcmds) {
                max_name_width = std::max(max_name_width, subcmd->name().size());
            }

            for (const auto* subcmd : visible_subcmds) {
                result += formatSubCommand(*subcmd, max_name_width);
            }
            result += '\n';
        }

        return result;
    }

    /**
     * @brief Returns a one-line usage string for the command.
     */
    [[nodiscard]] static std::string shortUsage(const Command& cmd, std::string_view program_name) {
        return cmd.usageString(program_name);
    }

    /**
     * @brief Formats a parse error for display to the user.
     *
     * Produces output like:
     * @code
     * Error: unknown option --foo
     *
     * Usage:
     *     myapp [options]
     *
     * Try 'myapp --help' for more information.
     * @endcode
     */
    [[nodiscard]] std::string formatError(const cli_error::ParseError& error,
                                          const Command& cmd,
                                          std::string_view program_name) const {
        // clang-format off
        return std::format("{}: {}\n\n{}:\n{}{}\n\nTry '{} --help' for more information.\n",
                            colorText("Error", color::kBrightRed, true),
                            error.message(),
                            colorText("Usage", color::kYellow, true),
                            formatIndent(config_.optionIndent), cmd.usageString(program_name),
                            program_name);
        // clang-format on
    }

private:
    /// Renders a section header like "Usage:" with color.
    [[nodiscard]] std::string formatSectionHeader(std::string_view title) const {
        return colorText(title, color::kBrightCyan, true) + ":";
    }

    /// Formats a single option line for help output.
    [[nodiscard]] std::string formatOption(const Option& opt) const {
        auto opt_str = opt.usageString();
        std::size_t current_width = config_.optionIndent + opt_str.size();
        std::size_t padding = current_width >= config_.descriptionIndent
                                  ? 2
                                  : config_.descriptionIndent - current_width;

        auto description = opt.description();
        if (config_.showDefaults && opt.hasDefault()) {
            description += std::format("{}[default: {}]", description.empty() ? "" : " ",
                                       opt.defaultValueStr());
        }
        if (config_.showRequired && opt.isRequired()) {
            description += std::format("{}{}", description.empty() ? "" : " ",
                                       colorText("[required]", color::kBrightYellow, false));
        }
        if (opt.hasChoices()) {
            description += std::format("{}[choices: {}]", description.empty() ? "" : " ",
                                       formatChoices(opt.choices()));
        }
        // clang-format off
        return std::format(
            "{}{}{}{}\n",
            formatIndent(config_.optionIndent),
            colorText(opt_str, color::kGreen, false),
            std::string(padding, ' '),
            wrapText(description, config_.maxDescriptionWidth, config_.descriptionIndent)
        );
        // clang-format on
    }

    /// Formats a single positional argument line for help output.
    [[nodiscard]] constexpr std::string formatArgument(const Argument& arg) const noexcept {
        auto arg_str = arg.usageString();
        std::size_t current_width = config_.optionIndent + arg_str.size();
        std::size_t padding = current_width >= config_.descriptionIndent
                                  ? 2
                                  : config_.descriptionIndent - current_width;

        auto description = arg.description();
        if (config_.showDefaults && arg.hasDefault()) {
            description += std::format("{}[default: {}]", description.empty() ? "" : " ",
                                       arg.defaultValueStr());
        }
        if (config_.showRequired && arg.isRequired() && !arg.isVariadic()) {
            description += std::format("{}{}", description.empty() ? "" : " ",
                                       colorText("[required]", color::kBrightYellow, false));
        }
        // clang-format off
        return std::format(
            "{}{}{}{}\n",
            formatIndent(config_.optionIndent),
            colorText(arg_str, color::kMagenta, false),
            std::string(padding, ' '),
            wrapText(description, config_.maxDescriptionWidth, config_.descriptionIndent)
        );
        // clang-format on
    }

    /// Formats a single subcommand entry for the command listing.
    [[nodiscard]] std::string formatSubCommand(const Command& subcmd,
                                               std::size_t max_name_width) const {
        std::size_t padding = max_name_width - subcmd.name().size() + 2;
        // clang-format off
        return std::format(
            "{}{}{}{}\n",
            formatIndent(config_.optionIndent),
            colorText(subcmd.name(), color::kCyan, false),
            std::string(padding + config_.descriptionIndent - config_.optionIndent -
                            max_name_width - 2,
                        ' '),
            wrapText(subcmd.description(), config_.maxDescriptionWidth, config_.descriptionIndent));
        // clang-format on
    }

    /// Returns a string of N spaces for indentation.
    [[nodiscard]] static constexpr std::string formatIndent(std::size_t n) noexcept {
        return std::string(n, ' ');
    }

    /// Applies color codes to text (or returns plain text if colors are disabled).
    [[nodiscard]] constexpr std::string colorText(std::string_view text,
                                                  std::string_view color_code,
                                                  bool bold) const noexcept {
        if (!config_.useColors) {
            return std::string(text);
        }
        return std::format("{}{}{}{}", bold ? color::kBold : "", color_code, text, color::kReset);
    }

    /**
     * @brief Word-wraps text to fit within a maximum line width.
     *
     * Words are kept intact; lines are broken at word boundaries. Subsequent
     * lines are indented to align with the description column.
     */
    [[nodiscard]] static constexpr std::string wrapText(std::string_view text,
                                                        std::size_t max_width,
                                                        std::size_t indent) noexcept {
        if (max_width <= indent || text.empty()) {
            return std::string(text);
        }
        if (text.size() + indent <= max_width) {
            return std::string(text);
        }
        std::string result;
        std::size_t estimated_newlines = (text.size() / (max_width - indent)) + 1;
        result.reserve(text.size() + (estimated_newlines * (indent + 1)));
        std::size_t current_line_width = indent;
        std::size_t pos = 0;

        while (pos < text.size()) {
            std::size_t word_start = text.find_first_not_of(' ', pos);
            if (word_start == std::string_view::npos) {
                break;
            }
            std::size_t word_end = text.find_first_of(' ', word_start);
            std::size_t word_len = (word_end == std::string_view::npos) ? text.size() - word_start
                                                                        : word_end - word_start;
            std::string_view word = text.substr(word_start, word_len);
            bool needs_space = (!result.empty() && current_line_width > indent);
            std::size_t space_len = needs_space ? 1 : 0;

            if (current_line_width + space_len + word_len > max_width) {
                if (!result.empty()) {
                    result += '\n';
                    result.append(indent, ' ');
                    current_line_width = indent;
                }
            } else if (needs_space) {
                result += ' ';
                current_line_width++;
            }

            result.append(word);
            current_line_width += word_len;

            pos = word_end;
        }
        return result;
    }

    /// Joins choices into a comma-separated string.
    [[nodiscard]] static constexpr std::string formatChoices(
        const std::vector<std::string>& choices) noexcept {
        auto formatted = choices | std::views::join_with(std::string_view(", "));
        return std::ranges::to<std::string>(formatted);
    }

    HelpConfig config_;
};

/**
 * @brief Post-parse validation of structural constraints.
 *
 * Runs after the parser has consumed all tokens and applied defaults. Checks
 * that required options are present, required positional argument counts are
 * satisfied, and no unexpected arguments exist when the command has no variadic
 * argument.
 *
 * All methods are static — the validator has no mutable state.
 */
class Validator {
public:
    /**
     * @brief Validates a parsed command against its definition.
     *
     * Checks performed:
     *   1. Every required option with a value is present in the parsed result.
     *   2. The number of positional arguments meets the minimum required count.
     *   3. Without a variadic argument, the positional count does not exceed
     *      the defined argument count.
     *
     * @param parsed The parsed command result.
     * @param cmd The command definition to validate against.
     * @return `VoidResult` — success or a descriptive `ParseError`.
     */
    [[nodiscard]] static cli_error::VoidResult validate(const ParsedCommand& parsed,
                                                        const Command& cmd) {
        for (const auto& opt : cmd.options()) {
            if (opt.isRequired() && opt.needsValue()) {
                bool has_value = false;

                if (!opt.longName().empty()) {
                    has_value = parsed.options.has(opt.longName());
                }

                if (!has_value && opt.shortName()) {
                    has_value = parsed.options.has(std::string(1, *opt.shortName()));
                }

                if (!has_value) {
                    return cli_error::err(
                        cli_error::ErrorCode::MissingRequiredOption,
                        std::format("Required option '{}' is missing", opt.displayName()));
                }
            }
        }

        auto [required_count, has_variadic] = std::ranges::fold_left(
            cmd.arguments(), std::make_pair(0U, false),
            [](std::pair<std::size_t, bool> acc,
               const Argument& arg) -> std::pair<std::size_t, bool> {
                return {acc.first + static_cast<std::size_t>(arg.isRequired() && !arg.isVariadic()),
                        acc.second | arg.isVariadic()};
            });

        if (parsed.positionalArgs.size() < required_count) {
            std::size_t arg_idx = parsed.positionalArgs.size();
            if (arg_idx < cmd.arguments().size()) {
                const auto& missing_arg = cmd.arguments()[arg_idx];
                return cli_error::err(
                    cli_error::ErrorCode::MissingRequiredArgument,
                    std::format("Required argument '{}' is missing", missing_arg.name()));
            }
        }

        if (!has_variadic && parsed.positionalArgs.size() > cmd.arguments().size()) {
            return cli_error::err(cli_error::ErrorCode::UnexpectedArgument,
                                  std::format("Unexpected argument: '{}'",
                                              parsed.positionalArgs[cmd.arguments().size()]));
        }

        return cli_error::ok();
    }

    /**
     * @brief Checks whether a value is in the allowed set.
     * @param value The value to check.
     * @param choices The set of allowed values.
     * @return `true` if the value is in the set, or if the set is empty
     * (meaning no constraint).
     */
    [[nodiscard]] static bool validateChoices(const std::string& value,
                                              const std::vector<std::string>& choices) {
        return choices.empty() ||
               rng::any_of(choices, [&](const std::string& choice) { return choice == value; });
    }

    /**
     * @brief Checks whether an arithmetic value is within `[min, max]`.
     * @tparam T An arithmetic type.
     * @param value The value to check.
     * @param min The inclusive lower bound.
     * @param max The inclusive upper bound.
     * @return `true` if `min <= value <= max`.
     */
    template <typename T>
        requires std::is_arithmetic_v<T>
    [[nodiscard]] static bool validateRange(T value, T min, T max) {
        return value >= min && value <= max;
    }
};

/**
 * @brief Typed token categories produced by the Tokenizer.
 */
enum class TokenType : std::uint8_t {
    LongOption,    ///< `--option` or `--option=value`.
    ShortOption,   ///< `-o` (single short option).
    ShortOptions,  ///< `-abc` (multiple combined short options).
    OptionValue,   ///< Value argument following an option.
    PositionArg,   ///< Standalone positional argument.
    Command,       ///< Subcommand name token.
    DoubleDash,    ///< `--` — terminates option parsing.
    HelpRequest    ///< `--help` or `-h` (when not redefined as a flag).
};

/**
 * @brief A single lexical token from the command line.
 *
 * Each token carries its type, the original argv index, the parsed value,
 * and optional metadata such as the triggering short character, the original
 * source string, and any attached value (for `--option=value` or `-o=value`).
 */
struct Token {
    TokenType type = TokenType::HelpRequest;
    std::size_t originalIndex{};          ///< Position in the original argv array.
    std::string value{};                  ///< Parsed token value (option name, arg text, etc.).
    std::optional<char> shortChar{};      ///< For short options, which character triggered it.
    std::optional<std::string> source{};  ///< Original string before any processing.

    /// For options with `=` attached values (e.g. `--output=file.txt` or `-o=file.txt`).
    std::optional<std::string> attachedValue{};

    /// @return The parsed option name or positional value.
    [[nodiscard]] constexpr std::string_view name() const noexcept { return value; }

    /**
     * @brief Returns the original argv string (preserving `--`/`-` prefixes).
     *
     * Use this when a token is reinterpreted as data (e.g. after `--`) so
     * syntactic prefixes are not lost.
     */
    [[nodiscard]] constexpr std::string_view lexeme() const noexcept {
        if (source) {
            return *source;
        }
        return value;
    }

    /**
     * @brief Returns `true` if this token represents any kind of option
     * (long, short, or combined short).
     */
    [[nodiscard]] constexpr bool isOption() const noexcept {
        return type == TokenType::LongOption || type == TokenType::ShortOption ||
               type == TokenType::ShortOptions;
    }
};

/**
 * @brief Lexer that converts raw command-line arguments into typed tokens.
 *
 * The tokenizer skips `argv[0]` (the program name) and processes each
 * subsequent argument independently. It handles:
 *   - `--option` and `--option=value` (long options)
 *   - `-o` (single short option)
 *   - `-abc` (combined short options)
 *   - `-o=value` (short option with `=` attached value)
 *   - `--` (double dash to stop option parsing)
 *   - `--help` and `-h` (help request, unless `-h` is redefined)
 *   - Negative numbers (`-42`, `-3.14`) correctly treated as positional
 */
class Tokenizer {
public:
    /**
     * @brief Tokenizes a span of string arguments (excluding `argv[0]`).
     * @param args Full argv span where `args[0]` is the program name.
     * @return A vector of `Token` on success, or a `ParseError` on lexing failure.
     */
    [[nodiscard]] cli_error::Result<std::vector<Token>> tokenize(
        std::span<std::string_view> args) const {
        if (args.empty()) {
            return std::vector<Token>{};
        }

        std::vector<Token> tokens;
        tokens.reserve(args.size());
        for (std::size_t i = 1; i < args.size(); ++i) {
            auto result = tokenizeSingle(args[i], i);
            if (!result) {
                return std::unexpected(result.error());
            }
            tokens.push_back(*result);
        }
        return tokens;
    }

private:
    /**
     * @brief Classifies a single argv string into a Token.
     * @param arg The raw argument string.
     * @param index The position in the original argv.
     * @return A `Token` or a `ParseError`.
     */
    [[nodiscard]] cli_error::Result<Token> tokenizeSingle(std::string_view arg,
                                                          std::size_t index) const {
        Token token;
        token.originalIndex = index;
        token.source = std::string(arg);

        using namespace std::literals;
        if (arg == "--") {
            token.type = TokenType::DoubleDash;
            token.value = "--";
            return token;
        }

        if (arg == "--help") {
            token.type = TokenType::HelpRequest;
            token.value = "help";
            return token;
        }

        if (arg == "-h" && !helpAsFlag_) {
            token.type = TokenType::HelpRequest;
            token.value = "help";
            return token;
        }

        if (arg.starts_with("--")) {
            auto rest = arg.substr(2);
            if (auto eq_pos = rest.find('='); eq_pos != std::string_view::npos) {
                token.type = TokenType::LongOption;
                token.value = std::string(rest.substr(0, eq_pos));
                token.attachedValue = std::string(rest.substr(eq_pos + 1));
            } else {
                token.type = TokenType::LongOption;
                token.value = std::string(rest);
            }
            return token;
        }

        if (arg.starts_with('-')) {
            if (arg.size() == 1) {
                return cli_error::err(cli_error::ErrorCode::InvalidOptionFormat,
                                      "Invalid option '-'");
            }
            auto rest = arg.substr(1);
            // Negative numbers are positional, not options
            if (std::isdigit(static_cast<unsigned char>(rest[0])) != 0 || rest[0] == '.') {
                token.type = TokenType::PositionArg;
                token.value = std::string(arg);
                return token;
            }

            if (auto eq_pos = rest.find('='); eq_pos != std::string_view::npos) {
                std::string_view opt_name = rest.substr(0, eq_pos);
                if (opt_name.size() == 1) {
                    token.type = TokenType::ShortOption;
                    token.shortChar = opt_name[0];
                } else {
                    token.type = TokenType::ShortOptions;
                }
                token.value = std::string(opt_name);
                token.attachedValue = std::string(rest.substr(eq_pos + 1));
            } else if (rest.size() > 1) {
                token.type = TokenType::ShortOptions;
                token.value = std::string(rest);
            } else {
                token.type = TokenType::ShortOption;
                token.value = std::string(rest);
                token.shortChar = rest[0];
            }

            return token;
        }

        token.type = TokenType::PositionArg;
        token.value = std::string(arg);
        return token;
    }

    bool helpAsFlag_ = false;  ///< If true, `-h` is treated as a regular option, not help.
};

/**
 * @brief Sequential token accessor with peek/consume/backtrack semantics.
 *
 * Wraps an owned vector of tokens and provides a cursor-based interface.
 * Tokens are never mutated, so `peek()` and `consumeView()` return stable
 * pointers into the buffer. The stream also tracks whether `--` (double
 * dash) has been seen, after which all remaining tokens are treated as
 * positional.
 */
class TokenStream {
public:
    /**
     * @brief Creates a stream from a vector of tokens (takes ownership).
     */
    explicit constexpr TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    /**
     * @brief Returns `true` if there are tokens remaining.
     */
    [[nodiscard]] constexpr bool hasMore() const noexcept { return position_ < tokens_.size(); }

    /**
     * @brief Returns a pointer to the current token without advancing.
     * @return Pointer to the token, or `nullptr` if at end.
     */
    [[nodiscard]] constexpr const Token* peek() const noexcept {
        if (!hasMore()) {
            return nullptr;
        }
        return &tokens_[position_];
    }

    /**
     * @brief Returns a pointer to the token after the current one.
     * @return Pointer to the next token, or `nullptr` if at end.
     */
    [[nodiscard]] constexpr const Token* peekNext() const noexcept {
        if (position_ + 1 >= tokens_.size()) {
            return nullptr;
        }
        return &tokens_[position_ + 1];
    }

    /**
     * @brief Consumes and returns the current token by value.
     * @return A copy of the token, or `std::nullopt` if at end.
     */
    [[nodiscard]] constexpr std::optional<Token> consume() noexcept {
        if (!hasMore()) {
            return std::nullopt;
        }
        return tokens_[position_++];
    }

    /**
     * @brief Consumes and returns a pointer to the current token (no copy).
     *
     * The returned pointer is valid for the lifetime of this `TokenStream`.
     */
    [[nodiscard]] constexpr const Token* consumeView() noexcept {
        if (!hasMore()) {
            return nullptr;
        }
        return &tokens_[position_++];
    }

    /**
     * @brief Consumes the current token if it matches a predicate.
     * @tparam Pred Predicate type accepting `const Token&`.
     * @param pred The predicate to test.
     * @return A copy of the token if matched, or `std::nullopt` otherwise.
     */
    template <typename Pred>
        requires std::predicate<Pred, const Token&>
    [[nodiscard]] constexpr std::optional<Token> consumeIf(Pred&& pred) {
        if (!hasMore()) {
            return std::nullopt;
        }
        if (std::invoke(std::forward<Pred>(pred), tokens_[position_])) {
            return tokens_[position_++];
        }
        return std::nullopt;
    }

    /**
     * @brief Returns the current cursor position.
     */
    [[nodiscard]] constexpr std::size_t position() const noexcept { return position_; }

    /**
     * @brief Sets the cursor position (clamped to valid range).
     *
     * Used for backtracking during parsing.
     */
    constexpr void setPosition(std::size_t pos) noexcept {
        position_ = std::min(pos, tokens_.size());
    }

    /**
     * @brief Returns a range view of all unconsumed tokens.
     */
    [[nodiscard]] constexpr auto remaining() const {
        using DiffType = std::vector<Token>::difference_type;
        return rng::subrange(tokens_.begin() + static_cast<DiffType>(position_), tokens_.end());
    }

    /**
     * @brief Returns the complete token buffer.
     */
    [[nodiscard]] constexpr const std::vector<Token>& all() const noexcept { return tokens_; }

    /**
     * @brief Returns `true` if `--` (double dash) has been seen.
     */
    [[nodiscard]] constexpr bool seenDoubleDash() const noexcept { return seenDoubleDash_; }

    /**
     * @brief Marks that `--` has been seen; all subsequent tokens are positional.
     */
    constexpr void markDoubleDash() noexcept { seenDoubleDash_ = true; }

private:
    std::vector<Token> tokens_;
    std::size_t position_{};
    bool seenDoubleDash_ = false;
};

/**
 * @brief Core parser that converts a token stream into a `ParsedCommand`.
 *
 * The parser walks the token stream against a `Command` definition tree,
 * resolving option names into canonical forms, consuming option values,
 * navigating subcommands, and collecting positional arguments. After all
 * tokens are consumed, option default values are applied in layer order
 * (root command first, then each subcommand along the invocation path).
 *
 * All methods are static — the parser is stateless.
 */
class Parser {
public:
    /**
     * @brief Parses raw command-line arguments against a command definition.
     * @param args Full argv span (including program name at index 0).
     * @param root The root command definition.
     * @return A `ParsedCommand` on success, or a `ParseError` on failure.
     */
    [[nodiscard]] static constexpr cli_error::Result<ParsedCommand> parse(
        std::span<std::string_view> args, const Command& root) {
        Tokenizer tokenizer;
        auto token_result = tokenizer.tokenize(args);
        if (!token_result) {
            return std::unexpected(token_result.error());
        }
        return parseTokens(*token_result, root);
    }

private:
    /// Attempts to match the token as a subcommand; falls back to positional.
    [[nodiscard]] static constexpr cli_error::Result<bool> handleSubcommandOrPositional(
        TokenStream& stream, ParseContext& context, ParsedCommand& result, const Token* token) {
        if (context.current().hasSubcommands() && result.positionalArgs.empty()) {
            const auto* subcmd = context.current().findSubcommand(token->value);
            if (subcmd) {
                (void)stream.consumeView();
                context.navigateToSubcommand(subcmd);
                result.commandPath = context.commandPath();
                result.name = subcmd->name();

                // Check for help after subcommand
                if (stream.hasMore() && stream.peek()->type == TokenType::HelpRequest) {
                    (void)stream.consumeView();
                    result.helpRequested = true;
                    return true;  // Indicates parsing is done (help requested)
                }
                return false;  // Subcommand handled, parsing continues
            }
        }
        // Not a subcommand, treat as positional
        const auto* consumed = stream.consumeView();
        if (consumed) {
            result.addPositional(consumed->value);
        }
        return false;
    }

    /// Dispatches a single token to the appropriate handler based on its type.
    [[nodiscard]] static constexpr cli_error::Result<bool> processSingleToken(TokenStream& stream,
                                                                              ParseContext& context,
                                                                              ParsedCommand& result,
                                                                              const Token* token) {
        cli_error::VoidResult parse_result;

        switch (token->type) {
            case TokenType::DoubleDash:
                (void)stream.consumeView();
                stream.markDoubleDash();
                break;
            case TokenType::LongOption:
                parse_result = parseLongOption(stream, context, result);
                break;
            case TokenType::ShortOption:
                parse_result = parseShortOption(stream, context, result);
                break;
            case TokenType::ShortOptions:
                parse_result = parseCombinedShortOptions(stream, context, result);
                break;
            case TokenType::PositionArg:
                return handleSubcommandOrPositional(stream, context, result, token);
            default:
                if (const auto* consumed = stream.consumeView()) {
                    result.addPositional(consumed->value);
                }
                break;
        }

        if (!parse_result) {
            return std::unexpected(parse_result.error());
        }
        return false;  // Return 'false' to indicate parsing should continue
    }

    /// Main parsing loop: walks the token stream, handles help/double-dash,
    /// dispatches tokens, and applies defaults when done.
    [[nodiscard]] constexpr static cli_error::Result<ParsedCommand> parseTokens(
        std::vector<Token> tokens, const Command& root) {
        TokenStream stream{std::move(tokens)};
        ParseContext context(root);
        ParsedCommand result;
        result.name = root.name();

        if (auto token =
                stream.consumeIf([](const Token& t) { return t.type == TokenType::HelpRequest; })) {
            result.helpRequested = true;
            return result;
        }

        while (stream.hasMore()) {
            const auto* token = stream.peek();
            if (!token) {
                break;
            }

            if (stream.seenDoubleDash()) {
                const auto* consumed = stream.consumeView();
                if (consumed) {
                    result.addPositional(std::string(consumed->lexeme()));
                }
                continue;
            }

            if (token->type == TokenType::HelpRequest) {
                (void)stream.consumeView();
                result.helpRequested = true;
                return result;
            }

            auto process_result = processSingleToken(stream, context, result, token);
            if (!process_result) {
                return std::unexpected(process_result.error());
            }

            // If processSingleToken returns true, it means we hit a subcommand help request and
            // should terminate.
            if (*process_result) {
                return result;
            }
        }

        auto default_result = applyOptionDefaults(result, context);
        if (!default_result) {
            return std::unexpected(default_result.error());
        }

        return result;
    }

    /// Parses a `--option` token (with optional `=value` or next-token value).
    [[nodiscard]] static cli_error::VoidResult parseLongOption(TokenStream& stream,
                                                               ParseContext& context,
                                                               ParsedCommand& result) {
        const auto* token = stream.consumeView();
        if (!token) {
            return cli_error::err(cli_error::ErrorCode::InternalError,
                                  "Unexpected end of token stream");
        }
        std::string option_name = token->value;

        // Find the option
        const Option* opt = context.current().findOption(option_name);
        if (!opt) {
            return cli_error::err(cli_error::ErrorCode::UnknownOption,
                                  std::format("Unknown option: --{}", option_name));
        }

        if (opt->needsValue()) {
            std::string value;

            if (token->attachedValue.has_value()) {
                value = *token->attachedValue;
            } else if (stream.hasMore()) {
                const auto* next = stream.consumeView();
                if (!next) {
                    return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                          std::format("Option --{} requires a value", option_name));
                }
                value = std::string(next->lexeme());
            } else {
                return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                      std::format("Option --{} requires a value", option_name));
            }

            auto validation = validateOptionValue(*opt, value, std::format("--{}", option_name));
            if (!validation) {
                return std::unexpected(validation.error());
            }

            result.options.set(canonicalOptionName(*opt, option_name), value);
        } else {
            // Flag option
            result.options.setFlag(canonicalOptionName(*opt, option_name));
        }

        return cli_error::ok();
    }

    /// Parses a single short option `-o` token (with optional attached or next-token value).
    [[nodiscard]] static cli_error::VoidResult parseShortOption(TokenStream& stream,
                                                                ParseContext& context,
                                                                ParsedCommand& result) {
        const auto* token = stream.consumeView();
        if (!token || !token->shortChar) {
            return cli_error::err(cli_error::ErrorCode::InternalError,
                                  "Invalid short option token");
        }

        char c = *token->shortChar;
        const Option* opt = context.current().findOption(c);

        if (!opt) {
            return cli_error::err(cli_error::ErrorCode::UnknownOption,
                                  std::format("Unknown option: -{}", c));
        }

        if (opt->needsValue()) {
            // Check for attached value (e.g., -ovalue)
            std::string value;

            // Check if there's more text after the short char in the token
            if (token->value.size() > 1) {
                value = token->value.substr(1);
            } else if (token->attachedValue.has_value()) {
                value = *token->attachedValue;
            } else if (stream.hasMore()) {
                const auto* consumed = stream.consumeView();
                value = std::string(consumed->lexeme());
            } else {
                return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                      std::format("Option -{} requires a value", c));
            }

            auto validation = validateOptionValue(*opt, value, std::format("-{}", c));
            if (!validation) {
                return std::unexpected(validation.error());
            }

            result.options.set(opt->longName().empty() ? std::string(1, c) : opt->longName(),
                               value);
        } else {
            // Flag option
            result.options.setFlag(opt->longName().empty() ? std::string(1, c) : opt->longName());
        }

        return cli_error::ok();
    }

    /// Runs choices validation and custom validators for an option value.
    [[nodiscard]] static cli_error::VoidResult validateOptionValue(const Option& opt,
                                                                   std::string_view value,
                                                                   std::string_view display_name) {
        if (opt.hasChoices()) {
            const auto& choices = opt.choices();
            if (!std::ranges::any_of(
                    choices, [value](std::string_view choice) { return choice == value; })) {
                return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                      std::format("Invalid value '{}' for {}. Valid values: {}",
                                                  value, display_name, formatChoices(choices)));
            }
        }
        for (const auto& validator : opt.validators()) {
            auto validation = validator(value);
            if (!validation) {
                return std::unexpected(validation.error());
            }
        }
        return cli_error::ok();
    }

    /// Extracts the value for a value-taking option within a combined short group
    /// (e.g. `-ofile.txt` yields `"file.txt"` for `-o`, or `-o file.txt` from the next token).
    [[nodiscard]] static cli_error::Result<std::string> extractCombinedOptionValue(
        char c, const std::string& token_value, std::size_t& index, TokenStream& stream) {
        // If there are more characters in this token, they form the value
        if (index + 1 < token_value.size()) {
            std::string value = token_value.substr(index + 1);
            index = token_value.size();  // Fast-forward processing to the end
            return value;
        }

        // Otherwise, the value must be the next token in the stream
        if (stream.hasMore()) {
            const auto* next = stream.consumeView();
            if (next) {
                return std::string(next->lexeme());
            }
        }

        // Missing argument
        return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                              std::format("Option -{} requires a value", c));
    }

    /// Parses a combined short options token like `-abc`, where `a` and `b` are
    /// flags and `c` may be a flag or a value-taking option consuming subsequent text/tokens.
    [[nodiscard]] static cli_error::VoidResult parseCombinedShortOptions(TokenStream& stream,
                                                                         ParseContext& context,
                                                                         ParsedCommand& result) {
        const auto* token = stream.consumeView();
        if (!token) {
            return cli_error::err(cli_error::ErrorCode::InternalError,
                                  "Unexpected end of token stream");
        }

        for (std::size_t i = 0; i < token->value.size(); ++i) {
            char c = token->value[i];
            const Option* opt = context.current().findOption(c);

            if (!opt) {
                return cli_error::err(cli_error::ErrorCode::UnknownOption,
                                      std::string("Unknown option: -") + c);
            }

            std::string opt_name = opt->longName().empty() ? std::string(1, c) : opt->longName();

            // 1. Handle flag option
            if (!opt->needsValue()) {
                result.options.setFlag(opt_name);
                continue;
            }

            // 2. Handle option requiring a value
            auto value_result = extractCombinedOptionValue(c, token->value, i, stream);
            if (!value_result) {
                return std::unexpected(value_result.error());
            }

            // 3. Validate choices and custom validators before mutating ParsedOptions.
            auto validation = validateOptionValue(*opt, *value_result, std::format("-{}", c));
            if (!validation) {
                return std::unexpected(validation.error());
            }

            // 4. Save
            result.options.set(opt_name, std::move(*value_result));
        }

        return cli_error::ok();
    }

    /// Applies default values layer-by-layer: root first, then each subcommand
    /// along the invocation path. A subcommand default overrides a root default
    /// for the same option only if the option was not explicitly provided.
    [[nodiscard]] static cli_error::VoidResult applyOptionDefaults(ParsedCommand& result,
                                                                   const ParseContext& context) {
        const Command* current = &context.root();

        auto root_defaults = applyOptionDefaultsForCommand(result, *current);
        if (!root_defaults) {
            return std::unexpected(root_defaults.error());
        }

        // Defaults are layered along the selected command path so root/global
        // options and the final subcommand both contribute their configured
        // values without requiring parser users to know where each option lives.
        for (const auto& command_name : context.commandPath()) {
            current = current->findSubcommand(command_name);
            if (!current) {
                return cli_error::err(
                    cli_error::ErrorCode::InternalError,
                    std::format("Parsed command path contains unknown command '{}'", command_name));
            }

            auto defaults = applyOptionDefaultsForCommand(result, *current);
            if (!defaults) {
                return std::unexpected(defaults.error());
            }
        }

        return cli_error::ok();
    }

    /// Applies defaults for a single command to the parsed result.
    [[nodiscard]] static cli_error::VoidResult applyOptionDefaultsForCommand(
        ParsedCommand& result, const Command& command) {
        for (const auto& opt : command.options()) {
            if (!opt.hasDefault()) {
                continue;
            }

            auto name = primaryOptionName(opt);
            if (name.empty() || result.options.has(name) || result.options.isFlag(name)) {
                continue;
            }

            auto validation = validateOptionValue(opt, opt.defaultValueStr(), opt.displayName());
            if (!validation) {
                return std::unexpected(validation.error());
            }
            result.options.set(std::move(name), opt.defaultValueStr());
        }

        return cli_error::ok();
    }

    [[nodiscard]] static std::string formatChoices(const std::vector<std::string>& choices) {
        return choices | std::views::join_with(std::string_view(", ")) | rng::to<std::string>();
    }

    /// Returns the canonical primary name for an option (long name preferred, then short).
    [[nodiscard]] static std::string primaryOptionName(const Option& opt) {
        if (!opt.longName().empty()) {
            return opt.longName();
        }
        if (opt.shortName()) {
            return std::string(1, *opt.shortName());
        }
        return {};
    }

    /// Returns the canonical name used for storage in ParsedOptions: the long
    /// name if available, otherwise the matched name as-is.
    [[nodiscard]] static std::string canonicalOptionName(const Option& opt,
                                                         std::string_view matched_name) {
        if (!opt.longName().empty()) {
            return opt.longName();
        }
        return std::string(matched_name);
    }
};

/**
 * @brief Top-level CLI application orchestrator.
 *
 * `CLI` is the primary entry point for the library. It owns the command tree
 * (root `Command`), wires together parsing, validation, help/version handling,
 * and command execution into a single `run()` call, and holds global
 * configuration such as the application version and `HelpConfig`.
 *
 * ### Lifecycle of a `run()` Call
 *
 *   1. **Parse** — `Parser::parse(args, root_)` tokenizes and parses the raw
 *      arguments into a `ParsedCommand`.
 *   2. **Version check** — If `--version` was parsed and `version()` was set,
 *      print the version and exit with 0.
 *   3. **Help check** — If `--help` (or `-h`) was parsed, print help for the
 *      target command and exit with 0.
 *   4. **Validate** — `Validator::validate()` checks structural constraints.
 *   5. **Execute** — The target command's handler is invoked with the
 *      `ParsedCommand`. The handler's `VoidResult` determines the exit code
 *      (0 on success, 1 on error).
 *
 * ### Program Name Extraction
 *
 * `run()` extracts the basename from `argv[0]` (removing any directory prefix)
 * for use in generated help output and error messages.
 */
class CLI {
public:
    /**
     * @brief Creates a CLI with a root command of the given name.
     * @param name The application name (used in help/usage output).
     */
    static CLI create(std::string name) { return CLI(Command::create(std::move(name))); }

    /**
     * @brief Creates a CLI from an existing pre-configured command tree.
     * @param cmd The root command (typically already populated with options and
     * subcommands).
     */
    static CLI fromCommand(Command cmd) { return CLI(std::move(cmd)); }

    // --- Configuration (fluent builder) ---

    /**
     * @brief Sets the application description (shown in root help).
     */
    template <typename Self>
    Self&& description(this Self&& self, std::string desc) {
        self.root_.description(std::move(desc));
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets the application version string (printed by `--version`).
     */
    template <typename Self>
    Self&& version(this Self&& self, std::string ver) {
        self.version_ = std::move(ver);
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets the author string (informational, shown in help).
     */
    template <typename Self>
    Self&& author(this Self&& self, std::string auth) {
        self.author_ = std::move(auth);
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets a long-form description for the root command.
     */
    template <typename Self>
    Self&& longDescription(this Self&& self, std::string desc) {
        self.root_.longDescription(std::move(desc));
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds a global option (available at the root and all subcommands).
     */
    template <typename Self>
    Self&& option(this Self&& self, Option opt) {
        self.root_.option(std::move(opt));
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds a global boolean flag option.
     */
    template <typename Self>
    Self&& flag(this Self&& self, std::string long_name, char short_name, std::string desc) {
        self.root_.flag(std::move(long_name), short_name, std::move(desc));
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds a global positional argument.
     */
    template <typename Self>
    Self&& argument(this Self&& self, Argument arg) {
        self.root_.argument(std::move(arg));
        return std::forward<Self>(self);
    }

    /**
     * @brief Registers a top-level subcommand.
     */
    template <typename Self>
    Self&& command(this Self&& self, Command cmd) {
        self.root_.subcommand(std::move(cmd));
        return std::forward<Self>(self);
    }

    /**
     * @brief Sets the root handler (invoked when no subcommand matches).
     */
    template <typename Self>
    Self&& handler(this Self&& self, CommandFn fn) {
        self.root_.handler(std::move(fn));
        return std::forward<Self>(self);
    }

    /**
     * @brief Configures help output rendering.
     */
    template <typename Self>
    Self&& helpConfig(this Self&& self, HelpConfig config) {
        self.helpConfig_ = std::move(config);
        return std::forward<Self>(self);
    }

    /**
     * @brief Adds a `--version` / `-V` flag that prints the version and exits.
     */
    template <typename Self>
    Self&& withVersionFlag(this Self&& self) {
        return std::forward<Self>(self).option(
            Option::withName("version", 'V').description("Show version information"));
    }

    /**
     * @brief Adds a `--help` / `-h` flag that prints help and exits.
     *
     * Note: `--help` and `-h` are handled automatically by the parser even if
     * this method is not called. Use this only if you need to customize the
     * description text.
     */
    template <typename Self>
    Self&& withHelpFlag(this Self&& self) {
        return std::forward<Self>(self).option(
            Option::withName("help", 'h').description("Show help information"));
    }

    // --- Execution ---

    /**
     * @brief Parses arguments, validates, and executes the matching handler.
     *
     * This is the primary entry point. It extracts the program name from
     * `args[0]` and delegates to `runWithName()`.
     *
     * @param args Full argv as a span of string views.
     * @return Exit code: 0 on success, 1 on error.
     */
    [[nodiscard]] int run(std::span<std::string_view> args) {
        std::string_view program_name = args.empty() ? "app" : args[0];

        if (auto pos = program_name.find_last_of("/\\"); pos != std::string_view::npos) {
            program_name = program_name.substr(pos + 1);
        }

        return runWithName(args, program_name);
    }

    /**
     * @brief Parses and runs with an explicit program name (for cases where
     * the program name is known separately from the args).
     * @param args Full argv span.
     * @param program_name The executable name to use in help/error output.
     * @return Exit code: 0 on success, 1 on error.
     */
    [[nodiscard]] int runWithName(std::span<std::string_view> args, std::string_view program_name) {
        auto result = Parser::parse(args, root_);
        if (!result) {
            HelpGenerator help_gen{helpConfig_};
            std::println(stderr, "{}", help_gen.formatError(result.error(), root_, program_name));
            return 1;
        }
        const auto& parsed = *result;

        if (parsed.options.isFlag("version") && !version_.empty()) {
            std::println("{} {}", program_name, version_);
            return 0;
        }

        if (parsed.helpRequested || parsed.options.isFlag("help")) {
            printHelp(program_name, findTargetCommand(parsed));
            return 0;
        }

        const Command& target_cmd = findTargetCommand(parsed);
        auto validation = Validator::validate(parsed, target_cmd);

        if (!validation) {
            HelpGenerator gen(helpConfig_);
            std::print(stderr, "{}", gen.formatError(validation.error(), target_cmd, program_name));
            return 1;
        }

        auto exec_result = target_cmd.execute(parsed);

        if (!exec_result) {
            std::println(stderr, "Error: {}", exec_result.error().format());
            return 1;
        }

        return 0;
    }

    /**
     * @brief Parses command-line arguments without executing a handler.
     *
     * Useful for testing or when the caller wants to inspect the parsed
     * result before deciding how to proceed.
     *
     * @param args Full argv span.
     * @return A `ParsedCommand` on success, or a `ParseError` on failure.
     */
    [[nodiscard]] cli_error::Result<ParsedCommand> parse(std::span<std::string_view> args) const {
        return Parser::parse(args, root_);
    }

    /**
     * @brief Returns the root command (const access).
     */
    [[nodiscard]] const Command& root() const noexcept { return root_; }

    /**
     * @brief Returns the root command (mutable access for further configuration).
     */
    [[nodiscard]] Command& root() noexcept { return root_; }

    /**
     * @brief Generates help text for the given command (or root if not specified).
     * @param program_name The executable name for the usage line.
     * @param cmd Optional command to generate help for (defaults to root).
     * @return Formatted help text.
     */
    [[nodiscard]] std::string getHelpText(std::string_view program_name,
                                          const std::optional<Command>& cmd = std::nullopt) const {
        HelpGenerator gen(helpConfig_);
        return gen.generate(cmd.value_or(root_), program_name);
    }

    /**
     * @brief Prints help text to stdout.
     * @param program_name The executable name.
     * @param cmd Optional command to print help for (defaults to root).
     */
    void printHelp(std::string_view program_name,
                   const std::optional<Command>& cmd = std::nullopt) const {
        std::print("{}", getHelpText(program_name, cmd));
    }

private:
    explicit CLI(Command root) : root_(std::move(root)) {}

    /**
     * @brief Walks the parsed command path to find the leaf command that
     * should handle this invocation.
     */
    [[nodiscard]] const Command& findTargetCommand(const ParsedCommand& parsed) const {
        const Command* current = &root_;
        rng::for_each(parsed.commandPath, [&](const std::string& name) {
            const auto* subcmd = current->findSubcommand(name);
            if (subcmd) {
                current = subcmd;
            }
        });
        return *current;
    }

    Command root_;                                    ///< The root command definition.
    std::string version_;                             ///< Application version string.
    std::string author_;                              ///< Application author string.
    HelpConfig helpConfig_ = HelpConfig::defaults();  ///< Help rendering configuration.
};

/**
 * @brief Creates a minimal CLI with a single root command and handler.
 *
 * Convenience for tools that only need one command without subcommands.
 *
 * @param name The application name.
 * @param description Short description for the root help.
 * @param handler The command handler callback.
 * @return A fully configured `CLI` instance ready for `run()`.
 */
[[nodiscard]] inline CLI simple_cli(std::string name, std::string description, CommandFn handler) {
    return CLI::create(std::move(name))
        .description(std::move(description))
        .handler(std::move(handler));
}

/**
 * @brief Creates an option with long and short names, a description, and an
 * optional value-taking flag.
 *
 * @param long_name Long option name (e.g. `"verbose"` for `--verbose`).
 * @param short_name Short option character (e.g. `'v'` for `-v`).
 * @param description Human-readable description for help.
 * @param takes_value If `true`, the option accepts a value argument.
 */
[[nodiscard]] inline Option make_option(std::string long_name,
                                        char short_name,
                                        std::string description,
                                        bool takes_value = false) {
    auto opt =
        Option::withName(std::move(long_name), short_name).description(std::move(description));
    if (takes_value) {
        opt.setTakesValue();
    }
    return opt;
}

/**
 * @brief Creates a boolean flag option (no value argument).
 *
 * @param long_name Long option name.
 * @param short_name Short option character.
 * @param description Human-readable description for help.
 */
[[nodiscard]] inline Option make_flag(std::string long_name,
                                      char short_name,
                                      std::string description) {
    return Option::withName(std::move(long_name), short_name).description(std::move(description));
}

/**
 * @brief Creates a required value-taking option.
 *
 * @param long_name Long option name.
 * @param short_name Short option character.
 * @param description Human-readable description for help.
 * @param value_name Placeholder name for the value in help output.
 */
[[nodiscard]] inline Option make_required_option(std::string long_name,
                                                 char short_name,
                                                 std::string description,
                                                 std::string value_name = "value") {
    return Option::withName(std::move(long_name), short_name)
        .description(std::move(description))
        .setTakesValue()
        .valueName(std::move(value_name))
        .required();
}

/**
 * @brief Creates a positional argument definition.
 *
 * @param name Argument name shown in help/usage.
 * @param description Human-readable description.
 * @param required If `true` (default), the argument must be present.
 */
[[nodiscard]] inline Argument make_argument(std::string name,
                                            std::string description,
                                            bool required = true) {
    auto arg = Argument::create(std::move(name)).description(std::move(description));
    if (required) {
        arg.required();
    } else {
        arg.optional();
    }
    return arg;
}
}  // namespace expp::app::cli

#endif  // EXPP_APP_CLI_HPP
