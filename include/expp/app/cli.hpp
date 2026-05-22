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
#include <map>
#include <memory>
#include <numeric>
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

namespace details {
struct StringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
};
}  // namespace details

namespace cli_error {
/// Error severity levels for diagnostics
enum class ErrorSeverity : std::uint8_t {
    Warning,
    Error,
    Fatal
};

/// Error codes for categorizing failures
enum class ErrorCode : std::uint8_t {
    None = 0,

    // Tokenization errors
    UnknownOption,
    MissingOptionArgument,
    InvalidOptionFormat,

    // Parsing errors
    UnexpectedArgument,
    MissingRequiredOption,
    MissingRequiredArgument,
    UnknownCommand,
    AmbiguousCommand,

    // Validation errors
    InvalidValue,
    ValueOutOfRange,
    ConstraintViolation,

    // Command errors
    CommandNotFound,
    NoCommandProvided,

    // Internal errors
    InternalError
};

class ParseError {
public:
    constexpr ParseError(ErrorCode code,
                         std::string message,
                         ErrorSeverity severity = ErrorSeverity::Error,
                         std::source_location location = std::source_location::current())
        : code_(code)
        , message_(std::move(message))
        , severity_(severity)
        , location_(location) {}

    [[nodiscard]] constexpr ErrorCode code() const noexcept { return code_; }

    [[nodiscard]] constexpr const std::string& message() const noexcept { return message_; }

    [[nodiscard]] constexpr ErrorSeverity severity() const noexcept { return severity_; }

    [[nodiscard]] constexpr const std::source_location& location() const noexcept {
        return location_;
    }

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

/// Result type for operations that can fail
template <typename T>
using Result = std::expected<T, ParseError>;

/// Unit result for operations with no return value
using VoidResult = Result<void>;

/// Helper to create a successful result
template <typename T>
[[nodiscard]] Result<std::remove_cvref_t<T>> ok(T&& value) {
    return std::expected<std::remove_cvref_t<T>, ParseError>(std::in_place, std::forward<T>(value));
}

[[nodiscard]] inline VoidResult ok() {
    return {};
}

/// Helper to create an error result

[[nodiscard]] inline auto err(ErrorCode code,
                              std::string msg,
                              ErrorSeverity severity = ErrorSeverity::Error,
                              std::source_location location = std::source_location::current()) {
    return std::unexpected(ParseError(code, std::move(msg), severity, location));
}
}  // namespace cli_error

class Option {
public:
    static Option withLong(std::string long_name) {
        Option opt;
        opt.longName_ = std::move(long_name);
        return opt;
    }

    static Option withShort(char short_name) {
        Option opt;
        opt.shortName_ = short_name;
        return opt;
    }

    static Option withName(std::string long_name, char short_name) {
        Option opt;
        opt.longName_ = std::move(long_name);
        opt.shortName_ = short_name;
        return opt;
    }

