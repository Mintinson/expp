/**
 * @file version_control.cpp
 * @brief Implementation of Git-backed version status tracking.
 *
 * ## Architecture
 *
 * The module compiles into two mutually exclusive backends controlled by the
 * `EXPP_HAS_LIBGIT2` preprocessor toggle:
 *
 *   - **CLI backend** (default): Invokes the `git` binary via `popen()` and
 *     parses the machine-readable porcelain output. Simple, always available,
 *     no extra dependencies.
 *   - **libgit2 backend**: Links against libgit2 for in-process repository
 *     access. Avoids the overhead of spawning a child process per directory
 *     load.
 *
 * Both backends produce identical `VersionStatusSnapshot` results through the
 * shared entry points `load_git_status()` and `parse_porcelain_status()`.
 *
 * ## Key Design Decisions
 *
 * ### Status Merging by Priority
 *
 * Multiple Git status entries can map to the same directory child (e.g. a
 * file that is both staged-modified and unstaged-modified, or a rename where
 * old and new paths share the same parent). `merge_status()` picks the
 * "loudest" status via a priority scale so the UI shows one definitive
 * indicator per entry.
 *
 * ### Direct-Child Aggregation
 *
 * `merge_entry_into_snapshot()` converts an arbitrary repository-relative
 * status path into its direct child under the queried directory. This is
 * what allows the UI to show `[M]` on a deeply nested changed file's parent
 * folder — the status "bubbles up" to the first visible level.
 *
 * ### Shell Quoting
 *
 * The CLI backend builds shell commands with `quote_for_shell()`. This is
 * safer than string concatenation but still relies on the `git` binary being
 * the intended one. Paths containing double-quotes are escaped.
 *
 * ## Git Commands Used (CLI backend)
 *
 * | Command | Purpose |
 * |---------|---------|
 * | `git -C <dir> rev-parse --show-toplevel` | Discover repository root |
 * | `git -C <dir> rev-parse --abbrev-ref HEAD` | Get current branch name |
 * | `git -C <dir> status --porcelain=v1 -z --ignored=matching --untracked-files=all -- <pathspec>`
 * | Full status listing | | `git -C <dir> rev-list --left-right --count HEAD...@{upstream}` |
 * Ahead/behind counts |
 *
 * @copyright Copyright (c) 2026
 *
 * ### TODO:
 *
 * ibgit2 is initialized and shut down on every load_git_status_libgit2 call — which means every
 * directory navigation in the explorer. Since the explorer refreshes the current directory
 * frequently, consider a reference-counted init guard or moving initialization to app startup
 * (matching libgit2's own recommendation to call git_libgit2_init() once at process start).
 */

#include "expp/core/version_control.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef EXPP_HAS_LIBGIT2
    #define EXPP_HAS_LIBGIT2 0
#endif

#if EXPP_HAS_LIBGIT2
    #include <git2.h>
#endif

#ifdef _WIN32
    #define EXPP_POPEN  _popen
    #define EXPP_PCLOSE _pclose
#else
    #define EXPP_POPEN  popen
    #define EXPP_PCLOSE pclose
#endif

