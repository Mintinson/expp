
#include "expp/app/explorer.hpp"

#include "expp/core/config.hpp"
#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#undef max
#undef min
#endif

namespace expp::app {

namespace fs = std::filesystem;
namespace rng = std::ranges;

namespace {

[[nodiscard]] char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] int compare_lexicographic_insensitive(std::string_view lhs, std::string_view rhs) {
    const size_t common = std::min(lhs.size(), rhs.size());
    for (size_t i = 0; i < common; ++i) {
        const char lc = to_lower_ascii(lhs[i]);
        const char rc = to_lower_ascii(rhs[i]);
        if (lc < rc) {
            return -1;
        }
        if (lc > rc) {
            return 1;
        }
    }

    if (lhs.size() < rhs.size()) {
        return -1;
    }
    if (lhs.size() > rhs.size()) {
        return 1;
    }
    return 0;
}

[[nodiscard]] int compare_natural_insensitive(std::string_view lhs, std::string_view rhs) {
    size_t i = 0;
    size_t j = 0;

    while (i < lhs.size() && j < rhs.size()) {
        const bool lhs_digit = std::isdigit(static_cast<unsigned char>(lhs[i])) != 0;
        const bool rhs_digit = std::isdigit(static_cast<unsigned char>(rhs[j])) != 0;

        if (lhs_digit && rhs_digit) {
            size_t lhs_start = i;
            size_t rhs_start = j;

            while (lhs_start < lhs.size() && lhs[lhs_start] == '0') {
                ++lhs_start;
            }
            while (rhs_start < rhs.size() && rhs[rhs_start] == '0') {
                ++rhs_start;
            }

            size_t lhs_end = lhs_start;
            size_t rhs_end = rhs_start;
            while (lhs_end < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[lhs_end])) != 0) {
                ++lhs_end;
            }
            while (rhs_end < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[rhs_end])) != 0) {
                ++rhs_end;
            }

            const size_t lhs_len = lhs_end - lhs_start;
            const size_t rhs_len = rhs_end - rhs_start;
            if (lhs_len != rhs_len) {
                return lhs_len < rhs_len ? -1 : 1;
            }

            const int number_cmp = compare_lexicographic_insensitive(lhs.substr(lhs_start, lhs_len),
                                                                      rhs.substr(rhs_start, rhs_len));
            if (number_cmp != 0) {
                return number_cmp;
            }

            while (i < lhs.size() && std::isdigit(static_cast<unsigned char>(lhs[i])) != 0) {
                ++i;
            }
            while (j < rhs.size() && std::isdigit(static_cast<unsigned char>(rhs[j])) != 0) {
                ++j;
            }
            continue;
        }

        const char lc = to_lower_ascii(lhs[i]);
        const char rc = to_lower_ascii(rhs[j]);
        if (lc < rc) {
            return -1;
        }
        if (lc > rc) {
            return 1;
        }

        ++i;
        ++j;
    }

    if (i < lhs.size()) {
        return 1;
    }
    if (j < rhs.size()) {
        return -1;
    }
    return 0;
}

template <typename T>
[[nodiscard]] int compare_value(const T& lhs, const T& rhs) {
    if (lhs < rhs) {
        return -1;
    }
    if (rhs < lhs) {
        return 1;
    }
    return 0;
}

// TODO: move this to a utils file
/**
 * @brief Converts a filesystem path to a UTF-8 string
 * @param path The filesystem path to convert
 * @return a UTF-8 encoded string representation of the path
 */
[[nodiscard]] std::string to_utf8_string(const fs::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};  // NOLINT
}

// TODO: Is this should be part of this file?
/**
 * @brief Copies text to the system clipboard
 * @param text The text to copy
 * @return A result indicating success or failure
 */
[[nodiscard]] core::VoidResult copy_text_to_sys_clipboard(const std::string& text) {
#ifdef _WIN32
    const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wide_length <= 0) {
        return core::make_error(core::ErrorCategory::System, "Failed to prepare clipboard text");
    }

    std::wstring wide_text(static_cast<size_t>(wide_length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide_text.data(), wide_length) == 0) {
        return core::make_error(core::ErrorCategory::System, "Failed to convert clipboard text");
    }

    if (OpenClipboard(nullptr) == 0) {
        return core::make_error(core::ErrorCategory::System, "Failed to open system clipboard");
    }

    if (EmptyClipboard() == 0) {
        CloseClipboard();
        return core::make_error(core::ErrorCategory::System, "Failed to clear system clipboard");
    }

    const SIZE_T byte_count = static_cast<SIZE_T>(wide_text.size() * sizeof(wchar_t));
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (memory == nullptr) {
        CloseClipboard();
        return core::make_error(core::ErrorCategory::System, "Failed to allocate clipboard memory");
    }

    void* destination = GlobalLock(memory);
    if (destination == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return core::make_error(core::ErrorCategory::System, "Failed to lock clipboard memory");
    }

    std::memcpy(destination, wide_text.data(), byte_count);
    GlobalUnlock(memory);

    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        return core::make_error(core::ErrorCategory::System, "Failed to set clipboard data");
    }

    CloseClipboard();
    return {};
