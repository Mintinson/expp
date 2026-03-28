#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <span>
//
#include "expp/app/explorer.hpp"
#include "expp/app/explorer_view.hpp"
#include "expp/core/config.hpp"
#include "expp/core/filesystem.hpp"
#include "expp/ui/theme.hpp"

namespace {

/**
 * @brief Resolves the startup directory for the explorer.
 *
 * Uses the current working directory by default. If a path argument is provided,
 * validates that it exists and is a directory, then returns its canonical form.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return expp::core::Result<std::filesystem::path> containing the resolved start path or an error if resolution fails.
 *
 * Returns the canonical startup path on success.
 * Returns an error when:
 * - current directory cannot be read (FileSystem),
 * - provided path cannot be accessed (FileSystem),
 * - provided path does not exist (NotFound),
 * - provided path is not a directory (InvalidArgument),
 * - canonicalization fails (propagated from filesystem::canonicalize).
 */
[[nodiscard]] expp::core::Result<std::filesystem::path> resolve_start_path(std::span<char*> argv) {
    namespace fs = std::filesystem;
    using expp::core::ErrorCategory;
    using expp::core::make_error;

    std::error_code ec;
    fs::path start_path = fs::current_path(ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem, std::format("Failed to get current directory: {}", ec.message()));
    }

    if (argv.size() > 1) {
        start_path = fs::path(argv[1]);
        if (!fs::exists(start_path, ec)) {
            if (ec) {
                return make_error(ErrorCategory::FileSystem, std::format("Failed to access start path '{}': {}",
                                                                         start_path.string(), ec.message()));
            }
            return make_error(ErrorCategory::NotFound, std::format("Start path not found: {}", start_path.string()));
        }
        if (!fs::is_directory(start_path, ec)) {
            if (ec) {
                return make_error(ErrorCategory::FileSystem, std::format("Failed to inspect start path '{}': {}",
                                                                         start_path.string(), ec.message()));
            }
            return make_error(ErrorCategory::InvalidArgument,
                              std::format("Start path is not a directory: {}", start_path.string()));
        }
    }

    return expp::core::filesystem::canonicalize(start_path);
}

void print_fatal(const expp::core::Error& error) {
    std::println(stderr, "Fatal: {}", error.message());
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace expp;

    // Load configuration from default locations
    auto& cfg = core::global_config();
    auto load_result = cfg.load();
    if (!load_result) {
        print_fatal(load_result.error());
        return 1;
    }

    // Apply loaded config to the global theme
    const auto& config = cfg.config();
    ui::global_theme().reload(config.theme);
    ui::global_theme().reloadIcons(config.icons);

    auto start_path_result = resolve_start_path({argv, static_cast<size_t>(argc)});
    if (!start_path_result) {
        print_fatal(start_path_result.error());
        return 1;
    }

    auto explorer_result = app::Explorer::create(*start_path_result);
    if (!explorer_result) {
        print_fatal(explorer_result.error());
        return 1;
    }

    app::ExplorerView view(*explorer_result);
    return view.run();
}
