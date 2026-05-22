/**
 * @file test_cli.cpp
 * @brief Comprehensive unit tests for the CLI library (expp/app/cli.hpp)
 */

#include "expp/app/cli.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace expp::app::cli;
using namespace expp::app::cli::cli_error;

// ============================================================================
// ParseError tests
// ============================================================================

TEST_CASE("ParseError construction and accessors", "[app][cli][error]") {
    SECTION("basic construction with message") {
        ParseError err(ErrorCode::UnknownOption, "unknown option --foo");

        CHECK(err.code() == ErrorCode::UnknownOption);
        CHECK(err.message() == "unknown option --foo");
        CHECK(err.severity() == ErrorSeverity::Error);
    }

    SECTION("construction with severity override") {
        ParseError err(ErrorCode::ValueOutOfRange, "value out of range", ErrorSeverity::Fatal);

        CHECK(err.severity() == ErrorSeverity::Fatal);
    }

    SECTION("format includes severity and message") {
        ParseError err(ErrorCode::MissingRequiredOption, "required option missing",
                       ErrorSeverity::Warning);
        auto formatted = err.format();

        CHECK(formatted.find("Warning") != std::string::npos);
        CHECK(formatted.find("required option missing") != std::string::npos);
    }

    SECTION("format for Fatal severity") {
        ParseError err(ErrorCode::InternalError, "internal crash", ErrorSeverity::Fatal);
        auto formatted = err.format();

        CHECK(formatted.find("Fatal") != std::string::npos);
    }
}

TEST_CASE("ok and err helper functions", "[app][cli][error]") {
    SECTION("ok with value creates successful Result") {
        auto result = ok(42);
        REQUIRE(result.has_value());
        CHECK(*result == 42);
    }

    SECTION("ok with void creates void Result") {
        auto result = ok();
        CHECK(result.has_value());
    }

    SECTION("err creates error Result") {
        auto result = static_cast<VoidResult>(err(ErrorCode::UnknownCommand, "command not found"));
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnknownCommand);
        CHECK(result.error().message() == "command not found");
    }
}

TEST_CASE("Result type operations", "[app][cli][error]") {
    SECTION("Result<int> success") {
        Result<int> r = ok(100);
        REQUIRE(r.has_value());
        CHECK(*r == 100);
    }

    SECTION("Result<int> error") {
        Result<int> r = std::unexpected(ParseError(ErrorCode::InvalidValue, "bad"));
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code() == ErrorCode::InvalidValue);
    }

    SECTION("VoidResult success") {
        VoidResult r = ok();
        CHECK(r.has_value());
    }

    SECTION("VoidResult error") {
        VoidResult r =
            std::unexpected(ParseError(ErrorCode::MissingRequiredArgument, "missing arg"));
        CHECK_FALSE(r.has_value());
    }
}

// ============================================================================
// Option tests
// ============================================================================

TEST_CASE("Option construction", "[app][cli][option]") {
    SECTION("withLong creates option with long name only") {
        auto opt = Option::withLong("verbose");
        CHECK(opt.longName() == "verbose");
        CHECK_FALSE(opt.shortName().has_value());
        CHECK_FALSE(opt.needsValue());
        CHECK_FALSE(opt.isRequired());
    }

    SECTION("withShort creates option with short name only") {
        auto opt = Option::withShort('v');
        REQUIRE(opt.shortName().has_value());
        CHECK(*opt.shortName() == 'v');
        CHECK(opt.longName().empty());
    }

    SECTION("withName creates option with both names") {
        auto opt = Option::withName("verbose", 'v');
        CHECK(opt.longName() == "verbose");
        REQUIRE(opt.shortName().has_value());
        CHECK(*opt.shortName() == 'v');
    }
}

TEST_CASE("Option configuration", "[app][cli][option]") {
    SECTION("description sets description string") {
        auto opt = Option::withLong("output").description("Output file path");
        CHECK(opt.description() == "Output file path");
    }

    SECTION("defaultValue sets default and marks hasDefault") {
        auto opt = Option::withLong("count").defaultValue(5);

        CHECK(opt.hasDefault());
        CHECK(opt.defaultValueStr() == "5");
    }

    SECTION("defaultValue with string") {
        auto opt = Option::withLong("name").defaultValue("default_name");

        CHECK(opt.hasDefault());
        CHECK(opt.defaultValueStr() == "default_name");
    }

    SECTION("required marks option as required") {
        auto opt = Option::withLong("config").required();

        CHECK(opt.isRequired());
    }

    SECTION("setTakesValue configures value-taking") {
        auto opt = Option::withLong("output").setTakesValue();

        CHECK(opt.needsValue());
    }

    SECTION("valueName sets custom value placeholder") {
        auto opt =
            Option::withLong("output").setTakesValue().valueName("FILE");

        CHECK(opt.valueName() == "FILE");
    }

    SECTION("alias adds aliases to the option") {
        auto opt = Option::withLong("verbose").alias("v").alias("verb");

        REQUIRE(opt.aliases().size() == 2);
        CHECK(opt.aliases()[0] == "v");
        CHECK(opt.aliases()[1] == "verb");
    }

    SECTION("choices sets valid values and marks hasChoices") {
        auto opt = Option::withLong("color")
                       .setTakesValue()
                       .choices({"red", "green", "blue"});

        CHECK(opt.hasChoices());
        REQUIRE(opt.choices().size() == 3);
        CHECK(opt.choices()[0] == "red");
        CHECK(opt.choices()[1] == "green");
        CHECK(opt.choices()[2] == "blue");
    }

    SECTION("empty choices vector") {
        auto opt = Option::withLong("mode").choices({});

        CHECK(opt.hasChoices());
        CHECK(opt.choices().empty());
    }
}

TEST_CASE("Option matching", "[app][cli][option]") {
    auto opt = Option::withLong("output").alias("out").alias("o");

    SECTION("matches by long name") {
        CHECK(opt.matches("output"));
    }

    SECTION("matches by alias") {
        CHECK(opt.matches("out"));
        CHECK(opt.matches("o"));
    }

    SECTION("does not match random name") {
        CHECK_FALSE(opt.matches("notfound"));
    }

    SECTION("does not match empty string") {
        CHECK_FALSE(opt.matches(""));
    }

    SECTION("does not match partial prefix") {
        CHECK_FALSE(opt.matches("outp"));
    }
}

TEST_CASE("Option matching by short name", "[app][cli][option]") {
    auto opt = Option::withName("verbose", 'v');

    SECTION("matches correct short char") {
        CHECK(opt.matchesShort('v'));
    }

    SECTION("does not match wrong short char") {
        CHECK_FALSE(opt.matchesShort('x'));
    }

    SECTION("without short name always returns false") {
        auto no_short = Option::withLong("verbose");
        CHECK_FALSE(no_short.matchesShort('v'));
    }
}

TEST_CASE("Option displayName", "[app][cli][option]") {
    SECTION("with both long and short") {
        auto opt = Option::withName("verbose", 'v');
        auto name = opt.displayName();

        CHECK(name.find("-v") != std::string::npos);
        CHECK(name.find("--verbose") != std::string::npos);
    }

    SECTION("with long name only") {
        auto opt = Option::withLong("verbose");
        CHECK(opt.displayName() == "--verbose");
    }

    SECTION("with short name only") {
        auto opt = Option::withShort('v');
        CHECK(opt.displayName() == "-v");
    }
}

TEST_CASE("Option usageString", "[app][cli][option]") {
    SECTION("flag option without value") {
        auto opt = Option::withLong("verbose");
        auto usage = opt.usageString();

        CHECK(usage == "--verbose");
    }

    SECTION("option with value and custom name") {
        auto opt = Option::withLong("output").setTakesValue().valueName("FILE");
        auto usage = opt.usageString();

        CHECK(usage.find("<FILE>") != std::string::npos);
    }

    SECTION("option with value and default name") {
        auto opt = Option::withLong("output").setTakesValue();
        auto usage = opt.usageString();

        CHECK(usage.find("<value>") != std::string::npos);
    }
}

// ============================================================================
// Argument tests
// ============================================================================

TEST_CASE("Argument construction", "[app][cli][argument]") {
    SECTION("create sets name") {
        auto arg = Argument::create("input");
        CHECK(arg.name() == "input");
        CHECK(arg.isRequired());
        CHECK_FALSE(arg.isVariadic());
    }
}

