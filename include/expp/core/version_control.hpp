/**
 * @file version_control.hpp
 * @brief Git-backed version tracking helpers for the TUI file explorer.
 *
 * ## Design Overview
 *
 * This module provides a compact, UI-ready view of Git repository status for a
 * single directory. It supports two backends selected at compile time:
 *
 *   - **CLI** (`popen`): Shells out to the `git` binary. Zero extra
 *     dependencies, always available where `git` is on `PATH`. Used when
 *     `EXPP_HAS_LIBGIT2` is `0`.
 *   - **libgit2**: Links against the native C library. Faster (no process
 *     spawn per directory) and handles edge cases like bare repositories
 *     natively. Used when `EXPP_HAS_LIBGIT2` is `1`.
 *
 * Both backends produce identical `VersionStatusSnapshot` results so the UI
 * layer never needs to know which backend is active.
 *
 * ### Data Flow
 *
 * ```
 *   directory ──► load_git_status()
 *                        │
 *           ┌────────────┴────────────┐
 *           ▼                         ▼
 *    load_git_status_cli()    load_git_status_libgit2()
 *           │                         │
 *           ▼                         ▼
 *    parse_porcelain_status()   parse_libgit2_status()
 *           │                         │
 *           └────────────┬────────────┘
 *                        ▼
 *              merge_entry_into_snapshot()
 *                        │
 *                        ▼
 *              VersionStatusSnapshot
 * ```
 *
 * ### Status Merging
 *
 * When multiple Git status entries map to the same directory child (e.g. a
 * staged rename where both the old and new path are reported), the highest-
 * priority status wins via `merge_status()`. The priority order is:
 * Conflicted > Deleted > Modified > Added > Renamed > Copied > Untracked >
 * Ignored > Clean.
 *
 * ### Usage in the Explorer
 *
 * The explorer calls `load_git_status()` when entering a directory. The
 * returned `VersionStatusSnapshot::statusesByPath` map is keyed by absolute
 * child path (lexically normal), so the UI can do an O(1) lookup per entry.
 * The summary fields (`dirty`, `stagedCount`, `branchName`, etc.) populate
 * the status bar and directory header.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_CORE_VERSION_CONTROL_HPP
#define EXPP_CORE_VERSION_CONTROL_HPP

#include "expp/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace expp::core {

namespace fs = std::filesystem;

/**
 * @brief Compact Git status state consumed by the explorer UI.
 *
 * Each enumerator maps to a specific Git porcelain status or libgit2 flag
 * combination. The numeric values are arbitrary; what matters is the
 * priority ordering used by `merge_status()` to pick the "most interesting"
 * status when multiple entries affect the same path.
 */
enum class VersionStatus : std::uint8_t {
    Clean,       ///< No changes (or not tracked by Git).
    Modified,    ///< File content or type has changed.
    Added,       ///< New file staged or detected in worktree.
    Deleted,     ///< File removed from index or worktree.
    Renamed,     ///< File renamed (detected via Git rename heuristics).
    Copied,      ///< File copied (detected via Git copy detection).
    Untracked,   ///< Present on disk but not tracked by Git.
    Ignored,     ///< Matches a `.gitignore` pattern.
    Conflicted,  ///< Merge conflict — both index and worktree disagree.
};

/**
 * @brief One path reported by `git status --porcelain=v1 -z`.
 *
 * Represents a single line (or record) from Git's machine-readable status
 * output. Fields `staged`, `unstaged`, and `untracked` are derived from the
 * porcelain status codes and used for summary counting in the snapshot.
 */
struct VersionStatusEntry {
    fs::path path;                       ///< Path relative to the repository root.
    VersionStatus status{VersionStatus::Clean};  ///< Resolved status for this entry.
    bool staged{false};                  ///< Change is present in the index.
    bool unstaged{false};               ///< Change is present in the worktree.
    bool untracked{false};              ///< File is not tracked by Git.
};

