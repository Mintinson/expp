/**
 * Git commands options that this file has used:
 * - `-C <directory>` jump to the given directory before executing the git command
 * - `rev-parse --show-toplevel` print the absolute path of the repository root
 * - `status` check the working tree status
 * - `--porcelain=v1` machine-readable output format with fixed-width status codes(without colorized or other
 * decorations)
 *      - '-z' use NUL character as the entry delimiter instead of newline, allowing for unambiguous parsing of file
 * path
 * - `--untracked-files=all` include all(recursively) untracked files in the status output
 * - `--ignored=matching  -- {<pathspec>}` include ignored files in the output if they match the pathspec
 */

#include "expp/core/version_control.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
 * @brief Removes trailing newline and carriage return characters from the end of a string.
 * @param text The input string to be trimmed.
 * @return A new string without the trailing newlines or carriage returns.
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
 * @brief Escapes and surrounds a file path in quotes for safe execution in shell commands.
 * @param path The filesystem path to be quoted.
 * @return A string containing the quoted and escaped path.
 */
[[nodiscard]] std::string quote_for_shell(const fs::path& path) {
    // std::string value = path.string();
    // std::string quoted;
    // quoted.reserve(value.size() + 2);
    // quoted.push_back('"');
    // for (char ch : value) {
    //     if (ch == '"') {
    //         quoted += "\\\"";
    //     } else {
    //         quoted.push_back(ch);
    //     }
    // }
    using namespace std::string_view_literals;
    auto quoted =
        path.string() | std::views::split('"') | std::views::join_with("\\\""sv) | std::ranges::to<std::string>();
    return std::format("\"{}\"", quoted);
}

/**
 * @brief Executes a shell command and reads its combined standard output and standard error.
 * @param command The shell command to execute.
 * @return A Result containing the command's output string on success, or an error if the command failed to execute.
 */
[[nodiscard]] Result<std::string> read_command_output(const std::string& command) {
    // Redirect stderr into stdout so the caller can surface git diagnostics
    // instead of only receiving a generic failure message.
    const std::string merged_command = std::format("{} 2>&1", command);
    FILE* pipe = EXPP_POPEN(merged_command.c_str(), "rb");
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
                return make_error(ErrorCategory::IO, std::format("Failed to read command output: {}", command));
            }
        }
    }

    const int exit_code = EXPP_PCLOSE(pipe);
    if (exit_code != 0) {
        const auto trimmed = trim_trailing_newlines(output);
        return make_error(ErrorCategory::NoSupport, std::format("Git command failed (exit {}): {}", exit_code,
                                                                trimmed.empty() ? "no output" : trimmed));
    }
    return output;
}

[[nodiscard]] Result<fs::path> discover_repository_root(const fs::path& directory) {
    // stderr is now merged into stdout by read_command_output so git diagnostics
    // are surfaced to the caller.
    const std::string command = std::format("git -C {} rev-parse --show-toplevel", quote_for_shell(directory));
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
 * @brief Normalizes a given filesystem path to a standard string representation, used as a key in maps.
 * @param path The filesystem path to normalize.
 * @return A lexically normal string representation of the path.
 */
[[nodiscard]] std::string normalize_key(const fs::path& path) {
    return path.lexically_normal().string();
}

/**
 * @brief Checks if a path starts with the parent directory marker ("..").
 * @param path The filesystem path to check.
 * @return True if the first component of the path is "..", false otherwise.
 */
[[nodiscard]] bool path_starts_with_parent_marker(const fs::path& path) {
    auto it = path.begin();
    return it != path.end() && *it == "..";
}

/**
 * @brief Determines the direct child path relative to the directory for a given status path.
 *
 * Computes whether the given status path (relative to the repository root) falls within the expected directory.
 * If so, extracts the direct child entry in `directory` representing the status.
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
        case VersionStatus::Clean:
            return 0;
        default:
            return 0;
    }
}

[[nodiscard]] bool is_conflict_status(char index_status, char worktree_status) noexcept {
    return index_status == 'U' || worktree_status == 'U' || (index_status == 'A' && worktree_status == 'A') ||
           (index_status == 'D' && worktree_status == 'D');
}

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
    if (index_status == 'M' || worktree_status == 'M') {
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

}  // namespace

Result<std::vector<VersionStatusEntry>> parse_porcelain_status(std::string_view output) {
    std::vector<VersionStatusEntry> entries;
    std::size_t cursor = 0;

    while (cursor < output.size()) {
        const std::size_t record_end = output.find('\0', cursor);
        if (record_end == std::string_view::npos) {
            return make_error(ErrorCategory::InvalidArgument, "Malformed Git status output: missing NUL terminator");
        }

        const std::string_view record = output.substr(cursor, record_end - cursor);
        cursor = record_end + 1;
        if (record.empty()) {
            continue;
        }
        if (record.size() < 4 || record[2] != ' ') {
            return make_error(ErrorCategory::InvalidArgument, "Malformed Git status output: invalid record header");
        }

        const VersionStatus status = parse_status_code(record[0], record[1]);
        fs::path path{std::string{record.substr(3)}};
        if (path.empty()) {
            continue;
        }
        path = path.lexically_normal();

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
        });
    }

    return entries;
}

Result<VersionStatusSnapshot> load_git_status(const fs::path& directory) {
    auto root_result = discover_repository_root(directory);
    if (!root_result) {
        return std::unexpected(root_result.error());
    }

    const fs::path repository_root = root_result->lexically_normal();
    const fs::path normalized_directory = directory.lexically_normal();
    const fs::path relative_directory = normalized_directory.lexically_relative(repository_root);
    const fs::path pathspec = relative_directory.empty() ? fs::path{"."} : relative_directory;

    const std::string command =
        std::format("git -C {} status --porcelain=v1 -z --ignored=matching --untracked-files=all -- {}",
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
        auto child = direct_child_for_status(repository_root, normalized_directory, entry.path);
        if (!child.has_value()) {
            continue;
        }

        auto key = normalize_key(*child);
        if (auto it = snapshot.statusesByPath.find(key); it != snapshot.statusesByPath.end()) {
            it->second = merge_status(it->second, entry.status);
        } else {
            snapshot.statusesByPath.emplace(std::move(key), entry.status);
        }
    }

    return snapshot;
}

VersionStatus merge_status(VersionStatus current, VersionStatus incoming) noexcept {
    return status_priority(incoming) > status_priority(current) ? incoming : current;
}

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
            return "   ";
    }
}

bool is_ignored(VersionStatus status) noexcept {
    return status == VersionStatus::Ignored;
}

}  // namespace expp::core