TEST_CASE("Argument configuration", "[app][cli][argument]") {
    SECTION("description sets description") {
        auto arg = Argument::create("file").description("Input file path");
        CHECK(arg.description() == "Input file path");
    }

    SECTION("required marks as required") {
        auto arg = Argument::create("file").required();
        CHECK(arg.isRequired());
    }

    SECTION("optional marks as not required") {
        auto arg = Argument::create("file").optional();
        CHECK_FALSE(arg.isRequired());
    }

    SECTION("variadic marks as variadic") {
        auto arg = Argument::create("files").variadic();
        CHECK(arg.isVariadic());
    }

    SECTION("defaultValue sets default") {
        auto arg = Argument::create("file").defaultValue("default.txt");
        CHECK(arg.hasDefault());
        CHECK(arg.defaultValueStr() == "default.txt");
    }
}

TEST_CASE("Argument usageString", "[app][cli][argument]") {
    SECTION("required argument") {
        auto arg = Argument::create("input");
        CHECK(arg.usageString() == "<input>");
    }

    SECTION("optional argument") {
        auto arg = Argument::create("input").optional();
        CHECK(arg.usageString() == "[<input>]");
    }

    SECTION("variadic argument") {
        auto arg = Argument::create("files").variadic();
        CHECK(arg.usageString() == "[<files>...]");
    }
}

// ============================================================================
// ValueParser tests
// ============================================================================

TEST_CASE("ValueParser parses strings", "[app][cli][value_parser]") {
    SECTION("string always succeeds") {
        auto result = ValueParser<std::string>::parse("hello world");
        REQUIRE(result.has_value());
        CHECK(*result == "hello world");
    }

    SECTION("empty string") {
        auto result = ValueParser<std::string>::parse("");
        REQUIRE(result.has_value());
        CHECK(*result == "");
    }
}

TEST_CASE("ValueParser parses booleans", "[app][cli][value_parser]") {
    SECTION("true values") {
        CHECK(*ValueParser<bool>::parse("true") == true);
        CHECK(*ValueParser<bool>::parse("1") == true);
        CHECK(*ValueParser<bool>::parse("yes") == true);
        CHECK(*ValueParser<bool>::parse("on") == true);
    }

    SECTION("false values") {
        CHECK(*ValueParser<bool>::parse("false") == false);
        CHECK(*ValueParser<bool>::parse("0") == false);
        CHECK(*ValueParser<bool>::parse("no") == false);
        CHECK(*ValueParser<bool>::parse("off") == false);
    }

    SECTION("invalid boolean fails") {
        auto result = ValueParser<bool>::parse("maybe");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidValue);
    }
}

TEST_CASE("ValueParser parses integers", "[app][cli][value_parser]") {
    SECTION("positive integer") {
        auto result = ValueParser<int>::parse("42");
        REQUIRE(result.has_value());
        CHECK(*result == 42);
    }

    SECTION("zero") {
        auto result = ValueParser<int>::parse("0");
        REQUIRE(result.has_value());
        CHECK(*result == 0);
    }

    SECTION("negative integer") {
        auto result = ValueParser<int>::parse("-17");
        REQUIRE(result.has_value());
        CHECK(*result == -17);
    }

    SECTION("large integer") {
        auto result = ValueParser<int64_t>::parse("9223372036854775807");
        REQUIRE(result.has_value());
        CHECK(*result == 9223372036854775807LL);
    }

    SECTION("invalid integer fails") {
        auto result = ValueParser<int>::parse("not_a_number");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidValue);
    }

    SECTION("float as integer fails") {
        auto result = ValueParser<int>::parse("3.14");
        CHECK_FALSE(result.has_value());
    }

    SECTION("unsigned integer") {
        auto result = ValueParser<unsigned int>::parse("100");
        REQUIRE(result.has_value());
        CHECK(*result == 100U);
    }
}

TEST_CASE("ValueParser parses floating point", "[app][cli][value_parser]") {
    SECTION("simple float") {
        auto result = ValueParser<double>::parse("3.14");
        REQUIRE(result.has_value());
        CHECK(*result == 3.14);
    }

    SECTION("negative float") {
        auto result = ValueParser<float>::parse("-2.5");
        REQUIRE(result.has_value());
        CHECK(*result == -2.5f);
    }

    SECTION("integer as float") {
        auto result = ValueParser<double>::parse("42");
        REQUIRE(result.has_value());
        CHECK(*result == 42.0);
    }

    SECTION("scientific notation") {
        auto result = ValueParser<double>::parse("1.5e3");
        REQUIRE(result.has_value());
        CHECK(*result == 1500.0);
    }

    SECTION("invalid float fails") {
        auto result = ValueParser<double>::parse("abc");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidValue);
    }
}

// Custom type for testing >> operator parsing
struct Point {
    int x = 0;
    int y = 0;

    friend std::istream& operator>>(std::istream& is, Point& p) {
        char comma = 0;
        is >> p.x >> comma >> p.y;
        return is;
    }
};

TEST_CASE("ValueParser parses custom types via >>", "[app][cli][value_parser]") {
    SECTION("valid custom type") {
        auto result = ValueParser<Point>::parse("10,20");
        REQUIRE(result.has_value());
        CHECK(result->x == 10);
        CHECK(result->y == 20);
    }

    SECTION("invalid custom type fails") {
        auto result = ValueParser<Point>::parse("bad_input_no_comma");
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidValue);
    }
}

// ============================================================================
// ParsedOptions tests
// ============================================================================

TEST_CASE("ParsedOptions set and get", "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("set and get string value") {
        opts.set("name", "John");
        CHECK(opts.has("name"));

        auto val = opts.get<std::string>("name");
        REQUIRE(val.has_value());
        CHECK(*val == "John");
    }

    SECTION("get integer value") {
        opts.set("count", "42");
        auto val = opts.get<int>("count");
        REQUIRE(val.has_value());
        CHECK(*val == 42);
    }

    SECTION("get bool value") {
        opts.set("enabled", "true");
        auto val = opts.get<bool>("enabled");
        REQUIRE(val.has_value());
        CHECK(*val == true);
    }

    SECTION("get float value") {
        opts.set("ratio", "3.14");
        auto val = opts.get<double>("ratio");
        REQUIRE(val.has_value());
        CHECK(*val == 3.14);
    }

    SECTION("get non-existent key returns nullopt") {
        auto val = opts.get<std::string>("missing");
        CHECK_FALSE(val.has_value());
    }

    SECTION("get with incompatible type returns nullopt") {
        opts.set("name", "abc");
        auto val = opts.get<int>("name");
        CHECK_FALSE(val.has_value());
    }

    SECTION("has returns false for missing key") {
        CHECK_FALSE(opts.has("nonexistent"));
    }
}

TEST_CASE("ParsedOptions getOr", "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("returns value when present") {
        opts.set("count", "10");
        CHECK(opts.getOr<int>("count", 0) == 10);
    }

    SECTION("returns default when absent") {
        CHECK(opts.getOr<int>("count", 42) == 42);
    }

    SECTION("returns default when type mismatches") {
        opts.set("name", "hello");
        CHECK(opts.getOr<int>("name", -1) == -1);
    }
}

TEST_CASE("ParsedOptions flags", "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("setFlag and isFlag") {
        opts.setFlag("verbose");
        CHECK(opts.isFlag("verbose"));
    }

    SECTION("isFlag returns false for unset flag") {
        CHECK_FALSE(opts.isFlag("verbose"));
    }

    SECTION("multiple flags can coexist") {
        opts.setFlag("debug");
        opts.setFlag("verbose");
        CHECK(opts.isFlag("debug"));
        CHECK(opts.isFlag("verbose"));
    }
}

TEST_CASE("ParsedOptions positional arguments", "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("add positional and retrieve") {
        opts.addPositional("file1.txt");
        opts.addPositional("file2.txt");

        CHECK(opts.positional().size() == 2);
        CHECK(opts.positional()[0] == "file1.txt");
        CHECK(opts.positional()[1] == "file2.txt");
    }

    SECTION("empty by default") {
        CHECK(opts.positional().empty());
    }
}

TEST_CASE("ParsedOptions rawOption", "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("returns raw string for set option") {
        opts.set("count", "42");
        auto raw = opts.rawOption("count");
        REQUIRE(raw.has_value());
        CHECK(*raw == "42");
    }

    SECTION("returns nullopt for unset option") {
        auto raw = opts.rawOption("missing");
        CHECK_FALSE(raw.has_value());
    }
}

// ============================================================================
// ParsedCommand tests
// ============================================================================