#elif defined(__APPLE__)
    FILE* pipe = popen("pbcopy", "w");
    if (pipe == nullptr) {
        return core::make_error(core::ErrorCategory::System, "Failed to access system clipboard");
    }

    const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
    const int close_result = pclose(pipe);
    if (written != text.size() || close_result != 0) {
        return core::make_error(core::ErrorCategory::System, "Failed to write to system clipboard");
    }

    return {};
#else
    constexpr std::array<std::string_view, 3> kCommands = {"wl-copy", "xclip -selection clipboard",
                                                           "xsel --clipboard --input"};

    for (std::string_view command : kCommands) {
        FILE* pipe = popen(std::string{command}.c_str(), "w");
        if (pipe == nullptr) {
            continue;
        }

        const size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
        const int close_result = pclose(pipe);
        if (written == text.size() && close_result == 0) {
            return {};
        }
    }

    return core::make_error(core::ErrorCategory::System,
                            "Failed to write to system clipboard (install wl-copy, xclip, or xsel)");
#endif
}
}  // namespace

struct Explorer::Impl {
    explicit Impl(fs::path start_path) : showHidden(core::global_config().config().behavior.showHiddenFiles) {
        state.currentDir = std::move(start_path);
        baseDirectory = core::filesystem::normalize(state.currentDir);
        
    }

    ExplorerState state;
    RefreshCallback refreshCallback;
    bool showHidden = true;
    fs::path baseDirectory;

    void clearVisualSelection() {
        state.visualModeActive = false;
        state.visualAnchor = -1;
        state.visualSelectedIndices.clear();
    }

    void updateVisualSelectionFromCursor() {
        if (!state.visualModeActive || state.entries.empty()) {
            state.visualSelectedIndices.clear();
            return;
        }

        state.visualAnchor = std::clamp(state.visualAnchor, 0, static_cast<int>(state.entries.size()) - 1);
        const int left = std::min(state.visualAnchor, state.currentSelected);
        const int right = std::max(state.visualAnchor, state.currentSelected);

        state.visualSelectedIndices.clear();
        const int selected_count = std::max(0, right - left + 1);
        state.visualSelectedIndices.reserve(static_cast<size_t>(selected_count));
        for (int i = left; i <= right; ++i) {
            state.visualSelectedIndices.push_back(i);
        }
    }

    // Return the paths of the currently selected entries (can be multiple if visual mode is active)
    [[nodiscard]] std::vector<fs::path> selectedEntryPaths() const {
        std::vector<fs::path> selected_paths;

        if (state.entries.empty()) {
            return selected_paths;
        }

        if (!state.visualModeActive || state.visualSelectedIndices.empty()) {
            selected_paths.push_back(state.entries[static_cast<size_t>(state.currentSelected)].path);
            return selected_paths;
        }

        selected_paths.reserve(state.visualSelectedIndices.size());
        for (int index : state.visualSelectedIndices) {
            if (index >= 0 && index < static_cast<int>(state.entries.size())) {
                selected_paths.push_back(state.entries[static_cast<size_t>(index)].path);
            }
        }

        return selected_paths;
    }