    // --- Configuration ---
    template <typename Self>
    Self&& description(this Self&& self, std::string desc) {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    template <typename Self, typename T>
    Self&& defaultValue(this Self&& self, T value) {
        self.defaultValueStr_ = std::format("{}", value);
        self.hasDefault_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& required(this Self&& self) {
        self.required_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& setTakesValue(this Self&& self) {
        self.takesValue_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& valueName(this Self&& self, std::string name) {
        self.valueName_ = std::move(name);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& alias(this Self&& self, std::string name) {
        self.aliases_.push_back(std::move(name));
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& choices(this Self&& self, std::vector<std::string> valid_values) {
        self.choices_ = std::move(valid_values);
        self.hasChoices_ = true;
        return std::forward<Self>(self);
    }

    // --- Accessors ---
    [[nodiscard]] const std::string& longName() const noexcept { return longName_; }

    [[nodiscard]] std::optional<char> shortName() const noexcept { return shortName_; }

    [[nodiscard]] const std::string& description() const noexcept { return description_; }

    [[nodiscard]] bool needsValue() const noexcept { return takesValue_; }

    [[nodiscard]] bool isRequired() const noexcept { return required_; }

    [[nodiscard]] bool hasDefault() const noexcept { return hasDefault_; }

    [[nodiscard]] const std::string& defaultValueStr() const noexcept { return defaultValueStr_; }

    [[nodiscard]] const std::string& valueName() const noexcept { return valueName_; }

    [[nodiscard]] const std::vector<std::string>& aliases() const noexcept { return aliases_; }

    [[nodiscard]] bool hasChoices() const noexcept { return hasChoices_; }

    [[nodiscard]] const std::vector<std::string>& choices() const noexcept { return choices_; }

    [[nodiscard]] bool matches(std::string_view name) const noexcept {
        return name == longName_ ||
               rng::any_of(aliases_, [&](auto& alias) { return alias == name; });
    }

    [[nodiscard]] bool matchesShort(char short_name) const noexcept {
        return shortName_ && *shortName_ == short_name;
    }

    /// Get display name for help
    [[nodiscard]] std::string displayName() const noexcept {
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

    /// Get usage string for help
    [[nodiscard]] std::string usageString() const {
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
    bool takesValue_ = false;
    bool required_ = false;
    bool hasDefault_ = false;
    bool hasChoices_ = false;
};

/// Represents a positional argument definition
class Argument {
public:
    static Argument create(std::string name) {
        Argument arg;
        arg.name_ = std::move(name);
        return arg;
    }

    template <typename Self>
    Self&& description(this Self&& self, std::string desc) {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& required(this Self&& self) {
        self.required_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& optional(this Self&& self) {
        self.required_ = false;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& variadic(this Self&& self) {
        self.variadic_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self, typename T>
    Self&& defaultValue(this Self&& self, T value) {
        self.defaultValueStr_ = std::format("{}", value);
        self.hasDefault_ = true;
        return std::forward<Self>(self);
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    [[nodiscard]] const std::string& description() const noexcept { return description_; }

    [[nodiscard]] bool isRequired() const noexcept { return required_; }

    [[nodiscard]] bool isVariadic() const noexcept { return variadic_; }

    [[nodiscard]] bool hasDefault() const noexcept { return hasDefault_; }

    [[nodiscard]] const std::string& defaultValueStr() const noexcept { return defaultValueStr_; }

    [[nodiscard]] std::string usageString() const {
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

// TODO: add concepts to check the T type
template <typename T>
struct ValueParser {
    static cli_error::Result<T> parse(std::string_view str) {
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
            if (ec == std::errc()) {
                return cli_error::ok(value);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Invalid integer value: '{}'", str));
        } else if constexpr (std::is_floating_point_v<T>) {
            T value{};
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);  // NOLINT
            if (ec == std::errc()) {
                return cli_error::ok(value);
            }
            return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                  std::format("Invalid floating point value: '{}'", str));
        } else {
            // try to use >> operator
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

/// Holds parsed option values
class ParsedOptions {
public:
    [[nodiscard]] bool has(std::string_view name) const noexcept { return options_.contains(name); }

    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view name) const {
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

    // TODO: universal forwarding for default value ???
    template <typename T>
    [[nodiscard]] T getOr(std::string_view name, T default_value) const {
        auto value = get<T>(name);
        return value.value_or(std::move(default_value));
    }

    /// Set an option value
    void set(std::string name, std::string value) { options_[std::move(name)] = std::move(value); }

    /// Mark a flag as present
    void setFlag(std::string name) { flags_.insert(std::move(name)); }

    /// Check if a flag is set
    [[nodiscard]] bool isFlag(std::string_view name) const { return flags_.contains(name); }

    /// Get positional arguments
    [[nodiscard]] const std::vector<std::string>& positional() const noexcept {
        return positional_;
    }

    /// Add a positional argument
    void addPositional(std::string value) { positional_.push_back(std::move(value)); }

    /// Get raw option string
    [[nodiscard]] std::optional<std::string> rawOption(std::string_view name) const {
        auto it = options_.find(name);
        if (it == options_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    // Using unordered_map/set with transparent hashing to allow lookup by
    // string_view without constructing temporary strings
    using MapType =
        std::unordered_map<std::string, std::string, details::StringHash, std::equal_to<>>;
    using SetType = std::unordered_set<std::string, details::StringHash, std::equal_to<>>;
    // if using map and set
    // using MapType = std::map<std::string, std::string, std::less<>>;
    // using SetType = std::set<std::string, std::less<>>;

    MapType options_;
    SetType flags_;
    std::vector<std::string> positional_;
};

/// Result of parsing a command
struct ParsedCommand {
    std::string name;
    ParsedOptions options;
    std::vector<std::string> positionalArgs;
    std::vector<std::string> commandPath;  // For nested commands: ["tool", "config", "set"]
    bool helpRequested = false;

    /// Get the invoked command path as a string
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

    /// Add a positional argument
    void addPositional(std::string value) { positionalArgs.push_back(std::move(value)); }
};

/// Handler function type
using CommandFn = std::function<cli_error::VoidResult(const ParsedCommand&)>;

class Command {
public:
    static Command create(std::string name) {
        Command cmd;
        cmd.name_ = std::move(name);
        return cmd;
    }

    // --- Configuration ---
    template <typename Self>
    Self&& description(this Self&& self, std::string desc) {
        self.description_ = std::move(desc);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& longDescription(this Self&& self, std::string desc) {
        self.detailedDescription_ = std::move(desc);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& option(this Self&& self, Option opt) {
        if (opt.longName().empty() && !opt.shortName()) {
            // Invalid option - should have at least one name
            return std::forward<Self>(self);
        }

        // Register in lookup maps
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

    template <typename Self>
    Self&& argument(this Self&& self, Argument arg) {
        // Only one variadic argument allowed, and it must be last
        if (!self.arguments_.empty() && self.arguments_.back().isVariadic()) {
            // Cannot add argument after variadic - just skip
            return std::forward<Self>(self);
        }

        self.arguments_.push_back(std::move(arg));
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& subcommand(this Self&& self, Command sub) {
        std::string name = sub.name();  // Copy the name before moving
        self.subcommands_[name] = std::make_shared<Command>(std::move(sub));
        // self.subcommands_[name] = std::move(std::make_unique<Command>(std::move(sub)));
        // self.subcommands_[name].reset(new Command(std::move(sub)));
        // self.subcommands_[name]
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& handler(this Self&& self, CommandFn fn) {
        self.handler_ = std::move(fn);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& flag(this Self&& self, std::string long_name, char short_name, std::string desc) {
        return std::forward<Self>(self).option(
            Option::withName(std::move(long_name), short_name).description(std::move(desc)));
    }

    template <typename Self>
    Self&& hidden(this Self&& self) {
        self.hidden_ = true;
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& usage(this Self&& self, std::string usage_str) {
        self.usageOverride_ = std::move(usage_str);
        return std::forward<Self>(self);
    }

    // --- Accessors ---
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    [[nodiscard]] const std::string& description() const noexcept { return description_; }

    [[nodiscard]] const std::string& longDescription() const noexcept {
        return detailedDescription_;
    }

    [[nodiscard]] const std::vector<Option>& options() const noexcept { return options_; }

    [[nodiscard]] const std::vector<Argument>& arguments() const noexcept { return arguments_; }

    [[nodiscard]] const auto& subcommands() const noexcept { return subcommands_; }

    [[nodiscard]] bool isHidden() const noexcept { return hidden_; }

    [[nodiscard]] const std::string& usageOverride() const noexcept { return usageOverride_; }

    [[nodiscard]] const Option* findOption(std::string_view name) const {
        auto it = longOptionMap_.find(name);
        if (it != longOptionMap_.end() && it->second < options_.size()) {
            return &options_[it->second];
        }
        return nullptr;
    }

    [[nodiscard]] const Option* findOption(char short_name) const {
        auto it = shortOptionMap_.find(short_name);
        if (it != shortOptionMap_.end() && it->second < options_.size()) {
            return &options_[it->second];
        }
        return nullptr;
    }

    [[nodiscard]] const Command* findSubcommand(std::string_view name) const {
        auto it = subcommands_.find(name);
        if (it != subcommands_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    [[nodiscard]] bool hasSubcommands() const noexcept { return !subcommands_.empty(); }

    [[nodiscard]] cli_error::VoidResult execute(const ParsedCommand& parsed) const {
        if (handler_) {
            return handler_(parsed);
        }
        return cli_error::ok();
    }

    /// Get visible subcommands (for help)
    [[nodiscard]] std::vector<const Command*> visibleSubcommands() const {
        std::vector<const Command*> result;
        for (const auto& [name, cmd] : subcommands_) {
            if (!cmd->isHidden()) {
                result.push_back(cmd.get());
            }
        }
        return result;
    }

    /// Generate usage string for this command
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

        // Arguments
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
    // using SetType = std::unordered_set<std::string, details::StringHash, std::equal_to<>>;
    // if using map and set
    // using MapType = std::map<std::string, std::string, std::less<>>;
    // using SetType = std::set<std::string, std::less<>>;

    Command() = default;
    std::string name_;
    std::string description_;
    std::string detailedDescription_;
    std::string usageOverride_;

    std::vector<Option> options_;
    std::vector<Argument> arguments_;

    MapType<std::shared_ptr<Command>> subcommands_;
    MapType<std::size_t> longOptionMap_;
    std::unordered_map<char, std::size_t> shortOptionMap_;

    CommandFn handler_;
    bool hidden_ = false;
};

/// Context passed during parsing
class ParseContext {
public:
    explicit ParseContext(const Command& root) : root_(&root), current_(&root) {}

    /// Get the root command
    [[nodiscard]] const Command& root() const noexcept { return *root_; }

    /// Get the current command being parsed
    [[nodiscard]] const Command& current() const noexcept { return *current_; }

    /// navigate to a subcommand
    void navigateToSubcommand(const Command* subcmd) {
        if (subcmd) {
            current_ = subcmd;
            commandPath_.push_back(subcmd->name());
        }
    }

    /// Get the command path
    [[nodiscard]] const std::vector<std::string>& commandPath() const noexcept {
        return commandPath_;
    }

private:
    const Command* root_;
    const Command* current_;
    std::vector<std::string> commandPath_;
};

/// ANSI color codes for terminal output
namespace color {
constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBold = "\033[1m";
constexpr std::string_view kDim = "\033[2m";
constexpr std::string_view kItalic = "\033[3m";
constexpr std::string_view kUnderline = "\033[4m";

// Foreground colors
constexpr std::string_view kBlack = "\033[30m";
constexpr std::string_view kRed = "\033[31m";
constexpr std::string_view kGreen = "\033[32m";
constexpr std::string_view kYellow = "\033[33m";
constexpr std::string_view kBlue = "\033[34m";
constexpr std::string_view kMagenta = "\033[35m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kWhite = "\033[37m";

// Bright foreground colors
constexpr std::string_view kBrightBlack = "\033[90m";
constexpr std::string_view kBrightRed = "\033[91m";
constexpr std::string_view kBrightGreen = "\033[92m";
constexpr std::string_view kBrightYellow = "\033[93m";
constexpr std::string_view kBrightBlue = "\033[94m";
constexpr std::string_view kBrightMagenta = "\033[95m";
constexpr std::string_view kBrightCyan = "\033[96m";
constexpr std::string_view kBrightWhite = "\033[97m";
}  // namespace color

struct HelpConfig {
    bool useColors = true;
    bool showDefaults = true;
    bool showRequired = true;

    std::size_t maxDescriptionWidth = 80;
    std::size_t optionIndent = 4;
    std::size_t descriptionIndent = 24;
    std::string programName;

    static HelpConfig defaults() {
        HelpConfig config;
        config.useColors = isTerminal();
        return config;
    }

    /// check if stdout is a terminal, if not, disable colors so that help output
    /// can be piped to other commands or files without escape codes
    static bool isTerminal() {
        // Check if stdout is a terminal
        return isatty(STDOUT_FILENO) != 0;
    }
};

/// Generates help text for commands
class HelpGenerator {
public:
    explicit HelpGenerator(HelpConfig config = HelpConfig::defaults())
        : config_(std::move(config)) {}

    /// Generate full help for a command
    // TODO: write a benchmark to compare the performance between generate and generate2.
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
        // Long description (if present)
        if (!cmd.longDescription().empty()) {
            std::format_to(
                out, "{}\n\n",
                wrapText(cmd.longDescription(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        // Positional arguments
        // clang-format off
        if (!cmd.arguments().empty()) {
            std::format_to(out, "{}\n{}\n",
                        formatSectionHeader("Arguments"),
                        cmd.arguments()
                            | views::transform([this](const Argument& arg) {return
                            formatArgument(arg);}) | views::join | rng::to<std::string>());
        }
        // Options
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

        // Subcommands
        auto visible_subcmds = cmd.visibleSubcommands();
        if (!visible_subcmds.empty()) {
            // Find max command name width for alignment
            // std::size_t max_name_width = 0;
            // for (const auto* subcmd : visible_subcmds) {
            //     max_name_width = std::max(max_name_width, subcmd->name().size());
            // }
            // clang-format off
            std::size_t max_name_width = rng::fold_left(visible_subcmds
                                | views::transform(&Command::name)
                                | views::transform(&std::string::size),
                            0U,
                            [](std::size_t acc, std::size_t size) {
                                return std::max(acc, size);
                            });

            // std::size_t max_name_width = std::ranges::fold_left()
            std::format_to(out, "{}\n{}\n", formatSectionHeader("Commands"),
                visible_subcmds
                    | views::transform([this, max_name_width](const Command* subcmd) {return
                    formatSubCommand(*subcmd, max_name_width);}) | views::join |
                    rng::to<std::string>()
            );
            // clang-format on
        }

        return result;
    }

    [[nodiscard]] std::string generate(const Command& cmd, std::string_view program_name) const {
        std::string result;

        result.reserve(2048);  // NOLINT

        auto out = std::back_inserter(result);

        // Usage section
        std::format_to(out, "{}\n{}{}\n\n", formatSectionHeader("Usage"),
                       formatIndent(config_.optionIndent), cmd.usageString(program_name));

        // Description (if present)
        if (!cmd.description().empty()) {
            std::format_to(
                out, "{}\n{}\n\n", formatSectionHeader("Description"),
                wrapText(cmd.description(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        // Long description (if present)
        if (!cmd.longDescription().empty()) {
            std::format_to(
                out, "{}\n\n",
                wrapText(cmd.longDescription(), config_.maxDescriptionWidth, config_.optionIndent));
        }

        // Positional arguments
        if (!cmd.arguments().empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Arguments"));
            for (const auto& arg : cmd.arguments()) {
                result += formatArgument(arg);
            }
            result += '\n';
        }

        // Options
        if (!cmd.options().empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Options"));
            for (const auto& opt : cmd.options()) {
                result += formatOption(opt);
            }
            result += '\n';
        }

        // Subcommands
        auto visible_subcmds = cmd.visibleSubcommands();
        if (!visible_subcmds.empty()) {
            std::format_to(out, "{}\n", formatSectionHeader("Commands"));

            // Find max command name width for alignment
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

    /// Generate short usage string
    [[nodiscard]] static std::string shortUsage(const Command& cmd, std::string_view program_name) {
        return cmd.usageString(program_name);
    }

    /// Generate error message with context
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
    [[nodiscard]] std::string formatSectionHeader(std::string_view title) const {
        return colorText(title, color::kBrightCyan, true) + ":";
    }

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

    [[nodiscard]] std::string formatArgument(const Argument& arg) const {
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

    [[nodiscard]] static std::string formatIndent(std::size_t n) { return std::string(n, ' '); }

    [[nodiscard]] std::string colorText(std::string_view text,
                                        std::string_view color_code,
                                        bool bold) const {
        if (!config_.useColors) {
            return std::string(text);
        }
        return std::format("{}{}{}{}", bold ? color::kBold : "", color_code, text, color::kReset);
    }

    [[nodiscard]] static std::string wrapText(std::string_view text,
                                              std::size_t max_width,
                                              std::size_t indent) {
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
        // Simple word wrap

        while (pos < text.size()) {
            // Skip leading spaces
            std::size_t word_start = text.find_first_not_of(' ', pos);
            if (word_start == std::string_view::npos) {
                break;
            }
            // Find next word
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

    [[nodiscard]] static std::string formatChoices(const std::vector<std::string>& choices) {
        auto formatted = choices | std::views::join_with(std::string_view(", "));
        return std::ranges::to<std::string>(formatted);
    }

    HelpConfig config_;
};

/// Validates parsed options against command definition
class Validator {
    /// Validate a parsed command
public:
    [[nodiscard]] static cli_error::VoidResult validate(const ParsedCommand& parsed,
                                                        const Command& cmd) {
        // Check required options
        for (const auto& opt : cmd.options()) {
            if (opt.isRequired() && opt.needsValue()) {
                bool has_value = false;

                // check by long name
                if (!opt.longName().empty()) {
                    has_value = parsed.options.has(opt.longName());
                }

                // check by short name if no long name or value not found
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

        // check required arguments
        auto [required_count, has_variadic] = std::ranges::fold_left(
            cmd.arguments(), std::make_pair(0U, false),
            [](std::pair<std::size_t, bool> acc,
               const Argument& arg) -> std::pair<std::size_t, bool> {
                return {acc.first + static_cast<std::size_t>(arg.isRequired() & !arg.isVariadic()),
                        acc.second | arg.isVariadic()};
            });

        if (parsed.positionalArgs.size() < required_count) {
            // Find which argument is missing
            std::size_t arg_idx = parsed.positionalArgs.size();
            if (arg_idx < cmd.arguments().size()) {
                const auto& missing_arg = cmd.arguments()[arg_idx];
                return cli_error::err(
                    cli_error::ErrorCode::MissingRequiredArgument,
                    std::format("Required argument '{}' is missing", missing_arg.name()));
            }
        }

        // Validate argument count if no variadic
        if (!has_variadic && parsed.positionalArgs.size() > cmd.arguments().size()) {
            return cli_error::err(cli_error::ErrorCode::UnexpectedArgument,
                                  std::format("Unexpected argument: '{}'",
                                              parsed.positionalArgs[cmd.arguments().size()]));
        }

        return cli_error::ok();
    }

    /// Validate a single value against choices
    [[nodiscard]] static bool validateChoices(const std::string& value,
                                              const std::vector<std::string>& choices) {
        return choices.empty() ||
               rng::any_of(choices, [&](const std::string& choice) { return choice == value; });
    }

    /// Validate numeric range
    template <typename T>
        requires std::is_arithmetic_v<T>
    [[nodiscard]] static bool validateRange(T value, T min, T max) {
        return value >= min && value <= max;
    }
};

/// Token types for the lexer
enum class TokenType : std::uint8_t {
    LongOption,    // --option
    ShortOption,   // -o
    ShortOptions,  // -abc (combined short options)
    OptionValue,   // value after option
    PositionArg,   // standalone argument
    Command,       // subcommand name
    DoubleDash,    // -- (ends option parsing)
    HelpRequest    // --help or -h in help mode
};

/// Represents a single token from command line
struct Token {
    TokenType type = TokenType::HelpRequest;
    std::size_t originalIndex{};  // Index in original argv
    std::string value;
    std::optional<char> shortChar;      // For short options, which character
    std::optional<std::string> source;  // Original string before parsing

    std::optional<std::string>
        attachedValue;  // For options with attached values (e.g. --option=value or -ovalue)

    [[nodiscard]] std::string_view name() const noexcept { return value; }

    [[nodiscard]] bool isOption() const noexcept {
        return type == TokenType::LongOption || type == TokenType::ShortOption ||
               type == TokenType::ShortOptions;
    }
};

/// Tokenizes command line arguments
class Tokenizer {
public:
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

    /// Tokenize from argc/argv

private:
    // TODO: this function has some logical mess and errors
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
        // Long option: --option or --option=value
        if (arg.starts_with("--")) {
            // if (arg.size() == 2) {
            //     return cli_error::err(cli_error::ErrorCode::InvalidOptionFormat,
            //                           "Empty long option '--'");
            // } // this is impossible

            auto rest = arg.substr(2);
            if (auto eq_pos = rest.find('='); eq_pos != std::string_view::npos) {
                // --option=value form
                token.type = TokenType::LongOption;
                token.value = std::string(rest.substr(0, eq_pos));
                token.attachedValue = std::string(rest.substr(eq_pos + 1));
                // token.value = std::string(rest.substr(eq_pos + 1));
                // Note: the value part will be handled by the parser
            } else {
                token.type = TokenType::LongOption;
                token.value = std::string(rest);
            }
            return token;
        }

        // Short option: -o or -abc
        if (arg.starts_with('-')) {
            if (arg.size() == 1) {
                return cli_error::err(cli_error::ErrorCode::InvalidOptionFormat,
                                      "Invalid option '-'");
            }
            auto rest = arg.substr(1);
            // Check if it's a number (negative number)
            if (std::isdigit(static_cast<unsigned char>(rest[0])) != 0 || rest[0] == '.') {
                token.type = TokenType::PositionArg;
                token.value = std::string(arg);
                return token;
            }

            // // check for attached value: -ovalue
            // if (rest.size() > 1) {
            //     // Could be -abc (multiple flags) or -ovalue (option with value)
            //     // We'll parse as combined short options; parser will handle value
            //     // attachment
            //     if (auto eq_pos = rest.find('='); eq_pos != std::string_view::npos) {
            //         token.type = TokenType::ShortOption;
            //         token.shortChar = rest[0];
            //         // token.value = std::string(rest.substr(0, eq_pos));
            //         token.value = std::string(rest.substr(eq_pos + 1));
            //     } else {
            //         token.type = TokenType::ShortOptions;
            //         token.value = std::string(rest);
            //     }
            // } else {
            //     token.type = TokenType::ShortOption;
            //     token.value = std::string(rest);
            //     token.shortChar = rest[0];
            // }

            // check for attached value: -ovalue or -o=value
            if (auto eq_pos = rest.find('='); eq_pos != std::string_view::npos) {
                std::string_view opt_name = rest.substr(0, eq_pos);
                if (opt_name.size() == 1) {
                    token.type = TokenType::ShortOption;
                    token.shortChar = opt_name[0];
                } else {
                    token.type = TokenType::ShortOptions;  // like -abc=val
                }
                token.value = std::string(opt_name);  // store the option part
                token.attachedValue = std::string(rest.substr(eq_pos + 1));  // store the value part
            }
            // no equals sign, but length > 1: e.g., -abc
            else if (rest.size() > 1) {
                token.type = TokenType::ShortOptions;
                token.value = std::string(rest);
            }
            // single short option: e.g., -o
            else {
                token.type = TokenType::ShortOption;
                token.value = std::string(rest);
                token.shortChar = rest[0];
            }

            return token;
        }

        // Positional argument or command
        token.type = TokenType::PositionArg;
        token.value = std::string(arg);
        return token;
    }

    bool helpAsFlag_ = false;  // If true, -h is treated as a regular option
};

/// Token stream for easier parsing
class TokenStream {
public:
    explicit TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    /// Check if there are more tokens
    [[nodiscard]] bool hasMore() const noexcept { return position_ < tokens_.size(); }

    /// Get current token without consuming
    [[nodiscard]] const Token* peek() const noexcept {
        if (!hasMore()) {
            return nullptr;
        }
        return &tokens_[position_];
    }

    /// Peek at next token after current
    [[nodiscard]] const Token* peekNext() const noexcept {
        if (position_ + 1 >= tokens_.size()) {
            return nullptr;
        }
        return &tokens_[position_ + 1];
    }

    /// Consume and return current token
    [[nodiscard]] std::optional<Token> consume() {
        if (!hasMore()) {
            return std::nullopt;
        }
        return tokens_[position_++];
    }

    /// Consume if the next token matches a predicate
    template <typename Pred>
        requires std::predicate<Pred, const Token&>
    [[nodiscard]] std::optional<Token> consumeIf(Pred&& pred) {
        if (!hasMore()) {
            return std::nullopt;
        }
        if (std::invoke(std::forward<Pred>(pred), tokens_[position_])) {
            return tokens_[position_++];
        }
        return std::nullopt;
    }

    /// Get current position
    [[nodiscard]] std::size_t position() const noexcept { return position_; }

    /// Set position (for backtracking)
    void setPosition(std::size_t pos) noexcept { position_ = std::min(pos, tokens_.size()); }

    /// Get remaining tokens as positional arguments
    [[nodiscard]] auto remaining() const {
        // std::vector<Token> result;
        // for (std::size_t i = position_; i < tokens_.size(); ++i) {
        //     result.push_back(tokens_[i]);
        // }
        // return result;
        using DiffType = std::vector<Token>::difference_type;
        return rng::subrange(tokens_.begin() + static_cast<DiffType>(position_), tokens_.end());
    }

    /// Get all tokens
    [[nodiscard]] const std::vector<Token>& all() const noexcept { return tokens_; }

    /// Check if we've seen -- (double dash)
    [[nodiscard]] bool seenDoubleDash() const noexcept { return seenDoubleDash_; }

    /// Mark that we've seen --
    void markDoubleDash() noexcept { seenDoubleDash_ = true; }

private:
    std::vector<Token> tokens_;
    std::size_t position_{};
    bool seenDoubleDash_ = false;
};

/// Parses command line arguments into a ParsedCommand
class Parser {
public:
    /// Parse command line arguments
    [[nodiscard]] static cli_error::Result<ParsedCommand> parse(std::span<std::string_view> args,
                                                                const Command& root) {
        Tokenizer tokenizer;
        auto token_result = tokenizer.tokenize(args);
        if (!token_result) {
            return std::unexpected(token_result.error());
        }
        return parseTokens(*token_result, root);
    }

private:
    [[nodiscard]] static cli_error::Result<bool> handleSubcommandOrPositional(TokenStream& stream,
                                                                              ParseContext& context,
                                                                              ParsedCommand& result,
                                                                              const Token* token) {
        if (context.current().hasSubcommands() && result.positionalArgs.empty()) {
            const auto* subcmd = context.current().findSubcommand(token->value);
            if (subcmd) {
                (void)stream.consume();
                context.navigateToSubcommand(subcmd);
                result.commandPath = context.commandPath();
                result.name = subcmd->name();

                // Check for help after subcommand
                if (stream.hasMore() && stream.peek()->type == TokenType::HelpRequest) {
                    (void)stream.consume();
                    result.helpRequested = true;
                    return true;  // Indicates parsing is done (help requested)
                }
                return false;  // Subcommand handled, parsing continues
            }
        }
        // Not a subcommand, treat as positional
        auto t = stream.consume();
        result.addPositional(t->value);
        return false;
    }

    [[nodiscard]] static cli_error::Result<bool> processSingleToken(TokenStream& stream,
                                                                    ParseContext& context,
                                                                    ParsedCommand& result,
                                                                    const Token* token) {
        cli_error::VoidResult parse_result;

        switch (token->type) {
            case TokenType::DoubleDash:
                (void)stream.consume();
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
                auto t = stream.consume();
                result.addPositional(t->value);
                break;
        }

        if (!parse_result) {
            return std::unexpected(parse_result.error());
        }
        return false;  // Return 'false' to indicate parsing should continue
    }

    [[nodiscard]] static cli_error::Result<ParsedCommand> parseTokens(std::vector<Token> tokens,
                                                                      const Command& root) {
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

            if (token->type == TokenType::HelpRequest) {
                (void)stream.consume();
                result.helpRequested = true;
                return result;
            }

            if (stream.seenDoubleDash()) {
                auto t = stream.consume();
                result.addPositional(t->value);
                continue;
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

        return result;
    }

    // [[nodiscard]] static cli_error::Result<ParsedCommand> parseTokens_x(std::vector<Token>
    // tokens,
    //                                                                   const Command& root) {
    //     TokenStream stream{std::move(tokens)};
    //     ParseContext context(root);
    //     ParsedCommand result;
    //     result.name = root.name();

    //     // check for help request at the start
    //     if (auto token =
    //             stream.consumeIf([](const Token& t) { return t.type == TokenType::HelpRequest;
    //             })) {
    //         result.helpRequested = true;
    //         return result;
    //     }

    //     while (stream.hasMore()) {
    //         const auto* token = stream.peek();

    //         if (!token) {
    //             break;
    //         }

    //         if (token->type == TokenType::HelpRequest) {
    //             (void)stream.consume();
    //             result.helpRequested = true;
    //             return result;
    //         }

    //         if (stream.seenDoubleDash()) {
    //             auto t = stream.consume();
    //             result.addPositional(t.value().value);
    //             continue;
    //         }
    //         cli_error::VoidResult parse_result;
    //         switch (token->type) {
    //             case TokenType::DoubleDash:
    //                 (void)stream.consume();
    //                 stream.markDoubleDash();
    //                 break;

    //             case TokenType::LongOption:
    //                 parse_result = parseLongOption(stream, context, result);
    //                 if (!parse_result) {
    //                     return std::unexpected(parse_result.error());
    //                 }
    //                 break;

    //             case TokenType::ShortOption:
    //                 parse_result = parseShortOption(stream, context, result);
    //                 if (!parse_result) {
    //                     return std::unexpected(parse_result.error());
    //                 }
    //                 break;

    //             case TokenType::ShortOptions:
    //                 parse_result = parseCombinedShortOptions(stream, context, result);
    //                 if (!parse_result) {
    //                     return std::unexpected(parse_result.error());
    //                 }
    //                 break;

    //             case TokenType::PositionArg:
    //                 // Could be a subcommand or an argument
    //                 if (context.current().hasSubcommands() && result.positionalArgs.empty()) {
    //                     const auto* subcmd = context.current().findSubcommand(token->value);
    //                     if (subcmd) {
    //                         (void)stream.consume();
    //                         context.navigateToSubcommand(subcmd);
    //                         result.commandPath = context.commandPath();
    //                         result.name = subcmd->name();

    //                         // Check for help after subcommand
    //                         if (stream.hasMore() && stream.peek()->type ==
    //                         TokenType::HelpRequest) {
    //                             (void)stream.consume();
    //                             result.helpRequested = true;
    //                             return result;
    //                         }
    //                         break;
    //                     }
    //                 }
    //                 // Not a subcommand, treat as positional
    //                 {
    //                     auto t = stream.consume();
    //                     result.addPositional(t->value);
    //                 }
    //                 break;

    //             default:
    //                 auto t = stream.consume();
    //                 result.addPositional(t->value);
    //                 break;
    //         }
    //     }

    //     return result;
    // }

    [[nodiscard]] static cli_error::VoidResult parseLongOption(TokenStream& stream,
                                                               ParseContext& context,
                                                               ParsedCommand& result) {
        auto token = stream.consume();
        if (!token) {
            return cli_error::err(cli_error::ErrorCode::InternalError,
                                  "Unexpected end of token stream");
        }

        // Check if option contains '='
        // auto value_pos = token->value.find('=');
        // std::string option_name;
        // std::string attached_value;

        // if (value_pos != std::string::npos) {
        //     option_name = token->value.substr(0, value_pos);
        //     attached_value = token->value.substr(value_pos + 1);
        // } else {
        //     option_name = token->value;
        // }
        std::string option_name = token->value;
        std::string attached_value = token->attachedValue.value_or("");

        // Find the option
        const Option* opt = context.current().findOption(option_name);
        if (!opt) {
            return cli_error::err(cli_error::ErrorCode::UnknownOption,
                                  std::format("Unknown option: --{}", option_name));
        }

        if (opt->needsValue()) {
            std::string value;

            if (!attached_value.empty()) {
                value = attached_value;
            } else if (stream.hasMore()) {
                auto next = stream.consume();
                if (!next) {
                    return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                          std::format("Option --{} requires a value", option_name));
                }
                value = next->value;
            } else {
                return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                      std::format("Option --{} requires a value", option_name));
            }

            // Validate choices if specified
            if (opt->hasChoices()) {
                const auto& choices = opt->choices();
                if (std::ranges::find(choices, value) == choices.end()) {
                    return cli_error::err(
                        cli_error::ErrorCode::InvalidValue,
                        std::format("Invalid value '{}' for --{}. Valid values: {}", value,
                                    option_name, formatChoices(choices)));
                }
            }

            result.options.set(option_name, value);
        } else {
            // Flag option
            result.options.setFlag(option_name);
        }

        return cli_error::ok();
    }

    [[nodiscard]] static cli_error::VoidResult parseShortOption(TokenStream& stream,
                                                                ParseContext& context,
                                                                ParsedCommand& result) {
        auto token = stream.consume();
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
                const auto* next = stream.peek();
                if (next && (next->type == TokenType::PositionArg ||
                             next->type == TokenType::OptionValue)) {
                    auto consumed = stream.consume();
                    value = consumed->value;
                } else {
                    return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                          std::format("Option -{} requires a value", c));
                }
            } else {
                return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                                      std::format("Option -{} requires a value", c));
            }

            // Validate choices if specified
            if (opt->hasChoices()) {
                const auto& choices = opt->choices();
                if (std::ranges::find(choices, value) == choices.end()) {
                    return cli_error::err(
                        cli_error::ErrorCode::InvalidValue,
                        std::format("Invalid value '{}' for -{}. Valid values: {}", value, c,
                                    formatChoices(choices)));
                }
            }

            result.options.set(opt->longName().empty() ? std::string(1, c) : opt->longName(),
                               value);
        } else {
            // Flag option
            result.options.setFlag(opt->longName().empty() ? std::string(1, c) : opt->longName());
        }

        return cli_error::ok();
    }

    [[nodiscard]] static cli_error::VoidResult validateOptionChoice(const Option& opt,
                                                                    const std::string& value,
                                                                    std::string_view display_name) {
        if (opt.hasChoices()) {
            const auto& choices = opt.choices();
            if (std::ranges::find(choices, value) == choices.end()) {
                return cli_error::err(cli_error::ErrorCode::InvalidValue,
                                      std::format("Invalid value '{}' for {}. Valid values: {}",
                                                  value, display_name, formatChoices(choices)));
            }
        }
        return cli_error::ok();
    }

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
            auto next = stream.consume();
            if (next) {
                return next->value;
            }
        }

        // Missing argument
        return cli_error::err(cli_error::ErrorCode::MissingOptionArgument,
                              std::format("Option -{} requires a value", c));
    }

    [[nodiscard]] static cli_error::VoidResult parseCombinedShortOptions(TokenStream& stream,
                                                                         ParseContext& context,
                                                                         ParsedCommand& result) {
        auto token = stream.consume();
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

            // 3. Validate choices
            auto validation = validateOptionChoice(*opt, *value_result, std::format("-{}", c));
            if (!validation) {
                return std::unexpected(validation.error());
            }

            // 4. Save
            result.options.set(opt_name, std::move(*value_result));
        }

        return cli_error::ok();
    }

    [[nodiscard]] static std::string formatChoices(const std::vector<std::string>& choices) {
        return choices | std::views::join_with(std::string_view(", ")) | rng::to<std::string>();
    }
};

// main cli application class
class CLI {
public:
    /// Create a CLI application with the given name
    static CLI create(std::string name) { return CLI(Command::create(std::move(name))); }

    /// Create a CLI from an existing command
    static CLI fromCommand(Command cmd) { return CLI(std::move(cmd)); }

    // --- Configuration ---
    template <typename Self>
    Self&& description(this Self&& self, std::string desc) {
        self.root_.description(std::move(desc));
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& version(this Self&& self, std::string ver) {
        self.version_ = std::move(ver);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& author(this Self&& self, std::string auth) {
        self.author_ = std::move(auth);
        return std::forward<Self>(self);
    }

    template <typename Self>
    Self&& longDescription(this Self&& self, std::string desc) {
        self.root_.longDescription(std::move(desc));
        return std::forward<Self>(self);
    }

    /// Add a global option
    template <typename Self>
    Self&& option(this Self&& self, Option opt) {
        self.root_.option(std::move(opt));
        return std::forward<Self>(self);
    }

    /// Add a global flag
    template <typename Self>
    Self&& flag(this Self&& self, std::string long_name, char short_name, std::string desc) {
        self.root_.flag(std::move(long_name), short_name, std::move(desc));
        return std::forward<Self>(self);
    }

    /// Add a positional argument
    template <typename Self>
    Self&& argument(this Self&& self, Argument arg) {
        self.root_.argument(std::move(arg));
        return std::forward<Self>(self);
    }

    /// Add a command
    template <typename Self>
    Self&& command(this Self&& self, Command cmd) {
        self.root_.subcommand(std::move(cmd));
        return std::forward<Self>(self);
    }

    /// Set the root handler (for when no subcommand is used)
    template <typename Self>
    Self&& handler(this Self&& self, CommandFn fn) {
        self.root_.handler(std::move(fn));
        return std::forward<Self>(self);
    }

    /// Configure help output
    template <typename Self>
    Self&& helpConfig(this Self&& self, HelpConfig config) {
        self.helpConfig_ = std::move(config);
        return std::forward<Self>(self);
    }

    /// Add version flag
    template <typename Self>
    Self&& withVersionFlag(this Self&& self) {
        return std::forward<Self>(self).option(
            Option::withName("version", 'V').description("Show version information"));
    }

    /// Add help flag (automatically added, but can be customized)
    template <typename Self>
    Self&& withHelpFlag(this Self&& self) {
        return std::forward<Self>(self).option(
            Option::withName("help", 'h').description("Show help information"));
    }

    // --- Execution ---
    /// Parse and run from argc/argv
    [[nodiscard]] int run(std::span<std::string_view> args) {
        std::string_view program_name = args.empty() ? "app" : args[0];

        // Extract just the filename
        if (auto pos = program_name.find_last_of("/\\"); pos != std::string_view::npos) {
            program_name = program_name.substr(pos + 1);
        }

        return runWithName(args, program_name);
    }

    [[nodiscard]] int runWithName(std::span<std::string_view> args, std::string_view program_name) {
        auto result = Parser::parse(args, root_);
        if (!result) {
            HelpGenerator help_gen{helpConfig_};
            std::println(stderr, "{}", help_gen.formatError(result.error(), root_, program_name));
            return 1;
        }
        const auto& parsed = *result;
        // Handle version flag

        if (parsed.options.isFlag("version") && !version_.empty()) {
            std::println("{} {}", program_name, version_);
            return 0;
        }
        // Handle help request
        if (parsed.helpRequested || parsed.options.isFlag("help")) {
            printHelp(program_name, findTargetCommand(parsed));
            return 0;
        }

        // Validate
        // Validator validator;
        const Command& target_cmd = findTargetCommand(parsed);
        auto validation = Validator::validate(parsed, target_cmd);

        if (!validation) {
            HelpGenerator gen(helpConfig_);
            std::print(stderr, "{}", gen.formatError(validation.error(), target_cmd, program_name));
            return 1;
        }

        // Execute handler
        auto exec_result = target_cmd.execute(parsed);

        if (!exec_result) {
            std::println(stderr, "Error: {}", exec_result.error().format());
            return 1;
        }

        return 0;
    }

    /// Just parse without executing
    [[nodiscard]] cli_error::Result<ParsedCommand> parse(std::span<std::string_view> args) const {
        return Parser::parse(args, root_);
    }

    /// Get the root command
    [[nodiscard]] const Command& root() const noexcept { return root_; }

    /// Get the root command (mutable)
    [[nodiscard]] Command& root() noexcept { return root_; }

    /// Generate help text
    [[nodiscard]] std::string getHelpText(std::string_view program_name,
                                          const std::optional<Command>& cmd = std::nullopt) const {
        HelpGenerator gen(helpConfig_);
        return gen.generate(cmd.value_or(root_), program_name);
    }

    /// Print help to stdout
    void printHelp(std::string_view program_name,
                   const std::optional<Command>& cmd = std::nullopt) const {
        // std::cout << help(program_name);
        std::print("{}", getHelpText(program_name, cmd));
    }

private:
    explicit CLI(Command root) : root_(std::move(root)) {}

    /// Find the target command based on the parsed command path
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

    Command root_;
    std::string version_;
    std::string author_;
    HelpConfig helpConfig_ = HelpConfig::defaults();
};

/// Create a simple CLI with a single command
[[nodiscard]] inline CLI simple_cli(std::string name, std::string description, CommandFn handler) {
    return CLI::create(std::move(name))
        .description(std::move(description))
        .handler(std::move(handler));
}

/// Create an option with common defaults
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

/// Create a flag option
[[nodiscard]] inline Option make_flag(std::string long_name,
                                      char short_name,
                                      std::string description) {
    return Option::withName(std::move(long_name), short_name).description(std::move(description));
}

/// Create a required option with value
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

/// Create a positional argument
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