TEST_CASE("ParsedCommand basics", "[app][cli][parsed_command]") {
    ParsedCommand cmd;

    SECTION("default name is empty") {
        CHECK(cmd.name.empty());
    }

    SECTION("helpRequested defaults to false") {
        CHECK_FALSE(cmd.helpRequested);
    }

    SECTION("addPositional appends to args") {
        cmd.addPositional("arg1");
        cmd.addPositional("arg2");
        CHECK(cmd.positionalArgs.size() == 2);
        CHECK(cmd.positionalArgs[0] == "arg1");
        CHECK(cmd.positionalArgs[1] == "arg2");
    }

    SECTION("pathString with no command path returns name") {
        cmd.name = "myapp";
        CHECK(cmd.pathString() == "myapp");
    }

    SECTION("pathString with command path") {
        cmd.name = "set";
        cmd.commandPath = {"tool", "config"};
        auto path = cmd.pathString();
        CHECK(path.find("tool") != std::string::npos);
        CHECK(path.find("config") != std::string::npos);
    }
}

// ============================================================================
// Command tests
// ============================================================================

TEST_CASE("Command construction and basic properties", "[app][cli][command]") {
    SECTION("create sets name") {
        auto cmd = Command::create("build");
        CHECK(cmd.name() == "build");
        CHECK(cmd.description().empty());
        CHECK_FALSE(cmd.isHidden());
    }
}

TEST_CASE("Command description", "[app][cli][command]") {
    auto cmd = Command::create("build")
                   .description("Build the project")
                   .longDescription("This command compiles all source files and links them.");

    CHECK(cmd.description() == "Build the project");
    CHECK(cmd.longDescription() ==
          "This command compiles all source files and links them.");
}

TEST_CASE("Command options registration", "[app][cli][command]") {
    SECTION("register option with long name") {
        auto cmd =
            Command::create("test")
                .option(Option::withLong("verbose").description("Verbose output"));

        REQUIRE(cmd.options().size() == 1);
        CHECK(cmd.options()[0].longName() == "verbose");

        auto found = cmd.findOption("verbose");
        REQUIRE(found != nullptr);
        CHECK(found->longName() == "verbose");
    }

    SECTION("register option with short name") {
        auto cmd = Command::create("test").option(Option::withShort('v'));

        auto found = cmd.findOption('v');
        REQUIRE(found != nullptr);
    }

    SECTION("register option with alias makes alias findable") {
        auto cmd =
            Command::create("test")
                .option(Option::withLong("output").alias("out"));

        auto found = cmd.findOption("out");
        REQUIRE(found != nullptr);
        CHECK(found->longName() == "output");
    }

    SECTION("findOption with unknown name returns null") {
        auto cmd = Command::create("test");
        CHECK(cmd.findOption("nonexistent") == nullptr);
    }

    SECTION("findOption with unknown short returns null") {
        auto cmd = Command::create("test");
        CHECK(cmd.findOption('z') == nullptr);
    }

    // SECTION("empty-name option is ignored") {
    //     auto cmd = Command::create("test").option(Option{});
    //     CHECK(cmd.options().empty());
    // }
}

TEST_CASE("Command flags", "[app][cli][command]") {
    auto cmd = Command::create("app").flag("verbose", 'v', "Verbose output");

    REQUIRE(cmd.options().size() == 1);
    CHECK(cmd.options()[0].longName() == "verbose");
    CHECK(cmd.options()[0].description() == "Verbose output");
}

TEST_CASE("Command arguments", "[app][cli][command]") {
    SECTION("add argument") {
        auto cmd = Command::create("copy")
                       .argument(Argument::create("source"))
                       .argument(Argument::create("dest"));

        REQUIRE(cmd.arguments().size() == 2);
        CHECK(cmd.arguments()[0].name() == "source");
        CHECK(cmd.arguments()[1].name() == "dest");
    }

    SECTION("cannot add after variadic") {
        auto cmd = Command::create("run")
                       .argument(Argument::create("files").variadic())
                       .argument(Argument::create("extra"));

        CHECK(cmd.arguments().size() == 1);
    }
}

TEST_CASE("Command subcommands", "[app][cli][command]") {
    auto cmd = Command::create("tool")
                   .subcommand(Command::create("build").description("Build"))
                   .subcommand(Command::create("test").description("Test"));

    SECTION("hasSubcommands is true") {
        CHECK(cmd.hasSubcommands());
    }

    SECTION("findSubcommand returns correct command") {
        const auto* sub = cmd.findSubcommand("build");
        REQUIRE(sub != nullptr);
        CHECK(sub->name() == "build");
    }

    SECTION("findSubcommand returns null for unknown") {
        CHECK(cmd.findSubcommand("unknown") == nullptr);
    }

    SECTION("visibleSubcommands returns non-hidden") {
        auto visible = cmd.visibleSubcommands();
        CHECK(visible.size() == 2);
    }

    SECTION("hidden subcommands are not visible") {
        auto cmd2 = Command::create("tool")
                        .subcommand(Command::create("internal").hidden())
                        .subcommand(Command::create("public"));

        auto visible = cmd2.visibleSubcommands();
        CHECK(visible.size() == 1);
        CHECK(visible[0]->name() == "public");
    }
}

TEST_CASE("Command handler and execute", "[app][cli][command]") {
    SECTION("handler receives ParsedCommand") {
        bool called = false;
        std::string received_name;

        auto cmd = Command::create("test").handler(
            [&](const ParsedCommand& p) -> VoidResult {
                called = true;
                received_name = p.name;
                return ok();
            });

        ParsedCommand parsed;
        parsed.name = "test";
        auto result = cmd.execute(parsed);

        CHECK(result.has_value());
        CHECK(called);
        CHECK(received_name == "test");
    }

    SECTION("handler can return error") {
        auto cmd = Command::create("test").handler(
            [](const ParsedCommand&) -> VoidResult {
                return std::unexpected(
                    ParseError(ErrorCode::InternalError, "handler failed"));
            });

        ParsedCommand parsed;
        auto result = cmd.execute(parsed);

        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InternalError);
    }

    SECTION("no handler returns success") {
        auto cmd = Command::create("test");
        ParsedCommand parsed;
        auto result = cmd.execute(parsed);

        CHECK(result.has_value());
    }
}

TEST_CASE("Command usage", "[app][cli][command]") {
    SECTION("default usage string") {
        auto cmd = Command::create("build")
                       .description("Build the project")
                       .option(Option::withLong("verbose"));

        auto usage = cmd.usageString("myapp");
        CHECK(usage.find("myapp") != std::string::npos);
        CHECK(usage.find("build") != std::string::npos);
        CHECK(usage.find("[options]") != std::string::npos);
    }

    SECTION("usage with subcommands") {
        auto cmd =
            Command::create("tool").subcommand(Command::create("build"));

        auto usage = cmd.usageString("myapp");
        CHECK(usage.find("<command>") != std::string::npos);
    }

    SECTION("usage with arguments") {
        auto cmd = Command::create("copy")
                       .argument(Argument::create("source"))
                       .argument(Argument::create("dest").optional());

        auto usage = cmd.usageString("myapp");
        CHECK(usage.find("<source>") != std::string::npos);
        CHECK(usage.find("[<dest>]") != std::string::npos);
    }

    SECTION("usage override") {
        auto cmd =
            Command::create("build").usage("myapp build [--options] <target>");

        CHECK(cmd.usageString("ignored") == "myapp build [--options] <target>");
    }

    SECTION("empty name uses program name") {
        auto cmd = Command::create("myapp");
        auto usage = cmd.usageString("myapp");
        CHECK(usage.find("myapp") != std::string::npos);
    }
}

// ============================================================================
// Tokenizer tests
// ============================================================================

TEST_CASE("Tokenizer empty input", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;
    SECTION("empty span returns empty tokens") {
        std::vector<std::string_view> args;
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        CHECK(result->empty());
    }
}

TEST_CASE("Tokenizer basic token types", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;

    SECTION("--help becomes HelpRequest") {
        std::vector<std::string_view> args = {"prog", "--help"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::HelpRequest);
    }

    SECTION("-h becomes HelpRequest") {
        std::vector<std::string_view> args = {"prog", "-h"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::HelpRequest);
    }

    SECTION("-- becomes DoubleDash") {
        std::vector<std::string_view> args = {"prog", "--"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::DoubleDash);
    }

    SECTION("--option becomes LongOption") {
        std::vector<std::string_view> args = {"prog", "--verbose"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::LongOption);
        CHECK(result->at(0).value == "verbose");
    }

    SECTION("--option=value becomes LongOption with attached value") {
        std::vector<std::string_view> args = {"prog", "--output=file.txt"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::LongOption);
        CHECK(result->at(0).value == "output");
        REQUIRE(result->at(0).attachedValue.has_value());
        CHECK(*result->at(0).attachedValue == "file.txt");
    }

    SECTION("single short option -o becomes ShortOption") {
        std::vector<std::string_view> args = {"prog", "-o"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::ShortOption);
        CHECK(result->at(0).value == "o");
        REQUIRE(result->at(0).shortChar.has_value());
        CHECK(*result->at(0).shortChar == 'o');
    }

    SECTION("combined short options -abc becomes ShortOptions") {
        std::vector<std::string_view> args = {"prog", "-abc"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::ShortOptions);
        CHECK(result->at(0).value == "abc");
    }

    SECTION("short with attached value via =") {
        std::vector<std::string_view> args = {"prog", "-o=file.txt"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::ShortOption);
        CHECK(result->at(0).value == "o");
        REQUIRE(result->at(0).attachedValue.has_value());
        CHECK(*result->at(0).attachedValue == "file.txt");
    }
}