    void updateScrollForSelection() {
        const int entry_count = static_cast<int>(state.entries.size());
        if (entry_count <= 0) {
            state.currentSelected = 0;
            state.currentScrollOffset = 0;
            return;
        }

        state.currentSelected = std::clamp(state.currentSelected, 0, entry_count - 1);

        const int viewport_rows = std::max(1, state.currentViewportRows);
        const int max_offset = std::max(0, entry_count - viewport_rows);
        state.currentScrollOffset = std::clamp(state.currentScrollOffset, 0, max_offset);

        const int top_anchor = std::max(0, viewport_rows / 4);
        const int bottom_anchor = std::max(0, (viewport_rows * 3) / 4);

        const int top_threshold = state.currentScrollOffset + top_anchor;
        const int bottom_threshold = state.currentScrollOffset + bottom_anchor;

        if (state.currentSelected < top_threshold) {
            state.currentScrollOffset = std::max(0, state.currentSelected - top_anchor);
        } else if (state.currentSelected > bottom_threshold) {
            state.currentScrollOffset = std::min(max_offset, std::max(0, state.currentSelected - bottom_anchor));
        }

        state.currentScrollOffset = std::min(state.currentScrollOffset, state.currentSelected);

        const int visible_last = state.currentScrollOffset + viewport_rows - 1;
        if (state.currentSelected > visible_last) {
            state.currentScrollOffset = state.currentSelected - viewport_rows + 1;
        }

        state.currentScrollOffset = std::clamp(state.currentScrollOffset, 0, max_offset);
    }

    void setViewportRows(int rows) {
        state.currentViewportRows = std::max(1, rows);
        updateScrollForSelection();
    }

    [[nodiscard]] core::VoidResult refresh() {
        auto result = core::filesystem::list_directory(state.currentDir, showHidden);
        if (!result) {
            return std::unexpected(result.error());
        }

        auto entries = std::move(*result);
        sortEntries(entries);

        // Get Parent Directory Contents
        std::vector<core::filesystem::FileEntry> parent_entries;
        if (state.currentDir.has_parent_path() && state.currentDir.parent_path() != state.currentDir) {
            auto parent_result = core::filesystem::list_directory(state.currentDir.parent_path(), showHidden);
            if (parent_result) {
                parent_entries = std::move(*parent_result);
                sortEntries(parent_entries);
            }
        }

        state.entries = std::move(entries);
        state.parentEntries = std::move(parent_entries);

        // clamp selection and preserve scrolling invariants
        updateScrollForSelection();

        if (state.visualModeActive) {
            updateVisualSelectionFromCursor();
        }

        // Update parent selection
        updateParentSelection();

        // clear seach state when directory changes
        state.searchMatches.clear();
        state.searchHighlightActive = false;
        state.searchPattern.clear();
        state.currentMatchIndex = -1;

        if (refreshCallback) {
            refreshCallback();
        }
        return {};
    }

    void setSortOrder(ExplorerState::SortField field, ExplorerState::SortDirection direction) {
        if (state.sortField == field && state.sortDirection == direction) {
            return;
        }

        // once change sort order, clear visual selection to avoid confusion (e.g. selected entries may no longer be adjacent)
        clearVisualSelection();

        fs::path selected_path;
        if (!state.entries.empty()) {
            selected_path = state.entries[static_cast<size_t>(state.currentSelected)].path;
        }

        state.sortField = field;
        state.sortDirection = direction;

        sortEntries(state.entries);
        sortEntries(state.parentEntries);

        if (!selected_path.empty()) {
            auto it = rng::find_if(state.entries,
                                   [&selected_path](const core::filesystem::FileEntry& entry) {
                                       return entry.path == selected_path;
                                   });
            if (it != state.entries.end()) {
                state.currentSelected = static_cast<int>(rng::distance(state.entries.begin(), it));
            }
        }

        updateScrollForSelection();
        updateParentSelection();

        if (state.searchHighlightActive && !state.searchPattern.empty()) {
            updateSearchMatches();

            auto current_match_it = rng::find(state.searchMatches, state.currentSelected);
            if (current_match_it != state.searchMatches.end()) {
                state.currentMatchIndex = static_cast<int>(rng::distance(state.searchMatches.begin(), current_match_it));
            }
        }

        if (refreshCallback) {
            refreshCallback();
        }
    }

    void updateParentSelection() {
        if (state.currentDir.has_parent_path()) {
            state.parentSelected =
                static_cast<int>(rng::distance(rng::find_if(state.parentEntries,
                                                            [this](const core::filesystem::FileEntry& entry) {
                                                                return entry.path == state.currentDir;
                                                            }),
                                               rng::begin(state.parentEntries)));
        }
    }