namespace expp::core {

namespace {
/**
 * @brief Checks whether a path's first component is the parent directory
 * marker `".."`.
 *
 * Used by `direct_child_for_status()` to detect when a status path falls
 * outside the queried directory tree.
 *
 * @param path The filesystem path to check.
 * @return `true` if the path starts with `..`.
 */
[[nodiscard]] bool path_starts_with_parent_marker(const fs::path& path) {
    auto it = path.begin();
    return it != path.end() && *it == "..";
}

/**
 * @brief Determines the direct child path relative to the directory for a given status path.
 *
 * Computes whether the given status path (relative to the repository root) falls within the
 * expected directory. If so, extracts the direct child entry in `directory` representing the
 * status.
 *
 * @param repository_root The absolute path resolving to the root of the Git repository.
 * @param directory The absolute path of the directory determining the status context.
 * @param status_path A path relative to the repository root where the status change occurred.
 * @return The absolute path of the expected child entry if it is valid, otherwise std::nullopt.
 */
[[nodiscard]] std::optional<fs::path> direct_child_for_status(const fs::path& repository_root,
                                                              const fs::path& directory,
                                                              const fs::path& status_path) {
    const fs::path absolute_path = (repository_root / status_path).lexically_normal();
    const fs::path relative_to_directory = absolute_path.lexically_relative(directory);
    if (relative_to_directory.empty() || path_starts_with_parent_marker(relative_to_directory)) {
        return std::nullopt;
    }

    auto it = relative_to_directory.begin();
    if (it == relative_to_directory.end()) {
        return std::nullopt;
    }
    return (directory / *it).lexically_normal();
}

/**
 * @brief Returns a numeric priority for status merging.
 *
 * Higher numbers win. The exact values are arbitrary; only relative ordering
 * matters. `merge_status()` uses this to pick the "most interesting" status
 * when multiple entries map to the same directory child.
 */
[[nodiscard]] int status_priority(VersionStatus status) noexcept {
    switch (status) {
        case VersionStatus::Conflicted:
            return 80;
        case VersionStatus::Deleted:
            return 70;
        case VersionStatus::Modified:
            return 60;
        case VersionStatus::Added:
            return 50;
        case VersionStatus::Renamed:
            return 45;
        case VersionStatus::Copied:
            return 40;
        case VersionStatus::Untracked:
            return 30;
        case VersionStatus::Ignored:
            return 20;
        default:
            return 0;
    }
}

/**
 * @brief Detects merge conflicts from porcelain status code pairs.
 *
 * Git porcelain represents conflicts as `UU` (both modified), `AA` (both
 * added), `DD` (both deleted), or any combination where at least one side
 * has `U`.
 *
 * @param index_status The index (staging area) status character.
 * @param worktree_status The worktree status character.
 * @return `true` if the code pair represents a conflict.
 */
[[nodiscard]] bool is_conflict_status(char index_status, char worktree_status) noexcept {
    return index_status == 'U' || worktree_status == 'U' ||
           (index_status == 'A' && worktree_status == 'A') ||
           (index_status == 'D' && worktree_status == 'D');
}

/**
 * @brief Checks whether the index status character indicates a staged change.
 */
[[nodiscard]] bool is_staged_status(char index_status) noexcept {
    return index_status == 'M' || index_status == 'A' || index_status == 'D' ||
           index_status == 'R' || index_status == 'C' || index_status == 'T';
}

/**
 * @brief Checks whether the worktree status character indicates an unstaged
 * change.
 */
[[nodiscard]] bool is_unstaged_status(char worktree_status) noexcept {
    return worktree_status == 'M' || worktree_status == 'D' || worktree_status == 'R' ||
           worktree_status == 'C' || worktree_status == 'T';
}

/**
 * @brief Converts a porcelain status code pair into a `VersionStatus` enum.
 *
 * The two-character porcelain code (e.g. `"M "`, `"??"`, `"!!"`) encodes both
 * the index (first char) and worktree (second char) state. This function
 * resolves the pair to a single `VersionStatus` using a precedence chain:
 * ignored > untracked > conflicted > deleted > modified > added > renamed >
 * copied > clean.
 *
 * @param index_status First character of the porcelain status code (index state).
 * @param worktree_status Second character (worktree state).
 * @return The resolved `VersionStatus`.
 */
[[nodiscard]] VersionStatus parse_status_code(char index_status, char worktree_status) noexcept {
    if (index_status == '!' && worktree_status == '!') {
        return VersionStatus::Ignored;
    }
    if (index_status == '?' && worktree_status == '?') {
        return VersionStatus::Untracked;
    }
    if (is_conflict_status(index_status, worktree_status)) {
        return VersionStatus::Conflicted;
    }
    if (index_status == 'D' || worktree_status == 'D') {
        return VersionStatus::Deleted;
    }
    if (index_status == 'M' || worktree_status == 'M' || index_status == 'T' ||
        worktree_status == 'T') {
        return VersionStatus::Modified;
    }
    if (index_status == 'A' || worktree_status == 'A') {
        return VersionStatus::Added;
    }
    if (index_status == 'R' || worktree_status == 'R') {
        return VersionStatus::Renamed;
    }
    if (index_status == 'C' || worktree_status == 'C') {
        return VersionStatus::Copied;
    }
    return VersionStatus::Clean;
}

/**
 * @brief Maps a raw status entry into the snapshot's per-child status map.
 *
 * Converts the repository-relative `entry.path` to its direct child under
 * `snapshot.directory` via `direct_child_for_status()`. If the child is
 * already tracked, the higher-priority status wins via `merge_status()`.
 *
 * @param snapshot The snapshot being populated.
 * @param entry A raw status entry from `parse_porcelain_status()`.
 */
void merge_entry_into_snapshot(VersionStatusSnapshot& snapshot, const VersionStatusEntry& entry) {
    auto child = direct_child_for_status(snapshot.repositoryRoot, snapshot.directory, entry.path);
    if (!child.has_value()) {
        return;
    }

    auto key = (*child).lexically_normal().string();
    if (auto it = snapshot.statusesByPath.find(key); it != snapshot.statusesByPath.end()) {
        it->second = merge_status(it->second, entry.status);
    } else {
        snapshot.statusesByPath.emplace(std::move(key), entry.status);
    }
}

/**
 * @brief Updates the snapshot's summary counters from a single status entry.
 *
 * Counts staged, unstaged, and untracked changes. Also sets `dirty` if the
 * entry represents a meaningful change (everything except Clean and Ignored).
 *
 * @param snapshot The snapshot whose counters to update.
 * @param entry The raw status entry to count.
 */
void accumulate_summary(VersionStatusSnapshot& snapshot, const VersionStatusEntry& entry) {
    if (entry.status != VersionStatus::Clean && entry.status != VersionStatus::Ignored) {
        snapshot.dirty = true;
    }
    if (entry.staged) {
        ++snapshot.stagedCount;
    }
    if (entry.unstaged) {
        ++snapshot.unstagedCount;
    }
    if (entry.untracked) {
        ++snapshot.untrackedCount;
    }
}

// ============================================================================
// CLI Backend — shells out to the `git` binary via popen()
// ============================================================================
#ifndef EXPP_HAS_LIBGIT2

/**
 * @brief Strips trailing `\n` and `\r` characters from a string view.
 *
 * Used to clean up command output before treating it as a filesystem path
 * or branch name. Operates on the view without allocating.
 *
 * @param text The raw command output.
 * @return A view with trailing newline/carriage-return characters removed.
 */
[[nodiscard]] std::string_view trim_trailing_newlines(std::string_view text) {
    auto it = std::ranges::find_if_not(std::ranges::crbegin(text), std::ranges::crend(text),
                                       [](char ch) { return ch == '\n' || ch == '\r'; });
    if (it == std::ranges::crend(text)) {
        return {};
    }
    return {text.data(), static_cast<size_t>(std::ranges::distance(text.begin(), it.base()))};
}

/**
 * @brief Escapes double-quote characters in a path and wraps it in quotes.
 *
 * Produces a string safe for embedding in a shell command. Double-quotes
 * inside the path are escaped as `\"` to prevent command injection through
 * crafted filenames.
 *
 * @param path The filesystem path to quote.
 * @return A double-quoted and escaped string suitable for shell use.
 */
[[nodiscard]] std::string quote_for_shell(const fs::path& path) {
    using namespace std::string_view_literals;
    auto quoted = path.string() | std::views::split('"') | std::views::join_with("\\\""sv) |
                  std::ranges::to<std::string>();
    return std::format("\"{}\"", quoted);
}

/**
 * @brief Runs a shell command and captures its combined stdout + stderr.
 *
 * Stderr is redirected into stdout (`2>&1`) so that Git diagnostics are
 * included in error messages rather than being silently lost.
 *
 * @param command The full shell command string to execute.
 * @return The command's combined output on success, or an `Error` describing
 * the failure (startup failure, read error, or non-zero exit).
 */
[[nodiscard]] Result<std::string> read_command_output(const std::string& command) {
    const std::string merged_command = std::format("{} 2>&1", command);
    FILE* pipe = EXPP_POPEN(merged_command.c_str(), "r");
    if (pipe == nullptr) {
        return make_error(ErrorCategory::System, std::format("Failed to run command: {}", command));
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (true) {
        const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe);
        if (read > 0) {
            output.append(buffer.data(), read);
        }
        if (read < buffer.size()) {
            if (std::feof(pipe) != 0) {
                break;
            }
            if (std::ferror(pipe) != 0) {
                (void)EXPP_PCLOSE(pipe);
                return make_error(ErrorCategory::IO,
                                  std::format("Failed to read command output: {}", command));
            }
        }
    }

    const int exit_code = EXPP_PCLOSE(pipe);
    if (exit_code != 0) {
        const auto trimmed = trim_trailing_newlines(output);
        return make_error(ErrorCategory::NoSupport,
                          std::format("Git command failed (exit {}): {}", exit_code,
                                      trimmed.empty() ? "no output" : trimmed));
    }
    return output;
}

/**
 * @brief Finds the root of the Git repository containing the given directory.
 *
 * Runs `git rev-parse --show-toplevel` from the target directory to discover
 * the repository boundary.
 *
 * @param directory The directory to start searching from.
 * @return The absolute, lexically-normal repository root path on success.
 */
[[nodiscard]] Result<fs::path> discover_repository_root_cli(const fs::path& directory) {
    const std::string command =
        std::format("git -C {} rev-parse --show-toplevel", quote_for_shell(directory));
    auto output = read_command_output(command);
    if (!output) {
        return std::unexpected(output.error());
    }

    const fs::path root = trim_trailing_newlines(*output);
    if (root.empty()) {
        return make_error(ErrorCategory::NoSupport, "Git repository root was not reported");
    }
    return root.lexically_normal();
}

/**
 * @brief Populates branch name, detached-head flag, and ahead/behind counts
 * on the snapshot.
 *
 * Runs two Git commands:
 *   - `rev-parse --abbrev-ref HEAD` for the branch name.
 *   - `rev-list --left-right --count HEAD...@{upstream}` for ahead/behind.
 *
 * Failures in either command are silently ignored (the snapshot will simply
 * lack that metadata).
 *
 * @param snapshot The snapshot to populate (mutated in-place).
 */
void load_cli_branch_summary(VersionStatusSnapshot& snapshot) {
    const std::string branch_command = std::format("git -C {} rev-parse --abbrev-ref HEAD",
                                                   quote_for_shell(snapshot.repositoryRoot));
    if (auto output = read_command_output(branch_command)) {
        const auto branch = trim_trailing_newlines(*output);
        snapshot.detachedHead = branch == "HEAD";
        snapshot.branchName = snapshot.detachedHead ? "detached" : std::string{branch};
    }

    const std::string ahead_command =
        std::format("git -C {} rev-list --left-right --count HEAD...@{{upstream}}",
                    quote_for_shell(snapshot.repositoryRoot));
    if (auto output = read_command_output(ahead_command)) {
        std::string text{trim_trailing_newlines(*output)};
        const auto separator = text.find_first_of(" \t");
        if (separator != std::string::npos) {
            std::uint32_t ahead = 0;
            std::uint32_t behind = 0;
            const std::string_view ahead_text{text.data(), separator};
            const std::string_view behind_text{text.data() + separator + 1,
                                               text.size() - separator - 1};
            const auto [ahead_ptr, ahead_ec] =
                std::from_chars(ahead_text.data(), ahead_text.data() + ahead_text.size(), ahead);
            const auto [behind_ptr, behind_ec] = std::from_chars(
                behind_text.data(), behind_text.data() + behind_text.size(), behind);
            if (ahead_ec == std::errc{} && behind_ec == std::errc{} &&
                ahead_ptr == ahead_text.data() + ahead_text.size() &&
                behind_ptr == behind_text.data() + behind_text.size()) {
                snapshot.aheadCount = ahead;
                snapshot.behindCount = behind;
            }
        }
    }
}

/**
 * @brief Loads Git status using the CLI backend.
 *
 * Full pipeline:
 *   1. Discover the repository root via `rev-parse --show-toplevel`.
 *   2. Compute the relative pathspec from the queried directory to the root.
 *   3. Run `git status --porcelain=v1 -z` scoped to that pathspec.
 *   4. Parse the NUL-delimited output via `parse_porcelain_status()`.
 *   5. Merge entries into the snapshot and accumulate summary counters.
 *   6. Load branch metadata via `load_cli_branch_summary()`.
 *
 * @param directory The directory to query.
 * @return A populated `VersionStatusSnapshot`, or an error at any stage.
 */
[[nodiscard]] Result<VersionStatusSnapshot> load_git_status_cli(const fs::path& directory) {
    auto root_result = discover_repository_root_cli(directory);
    if (!root_result) {
        return std::unexpected(root_result.error());
    }

    const fs::path repository_root = root_result->lexically_normal();
    const fs::path normalized_directory = directory.lexically_normal();
    const fs::path relative_directory = normalized_directory.lexically_relative(repository_root);
    const fs::path pathspec = relative_directory.empty() ? fs::path{"."} : relative_directory;

    const std::string command = std::format(
        "git -C {} status --porcelain=v1 -z --ignored=matching --untracked-files=all -- {}",
        quote_for_shell(repository_root), quote_for_shell(pathspec));
    auto output = read_command_output(command);
    if (!output) {
        return std::unexpected(output.error());
    }

    auto entries_result = parse_porcelain_status(*output);
    if (!entries_result) {
        return std::unexpected(entries_result.error());
    }

    VersionStatusSnapshot snapshot{
        .repositoryRoot = repository_root,
        .directory = normalized_directory,
        .repositoryFound = true,
    };

    for (const auto& entry : *entries_result) {
        merge_entry_into_snapshot(snapshot, entry);
        accumulate_summary(snapshot, entry);
    }
    load_cli_branch_summary(snapshot);

    return snapshot;
}

// ============================================================================
// libgit2 Backend — in-process Git access via the libgit2 C library
// ============================================================================
#else

/**
 * @brief Custom deleter that binds a libgit2 `_free()` function to a
 * `std::unique_ptr`.
 *
 * @tparam T The libgit2 opaque struct type (e.g. `git_repository`).
 * @tparam FreeFn The matching `_free()` function pointer.
 */
template <typename T, void (*FreeFn)(T*)>
struct GitDeleter {
    void operator()(T* value) const noexcept {
        if (value != nullptr) {
            FreeFn(value);
        }
    }
};

/// RAII wrapper for `git_repository`.
using GitRepositoryPtr =
    std::unique_ptr<git_repository, GitDeleter<git_repository, git_repository_free>>;
/// RAII wrapper for `git_status_list`.
using GitStatusListPtr =
    std::unique_ptr<git_status_list, GitDeleter<git_status_list, git_status_list_free>>;
/// RAII wrapper for `git_reference`.
using GitReferencePtr =
    std::unique_ptr<git_reference, GitDeleter<git_reference, git_reference_free>>;

/**
 * @brief RAII guard that initializes and shuts down the libgit2 library.
 *
 * Calls `git_libgit2_init()` on construction and `git_libgit2_shutdown()` on
 * destruction. Not copyable or movable — each backend invocation creates its
 * own guard instance.
 */
class Libgit2Call {
public:
    Libgit2Call() : initialized_(git_libgit2_init() >= 0) {}

