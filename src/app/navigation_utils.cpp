#include "expp/app/navigation_utils.hpp"

#include "expp/core/filesystem.hpp"

#include <cstdlib>
#include <format>
#include <string>

namespace expp::app {
namespace {

/**
 * @brief Trims leading and trailing whitespace from a string view and returns a new string.
 *
 * @param value The string view to trim.
 * @return std::string A new string with leading and trailing whitespace removed.
 */
[[nodiscard]] std::string trim_copy(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1)};
}

/**
 * @brief Validates a directory path, ensuring it exists and is a directory.
 *
 * @param path The directory path to validate.
 * @return core::Result<std::filesystem::path> The validated directory path or an error.
 */
[[nodiscard]] core::Result<std::filesystem::path> validate_directory_path(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    std::error_code canonical_ec;
    fs::path resolved_path = fs::weakly_canonical(path, canonical_ec);
    if (canonical_ec) {
        resolved_path = path.lexically_normal();
    }

    std::error_code exists_ec;
    if (!fs::exists(resolved_path, exists_ec)) {
        if (exists_ec) {
            return core::make_error(
                core::ErrorCategory::FileSystem,
                std::format("Cannot access directory '{}': {}", resolved_path.string(), exists_ec.message()));
        }
        return core::make_error(core::ErrorCategory::NotFound,
                                std::format("Directory does not exist: {}", resolved_path.string()));
    }

    std::error_code directory_ec;
    if (!fs::is_directory(resolved_path, directory_ec)) {
        if (directory_ec) {
            return core::make_error(
                core::ErrorCategory::FileSystem,
                std::format("Cannot inspect directory '{}': {}", resolved_path.string(), directory_ec.message()));
        }
        return core::make_error(core::ErrorCategory::InvalidArgument,
                                std::format("Not a directory: {}", resolved_path.string()));
    }

    return core::filesystem::normalize(resolved_path);
}

#ifdef _WIN32
[[nodiscard]] std::string readEnvironmentVariable(const char* name) {
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result{value};
    std::free(value);
    return result;
}
#endif

}  // namespace

core::Result<std::filesystem::path> resolve_home_directory() {
    namespace fs = std::filesystem;

#ifdef _WIN32
    if (const std::string user_profile = readEnvironmentVariable("USERPROFILE"); !user_profile.empty()) {
        return validate_directory_path(fs::path{user_profile});
    }
    const std::string home_drive = readEnvironmentVariable("HOMEDRIVE");
    const std::string home_path = readEnvironmentVariable("HOMEPATH");
    if (!home_drive.empty() && !home_path.empty()) {
        return validate_directory_path(fs::path{home_drive + home_path});
    }
#else
    if (const char* home = std::getenv("HOME")) {
        return validate_directory_path(fs::path{home});
    }
#endif

    return core::make_error(core::ErrorCategory::NotFound, "Home directory is not configured");
}

core::Result<std::filesystem::path> resolve_directory_input(std::string_view input,
                                                            const std::filesystem::path& current_dir,
                                                            const std::filesystem::path& home_dir) {
    namespace fs = std::filesystem;

    const std::string trimmed = trim_copy(input);
    if (trimmed.empty()) {
        return core::make_error(core::ErrorCategory::InvalidArgument, "Directory path is empty");
    }

    fs::path candidate{trimmed};
    // resolve home directory if input starts with ~, otherwise treat as relative to current directory if not absolute
    if (trimmed.starts_with('~')) {
        fs::path resolved_home = home_dir;
        if (resolved_home.empty()) {
            auto home_result = resolve_home_directory();
            if (!home_result) {
                return std::unexpected(home_result.error());
            }
            resolved_home = *home_result;
        }

        if (trimmed.size() == 1U) {
            candidate = resolved_home;
        } else if (trimmed[1] == '/' || trimmed[1] == '\\') {
            candidate = resolved_home / trimmed.substr(2);
        } else {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    std::format("Unsupported home path syntax: {}", trimmed));
        }
    } else if (candidate.is_relative()) {
        candidate = current_dir / candidate;
    }

    return validate_directory_path(candidate);
}

}  // namespace expp::app