    core::VoidResult navigateTo(const fs::path& path) {
        std::error_code directory_ec;
        if (!fs::is_directory(path, directory_ec)) {
            if (directory_ec) {
                return core::make_error(core::ErrorCategory::FileSystem,
                                        std::format("Cannot access directory '{}': {}", path.string(),
                                                    directory_ec.message()));
            }
            return core::make_error(core::ErrorCategory::FileSystem, std::format("Not a directory: {}", path.string()));
        }
        auto canonical_result = core::filesystem::canonicalize(path);
        if (!canonical_result) {
            return std::unexpected(canonical_result.error());
        }

        const fs::path previous_dir = state.currentDir;
        const int previous_selected = state.currentSelected;
        const int previous_offset = state.currentScrollOffset;
        const bool previous_visual = state.visualModeActive;
        const int previous_anchor = state.visualAnchor;
        const auto previous_visual_indices = state.visualSelectedIndices;

        state.currentDir = *canonical_result;
        state.currentSelected = 0;
        state.currentScrollOffset = 0;
        clearVisualSelection();

        auto refresh_result = refresh();
        if (!refresh_result) {
            state.currentDir = previous_dir;
            state.currentSelected = previous_selected;
            state.currentScrollOffset = previous_offset;
            state.visualModeActive = previous_visual;
            state.visualAnchor = previous_anchor;
            state.visualSelectedIndices = previous_visual_indices;
            return refresh_result;
        }
        return {};
    }

    core::VoidResult goParent() {
        if (!state.currentDir.has_parent_path() || state.currentDir.parent_path() == state.currentDir) {
            return {};  // Already at root
        }

        auto old_path = state.currentDir.filename();
        const fs::path previous_dir = state.currentDir;
        const int previous_selected = state.currentSelected;
        const int previous_offset = state.currentScrollOffset;
        const bool previous_visual = state.visualModeActive;
        const int previous_anchor = state.visualAnchor;
        const auto previous_visual_indices = state.visualSelectedIndices;

        state.currentDir = state.currentDir.parent_path();
        state.currentSelected = 0;
        state.currentScrollOffset = 0;
        clearVisualSelection();

        auto refresh_result = refresh();
        if (!refresh_result) {
            state.currentDir = previous_dir;
            state.currentSelected = previous_selected;
            state.currentScrollOffset = previous_offset;
            state.visualModeActive = previous_visual;
            state.visualAnchor = previous_anchor;
            state.visualSelectedIndices = previous_visual_indices;
            return refresh_result;
        }

        // Try to select the directory we came from
        for (size_t i = 0; i < state.entries.size(); ++i) {
            if (state.entries[i].path.filename() == old_path) {
                state.currentSelected = static_cast<int>(i);
                break;
            }
        }
        updateScrollForSelection();

        return {};
    }

    core::VoidResult enterSelected(bool open_file) {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<size_t>(state.currentSelected)];

        if (entry.isDirectory()) {
            return navigateTo(entry.path);
        }
        if (open_file) {
            auto result = core::filesystem::open_with_default(entry.path);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        return {};
    }

    core::VoidResult create(const std::string& name) {
        if (name.empty()) {
            return {};
        }

        fs::path new_path = state.currentDir / name;

        // Check if path ends with '/' or '\' - create directory
        if (name.back() == '/' || name.back() == '\\') {
            auto result = core::filesystem::create_directory(new_path);
            if (!result) {
                return std::unexpected(result.error());
            }
        } else {
            auto result = core::filesystem::create_file(new_path);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        return refresh();
    }

    core::VoidResult rename(const std::string& new_name) {
        if (state.entries.empty() || new_name.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<size_t>(state.currentSelected)];
        fs::path new_path = state.currentDir / new_name;

        // Use core::fs::rename
        auto result = core::filesystem::rename(entry.path, new_path);
        if (!result) {
            return result;
        }
        return refresh();
    }

    core::VoidResult deleteSelected() {
        auto targets = selectedEntryPaths();
        if (targets.empty()) {
            return {};
        }

        for (const auto& target : targets) {
            core::VoidResult result;
            if (fs::is_directory(target)) {
                result = core::filesystem::remove_directory(target);
            } else {
                result = core::filesystem::remove_file(target);
            }

            if (!result) {
                return result;
            }
        }

        clearVisualSelection();
        return refresh();
    }

    core::VoidResult trashSelected() {
        auto targets = selectedEntryPaths();
        if (targets.empty()) {
            return {};
        }

        for (const auto& target : targets) {
            auto result = core::filesystem::move_to_trash(target);
            if (!result) {
                return result;
            }
        }

        clearVisualSelection();
        return refresh();
    }

    core::VoidResult openSelected() {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<size_t>(state.currentSelected)];
        if (!entry.isDirectory()) {
            // Use core::filesystem::open_with_default
            return core::filesystem::open_with_default(entry.path);
        }
        return {};
    }

    core::VoidResult yankSelected() {
        return setClipboardFromSelection(ExplorerState::ClipboardOperation::Copy);
    }