    ~Libgit2Call() {
        if (initialized_) {
            (void)git_libgit2_shutdown();
        }
    }

    Libgit2Call(const Libgit2Call&) = delete;
    Libgit2Call& operator=(const Libgit2Call&) = delete;
    Libgit2Call(Libgit2Call&&) = delete;
    Libgit2Call& operator=(Libgit2Call&&) = delete;

    /// @return `true` if libgit2 was successfully initialized.
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_{false};
};

/**
 * @brief Create an @c Error describing the most recent libgit2 failure.
 *
 * This helper queries libgit2's last error via @c git_error_last() and
 * formats a human-readable @c Error that includes the provided
 * @p operation description and the associated filesystem @p path.
 *
 * The function always returns an @c Error with category
 * @c ErrorCategory::NoSupport because libgit2 failures indicate that the
 * requested repository operation could not be performed.
 *
 * @param operation Short description of the libgit2 operation that failed
 *                  (for example "Open Git repository").
 * @param path The filesystem path that was the target of the operation; this
 *             is included in the formatted diagnostic message.
 * @param location Source location where the error was detected; defaults to
 *                 the call site via @c std::source_location::current().
 * @return An @c Error containing a formatted message with libgit2's last
 *         diagnostic text when available, otherwise a generic message.
 */
[[nodiscard]] Error libgit2_error(std::string_view operation,
                                  const fs::path& path,
                                  std::source_location location = std::source_location::current()) {
    const git_error* error = git_error_last();
    const std::string detail =
        error != nullptr && error->message != nullptr ? error->message : "unknown libgit2 error";
    return Error{ErrorCategory::NoSupport,
                 std::format("{} failed for '{}': {}", operation, path.string(), detail), location};
}

/**
 * @brief Extracts a usable path from a libgit2 diff delta.
 *
 * Prefers the `new_file.path` (the path after the change); falls back to
 * `old_file.path` for deletions where the new path may be absent.
 *
 * @param delta A git_diff_delta from a status entry, or nullptr.
 * @return The file path string, or nullptr if unavailable.
 */
[[nodiscard]] const char* delta_path(const git_diff_delta* delta) noexcept {
    if (delta == nullptr) {
        return nullptr;
    }
    if (delta->new_file.path != nullptr) {
        return delta->new_file.path;
    }
    return delta->old_file.path;
}

/**
 * @brief Converts a libgit2 status bitmask to a `VersionStatus` enum.
 *
 * Uses the same precedence chain as `parse_status_code()` so that both
 * backends produce consistent results: ignored > untracked > conflicted >
 * deleted > modified > added > renamed > copied > clean.
 *
 * @param flags Bitmask of `GIT_STATUS_*` flags from a `git_status_entry`.
 * @return The resolved `VersionStatus`.
 */
[[nodiscard]] VersionStatus parse_libgit2_status(unsigned int flags) noexcept {
    // GIT_STATUS_INDEX_xxx stands for staged changes, GIT_STATUS_WT_xxx for unstaged changes.
    if ((flags & GIT_STATUS_IGNORED) != 0U) {
        return VersionStatus::Ignored;
    }
    if ((flags & GIT_STATUS_WT_NEW) != 0U) {
        return VersionStatus::Untracked;
    }
    if ((flags & GIT_STATUS_CONFLICTED) != 0U) {
        return VersionStatus::Conflicted;
    }
    if ((flags & (GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_DELETED)) != 0U) {
        return VersionStatus::Deleted;
    }
    if ((flags & (GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_MODIFIED | GIT_STATUS_INDEX_TYPECHANGE |
                  GIT_STATUS_WT_TYPECHANGE)) != 0U) {
        return VersionStatus::Modified;
    }
    if ((flags & GIT_STATUS_INDEX_NEW) != 0U) {
        return VersionStatus::Added;
    }
    if ((flags & (GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_RENAMED)) != 0U) {
        return VersionStatus::Renamed;
    }
    return VersionStatus::Clean;
}

/**
 * @brief Checks whether a libgit2 status bitmask includes any staged changes.
 */
[[nodiscard]] bool has_staged_libgit2_status(unsigned int flags) noexcept {
    return (flags & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED |
                     GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)) != 0U;
}

/**
 * @brief Checks whether a libgit2 status bitmask includes any unstaged changes.
 */
[[nodiscard]] bool has_unstaged_libgit2_status(unsigned int flags) noexcept {
    // ignore untracked files for now
    return (flags & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_RENAMED |
                     GIT_STATUS_WT_TYPECHANGE)) != 0U;
}

