/**
 * @file navigation_utils.hpp
 * @brief Small helpers for path-oriented navigation commands.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_APP_NAVIGATION_UTILS_HPP
#define EXPP_APP_NAVIGATION_UTILS_HPP

#include "expp/core/error.hpp"

#include <filesystem>
#include <string_view>

namespace expp::app {

/**
 * @brief Resolves the current user's home directory.
 * @return Absolute home directory path or Error
 */
[[nodiscard]] core::Result<std::filesystem::path> resolve_home_directory();

/**
 * @brief Resolves a user-entered directory path against the current explorer directory.
 *
 * Supports `~` expansion and relative paths.
 *
 * @param input Raw user input
 * @param current_dir Current explorer directory
 * @param home_dir Optional explicit home directory for tests
 * @return Resolved existing directory path or Error
 */
[[nodiscard]] core::Result<std::filesystem::path> resolve_directory_input(std::string_view input,
                                                                        const std::filesystem::path& current_dir,
                                                                        const std::filesystem::path& home_dir = {});

}  // namespace expp::app

#endif  // EXPP_APP_NAVIGATION_UTILS_HPP