    core::VoidResult cutSelected() {
        return setClipboardFromSelection(ExplorerState::ClipboardOperation::Cut);
    }

    core::VoidResult discardYank() {
        if (state.clipboardOperation == ExplorerState::ClipboardOperation::None || state.clipboardPath.empty()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Clipboard is empty");
        }

        clearClipboard();
        return {};
    }

    core::VoidResult pasteYanked(bool overwrite) {
        if (state.clipboardOperation == ExplorerState::ClipboardOperation::None || state.clipboardPaths.empty()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Clipboard is empty");
        }

        for (const auto& source : state.clipboardPaths) {
            if (!fs::exists(source)) {
                clearClipboard();
                return core::make_error(core::ErrorCategory::FileSystem, "Clipboard source does not exist");
            }

            const fs::path destination = state.currentDir / source.filename();

            std::error_code ec;
            if (fs::exists(destination, ec)) {
                if (!overwrite) {
                    return core::make_error(core::ErrorCategory::FileSystem,
                                            std::format("Destination already exists: {}", destination.string()));
                }

                if (fs::is_directory(destination, ec)) {
                    core::VoidResult remove_result = core::filesystem::remove_directory(destination);
                    if (!remove_result) {
                        return remove_result;
                    }
                } else {
                    core::VoidResult remove_result = core::filesystem::remove_file(destination);
                    if (!remove_result) {
                        return remove_result;
                    }
                }
            }

            std::error_code equivalent_ec;
            if (fs::equivalent(source, destination, equivalent_ec)) {
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        "Source and destination resolve to the same path");
            }

            if (state.clipboardOperation == ExplorerState::ClipboardOperation::Copy &&
                isSourceParentOfDestination(source, destination)) {
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        "Cannot copy a directory into itself or its subdirectory");
            }

            core::VoidResult op_result;
            if (state.clipboardOperation == ExplorerState::ClipboardOperation::Copy) {
                op_result = copyClipboardSourceTo(source, destination, overwrite);
            } else {
                op_result = moveClipboardSourceTo(source, destination, overwrite);
            }