/**
 * @brief Populates branch and upstream tracking metadata on the snapshot
 * using the libgit2 C API.
 *
 * Queries the repository HEAD to determine the current branch (or detached
 * state), then resolves the upstream tracking branch to compute ahead/behind
 * commit counts via `git_graph_ahead_behind()`.
 *
 * @param repository An open `git_repository` handle.
 * @param snapshot The snapshot to populate (mutated in-place).
 */
void load_libgit2_branch_summary(git_repository* repository, VersionStatusSnapshot& snapshot) {
    git_reference* head_raw = nullptr;
    if (git_repository_head(&head_raw, repository) != 0) {
        return;
    }
    GitReferencePtr head{head_raw};

    snapshot.detachedHead = git_repository_head_detached(repository) == 1;
    if (snapshot.detachedHead) {
        snapshot.branchName = "detached";
        return;
    }

    const char* branch_name = nullptr;
    if (git_branch_name(&branch_name, head.get()) == 0 && branch_name != nullptr) {
        snapshot.branchName = branch_name;
    }

    git_reference* upstream_raw = nullptr;
    // find the upstream tracking branch for HEAD; if it doesn't exist or can't be resolved,
    if (git_branch_upstream(&upstream_raw, head.get()) != 0) {
        return;
    }
    GitReferencePtr upstream{upstream_raw};

    // get the commit OIDs for HEAD and the upstream tracking branch
    const git_oid* head_oid = git_reference_target(head.get());
    const git_oid* upstream_oid = git_reference_target(upstream.get());
    if (head_oid == nullptr || upstream_oid == nullptr) {
        return;
    }

    std::size_t ahead = 0;
    std::size_t behind = 0;
    // calculate ahead/behind counts between HEAD and the upstream tracking branch
    if (git_graph_ahead_behind(&ahead, &behind, repository, head_oid, upstream_oid) == 0) {
        snapshot.aheadCount = static_cast<std::uint32_t>(ahead);
        snapshot.behindCount = static_cast<std::uint32_t>(behind);
    }
}

