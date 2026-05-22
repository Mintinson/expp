#include "expp/app/cli.hpp"

#include <iostream>

using namespace expp::app::cli;

cli_error::VoidResult handle_build(const ParsedCommand& cmd) {
    std::cout << "=== Build Command ===\n\n";

    // Get positional arguments
    const auto& args = cmd.positionalArgs;

    if (args.empty()) {
        return cli_error::err(cli_error::ErrorCode::MissingRequiredArgument,
                              "No source file specified");
    }

    std::string source = args[0];

    // Get options
    auto output = cmd.options.getOr<std::string>("output", "a.out");
    bool release = cmd.options.isFlag("release");
    bool verbose = cmd.options.isFlag("verbose");
    auto opt_level = cmd.options.getOr<std::string>("optimize", "0");

    std::cout << "Source file:  " << source << "\n";
    std::cout << "Output file:  " << output << "\n";
    std::cout << "Mode:         " << (release ? "Release" : "Debug") << "\n";
    std::cout << "Optimization: -O" << opt_level << "\n";
    std::cout << "Verbose:      " << (verbose ? "Yes" : "No") << "\n";

    if (args.size() > 1) {
        std::cout << "\nAdditional source files:\n";
        for (std::size_t i = 1; i < args.size(); ++i) {
            std::cout << "  - " << args[i] << "\n";
        }
    }

    std::cout << "\nBuild completed successfully!\n";
    return cli_error::ok();
}

/// Handler for the 'run' command
cli_error::VoidResult handle_run(const ParsedCommand& cmd) {
    std::cout << "=== Run Command ===\n\n";

    const auto& args = cmd.positionalArgs;
    std::string file = args.empty() ? "main.lua" : args[0];

    bool verbose = cmd.options.isFlag("verbose");
    auto port = cmd.options.getOr<int>("port", 8080);
    auto port_str = cmd.options.getOr<std::string>("port", "8080");

    std::cout << "Running:    " << file << "\n";
    std::cout << "Port:       " << port << "\n";
    std::cout << "Port Str:   " << port_str << "\n";
    std::cout << "Verbose:    " << (verbose ? "Yes" : "No") << "\n";

    std::cout << "\nExecution started...\n";
    return cli_error::ok();
}

/// Handler for 'config set' command
cli_error::VoidResult handle_config_set(const ParsedCommand& cmd) {
    std::cout << "=== Config Set ===\n\n";

    const auto& args = cmd.positionalArgs;

    if (args.size() < 2) {
        return cli_error::err(cli_error::ErrorCode::MissingRequiredArgument, "Usage: config set <key> <value>");
    }

    std::string key = args[0];
    std::string value = args[1];

    bool global = cmd.options.isFlag("global");

    std::cout << "Setting configuration:\n";
    std::cout << "  Key:    " << key << "\n";
    std::cout << "  Value:  " << value << "\n";
    std::cout << "  Scope:  " << (global ? "Global" : "Local") << "\n";

    std::cout << "\nConfiguration saved.\n";
    return cli_error::ok();
}

/// Handler for 'config get' command
cli_error::VoidResult handle_config_get(const ParsedCommand& cmd) {
    std::cout << "=== Config Get ===\n\n";

    const auto& args = cmd.positionalArgs;

    if (args.empty()) {
        // List all config
        std::cout << "All configuration:\n";
        std::cout << "  theme = dark\n";
        std::cout << "  editor = vim\n";
        std::cout << "  log_level = info\n";
    } else {
        std::string key = args[0];
        std::cout << "Configuration value:\n";
        std::cout << "  " << key << " = sample_value\n";
    }

    return cli_error::ok();
}

/// Handler for 'config list' command
cli_error::VoidResult handle_config_list(const ParsedCommand& cmd) {
    std::cout << "=== Configuration List ===\n\n";

    bool show_all = cmd.options.isFlag("all");

    std::cout << "Configuration settings:\n";
    std::cout << "  theme       = dark\n";
    std::cout << "  editor      = vim\n";
    std::cout << "  log_level   = info\n";

    if (show_all) {
        std::cout << "\nHidden settings:\n";
        std::cout << "  debug_mode  = false\n";
        std::cout << "  cache_dir   = /tmp/cache\n";
    }

    return cli_error::ok();
}

int main(int argc, char* argv[]) {
    auto app =
        CLI::create("demo")
            .version("0.0.1")
            .description("A demo application showcasing the Modern C++23 CLI Parser")
            .flag("verbose", 'v', "Enable verbose output")
            .flag("help", 'h', "Show this help message")
            .flag("version", 'V', "Show version information")  // === BUILD COMMAND ===
            .command(Command::create("build")
                         .description("Build the project from source files")
                         .longDescription("Compiles source files into an executable.\n\n"
                                          "Examples:\n"
                                          "  demo build main.cpp\n"
                                          "  demo build main.cpp lib.cpp --release -o myapp\n"
                                          "  demo build src/*.cpp --optimize 3")
                         .option(Option::withName("output", 'o')
                                     .description("Output file name")
                                     .setTakesValue()
                                     .valueName("FILE"))
                         .flag("release", 'r', "Build in release mode with optimizations")
                         .option(Option::withName("optimize", 'O')
                                     .description("Optimization level (0-3)")
                                     .setTakesValue()
                                     .valueName("LEVEL")
                                     .choices({"0", "1", "2", "3"})
                                     .defaultValue(0))
                         .argument(Argument::create("source")
                                       .description("Source file(s) to compile")
                                       .variadic())
                         .handler(handle_build))
            // === RUN COMMAND ===
            .command(Command::create("run")
                         .description("Run a script or application")
                         .longDescription("Executes a Lua script or runs an application.\n\n"
                                          "Examples:\n"
                                          "  demo run app.lua\n"
                                          "  demo run --port 3000 server.lua")
                         .option(Option::withName("port", 'p')
                                     .description("Port number to listen on")
                                     .setTakesValue()
                                     .valueName("PORT")
                                     .defaultValue(8080))
                         .argument(Argument::create("file")
                                       .description("Script or application file to run")
                                       .optional())
                         .handler(handle_run))

            // === CONFIG COMMAND (with nested subcommands) ===
            .command(
                Command::create("config")
                    .description("Manage configuration settings")
                    .longDescription("View and modify application configuration.\n\n"
                                     "Subcommands:\n"
                                     "  set   - Set a configuration value\n"
                                     "  get   - Get a configuration value\n"
                                     "  list  - List all configuration settings")
                    // Config has no handler of its own - requires a subcommand

                    // === CONFIG SET ===
                    .subcommand(
                        Command::create("set")
                            .description("Set a configuration value")
                            .flag("global", 'g', "Set globally for all projects")
                            .argument(Argument::create("key").description("Configuration key name"))
                            .argument(Argument::create("value").description("Configuration value"))
                            .handler(handle_config_set))

                    // === CONFIG GET ===
                    .subcommand(Command::create("get")
                                    .description("Get a configuration value")
                                    .argument(Argument::create("key")
                                                  .description("Configuration key name")
                                                  .optional())
                                    .handler(handle_config_get))

                    // === CONFIG LIST ===
                    .subcommand(Command::create("list")
                                    .description("List all configuration settings")
                                    .flag("all", 'a', "Show hidden settings as well")
                                    .handler(handle_config_list)));
    std::span<char*> c_args(argv, argc);

    std::vector<std::string_view> cpp_args(c_args.begin(), c_args.end());
    return app.run(cpp_args);
}