/**
 * @brief Status snapshot aggregated to direct children of a directory.
 *
 * This is the primary output type consumed by the explorer UI. It condenses
 * all Git status information for one directory into:
 *   - A map from absolute child path to its most-interesting status.
 *   - Summary counters for the status bar.
 *   - Branch/tracking metadata for the header.
 *
 * Only direct children of `directory` are tracked; deeper entries are
 * collapsed into their immediate parent via `merge_entry_into_snapshot()`.
 */
struct VersionStatusSnapshot {
    fs::path repositoryRoot;             ///< Absolute path to the repository root.
    fs::path directory;                  ///< Absolute path of the directory being queried.
    /// Map from absolute child path (lexically normal) to its aggregated status.
    std::unordered_map<std::string, VersionStatus> statusesByPath{}; // NOLINT
    std::string branchName{};            ///< Current branch name, or "detached" in detached HEAD.
    bool detachedHead{false};            ///< `true` if HEAD does not point to a branch.
    bool dirty{false};                   ///< `true` if any tracked file has unstaged changes.
    std::uint32_t stagedCount{0};        ///< Number of staged changes in this directory.
    std::uint32_t unstagedCount{0};      ///< Number of unstaged changes in this directory.
    std::uint32_t untrackedCount{0};     ///< Number of untracked files in this directory.
    std::optional<std::uint32_t> aheadCount{};   ///< Commits ahead of upstream (absent if no upstream).
    std::optional<std::uint32_t> behindCount{};  ///< Commits behind upstream (absent if no upstream).
    bool repositoryFound{false};         ///< `true` if a Git repository was found at this path.
};

/**
 * @brief Parses raw NUL-delimited porcelain v1 status output.
 *
 * Consumes the output of `git status --porcelain=v1 -z` and returns a
 * structured list of `VersionStatusEntry`. Handles rename/copy records
 * (which span two NUL-delimited fields).
 *
 * @param output Raw NUL-delimited status text from Git.
 * @return A vector of parsed entries, or an error if the format is malformed.
 */
[[nodiscard]] Result<std::vector<VersionStatusEntry>> parse_porcelain_status(std::string_view output);

/**
 * @brief Loads Git status for the direct children of `directory`.
 *
 * This is the main entry point. It dispatches to the fastest available
 * backend (libgit2 if compiled in, otherwise CLI via `popen`).
 *
 * @param directory Absolute path to the directory to query.
 * @return A populated `VersionStatusSnapshot`, or an error if Git is not
 * available or the command fails.
 */
[[nodiscard]] Result<VersionStatusSnapshot> load_git_status(const fs::path& directory);

/**
 * @brief Merges two statuses when multiple nested changes affect one child
 * directory.
 *
 * The higher-priority status wins. Priority order:
 * Conflicted > Deleted > Modified > Added > Renamed > Copied > Untracked >
 * Ignored > Clean.
 *
 * @param current The existing status already recorded for the path.
 * @param incoming The new status to merge in.
 * @return The status with higher priority.
 */
[[nodiscard]] VersionStatus merge_status(VersionStatus current, VersionStatus incoming) noexcept;

/**
 * @brief Returns the compact UI marker for a version status.
 *
 * Used to decorate file entries in the explorer listing:
 *   - `[M]` Modified, `[A]` Added, `[D]` Deleted, `[R]` Renamed,
 *     `[C]` Copied, `[?]` Untracked, `[I]` Ignored, `[!]` Conflicted.
 *   - Empty string for `Clean`.
 *
 * @param status The version status to render.
 * @return A short marker string suitable for inline display.
 */
[[nodiscard]] std::string_view status_marker(VersionStatus status) noexcept;

/**
 * @brief Returns whether the status represents a Git-ignored path.
 *
 * Ignored paths are typically hidden unless the user has toggled
 * "show ignored" in the explorer.
 *
 * @param status The version status to check.
 * @return `true` if the status is `VersionStatus::Ignored`.
 */
[[nodiscard]] bool is_ignored(VersionStatus status) noexcept;

}  // namespace expp::core

#endif  // EXPP_CORE_VERSION_CONTROL_HPP