/**
 * @brief Loads Git status using the libgit2 backend.
 *
 * Full pipeline:
 *   1. Initialize libgit2 via `Libgit2Call` (RAII).
 *   2. Open the repository with `git_repository_open_ext()`.
 *   3. Build a `git_status_options` struct with untracked/ignored recursion
 *      and rename detection enabled.
 *   4. Scope the query to the requested directory via the `pathspec` option.
 *   5. Enumerate status entries; convert each to a `VersionStatusEntry` and
 *      merge into the snapshot.
 *   6. Load branch metadata via `load_libgit2_branch_summary()`.
 *
 * @param directory The directory to query.
 * @return A populated `VersionStatusSnapshot`, or an error at any stage.
 *
 * TODO: the string maybe stick to utf-8
 */
[[nodiscard]] Result<VersionStatusSnapshot> load_git_status_libgit2(const fs::path& directory) {
    Libgit2Call call;
    if (!call.initialized()) {
        return make_error(ErrorCategory::NoSupport, "Failed to initialize libgit2");
    }

    const std::string utf8_dir = reinterpret_cast<const char*>(directory.u8string().c_str());
    git_repository* repository_raw = nullptr;
    if (git_repository_open_ext(&repository_raw, utf8_dir.c_str(), 0, nullptr) != 0) {
        return std::unexpected(libgit2_error("Open Git repository", directory));
    }
    GitRepositoryPtr repository{repository_raw};

    const char* workdir = git_repository_workdir(repository.get());
    if (workdir == nullptr) {
        return make_error(ErrorCategory::NoSupport,
                          std::format("Git repository has no worktree: {}", directory.string()));
    }

    // const fs::path repository_root = fs::path{workdir}.lexically_normal();
    const fs::path repository_root =
        fs::path{reinterpret_cast<const char8_t*>(workdir)}.lexically_normal();
    const fs::path normalized_directory = directory.lexically_normal();
    const fs::path relative_directory = normalized_directory.lexically_relative(repository_root);

    // git_status_options options ;= GIT_STATUS_OPTIONS_INIT;
    git_status_options options;
    git_status_options_init(&options, GIT_STATUS_OPTIONS_VERSION);
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    // clang-format off
    options.flags =
        GIT_STATUS_OPT_INCLUDE_UNTRACKED 
        | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS 
        | GIT_STATUS_OPT_INCLUDE_IGNORED 
        // | GIT_STATUS_OPT_RECURSE_IGNORED_DIRS     // this maybe too expensive, consider only top-level
        // // ignored files with GIT_STATUS_OPT_INCLUDE_IGNORED
        // | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX  // this maybe too expensive on larger repositories,
        // // consider disabling or limit the number of rename
        // // detections
        | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
    // clang-format on

    std::string pathspec_text;
    char* pathspec_ptr = nullptr;
    git_strarray pathspec{};
    if (!relative_directory.empty() && relative_directory != fs::path{"."}) {
        pathspec_text =
            reinterpret_cast<const char*>(relative_directory.generic_u8string().c_str());
        // pathspec_text = relative_directory.generic_string();  // turn in / format in all
        // platforms
        pathspec_ptr = pathspec_text.data();
        pathspec.strings = &pathspec_ptr;
        pathspec.count = 1;
        options.pathspec = pathspec;
    }

    git_status_list* status_list_raw = nullptr;
    // this operation takes time
    if (git_status_list_new(&status_list_raw, repository.get(), &options) != 0) {
        return std::unexpected(libgit2_error("Load Git status", directory));
    }
    GitStatusListPtr status_list{status_list_raw};

    VersionStatusSnapshot snapshot{
        .repositoryRoot = repository_root,
        .directory = normalized_directory,
        .repositoryFound = true,
    };
    load_libgit2_branch_summary(repository.get(), snapshot);

    const std::size_t count = git_status_list_entrycount(status_list.get());
    for (std::size_t index = 0; index < count; ++index) {
        const git_status_entry* status_entry = git_status_byindex(status_list.get(), index);
        if (status_entry == nullptr) {
            continue;
        }

        const char* path = delta_path(status_entry->index_to_workdir);
        if (path == nullptr) {
            path = delta_path(status_entry->head_to_index);
        }
        if (path == nullptr) {
            continue;
        }

        VersionStatusEntry entry{
            // .path = fs::path{path}.lexically_normal(),
            .path = fs::path{reinterpret_cast<const char8_t*>(path)}.lexically_normal(),
            .status = parse_libgit2_status(status_entry->status),
            .staged = has_staged_libgit2_status(status_entry->status),
            .unstaged = has_unstaged_libgit2_status(status_entry->status),
            .untracked = (status_entry->status & GIT_STATUS_WT_NEW) != 0U,
        };

        merge_entry_into_snapshot(snapshot, entry);
        accumulate_summary(snapshot, entry);
    }

    return snapshot;
}
#endif

}  // namespace

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Parses NUL-delimited Git porcelain v1 status output.
 *
 * Each record has the format: `XY<SPACE><path>\0`. Rename and copy records
 * (`R` or `C` as the first character) are followed by an additional
 * NUL-terminated field containing the original path, which this function
 * skips.
 *
 * @param output Raw NUL-delimited output from `git status --porcelain=v1 -z`.
 * @return A vector of parsed `VersionStatusEntry` objects, or an error if the
 * output is malformed.
 */