TEST_CASE("Tokenizer negative numbers treated as positional",
          "[app][cli][tokenizer]") {
    Tokenizer tokenizer;

    SECTION("negative integer") {
        std::vector<std::string_view> args = {"prog", "-42"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::PositionArg);
        CHECK(result->at(0).value == "-42");
    }

    SECTION("negative float") {
        std::vector<std::string_view> args = {"prog", "-3.14"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::PositionArg);
        CHECK(result->at(0).value == "-3.14");
    }
}

TEST_CASE("Tokenizer positional arguments", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;

    SECTION("plain string is positional") {
        std::vector<std::string_view> args = {"prog", "file.txt"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1);
        CHECK(result->at(0).type == TokenType::PositionArg);
        CHECK(result->at(0).value == "file.txt");
    }

    SECTION("multiple positionals") {
        std::vector<std::string_view> args = {"prog", "a.txt", "b.txt", "c.txt"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 3);
        for (const auto& t : *result) {
            CHECK(t.type == TokenType::PositionArg);
        }
    }
}

TEST_CASE("Tokenizer single dash produces error", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;
    std::vector<std::string_view> args = {"prog", "-"};
    auto result = tokenizer.tokenize(args);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidOptionFormat);
}

TEST_CASE("Tokenizer preserves originalIndex", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;
    std::vector<std::string_view> args = {"prog", "--verbose", "-o",
                                           "file.txt"};
    auto result = tokenizer.tokenize(args);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
    CHECK(result->at(0).originalIndex == 1);
    CHECK(result->at(1).originalIndex == 2);
    CHECK(result->at(2).originalIndex == 3);
}

TEST_CASE("Tokenizer complex args", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;

    SECTION("mix of options and positionals") {
        std::vector<std::string_view> args = {"prog", "--verbose", "-o",
                                               "output.txt", "input.txt"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 4);

        CHECK(result->at(0).type == TokenType::LongOption);
        CHECK(result->at(0).value == "verbose");
        CHECK(result->at(1).type == TokenType::ShortOption);
        CHECK(result->at(1).value == "o");
        CHECK(result->at(2).type == TokenType::PositionArg);
        CHECK(result->at(2).value == "output.txt");
        CHECK(result->at(3).type == TokenType::PositionArg);
        CHECK(result->at(3).value == "input.txt");
    }

    SECTION("combined shorts followed by value") {
        std::vector<std::string_view> args = {"prog", "-abc", "value_for_c"};
        auto result = tokenizer.tokenize(args);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 2);

        CHECK(result->at(0).type == TokenType::ShortOptions);
        CHECK(result->at(0).value == "abc");
        CHECK(result->at(1).type == TokenType::PositionArg);
        CHECK(result->at(1).value == "value_for_c");
    }
}

// ============================================================================
// TokenStream tests
// ============================================================================

TEST_CASE("TokenStream basic operations", "[app][cli][token_stream]") {
    SECTION("empty stream reports no more") {
        TokenStream stream({});
        CHECK_FALSE(stream.hasMore());
        CHECK(stream.peek() == nullptr);
        CHECK_FALSE(stream.consume().has_value());
    }

    SECTION("stream with tokens") {
        std::vector<Token> tokens;
        Token t1;
        t1.type = TokenType::LongOption;
        t1.value = "verbose";
        Token t2;
        t2.type = TokenType::PositionArg;
        t2.value = "file.txt";
        tokens.push_back(t1);
        tokens.push_back(t2);

        TokenStream stream(std::move(tokens));

        CHECK(stream.hasMore());
        CHECK(stream.peek()->value == "verbose");

        auto consumed = stream.consume();
        REQUIRE(consumed.has_value());
        CHECK(consumed->value == "verbose");

        CHECK(stream.hasMore());
        CHECK(stream.peek()->value == "file.txt");
    }

    SECTION("consumeIf with matching predicate") {
        std::vector<Token> tokens;
        Token t;
        t.type = TokenType::PositionArg;
        t.value = "arg1";
        tokens.push_back(t);

        TokenStream stream(std::move(tokens));
        auto consumed = stream.consumeIf(
            [](const Token& tok) { return tok.type == TokenType::PositionArg; });

        REQUIRE(consumed.has_value());
        CHECK(consumed->value == "arg1");
        CHECK_FALSE(stream.hasMore());
    }

    SECTION("consumeIf with non-matching predicate") {
        std::vector<Token> tokens;
        Token t;
        t.type = TokenType::PositionArg;
        t.value = "arg1";
        tokens.push_back(t);

        TokenStream stream(std::move(tokens));
        auto consumed = stream.consumeIf(
            [](const Token& tok) { return tok.type == TokenType::LongOption; });

        CHECK_FALSE(consumed.has_value());
        CHECK(stream.hasMore());  // Token still available
    }
}

TEST_CASE("TokenStream peekNext", "[app][cli][token_stream]") {
    std::vector<Token> tokens;
    Token t1;
    t1.value = "first";
    Token t2;
    t2.value = "second";
    tokens.push_back(t1);
    tokens.push_back(t2);

    TokenStream stream(std::move(tokens));

    CHECK(stream.peek()->value == "first");
    auto next = stream.peekNext();
    REQUIRE(next != nullptr);
    CHECK(next->value == "second");

    // peeked but didn't consume
    CHECK(stream.peek()->value == "first");
}

TEST_CASE("TokenStream position control", "[app][cli][token_stream]") {
    std::vector<Token> tokens;
    Token t1;
    t1.value = "a";
    Token t2;
    t2.value = "b";
    Token t3;
    t3.value = "c";
    tokens.push_back(t1);
    tokens.push_back(t2);
    tokens.push_back(t3);

    TokenStream stream(std::move(tokens));

    CHECK(stream.position() == 0);
    (void)stream.consume();
    CHECK(stream.position() == 1);

    stream.setPosition(0);
    CHECK(stream.position() == 0);
    CHECK(stream.peek()->value == "a");

    // setting beyond end clamps
    stream.setPosition(100);
    CHECK(stream.position() == 3);
    CHECK_FALSE(stream.hasMore());
}

TEST_CASE("TokenStream remaining", "[app][cli][token_stream]") {
    std::vector<Token> tokens;
    Token t1;
    t1.value = "a";
    Token t2;
    t2.value = "b";
    Token t3;
    t3.value = "c";
    tokens.push_back(t1);
    tokens.push_back(t2);
    tokens.push_back(t3);

    TokenStream stream(std::move(tokens));

    (void)stream.consume();  // consume "a"
    auto remaining = stream.remaining();

    std::size_t count = 0;
    for (const auto& t : remaining) {
        (void)t;
        ++count;
    }
    CHECK(count == 2);
}

TEST_CASE("TokenStream doubleDash tracking", "[app][cli][token_stream]") {
    TokenStream stream({});

    CHECK_FALSE(stream.seenDoubleDash());
    stream.markDoubleDash();
    CHECK(stream.seenDoubleDash());
}

// ============================================================================
// Parser tests
// ============================================================================

std::vector<std::string_view> make_args(
    std::initializer_list<const char*> args) {
    return std::vector<std::string_view>(args.begin(), args.end());
}