            if (!op_result) {
                return op_result;
            }
        }

        if (state.clipboardOperation == ExplorerState::ClipboardOperation::Cut) {
            clearClipboard();
        }

        clearVisualSelection();
        return refresh();
    }

    [[nodiscard]] core::VoidResult copySelectedPathToSystemClipboard(bool absolute) const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path selected = state.entries[static_cast<size_t>(state.currentSelected)].path;
        return copy_text_to_sys_clipboard(formatPathForClipboard(selected, absolute));
    }

    [[nodiscard]] core::VoidResult copyCurrentDirectoryPathToSystemClipboard(bool absolute) const {
        return copy_text_to_sys_clipboard(formatPathForClipboard(state.currentDir, absolute));
    }

    [[nodiscard]] core::VoidResult copySelectedFileNameToSystemClipboard() const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path name = state.entries[static_cast<size_t>(state.currentSelected)].path.filename();
        if (name.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument, "Selected entry has no file name");
        }

        return copy_text_to_sys_clipboard(to_utf8_string(name));
    }

    [[nodiscard]] core::VoidResult copySelectedNameWithoutExtensionToSystemClipboard() const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path stem = state.entries[static_cast<size_t>(state.currentSelected)].path.stem();
        if (stem.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    "Selected entry has no name without extension");
        }

        return copy_text_to_sys_clipboard(to_utf8_string(stem));
    }

    core::VoidResult setClipboardFromSelection(ExplorerState::ClipboardOperation operation) {
        auto selected_paths = selectedEntryPaths();
        if (selected_paths.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        state.clipboardPaths = std::move(selected_paths);
        state.clipboardPath = state.clipboardPaths.front();
        state.clipboardOperation = operation;
        clearVisualSelection();
        return {};
    }

    void clearClipboard() {
        state.clipboardPath.clear();
        state.clipboardPaths.clear();
        state.clipboardOperation = ExplorerState::ClipboardOperation::None;
    }

    [[nodiscard]] static bool isSourceParentOfDestination(const fs::path& source, const fs::path& destination) {
        std::error_code source_ec;
        std::error_code destination_ec;

        const fs::path source_normalized = fs::weakly_canonical(source, source_ec).lexically_normal();
        const fs::path destination_normalized = fs::weakly_canonical(destination, destination_ec).lexically_normal();

        if (source_ec || destination_ec || source_normalized.empty() || destination_normalized.empty()) {
            return false;
        }

        auto source_it = source_normalized.begin();
        auto destination_it = destination_normalized.begin();

        for (; source_it != source_normalized.end(); ++source_it, ++destination_it) {
            if (destination_it == destination_normalized.end() || *source_it != *destination_it) {
                return false;
            }
        }

        return destination_it != destination_normalized.end();
    }

    [[nodiscard]] std::string formatPathForClipboard(const fs::path& path, bool absolute) const {
        const fs::path normalized = core::filesystem::normalize(path);

        if (absolute) {
            return to_utf8_string(normalized);
        }

        const fs::path normalized_base = core::filesystem::normalize(baseDirectory);
        fs::path relative = normalized.lexically_relative(normalized_base);
        relative.make_preferred();

        if (!relative.empty()) {
            return to_utf8_string(relative);
        }

        if (normalized == normalized_base) {
            return ".";
        }

        return to_utf8_string(normalized);
    }

    [[nodiscard]] core::VoidResult copyClipboardSourceTo(const fs::path& source,
                                                         const fs::path& destination,
                                                         bool overwrite) const {
        const bool is_directory = fs::is_directory(source);

        std::error_code copy_ec;
        fs::copy_options options = fs::copy_options::copy_symlinks;
        if (is_directory) {
            options |= fs::copy_options::recursive;
        }
        options |= overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::skip_existing;

        fs::copy(source, destination, options, copy_ec);
        if (copy_ec) {
            return core::make_error(core::ErrorCategory::FileSystem,
                                    std::format("Failed to copy to '{}': {}", destination.string(),
                                                copy_ec.message()));
        }

        return {};
    }

    [[nodiscard]] core::VoidResult moveClipboardSourceTo(const fs::path& source,
                                                         const fs::path& destination,
                                                         bool overwrite) const {
        auto rename_result = core::filesystem::rename(source, destination);
        if (rename_result) {
            return {};
        }

        auto copy_result = copyClipboardSourceTo(source, destination, overwrite);
        if (!copy_result) {
            return copy_result;
        }

        if (fs::is_directory(source)) {
            return core::filesystem::remove_directory(source);
        }

        return core::filesystem::remove_file(source);
    }

    void sortEntries(std::vector<core::filesystem::FileEntry>& entries) const {
        rng::stable_sort(entries, [this](const auto& lhs, const auto& rhs) {
        //std::stable_sort(entries.begin(), entries.end(), [this](const auto& lhs, const auto& rhs) {
            if (lhs.isDirectory() != rhs.isDirectory()) {
                return lhs.isDirectory();
            }

            const int field_cmp = compareBySortField(lhs, rhs);
            if (field_cmp != 0) {
                return state.sortDirection == ExplorerState::SortDirection::Ascending ? field_cmp < 0 : field_cmp > 0;
            }

            const int natural_cmp = compare_natural_insensitive(lhs.filename(), rhs.filename());
            if (natural_cmp != 0) {
                return natural_cmp < 0;
            }

            return lhs.path.generic_u8string() < rhs.path.generic_u8string();
        });
    }

    [[nodiscard]] int compareBySortField(const core::filesystem::FileEntry& lhs,
                                         const core::filesystem::FileEntry& rhs) const {
        switch (state.sortField) {
            case ExplorerState::SortField::ModifiedTime:
                return compare_value(lhs.lastModified, rhs.lastModified);
            case ExplorerState::SortField::BirthTime:
                return compare_value(lhs.birthTime, rhs.birthTime);
            case ExplorerState::SortField::Extension: {
                const int extension_cmp =
                    compare_lexicographic_insensitive(lhs.extension(), rhs.extension());
                if (extension_cmp != 0) {
                    return extension_cmp;
                }
                return compare_lexicographic_insensitive(lhs.filename(), rhs.filename());
            }
            case ExplorerState::SortField::Alphabetical:
                return compare_lexicographic_insensitive(lhs.filename(), rhs.filename());
            case ExplorerState::SortField::Natural:
                return compare_natural_insensitive(lhs.filename(), rhs.filename());
            case ExplorerState::SortField::Size:
                return compare_value(lhs.size, rhs.size);
            default:
                return 0;
        }
    }

    void search(const std::string& pattern) {
        state.searchPattern = pattern;
        state.searchHighlightActive = !pattern.empty();
        updateSearchMatches();

        // Jump to nearest match
        if (!state.searchMatches.empty()) {
            jumpToNextMatch();
        }
    }

    void updateSearchMatches() {
        state.searchMatches.clear();
        state.currentMatchIndex = -1;

        if (state.searchPattern.empty()) {
            return;
        }

        for (size_t i = 0; i < state.entries.size(); ++i) {
            const auto& entry = state.entries[i];
            const std::string filename = entry.filename();
            if (filename.contains(state.searchPattern)) {
                state.searchMatches.push_back(static_cast<int>(i));
            }
        }
    }

    void jumpToNextMatch() {
        if (state.searchMatches.empty()) {
            return;
        }

        // Find the next match after current_selected
        for (size_t i = 0; i < state.searchMatches.size(); ++i) {
            if (state.searchMatches[i] > state.currentSelected) {
                state.currentMatchIndex = static_cast<int>(i);
                state.currentSelected = state.searchMatches[i];
                updateScrollForSelection();
                return;
            }
        }
        // Wrap around to the first match
        state.currentMatchIndex = 0;
        state.currentSelected = state.searchMatches[0];
        updateScrollForSelection();
    }

    void jumpToPrevMatch() {
        if (state.searchMatches.empty()) {
            return;
        }

        // Find the previous match before current_selected
        for (int i = static_cast<int>(state.searchMatches.size()) - 1; i >= 0; --i) {
            if (state.searchMatches[static_cast<size_t>(i)] < state.currentSelected) {
                state.currentMatchIndex = i;
                state.currentSelected = state.searchMatches[static_cast<size_t>(i)];
                updateScrollForSelection();
                return;
            }
        }
        // Wrap around to the last match
        state.currentMatchIndex = static_cast<int>(state.searchMatches.size()) - 1;
        state.currentSelected = state.searchMatches[static_cast<size_t>(state.currentMatchIndex)];
        updateScrollForSelection();
    }

};  // Explorer::Impl