Result<std::vector<VersionStatusEntry>> parse_porcelain_status(std::string_view output) {
    std::vector<VersionStatusEntry> entries;
    std::size_t cursor = 0;

    while (cursor < output.size()) {
        const std::size_t record_end = output.find('\0', cursor);
        if (record_end == std::string_view::npos) {
            return make_error(ErrorCategory::InvalidArgument,
                              "Malformed Git status output: missing NUL terminator");
        }

        const std::string_view record = output.substr(cursor, record_end - cursor);
        cursor = record_end + 1;
        if (record.empty()) {
            continue;
        }
        if (record.size() < 4 || record[2] != ' ') {
            return make_error(ErrorCategory::InvalidArgument,
                              "Malformed Git status output: invalid record header");
        }

        const VersionStatus status = parse_status_code(record[0], record[1]);
        fs::path path{std::string{record.substr(3)}};
        if (path.empty()) {
            continue;
        }
        path = path.lexically_normal();

        // Rename/copy records have an extra NUL-delimited field for the old path
        if (record[0] == 'R' || record[0] == 'C') {
            const std::size_t old_path_end = output.find('\0', cursor);
            if (old_path_end == std::string_view::npos) {
                return make_error(ErrorCategory::InvalidArgument,
                                  "Malformed Git status output: missing rename source path");
            }
            cursor = old_path_end + 1;
        }

        entries.push_back(VersionStatusEntry{
            .path = std::move(path),
            .status = status,
            .staged = is_staged_status(record[0]),
            .unstaged = is_unstaged_status(record[1]) || status == VersionStatus::Conflicted,
            .untracked = status == VersionStatus::Untracked,
        });
    }

    return entries;
}