TEST_CASE("Parser help request", "[app][cli][parser]") {
    auto root = Command::create("prog");

    SECTION("--help at start") {
        auto args = make_args({"prog", "--help"});
        auto result = Parser::parse(args, root);
        REQUIRE(result.has_value());
        CHECK(result->helpRequested);
    }

    SECTION("-h at start") {
        auto args = make_args({"prog", "-h"});
        auto result = Parser::parse(args, root);
        REQUIRE(result.has_value());
        CHECK(result->helpRequested);
    }

    SECTION("--help after subcommand") {
        auto cmd =
            Command::create("prog").subcommand(Command::create("build"));
        auto args = make_args({"prog", "build", "--help"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->helpRequested);
        CHECK(result->name == "build");
    }
}

TEST_CASE("Parser flag options", "[app][cli][parser]") {
    auto cmd = Command::create("prog").option(Option::withLong("verbose"));

    SECTION("--verbose sets flag") {
        auto args = make_args({"prog", "--verbose"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("verbose"));
    }

    SECTION("short flag -v") {
        auto cmd2 = Command::create("prog").option(Option::withShort('v'));
        auto args = make_args({"prog", "-v"});
        auto result = Parser::parse(args, cmd2);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("v"));
    }
}

TEST_CASE("Parser options with values", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("output").setTakesValue().valueName(
                       "FILE"));

    SECTION("--output=value form") {
        auto args = make_args({"prog", "--output=result.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("output") == "result.txt");
    }

    SECTION("--output value form") {
        auto args = make_args({"prog", "--output", "result.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("output") == "result.txt");
    }
}

TEST_CASE("Parser short options with values", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .option(Option::withName("output", 'o')
                               .setTakesValue()
                               .valueName("FILE"));

    SECTION("-o value form") {
        auto args = make_args({"prog", "-o", "result.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("output") == "result.txt");
    }

    SECTION("-o=value form") {
        auto args = make_args({"prog", "-o=result.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("output") == "result.txt");
    }
}

TEST_CASE("Parser combined short options", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .flag("verbose", 'v', "Verbose")
                   .flag("debug", 'd', "Debug")
                   .option(Option::withName("output", 'o')
                               .setTakesValue()
                               .valueName("FILE"));

    SECTION("-vd sets two flags") {
        auto args = make_args({"prog", "-vd"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("verbose"));
        CHECK(result->options.isFlag("debug"));
    }

    SECTION("-vo file.txt sets flag and value") {
        auto args = make_args({"prog", "-vo", "output.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("verbose"));
        CHECK(result->options.get<std::string>("output") == "output.txt");
    }
}

TEST_CASE("Parser double dash stops option parsing", "[app][cli][parser]") {
    auto cmd = Command::create("prog").option(Option::withLong("verbose"));

    SECTION("-- stops option parsing") {
        auto args = make_args({"prog", "--", "--verbose", "file.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        // --verbose should be positional after --
        CHECK_FALSE(result->options.isFlag("verbose"));
        REQUIRE(result->positionalArgs.size() == 2);
        CHECK(result->positionalArgs[0] == "--verbose");
        CHECK(result->positionalArgs[1] == "file.txt");
    }
}

TEST_CASE("Parser subcommands", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .subcommand(
                       Command::create("build").description("Build project"))
                   .subcommand(
                       Command::create("test").description("Run tests"));

    SECTION("parse subcommand changes name") {
        auto args = make_args({"prog", "build"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->name == "build");
        REQUIRE(result->commandPath.size() == 1);
        CHECK(result->commandPath[0] == "build");
    }

    SECTION("parse subcommand with options") {
        auto cmd2 = Command::create("prog").subcommand(
            Command::create("build").option(Option::withLong("release")));

        auto args = make_args({"prog", "build", "--release"});
        auto result = Parser::parse(args, cmd2);
        REQUIRE(result.has_value());
        CHECK(result->name == "build");
        CHECK(result->options.isFlag("release"));
    }

    SECTION("parse subcommand with help flag") {
        auto args = make_args({"prog", "build", "--help"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->helpRequested);
        CHECK(result->name == "build");
    }
}

TEST_CASE("Parser positional arguments", "[app][cli][parser]") {
    auto cmd = Command::create("prog").argument(Argument::create("input"));

    SECTION("single positional") {
        auto args = make_args({"prog", "file.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        REQUIRE(result->positionalArgs.size() == 1);
        CHECK(result->positionalArgs[0] == "file.txt");
    }
}

TEST_CASE("Parser unknown option errors", "[app][cli][parser]") {
    auto cmd = Command::create("prog");

    SECTION("unknown long option") {
        auto args = make_args({"prog", "--nonexistent"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnknownOption);
    }

    SECTION("unknown short option") {
        auto args = make_args({"prog", "-x"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnknownOption);
    }

    SECTION("unknown combined short option") {
        auto args = make_args({"prog", "-xyz"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnknownOption);
    }
}

TEST_CASE("Parser missing option value errors", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("output").setTakesValue());

    SECTION("long option without value at end") {
        auto args = make_args({"prog", "--output"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MissingOptionArgument);
    }

    SECTION("short option without value") {
        auto cmd2 = Command::create("prog").option(
            Option::withName("output", 'o').setTakesValue());
        auto args = make_args({"prog", "-o"});
        auto result = Parser::parse(args, cmd2);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MissingOptionArgument);
    }
}

TEST_CASE("Parser choice validation", "[app][cli][parser]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("color")
                               .setTakesValue()
                               .choices({"red", "green", "blue"}));

    SECTION("valid choice accepted") {
        auto args = make_args({"prog", "--color=red"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("color") == "red");
    }

    SECTION("invalid choice rejected") {
        auto args = make_args({"prog", "--color=yellow"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidValue);
    }
}

// ============================================================================
// Validator tests
// ============================================================================

TEST_CASE("Validator required options", "[app][cli][validator]") {
    SECTION("required option present passes") {
        auto cmd = Command::create("prog")
                       .option(Option::withLong("config")
                                   .setTakesValue()
                                   .required());

        ParsedCommand parsed;
        parsed.options.set("config", "config.toml");

        auto result = Validator::validate(parsed, cmd);
        CHECK(result.has_value());
    }

    SECTION("required option missing fails") {
        auto cmd = Command::create("prog")
                       .option(Option::withLong("config")
                                   .setTakesValue()
                                   .required());

        ParsedCommand parsed;
        auto result = Validator::validate(parsed, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MissingRequiredOption);
    }

    SECTION("required option with only short name") {
        auto cmd = Command::create("prog")
                       .option(Option::withShort('c')
                                   .setTakesValue()
                                   .required());

        ParsedCommand parsed;
        parsed.options.set("c", "value");

        auto result = Validator::validate(parsed, cmd);
        CHECK(result.has_value());
    }
}

TEST_CASE("Validator required arguments", "[app][cli][validator]") {
    SECTION("required argument present passes") {
        auto cmd =
            Command::create("prog").argument(Argument::create("input").required());

        ParsedCommand parsed;
        parsed.positionalArgs = {"file.txt"};

        auto result = Validator::validate(parsed, cmd);
        CHECK(result.has_value());
    }

    SECTION("required argument missing fails") {
        auto cmd =
            Command::create("prog").argument(Argument::create("input").required());

        ParsedCommand parsed;
        auto result = Validator::validate(parsed, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MissingRequiredArgument);
    }

    SECTION("optional argument missing is ok") {
        auto cmd = Command::create("prog")
                       .argument(Argument::create("extra").optional());

        ParsedCommand parsed;
        auto result = Validator::validate(parsed, cmd);
        CHECK(result.has_value());
    }

    SECTION("variadic argument can take any number") {
        auto cmd = Command::create("prog")
                       .argument(Argument::create("files").variadic());

        ParsedCommand parsed;
        parsed.positionalArgs = {"a.txt", "b.txt", "c.txt", "d.txt"};

        auto result = Validator::validate(parsed, cmd);
        CHECK(result.has_value());
    }
}

TEST_CASE("Validator unexpected arguments", "[app][cli][validator]") {
    SECTION("extra argument without variadic fails") {
        auto cmd =
            Command::create("prog").argument(Argument::create("input"));

        ParsedCommand parsed;
        parsed.positionalArgs = {"a.txt", "b.txt"};

        auto result = Validator::validate(parsed, cmd);
        CHECK_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnexpectedArgument);
    }
}

TEST_CASE("Validator validateChoices", "[app][cli][validator]") {
    SECTION("matching choice returns true") {
        CHECK(Validator::validateChoices("red", {"red", "green", "blue"}));
    }

    SECTION("non-matching choice returns false") {
        CHECK_FALSE(
            Validator::validateChoices("yellow", {"red", "green", "blue"}));
    }

    SECTION("empty choices always returns true") {
        CHECK(Validator::validateChoices("anything", {}));
    }
}

TEST_CASE("Validator validateRange", "[app][cli][validator]") {
    SECTION("value within range") {
        CHECK(Validator::validateRange(5, 1, 10));
    }

    SECTION("value at lower bound") {
        CHECK(Validator::validateRange(1, 1, 10));
    }

    SECTION("value at upper bound") {
        CHECK(Validator::validateRange(10, 1, 10));
    }

    SECTION("value below range") {
        CHECK_FALSE(Validator::validateRange(0, 1, 10));
    }

    SECTION("value above range") {
        CHECK_FALSE(Validator::validateRange(11, 1, 10));
    }

    SECTION("double range validation") {
        CHECK(Validator::validateRange(3.14, 0.0, 5.0));
        CHECK_FALSE(Validator::validateRange(-1.0, 0.0, 5.0));
    }
}

// ============================================================================
// HelpGenerator tests
// ============================================================================

HelpConfig no_color_config() {
    HelpConfig config;
    config.useColors = false;
    config.maxDescriptionWidth = 80;
    config.optionIndent = 4;
    config.descriptionIndent = 24;
    return config;
}

TEST_CASE("HelpGenerator sections", "[app][cli][help]") {
    HelpGenerator gen(no_color_config());

    SECTION("help includes usage section") {
        auto cmd = Command::create("myapp").description("My app");
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Usage:") != std::string::npos);
    }

    SECTION("help includes description section when present") {
        auto cmd = Command::create("myapp").description("A test application");
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Description:") != std::string::npos);
        CHECK(text.find("A test application") != std::string::npos);
    }

    SECTION("help includes long description when present") {
        auto cmd = Command::create("myapp")
                       .description("Short desc")
                       .longDescription("This is a detailed description.");
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("detailed description") != std::string::npos);
    }

    SECTION("help includes options section when options exist") {
        auto cmd = Command::create("myapp")
                       .option(Option::withLong("verbose").description(
                           "Verbose output"));
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Options:") != std::string::npos);
        CHECK(text.find("--verbose") != std::string::npos);
    }

    SECTION("help includes arguments section when arguments exist") {
        auto cmd = Command::create("myapp")
                       .argument(
                           Argument::create("input").description("Input file"));
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Arguments:") != std::string::npos);
        CHECK(text.find("<input>") != std::string::npos);
    }

    SECTION("help includes commands section for subcommands") {
        auto cmd = Command::create("myapp").subcommand(
            Command::create("build").description("Build the project"));
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Commands:") != std::string::npos);
        CHECK(text.find("build") != std::string::npos);
    }

    SECTION("help shows defaults when configured") {
        auto cmd = Command::create("myapp")
                       .option(Option::withLong("count").defaultValue(5));
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("[default: 5]") != std::string::npos);
    }

    SECTION("help shows required marker when configured") {
        auto cmd = Command::create("myapp")
                       .option(Option::withLong("config")
                                   .setTakesValue()
                                   .required());
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("[required]") != std::string::npos);
    }

    SECTION("help shows choices when present") {
        auto cmd = Command::create("myapp")
                       .option(Option::withLong("color")
                                   .setTakesValue()
                                   .choices({"red", "green", "blue"}));
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("[choices:") != std::string::npos);
    }

    SECTION("help without description omits description section") {
        auto cmd = Command::create("myapp");
        auto text = gen.generate(cmd, "myapp");
        CHECK(text.find("Description:") == std::string::npos);
    }
}

TEST_CASE("HelpGenerator generate2 matches generate structure",
          "[app][cli][help]") {
    HelpGenerator gen(no_color_config());
    auto cmd = Command::create("myapp")
                   .description("Test app")
                   .option(Option::withLong("verbose").description("Verbose"))
                   .subcommand(Command::create("build").description("Build"));

    auto text1 = gen.generate(cmd, "myapp");
    auto text2 = gen.generate2(cmd, "myapp");

    CHECK(text1.find("Usage:") != std::string::npos);
    CHECK(text2.find("Usage:") != std::string::npos);
    CHECK(text1.find("build") != std::string::npos);
    CHECK(text2.find("build") != std::string::npos);
}

TEST_CASE("HelpGenerator shortUsage", "[app][cli][help]") {
    auto cmd = Command::create("build")
                   .description("Build project")
                   .option(Option::withLong("release"));

    auto usage = HelpGenerator::shortUsage(cmd, "myapp");
    CHECK(usage.find("myapp") != std::string::npos);
    CHECK(usage.find("build") != std::string::npos);
    CHECK(usage.find("[options]") != std::string::npos);
}

TEST_CASE("HelpGenerator formatError", "[app][cli][help]") {
    HelpGenerator gen(no_color_config());
    auto cmd = Command::create("myapp");

    ParseError err(ErrorCode::UnknownOption, "unknown option --foo");
    auto formatted = gen.formatError(err, cmd, "myapp");

    CHECK(formatted.find("Error") != std::string::npos);
    CHECK(formatted.find("unknown option --foo") != std::string::npos);
    CHECK(formatted.find("Usage") != std::string::npos);
    CHECK(formatted.find("--help") != std::string::npos);
}

TEST_CASE("HelpGenerator no color output", "[app][cli][help]") {
    HelpGenerator gen(no_color_config());
    auto cmd = Command::create("myapp").description("Test app");

    auto text = gen.generate(cmd, "myapp");
    // No ANSI escape sequences
    CHECK(text.find("\033[") == std::string::npos);
}

// ============================================================================
// HelpConfig tests
// ============================================================================

TEST_CASE("HelpConfig defaults", "[app][cli][help_config]") {
    auto config = HelpConfig::defaults();

    CHECK(config.maxDescriptionWidth == 80);
    CHECK(config.optionIndent == 4);
    CHECK(config.descriptionIndent == 24);
    CHECK(config.showDefaults);
    CHECK(config.showRequired);
}

// ============================================================================
// CLI class tests
// ============================================================================

TEST_CASE("CLI create and fromCommand", "[app][cli][cli]") {
    SECTION("create sets root command name") {
        auto cli = CLI::create("myapp");
        CHECK(cli.root().name() == "myapp");
    }

    SECTION("fromCommand takes ownership") {
        auto cli = CLI::fromCommand(Command::create("custom"));
        CHECK(cli.root().name() == "custom");
    }
}

TEST_CASE("CLI configuration chain", "[app][cli][cli]") {
    auto cli = CLI::create("myapp")
                   .description("My application")
                   .version("1.0.0")
                   .author("Test Author");

    CHECK(cli.root().description() == "My application");
}

TEST_CASE("CLI option and flag registration", "[app][cli][cli]") {
    SECTION("option added to root command") {
        auto cli = CLI::create("myapp")
                       .option(
                           Option::withLong("config").description("Config file"));

        CHECK(cli.root().options().size() == 1);
    }

    SECTION("flag added to root command") {
        auto cli = CLI::create("myapp").flag("debug", 'd', "Debug mode");

        CHECK(cli.root().options().size() == 1);
        CHECK(cli.root().options()[0].longName() == "debug");
    }
}

TEST_CASE("CLI subcommand registration", "[app][cli][cli]") {
    auto cli = CLI::create("myapp")
                   .command(Command::create("build").description("Build"));

    CHECK(cli.root().hasSubcommands());
    CHECK(cli.root().findSubcommand("build") != nullptr);
}

TEST_CASE("CLI handler execution", "[app][cli][cli]") {
    SECTION("root handler called on parse result with no subcommand") {
        bool called = false;
        std::string captured;

        auto cli = CLI::create("myapp")
                       .handler([&](const ParsedCommand& cmd) -> VoidResult {
                           called = true;
                           captured = cmd.name;
                           return ok();
                       });

        auto args = make_args({"myapp"});
        auto result = cli.parse(args);
        REQUIRE(result.has_value());

        auto exec_result = cli.root().execute(*result);
        CHECK(exec_result.has_value());
        CHECK(called);
        CHECK(captured == "myapp");
    }
}

TEST_CASE("CLI parse error", "[app][cli][cli]") {
    auto cli = CLI::create("myapp");
    auto args = make_args({"myapp", "--unknown"});
    auto result = cli.parse(args);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::UnknownOption);
}

TEST_CASE("CLI getHelpText", "[app][cli][cli]") {
    auto cli = CLI::create("myapp")
                   .description("Application description")
                   .helpConfig(no_color_config());

    auto help = cli.getHelpText("myapp");
    CHECK(help.find("Application description") != std::string::npos);
    CHECK(help.find("Usage:") != std::string::npos);
}

TEST_CASE("CLI help and version flags in parse", "[app][cli][cli]") {
    SECTION("--help sets helpRequested") {
        auto cli = CLI::create("myapp");
        auto args = make_args({"myapp", "--help"});
        auto result = cli.parse(args);
        REQUIRE(result.has_value());
        CHECK(result->helpRequested);
    }

    SECTION("--version sets version flag") {
        auto cli = CLI::create("myapp").withVersionFlag();
        auto args = make_args({"myapp", "--version"});
        auto result = cli.parse(args);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("version"));
    }
}

TEST_CASE("CLI double dash handling in parse", "[app][cli][cli]") {
    auto cli = CLI::create("myapp");
    auto args = make_args({"myapp", "--", "arg1", "--not-a-flag"});
    auto result = cli.parse(args);
    REQUIRE(result.has_value());
    REQUIRE(result->positionalArgs.size() == 2);
    CHECK(result->positionalArgs[0] == "arg1");
    CHECK(result->positionalArgs[1] == "--not-a-flag");
}

// ============================================================================
// Convenience function tests
// ============================================================================

TEST_CASE("simple_cli creates CLI with handler", "[app][cli][convenience]") {
    bool called = false;
    auto cli = simple_cli(
        "greet", "Greet command",
        [&](const ParsedCommand&) -> VoidResult {
            called = true;
            return ok();
        });

    CHECK(cli.root().name() == "greet");
    CHECK(cli.root().description() == "Greet command");
}

TEST_CASE("make_option creates option with names and description",
          "[app][cli][convenience]") {
    SECTION("flag option (no value)") {
        auto opt = make_option("verbose", 'v', "Verbose output");
        CHECK(opt.longName() == "verbose");
        CHECK(opt.description() == "Verbose output");
        CHECK_FALSE(opt.needsValue());
    }

    SECTION("value-taking option") {
        auto opt = make_option("output", 'o', "Output file", true);
        CHECK(opt.needsValue());
    }
}

TEST_CASE("make_flag creates flag option", "[app][cli][convenience]") {
    auto opt = make_flag("debug", 'd', "Debug mode");
    CHECK(opt.longName() == "debug");
    CHECK_FALSE(opt.needsValue());
    CHECK(opt.description() == "Debug mode");
}

TEST_CASE("make_required_option creates required value option",
          "[app][cli][convenience]") {
    auto opt = make_required_option("config", 'c', "Config file", "TOML");

    CHECK(opt.longName() == "config");
    CHECK(opt.description() == "Config file");
    CHECK(opt.needsValue());
    CHECK(opt.isRequired());
    CHECK(opt.valueName() == "TOML");
}

TEST_CASE("make_argument creates argument definition",
          "[app][cli][convenience]") {
    SECTION("required by default") {
        auto arg = make_argument("input", "Input file");
        CHECK(arg.name() == "input");
        CHECK(arg.description() == "Input file");
        CHECK(arg.isRequired());
    }

    SECTION("optional when specified") {
        auto arg = make_argument("extra", "Extra file", false);
        CHECK_FALSE(arg.isRequired());
    }
}

// ============================================================================
// Nested / complex scenario tests
// ============================================================================

TEST_CASE("Complex scenario: nested subcommands with options and args",
          "[app][cli][scenario]") {
    auto cmd =
        Command::create("tool")
            .description("Development tool")
            .option(Option::withLong("verbose").alias("v"))
            .subcommand(
                Command::create("config")
                    .description("Configuration management")
                    .option(Option::withLong("global")
                                .description("Use global config"))
                    .subcommand(
                        Command::create("set")
                            .description("Set a config value")
                            .argument(Argument::create("key").required())
                            .argument(Argument::create("value").required())));

    SECTION("navigate to deep subcommand") {
        auto args =
            make_args({"tool", "config", "set", "theme", "dark"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->name == "set");
        REQUIRE(result->commandPath.size() == 2);
        CHECK(result->commandPath[0] == "config");
        CHECK(result->commandPath[1] == "set");
        CHECK(result->positionalArgs.size() == 2);
        CHECK(result->positionalArgs[0] == "theme");
        CHECK(result->positionalArgs[1] == "dark");
    }

    SECTION("root option before subcommand") {
        auto args =
            make_args({"tool", "--verbose", "config", "set", "key", "val"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->name == "set");
        CHECK(result->options.isFlag("verbose"));
    }
}

TEST_CASE("Complex scenario: variadic positional arguments",
          "[app][cli][scenario]") {
    auto cmd = Command::create("cat")
                   .argument(Argument::create("files")
                                 .variadic()
                                 .description("Files to display"))
                   .option(Option::withLong("number").alias("n"));

    SECTION("variadic with multiple args") {
        auto args = make_args(
            {"cat", "a.txt", "b.txt", "c.txt", "d.txt", "e.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->positionalArgs.size() == 5);
    }

    SECTION("variadic with zero args") {
        auto args = make_args({"cat"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->positionalArgs.empty());
    }

    SECTION("variadic with flag before args") {
        auto args = make_args({"cat", "--number", "a.txt", "b.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("number"));
        CHECK(result->positionalArgs.size() == 2);
    }
}

TEST_CASE("Complex scenario: options with aliases in subcommands",
          "[app][cli][scenario]") {
    auto cmd = Command::create("app")
                   .option(Option::withLong("quiet").alias("q"))
                   .subcommand(Command::create("run").option(
                       Option::withLong("release").alias("r")));

    SECTION("alias at root level") {
        auto args = make_args({"app", "--q"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("quiet"));
    }

    SECTION("alias at subcommand level") {
        auto args = make_args({"app", "run", "--r"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("release"));
    }
}

TEST_CASE("Complex scenario: validation of full parse results",
          "[app][cli][scenario]") {
    auto cmd = Command::create("copy")
                   .option(Option::withLong("force")
                               .description("Force overwrite"))
                   .argument(Argument::create("source").required())
                   .argument(Argument::create("dest").required());

    SECTION("valid parse passes validation") {
        auto args = make_args({"copy", "--force", "a.txt", "b.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        auto validation = Validator::validate(*result, cmd);
        CHECK(validation.has_value());
    }

    SECTION("missing required argument fails validation") {
        auto args = make_args({"copy", "--force", "a.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        auto validation = Validator::validate(*result, cmd);
        CHECK_FALSE(validation.has_value());
        CHECK(validation.error().code() ==
              ErrorCode::MissingRequiredArgument);
    }

    SECTION("too many arguments fails validation") {
        auto args =
            make_args({"copy", "--force", "a.txt", "b.txt", "c.txt"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        auto validation = Validator::validate(*result, cmd);
        CHECK_FALSE(validation.has_value());
        CHECK(validation.error().code() == ErrorCode::UnexpectedArgument);
    }
}

// ============================================================================
// Edge case tests
// ============================================================================

TEST_CASE("Edge case: empty command arguments", "[app][cli][edge]") {
    auto cmd = Command::create("prog");
    auto args = make_args({"prog"});
    auto result = Parser::parse(args, cmd);
    REQUIRE(result.has_value());
    CHECK(result->name == "prog");
    CHECK(result->positionalArgs.empty());
    CHECK_FALSE(result->helpRequested);
}

TEST_CASE("Edge case: option with same short name as another in subcommand",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .option(Option::withShort('v'))
                   .subcommand(
                       Command::create("sub").option(Option::withShort('v')));

    SECTION("root short option consumed at root") {
        auto args = make_args({"prog", "-v", "sub"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->name == "sub");
    }

    SECTION("subcommand short option consumed at sub") {
        auto args = make_args({"prog", "sub", "-v"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->name == "sub");
    }
}

TEST_CASE("Edge case: option with choices in combined shorts",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .flag("verbose", 'v', "")
                   .option(Option::withName("format", 'f')
                               .setTakesValue()
                               .choices({"json", "xml", "yaml"}));

    SECTION("valid choice in combined shorts") {
        auto args = make_args({"prog", "-vf", "json"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.isFlag("verbose"));
        CHECK(result->options.get<std::string>("format") == "json");
    }

    SECTION("invalid choice in combined shorts returns error") {
        auto args = make_args({"prog", "-vf", "csv"});
        auto result = Parser::parse(args, cmd);
        CHECK_FALSE(result.has_value());
    }
}

TEST_CASE("Edge case: argument after variadic is rejected by Command",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .argument(Argument::create("files").variadic())
                   .argument(Argument::create("extra"));

    CHECK(cmd.arguments().size() == 1);
    CHECK(cmd.arguments()[0].isVariadic());
}

TEST_CASE("Edge case: help request in middle of args", "[app][cli][edge]") {
    auto cmd = Command::create("prog").option(Option::withLong("verbose"));

    auto args = make_args({"prog", "--verbose", "--help", "--more"});
    auto result = Parser::parse(args, cmd);
    REQUIRE(result.has_value());
    CHECK(result->helpRequested);
}

TEST_CASE("Edge case: option value that looks like an option",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("name").setTakesValue());

    SECTION("next token consumed even if it looks like an option") {
        auto args = make_args({"prog", "--name", "--not-an-option"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        CHECK(result->options.get<std::string>("name") == "--not-an-option");
    }
}

TEST_CASE("Edge case: unknown word becomes positional",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog").argument(Argument::create("file"));

    auto args = make_args({"prog", "build"});
    auto result = Parser::parse(args, cmd);
    REQUIRE(result.has_value());
    REQUIRE(result->positionalArgs.size() == 1);
    CHECK(result->positionalArgs[0] == "build");
}

TEST_CASE("Edge case: default values applied via handler",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("count").defaultValue(10));

    auto args = make_args({"prog"});
    auto result = Parser::parse(args, cmd);
    REQUIRE(result.has_value());
    CHECK(result->options.getOr<int>("count", 10) == 10);
}

TEST_CASE("Edge case: raw option values can be accessed",
          "[app][cli][edge]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("output").setTakesValue());

    auto args = make_args({"prog", "--output", "result.txt"});
    auto result = Parser::parse(args, cmd);
    REQUIRE(result.has_value());

    auto raw = result->options.rawOption("output");
    REQUIRE(raw.has_value());
    CHECK(*raw == "result.txt");
}

// ============================================================================
// Error code / severity / token type enumeration tests
// ============================================================================

TEST_CASE("Error code enumeration coverage", "[app][cli][error_code]") {
    CHECK(static_cast<int>(ErrorCode::None) == 0);
    CHECK(static_cast<int>(ErrorCode::UnknownOption) !=
          static_cast<int>(ErrorCode::None));
    CHECK(static_cast<int>(ErrorCode::MissingOptionArgument) !=
          static_cast<int>(ErrorCode::UnknownOption));
    CHECK(static_cast<int>(ErrorCode::InvalidOptionFormat) !=
          static_cast<int>(ErrorCode::MissingOptionArgument));
    CHECK(static_cast<int>(ErrorCode::UnexpectedArgument) !=
          static_cast<int>(ErrorCode::InvalidOptionFormat));
    CHECK(static_cast<int>(ErrorCode::MissingRequiredOption) !=
          static_cast<int>(ErrorCode::UnexpectedArgument));
    CHECK(static_cast<int>(ErrorCode::MissingRequiredArgument) !=
          static_cast<int>(ErrorCode::MissingRequiredOption));
    CHECK(static_cast<int>(ErrorCode::UnknownCommand) !=
          static_cast<int>(ErrorCode::MissingRequiredArgument));
    CHECK(static_cast<int>(ErrorCode::AmbiguousCommand) !=
          static_cast<int>(ErrorCode::UnknownCommand));
    CHECK(static_cast<int>(ErrorCode::InvalidValue) !=
          static_cast<int>(ErrorCode::AmbiguousCommand));
    CHECK(static_cast<int>(ErrorCode::ValueOutOfRange) !=
          static_cast<int>(ErrorCode::InvalidValue));
    CHECK(static_cast<int>(ErrorCode::ConstraintViolation) !=
          static_cast<int>(ErrorCode::ValueOutOfRange));
    CHECK(static_cast<int>(ErrorCode::CommandNotFound) !=
          static_cast<int>(ErrorCode::ConstraintViolation));
    CHECK(static_cast<int>(ErrorCode::NoCommandProvided) !=
          static_cast<int>(ErrorCode::CommandNotFound));
    CHECK(static_cast<int>(ErrorCode::InternalError) !=
          static_cast<int>(ErrorCode::NoCommandProvided));
}

TEST_CASE("Error severity enumeration coverage",
          "[app][cli][error_severity]") {
    CHECK(static_cast<int>(ErrorSeverity::Warning) !=
          static_cast<int>(ErrorSeverity::Error));
    CHECK(static_cast<int>(ErrorSeverity::Error) !=
          static_cast<int>(ErrorSeverity::Fatal));
}

TEST_CASE("Token type enumeration coverage", "[app][cli][token_type]") {
    CHECK(static_cast<int>(TokenType::LongOption) !=
          static_cast<int>(TokenType::ShortOption));
    CHECK(static_cast<int>(TokenType::ShortOption) !=
          static_cast<int>(TokenType::ShortOptions));
    CHECK(static_cast<int>(TokenType::ShortOptions) !=
          static_cast<int>(TokenType::OptionValue));
    CHECK(static_cast<int>(TokenType::OptionValue) !=
          static_cast<int>(TokenType::PositionArg));
    CHECK(static_cast<int>(TokenType::PositionArg) !=
          static_cast<int>(TokenType::Command));
    CHECK(static_cast<int>(TokenType::Command) !=
          static_cast<int>(TokenType::DoubleDash));
    CHECK(static_cast<int>(TokenType::DoubleDash) !=
          static_cast<int>(TokenType::HelpRequest));
}

// ============================================================================
// Token isOption() tests
// ============================================================================

TEST_CASE("Token isOption", "[app][cli][token]") {
    Token t;

    SECTION("LongOption is an option") {
        t.type = TokenType::LongOption;
        CHECK(t.isOption());
    }

    SECTION("ShortOption is an option") {
        t.type = TokenType::ShortOption;
        CHECK(t.isOption());
    }

    SECTION("ShortOptions is an option") {
        t.type = TokenType::ShortOptions;
        CHECK(t.isOption());
    }

    SECTION("PositionArg is not an option") {
        t.type = TokenType::PositionArg;
        CHECK_FALSE(t.isOption());
    }

    SECTION("DoubleDash is not an option") {
        t.type = TokenType::DoubleDash;
        CHECK_FALSE(t.isOption());
    }
}

// ============================================================================
// ParseContext tests
// ============================================================================

TEST_CASE("ParseContext basic usage", "[app][cli][parse_context]") {
    auto root = Command::create("root");
    ParseContext ctx(root);

    CHECK(ctx.root().name() == "root");
    CHECK(ctx.current().name() == "root");
    CHECK(ctx.commandPath().empty());

    SECTION("navigate to subcommand") {
        auto sub = Command::create("sub");
        ctx.navigateToSubcommand(&sub);

        CHECK(ctx.current().name() == "sub");
        REQUIRE(ctx.commandPath().size() == 1);
        CHECK(ctx.commandPath()[0] == "sub");
    }
}

// ============================================================================
// Additional edge cases
// ============================================================================

TEST_CASE("ParsedOptions type mismatch edge cases",
          "[app][cli][parsed_options]") {
    ParsedOptions opts;

    SECTION("get int from non-numeric string returns nullopt") {
        opts.set("x", "abc");
        CHECK_FALSE(opts.get<int>("x").has_value());
    }

    SECTION("get bool from non-boolean string returns nullopt") {
        opts.set("flag", "maybe");
        CHECK_FALSE(opts.get<bool>("flag").has_value());
    }
}

TEST_CASE("Command option with multiple aliases", "[app][cli][command]") {
    auto cmd = Command::create("prog")
                   .option(Option::withLong("output").alias("o").alias("out"));

    CHECK(cmd.findOption("output") != nullptr);
    CHECK(cmd.findOption("o") != nullptr);
    CHECK(cmd.findOption("out") != nullptr);
}

TEST_CASE("Parser negative numbers positional", "[app][cli][parser]") {
    auto cmd = Command::create("prog").argument(Argument::create("num"));

    SECTION("negative integer is positional") {
        auto args = make_args({"prog", "-5"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        REQUIRE(result->positionalArgs.size() == 1);
        CHECK(result->positionalArgs[0] == "-5");
    }

    SECTION("negative integer after double dash") {
        auto args = make_args({"prog", "--", "-10"});
        auto result = Parser::parse(args, cmd);
        REQUIRE(result.has_value());
        REQUIRE(result->positionalArgs.size() == 1);
        CHECK(result->positionalArgs[0] == "-10");
    }
}

TEST_CASE("Tokenizer --option= with empty value", "[app][cli][tokenizer]") {
    Tokenizer tokenizer;
    std::vector<std::string_view> args = {"prog", "--output="};
    auto result = tokenizer.tokenize(args);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    CHECK(result->at(0).type == TokenType::LongOption);
    CHECK(result->at(0).value == "output");
    REQUIRE(result->at(0).attachedValue.has_value());
    CHECK(result->at(0).attachedValue->empty());
}

TEST_CASE("Panic handler tracing toggle", "[app][cli][cli]") {
    SECTION("handler can toggle state") {
        int counter = 0;
        auto cli = CLI::create("counter")
                       .handler([&](const ParsedCommand&) -> VoidResult {
                           ++counter;
                           return ok();
                       });

        auto args1 = make_args({"counter"});
        auto result1 = cli.parse(args1);
        REQUIRE(result1.has_value());
        (void)cli.root().execute(*result1);
        CHECK(counter == 1);

        auto args2 = make_args({"counter"});
        auto result2 = cli.parse(args2);
        REQUIRE(result2.has_value());
        (void)cli.root().execute(*result2);
        CHECK(counter == 2);
    }
}

TEST_CASE("String value preserves special characters",
          "[app][cli][value_parser]") {
    SECTION("preserves spaces") {
        auto result = ValueParser<std::string>::parse("hello world foo bar");
        REQUIRE(result.has_value());
        CHECK(*result == "hello world foo bar");
    }

    SECTION("preserves special chars") {
        auto result = ValueParser<std::string>::parse("!@#$%^&*()");
        REQUIRE(result.has_value());
        CHECK(*result == "!@#$%^&*()");
    }
}
