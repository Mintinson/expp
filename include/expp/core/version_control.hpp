/**
 * @file version_control.hpp
 * @brief Git-backed version tracking helpers.
 *
 * The public API is intentionally small: parse porcelain status output for
 * deterministic tests, and load an aggregated status snapshot for one
 * directory through the Git CLI backend.
 */

#ifndef EXPP_CORE_VERSION_CONTROL_HPP
#define EXPP_CORE_VERSION_CONTROL_HPP

#include "expp/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace expp::core {

namespace fs = std::filesystem;

/**
 * @brief Compact Git status state consumed by the explorer UI.
 */
enum class VersionStatus : std::uint8_t {
    Clean,
    Modified,
    Added,
    Deleted,
    Renamed,
    Copied,
    Untracked,
    Ignored,
    Conflicted,
};

/**
 * @brief One path reported by `git status --porcelain=v1 -z`.
 */
struct VersionStatusEntry {
    fs::path path;
    VersionStatus status{VersionStatus::Clean};
};

/**
 * @brief Status snapshot aggregated to direct children of a directory.
 */
struct VersionStatusSnapshot {
    fs::path repositoryRoot;
    fs::path directory;
    std::unordered_map<std::string, VersionStatus> statusesByPath{};
    bool repositoryFound{false};
};

/**
 * @brief Parses raw NUL-delimited porcelain v1 status output.
 */
[[nodiscard]] Result<std::vector<VersionStatusEntry>> parse_porcelain_status(std::string_view output);

/**
 * @brief Loads Git status for the direct children of `directory`.
 */
[[nodiscard]] Result<VersionStatusSnapshot> load_git_status(const fs::path& directory);

/**
 * @brief Merges two statuses when multiple nested changes affect one child directory.
 */
[[nodiscard]] VersionStatus merge_status(VersionStatus current, VersionStatus incoming) noexcept;

/**
 * @brief Returns the compact UI marker for a version status.
 */
[[nodiscard]] std::string_view status_marker(VersionStatus status) noexcept;

/**
 * @brief Returns whether the status represents a Git-ignored path.
 */
[[nodiscard]] bool is_ignored(VersionStatus status) noexcept;

}  // namespace expp::core

#endif  // EXPP_CORE_VERSION_CONTROL_HPP