/**
 * @brief Loads Git status for the direct children of a directory.
 *
 * Dispatches to the fastest available backend at compile time. Both backends
 * produce identical `VersionStatusSnapshot` results.
 *
 * @param directory The directory to query.
 * @return A populated snapshot, or an error if Git is unavailable or the
 * operation fails.
 */
Result<VersionStatusSnapshot> load_git_status(const fs::path& directory) {
#if EXPP_HAS_LIBGIT2
    return load_git_status_libgit2(directory);
#else
    return load_git_status_cli(directory);
#endif
}

/**
 * @brief Merges two statuses, retaining the one with higher priority.
 *
 * @see status_priority() for the priority ordering used.
 */
VersionStatus merge_status(VersionStatus current, VersionStatus incoming) noexcept {
    return status_priority(incoming) > status_priority(current) ? incoming : current;
}

/**
 * @brief Returns a compact single-character marker for display in the UI.
 *
 * Markers:
 *   - `[M]` Modified, `[A]` Added, `[D]` Deleted, `[R]` Renamed
 *   - `[C]` Copied, `[?]` Untracked, `[I]` Ignored, `[!]` Conflicted
 *   - Empty string `""` for Clean.
 */
std::string_view status_marker(VersionStatus status) noexcept {
    switch (status) {
        case VersionStatus::Modified:
            return "[M]";
        case VersionStatus::Added:
            return "[A]";
        case VersionStatus::Deleted:
            return "[D]";
        case VersionStatus::Renamed:
            return "[R]";
        case VersionStatus::Copied:
            return "[C]";
        case VersionStatus::Untracked:
            return "[?]";
        case VersionStatus::Ignored:
            return "[I]";
        case VersionStatus::Conflicted:
            return "[!]";
        case VersionStatus::Clean:
        default:
            return "";
    }
}

/**
 * @brief Returns whether the entry should be hidden by default in the
 * explorer when "show ignored" is toggled off.
 */
bool is_ignored(VersionStatus status) noexcept {
    return status == VersionStatus::Ignored;
}

}  // namespace expp::core