Explorer::Explorer(std::filesystem::path start_path) : impl_(std::make_unique<Impl>(std::move(start_path))) {}

core::Result<std::shared_ptr<Explorer>> Explorer::create(std::filesystem::path start_path) {
    auto explorer = std::shared_ptr<Explorer>(new Explorer(std::move(start_path)));
    auto refresh_result = explorer->refresh();
    if (!refresh_result) {
        return std::unexpected(refresh_result.error());
    }
    return explorer;
}

Explorer::~Explorer() = default;
Explorer::Explorer(Explorer&&) noexcept = default;
Explorer& Explorer::operator=(Explorer&&) noexcept = default;

const ExplorerState& Explorer::state() const noexcept {
    return impl_->state;
}

core::VoidResult Explorer::navigateTo(const fs::path& path) {
    return impl_->navigateTo(path);
}
core::VoidResult Explorer::goParent() {
    return impl_->goParent();
}

core::VoidResult Explorer::enterSelected(bool open_file) {
    return impl_->enterSelected(open_file);
}

void Explorer::moveDown(int count) {
    if (!impl_->state.entries.empty()) {
        impl_->state.currentSelected =
            std::min(impl_->state.currentSelected + count, static_cast<int>(impl_->state.entries.size()) - 1);
        impl_->updateScrollForSelection();
        impl_->updateVisualSelectionFromCursor();
    }
}

void Explorer::moveUp(int count) {
    impl_->state.currentSelected = std::max(impl_->state.currentSelected - count, 0);
    impl_->updateScrollForSelection();
    impl_->updateVisualSelectionFromCursor();
}

void Explorer::goToTop() {
    impl_->state.currentSelected = 0;
    impl_->updateScrollForSelection();
    impl_->updateVisualSelectionFromCursor();
}

void Explorer::goToBottom() {
    if (!impl_->state.entries.empty()) {
        impl_->state.currentSelected = static_cast<int>(impl_->state.entries.size()) - 1;
        impl_->updateScrollForSelection();
        impl_->updateVisualSelectionFromCursor();
    }
}

void Explorer::goToLine(int line) {
    if (!impl_->state.entries.empty()) {
        impl_->state.currentSelected = std::min(line - 1,  // 1-indexed to 0-indexed
                                                static_cast<int>(impl_->state.entries.size()) - 1);
        impl_->state.currentSelected = std::max(impl_->state.currentSelected, 0);
        impl_->updateScrollForSelection();
        impl_->updateVisualSelectionFromCursor();
    }
}

void Explorer::setViewportRows(int rows) {
    impl_->setViewportRows(rows);
}

void Explorer::setSortOrder(ExplorerState::SortField field, ExplorerState::SortDirection direction) {
    impl_->setSortOrder(field, direction);
}

core::VoidResult Explorer::create(const std::string& name) {
    return impl_->create(name);
}

core::VoidResult Explorer::rename(const std::string& new_name) {
    return impl_->rename(new_name);
}

core::VoidResult Explorer::deleteSelected() {
    return impl_->deleteSelected();
}

core::VoidResult Explorer::trashSelected() {
    return impl_->trashSelected();
}

core::VoidResult Explorer::openSelected() {
    return impl_->openSelected();
}

core::VoidResult Explorer::yankSelected() {
    return impl_->yankSelected();
}
core::VoidResult Explorer::cutSelected() {
    return impl_->cutSelected();
}
core::VoidResult Explorer::discardYank() {
    return impl_->discardYank();
}
core::VoidResult Explorer::pasteYanked(bool overwrite) {
    return impl_->pasteYanked(overwrite);
}

core::VoidResult Explorer::copySelectedPathToSystemClipboard(bool absolute) {
    return impl_->copySelectedPathToSystemClipboard(absolute);
}

core::VoidResult Explorer::copyCurrentDirectoryPathToSystemClipboard(bool absolute) {
    return impl_->copyCurrentDirectoryPathToSystemClipboard(absolute);
}

core::VoidResult Explorer::copySelectedFileNameToSystemClipboard() {
    return impl_->copySelectedFileNameToSystemClipboard();
}

core::VoidResult Explorer::copySelectedNameWithoutExtensionToSystemClipboard() {
    return impl_->copySelectedNameWithoutExtensionToSystemClipboard();
}

void Explorer::enterVisualMode() {
    if (impl_->state.entries.empty()) {
        return;
    }

    impl_->state.visualModeActive = true;
    impl_->state.visualAnchor = impl_->state.currentSelected;
    impl_->updateVisualSelectionFromCursor();
}

void Explorer::exitVisualMode() {
    impl_->clearVisualSelection();
}

int Explorer::visualSelectionCount() const noexcept {
    if (!impl_->state.visualModeActive) {
        return 0;
    }

    return static_cast<int>(impl_->state.visualSelectedIndices.size());
}

void Explorer::search(const std::string& pattern) {
    impl_->search(pattern);
}

void Explorer::nextMatch() {
    if (impl_->state.searchHighlightActive && !impl_->state.searchMatches.empty()) {
        impl_->jumpToNextMatch();
    }
}

void Explorer::prevMatch() {
    if (impl_->state.searchHighlightActive && !impl_->state.searchMatches.empty()) {
        impl_->jumpToPrevMatch();
    }
}

void Explorer::clearSearch() {
    impl_->state.searchHighlightActive = false;
    impl_->state.searchPattern.clear();
    impl_->state.searchMatches.clear();
    impl_->state.currentMatchIndex = -1;
}

void Explorer::showDeleteDialog() {
    if (!impl_->state.entries.empty()) {
        impl_->state.trashDeletePath = impl_->state.entries[static_cast<size_t>(impl_->state.currentSelected)].path;
        impl_->state.showDeleteDialog = true;
    }
}

void Explorer::showTrashDialog() {
    if (!impl_->state.entries.empty()) {
        impl_->state.trashDeletePath = impl_->state.entries[static_cast<size_t>(impl_->state.currentSelected)].path;
        impl_->state.showTrashDialog = true;
    }
}

void Explorer::showCreateDialog() {
    impl_->state.showCreateDialog = true;
}

void Explorer::showRenameDialog() {
    impl_->state.showRenameDialog = true;
}

void Explorer::showSearchDialog() {
    impl_->state.showSearchDialog = true;
}

void Explorer::hideAllDialogs() {
    impl_->state.showDeleteDialog = false;
    impl_->state.showTrashDialog = false;
    impl_->state.showCreateDialog = false;
    impl_->state.showRenameDialog = false;
    impl_->state.showSearchDialog = false;
}

void Explorer::setInput(const std::string& input) {
    impl_->state.inputBuffer = input;
}

core::VoidResult Explorer::refresh() {
    return impl_->refresh();
}

void Explorer::onRefresh(RefreshCallback callback) {
    impl_->refreshCallback = std::move(callback);
}

core::VoidResult Explorer::toggleShowHidden() {
    const bool previous_show_hidden = impl_->showHidden;
    impl_->showHidden = !impl_->showHidden;
    auto result = refresh();
    if (!result) {
        impl_->showHidden = previous_show_hidden;
        return result;
    }
    return {};
}
}  // namespace expp::app
