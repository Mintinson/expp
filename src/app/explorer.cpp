#include "expp/app/explorer.hpp"

#include "expp/app/explorer_sort.hpp"
#include "expp/app/explorer_state_helpers.hpp"
#include "expp/core/config.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <ranges>
#include <system_error>
#include <utility>

namespace expp::app {

namespace fs = std::filesystem;
namespace rng = std::ranges;
namespace views = std::ranges::views;

namespace {

// Navigation operations update multiple correlated fields. Capturing them as a
// value object makes failure rollback deterministic and keeps error paths local.
struct NavigationSnapshot {
    fs::path currentDir;
    SelectionState selection;
    SearchState search;
};

[[nodiscard]] std::string to_utf8_string(const fs::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};  // NOLINT
}

}  // namespace

struct Explorer::Impl {
    explicit Impl(fs::path start_path, ExplorerServices service_bundle)
        : services(std::move(service_bundle))
        , showHidden(core::global_config().config().behavior.showHiddenFiles) {
        state.currentDir = std::move(start_path);
        baseDirectory = services.fileSystem->normalize(state.currentDir);
    }

    ExplorerServices services;
    ExplorerState state;
    RefreshCallback refreshCallback;
    bool showHidden{true};
    fs::path baseDirectory;

    template <typename T>
    [[nodiscard]] T blockOn(core::Task<T> task) const {
        return services.runtime->blockOn(std::move(task));
    }

    [[nodiscard]] NavigationSnapshot snapshotNavigationState() const {
        return NavigationSnapshot{
            .currentDir = state.currentDir,
            .selection = state.selection,
            .search = state.search,
        };
    }

    void restoreNavigationState(NavigationSnapshot snapshot) {
        state.currentDir = std::move(snapshot.currentDir);
        state.selection = std::move(snapshot.selection);
        state.search = std::move(snapshot.search);
    }

    void updateParentSelection() {
        if (state.currentDir.has_parent_path()) {
            // The parent panel mirrors the directory above `currentDir`, so its
            // selection should point at the entry representing the current node.
            state.selection.parentSelected = static_cast<int>(
                rng::distance(rng::begin(state.parentEntries),
                              rng::find_if(state.parentEntries, [this](const core::filesystem::FileEntry& entry) {
                                  return entry.path == state.currentDir;
                              })));
        }
    }

    void clearSearchState() {
        state.search.highlightActive = false;
        state.search.pattern.clear();
        state.search.matches.clear();
        state.search.currentMatchIndex = -1;
    }

    void clearSelectionForNewDirectory() {
        state.selection.currentSelected = 0;
        state.selection.currentScrollOffset = 0;
        clear_visual_selection(state.selection);
    }

    void notifyRefresh() const {
        if (refreshCallback) {
            refreshCallback();
        }
    }

    void beginDirectoryListing(fs::path directory, const std::uint64_t generation) {
        state.currentDir = std::move(directory);
        state.entries.clear();
        state.parentEntries.clear();
        clearSelectionForNewDirectory();
        clearSearchState();
        state.listing = DirectoryListingState{
            .loading = true,
            .scanInProgress = true,
            .loadedEntries = 0,
            .totalEntries = 0,
            .hasMore = true,
            .generation = generation,
        };
        notifyRefresh();
    }

    void appendDirectoryChunk(std::vector<core::filesystem::FileEntry> entries,
                              std::size_t /*loaded_entries*/,
                              std::size_t total_entries,
                              bool has_more,
                              const std::uint64_t generation) {
        if (state.listing.generation != generation) {
            return;
        }

        sort_entries(entries, state.sortOrder);

        if (state.entries.empty()) {
            state.entries = std::move(entries);
        } else {
            std::vector<core::filesystem::FileEntry> merged;
            merged.reserve(state.entries.size() + entries.size());
            std::ranges::merge(
                state.entries, entries, std::back_inserter(merged),
                [this](const auto& lhs, const auto& rhs) { return less_by_sort_order(lhs, rhs, state.sortOrder); });
            state.entries = std::move(merged);
        }

        state.listing.loadedEntries = state.entries.size();
        state.listing.totalEntries = std::max(total_entries, state.listing.loadedEntries);
        state.listing.hasMore = has_more;
        state.listing.loading = has_more;
        state.listing.scanInProgress = has_more;

        auto update_helper = selectionUpdateHelper();
        if (state.search.highlightActive && !state.search.pattern.empty()) {
            updateSearchMatches();
        }
        notifyRefresh();
    }

    void completeDirectoryListing(const std::uint64_t generation) {
        if (state.listing.generation != generation) {
            return;
        }

        state.listing.loading = false;
        state.listing.scanInProgress = false;
        state.listing.hasMore = false;
        state.listing.loadedEntries = state.entries.size();
        state.listing.totalEntries = std::max(state.listing.totalEntries, state.listing.loadedEntries);
        auto update_helper = selectionUpdateHelper();
        notifyRefresh();
    }

    void setParentEntries(std::vector<core::filesystem::FileEntry> entries) {
        sort_entries(entries, state.sortOrder);
        state.parentEntries = std::move(entries);
        updateParentSelection();
        notifyRefresh();
    }

    [[nodiscard]] core::VoidResult refresh() {
        auto entries_result = blockOn(services.fileSystem->listDirectory({
            .directory = state.currentDir,
            .includeHidden = showHidden,
            .cancellation = {},
        }));
        if (!entries_result) {
            return std::unexpected(entries_result.error());
        }

        auto& entries = entries_result->entries;
        // Sorting is centralized here so every refresh path preserves the same
        // ordering policy, including explicit navigation and file mutations.
        sort_entries(entries, state.sortOrder);

        std::vector<core::filesystem::FileEntry> parent_entries;
        if (state.currentDir.has_parent_path() && state.currentDir.parent_path() != state.currentDir) {
            auto parent_result = blockOn(services.fileSystem->listDirectory({
                .directory = state.currentDir.parent_path(),
                .includeHidden = showHidden,
                .cancellation = {},
            }));
            if (parent_result) {
                parent_entries = std::move(parent_result->entries);
                sort_entries(parent_entries, state.sortOrder);
            }
        }

        state.entries = std::move(entries);
        state.parentEntries = std::move(parent_entries);
        state.listing = DirectoryListingState{
            .loading = false,
            .scanInProgress = false,
            .loadedEntries = state.entries.size(),
            .totalEntries = state.entries.size(),
            .hasMore = false,
            .generation = state.listing.generation + 1,
        };

        // Selection, visual range state, parent focus, and search projection are
        // derived from the entry list and must be recomputed together.
        auto update_helper = selectionUpdateHelper();
        // update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
        // update_visual_selection(state.selection, state.entries);
        updateParentSelection();
        clearSearchState();

        notifyRefresh();
        return {};
    }

    SelectionUpdateHelper selectionUpdateHelper() {
        return {state.selection, state.entries, static_cast<int>(state.entries.size())};
    }

    void setSelectedIndexAndUpdate(int target_index) {
        auto update_helper = selectionUpdateHelper();
        state.selection.currentSelected = target_index;
    }

    void setViewportRows(int rows) {
        state.selection.currentViewportRows = std::max(1, rows);
        update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
    }

    void updateSearchMatches() {
        state.search.matches.clear();
        state.search.currentMatchIndex = -1;
        if (state.search.pattern.empty()) {
            return;
        }
#if __cpp_lib_ranges_enumerate >= 202302L
        state.search.matches =
            views::enumerate(state.entries) |
            views::filter([&pattern = state.search.pattern](const auto& indexed_entry) {
                return std::get<1>(indexed_entry).filename().contains(pattern);
            }) |
            views::transform([](const auto& indexed_entry) { return static_cast<int>(std::get<0>(indexed_entry)); })
            | rng::to<std::vector>();
#else
        for (std::size_t index = 0; index < state.entries.size(); ++index) {
            if (state.entries[index].filename().contains(state.search.pattern)) {
                state.search.matches.push_back(static_cast<int>(index));
            }
        }
#endif  // __cpp_lib_ranges_enumerate >= 202302L
    }

    void jumpToNextMatch() {
        if (state.search.matches.empty()) {
            return;
        }

        for (std::size_t index = 0; index < state.search.matches.size(); ++index) {
            if (state.search.matches[index] > state.selection.currentSelected) {
                auto update_helper = selectionUpdateHelper();
                state.search.currentMatchIndex = static_cast<int>(index);
                state.selection.currentSelected = state.search.matches[index];
                // update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
                // update_visual_selection(state.selection, state.entries);
                return;
            }
        }
        auto update_helper = selectionUpdateHelper();
        state.search.currentMatchIndex = 0;
        state.selection.currentSelected = state.search.matches[0];
        // update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
        // update_visual_selection(state.selection, state.entries);
    }

    void jumpToPrevMatch() {
        if (state.search.matches.empty()) {
            return;
        }

        for (int index = static_cast<int>(state.search.matches.size()) - 1; index >= 0; --index) {
            if (state.search.matches[static_cast<std::size_t>(index)] < state.selection.currentSelected) {
                state.search.currentMatchIndex = index;
                state.selection.currentSelected = state.search.matches[static_cast<std::size_t>(index)];
                auto update_helper = selectionUpdateHelper();
                return;
            }
        }

        state.search.currentMatchIndex = static_cast<int>(state.search.matches.size()) - 1;
        state.selection.currentSelected =
            state.search.matches[static_cast<std::size_t>(state.search.currentMatchIndex)];
        auto update_helper = selectionUpdateHelper();
    }

    void search(std::string pattern) {
        state.search.pattern = std::move(pattern);
        state.search.highlightActive = !state.search.pattern.empty();
        updateSearchMatches();
        if (!state.search.matches.empty()) {
            jumpToNextMatch();
        }
    }

    void setSortOrder(SortOrder::Field field, SortOrder::Direction direction) {
        // no need to reapply the same sort order since sorting is stable and the entry list is unchanged
        if (state.sortOrder.field == field && state.sortOrder.direction == direction) {
            return;
        }

        // Sort changes redefine ordering semantics, so any contiguous visual
        // selection range becomes ambiguous and is intentionally cleared.
        clear_visual_selection(state.selection);

        fs::path selected_path;
        if (!state.entries.empty()) {
            selected_path = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path;
        }

        state.sortOrder.field = field;
        state.sortOrder.direction = direction;

        sort_entries(state.entries, state.sortOrder);
        sort_entries(state.parentEntries, state.sortOrder);

        // Re-anchor selection by path rather than by index so the user keeps the
        // same logical item selected after a resort.
        if (!selected_path.empty()) {
            const auto it = rng::find_if(state.entries, [&](const auto& entry) { return entry.path == selected_path; });
            if (it != state.entries.end()) {
                state.selection.currentSelected = static_cast<int>(rng::distance(state.entries.begin(), it));
            }
        }

        update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
        updateParentSelection();

        if (state.search.highlightActive && !state.search.pattern.empty()) {
            updateSearchMatches();
            const auto current_it = rng::find(state.search.matches, state.selection.currentSelected);
            if (current_it != state.search.matches.end()) {
                state.search.currentMatchIndex =
                    static_cast<int>(rng::distance(state.search.matches.begin(), current_it));
            }
        }

        if (refreshCallback) {
            refreshCallback();
        }
    }

    [[nodiscard]] core::VoidResult transitionToDirectory(const fs::path& target_directory,
                                                         const std::function<void()>& after_refresh = {}) {
        const NavigationSnapshot snapshot = snapshotNavigationState();

        // Reset to a conservative top-of-directory view before refresh so the
        // destination directory always starts from a valid state.
        state.currentDir = target_directory;
        state.selection.currentSelected = 0;
        state.selection.currentScrollOffset = 0;
        clear_visual_selection(state.selection);

        auto refresh_result = refresh();
        if (!refresh_result) {
            // Failed navigation should behave atomically from the caller's
            // perspective, so restore the full pre-navigation snapshot.
            restoreNavigationState(snapshot);
            return refresh_result;
        }

        if (after_refresh) {
            // Some transitions, such as "go parent", need a post-refresh
            // selection adjustment once the new directory entries are known.
            after_refresh();
            update_scroll_for_selection(state.selection, static_cast<int>(state.entries.size()));
        }

        return {};
    }

    [[nodiscard]] core::VoidResult navigateTo(const fs::path& path) {
        std::error_code directory_ec;
        if (!fs::is_directory(path, directory_ec)) {
            if (directory_ec) {
                return core::make_error(
                    core::ErrorCategory::FileSystem,
                    std::format("Cannot access directory '{}': {}", path.string(), directory_ec.message()));
            }
            return core::make_error(core::ErrorCategory::FileSystem, std::format("Not a directory: {}", path.string()));
        }

        auto canonical_result = blockOn(services.fileSystem->canonicalize(path));
        if (!canonical_result) {
            return std::unexpected(canonical_result.error());
        }

        return transitionToDirectory(*canonical_result);
    }

    [[nodiscard]] core::VoidResult goParent() {
        if (!state.currentDir.has_parent_path() || state.currentDir.parent_path() == state.currentDir) {
            return {};
        }

        const fs::path old_path = state.currentDir.filename();
        return transitionToDirectory(state.currentDir.parent_path(), [this, old_path] {
            for (std::size_t index = 0; index < state.entries.size(); ++index) {
                if (state.entries[index].path.filename() == old_path) {
                    state.selection.currentSelected = static_cast<int>(index);
                    break;
                }
            }
        });
    }

    [[nodiscard]] core::VoidResult navigateToSelectedLinkTargetDirectory() {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::InvalidState, "No entry selected");
        }

        const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
        if (!entry.isSymlink()) {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    std::format("'{}' is not a symbolic link", entry.filename()));
        }
        if (entry.symlinkTarget.empty()) {
            return core::make_error(core::ErrorCategory::NotFound,
                                    std::format("Cannot resolve link target for '{}'", entry.filename()));
        }

        fs::path target_path = entry.symlinkTarget;
        if (target_path.is_relative()) {
            target_path = entry.path.parent_path() / target_path;
        }
        target_path = services.fileSystem->normalize(target_path);

        std::error_code exists_ec;
        if (!fs::exists(target_path, exists_ec)) {
            if (exists_ec) {
                return core::make_error(
                    core::ErrorCategory::FileSystem,
                    std::format("Cannot access link target '{}': {}", target_path.string(), exists_ec.message()));
            }
            return core::make_error(core::ErrorCategory::NotFound,
                                    std::format("Link target does not exist: {}", target_path.string()));
        }

        std::error_code directory_ec;
        if (!fs::is_directory(target_path, directory_ec)) {
            if (directory_ec) {
                return core::make_error(
                    core::ErrorCategory::FileSystem,
                    std::format("Cannot inspect link target '{}': {}", target_path.string(), directory_ec.message()));
            }
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    std::format("Link target is not a directory: {}", target_path.string()));
        }

        return navigateTo(target_path);
    }

    [[nodiscard]] core::VoidResult enterSelected(bool open_file) {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
        if (entry.isDirectory()) {
            return navigateTo(entry.path);
        }
        if (open_file) {
            return blockOn(services.fileSystem->openWithDefault(entry.path));
        }
        return {};
    }

    [[nodiscard]] core::VoidResult create(const std::string& name) {
        if (name.empty()) {
            return {};
        }

        const fs::path new_path = state.currentDir / name;
        auto result = (name.back() == '/' || name.back() == '\\')
                          ? blockOn(services.fileSystem->createDirectory(new_path))
                          : blockOn(services.fileSystem->createFile(new_path));
        if (!result) {
            return std::unexpected(result.error());
        }
        return refresh();
    }

    [[nodiscard]] core::VoidResult rename(const std::string& new_name) {
        if (state.entries.empty() || new_name.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
        auto result = blockOn(services.fileSystem->rename(entry.path, state.currentDir / new_name));
        if (!result) {
            return std::unexpected(result.error());
        }
        return refresh();
    }

    [[nodiscard]] core::VoidResult deleteSelected() {
        const auto targets = selected_entry_paths(state);
        if (targets.empty()) {
            return {};
        }

        for (const auto& target : targets) {
            core::VoidResult result = fs::is_directory(target) ? blockOn(services.fileSystem->removeDirectory(target))
                                                               : blockOn(services.fileSystem->removeFile(target));
            if (!result) {
                return result;
            }
        }

        clear_visual_selection(state.selection);
        return refresh();
    }

    [[nodiscard]] core::VoidResult trashSelected() {
        const auto targets = selected_entry_paths(state);
        if (targets.empty()) {
            return {};
        }

        for (const auto& target : targets) {
            auto result = blockOn(services.fileSystem->moveToTrash(target));
            if (!result) {
                return result;
            }
        }

        clear_visual_selection(state.selection);
        return refresh();
    }

    [[nodiscard]] core::VoidResult openSelected() {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
        return entry.isDirectory() ? core::VoidResult{} : blockOn(services.fileSystem->openWithDefault(entry.path));
    }

    [[nodiscard]] core::VoidResult setClipboardFromSelection(ClipboardState::Operation operation) {
        auto selected_paths = selected_entry_paths(state);
        if (selected_paths.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        state.clipboard.paths = std::move(selected_paths);
        state.clipboard.operation = operation;
        clear_visual_selection(state.selection);
        return {};
    }

    void clearClipboard() {
        state.clipboard.paths.clear();
        state.clipboard.operation = ClipboardState::Operation::None;
    }

    [[nodiscard]] core::VoidResult yankSelected() { return setClipboardFromSelection(ClipboardState::Operation::Copy); }

    [[nodiscard]] core::VoidResult cutSelected() { return setClipboardFromSelection(ClipboardState::Operation::Cut); }

    [[nodiscard]] core::VoidResult discardYank() {
        if (state.clipboard.empty()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Clipboard is empty");
        }
        clearClipboard();
        return {};
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
        const fs::path normalized = services.fileSystem->normalize(path);
        if (absolute) {
            return to_utf8_string(normalized);
        }

        const fs::path normalized_base = services.fileSystem->normalize(baseDirectory);
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

    [[nodiscard]] core::VoidResult moveClipboardSourceTo(const fs::path& source,
                                                         const fs::path& destination,
                                                         bool overwrite) const {
        auto rename_result = blockOn(services.fileSystem->rename(source, destination));
        if (rename_result) {
            return {};
        }

        auto copy_result = blockOn(services.fileSystem->copy(source, destination, overwrite));
        if (!copy_result) {
            return copy_result;
        }

        return fs::is_directory(source) ? blockOn(services.fileSystem->removeDirectory(source))
                                        : blockOn(services.fileSystem->removeFile(source));
    }

    [[nodiscard]] core::VoidResult pasteYanked(bool overwrite) {
        if (state.clipboard.empty()) {
            return core::make_error(core::ErrorCategory::InvalidState, "Clipboard is empty");
        }

        for (const auto& source : state.clipboard.paths) {
            if (!fs::exists(source)) {
                // A stale clipboard should not keep failing repeatedly once the
                // underlying source disappears.
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

                core::VoidResult remove_result = fs::is_directory(destination, ec)
                                                     ? blockOn(services.fileSystem->removeDirectory(destination))
                                                     : blockOn(services.fileSystem->removeFile(destination));
                if (!remove_result) {
                    return remove_result;
                }
            }

            std::error_code equivalent_ec;
            if (fs::equivalent(source, destination, equivalent_ec)) {
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        "Source and destination resolve to the same path");
            }

            if (state.clipboard.operation == ClipboardState::Operation::Copy &&
                isSourceParentOfDestination(source, destination)) {
                return core::make_error(core::ErrorCategory::InvalidArgument,
                                        "Cannot copy a directory into itself or its subdirectory");
            }

            // Copy and cut share most validation. The only difference is the
            // final mutation primitive used once the destination is accepted.
            auto operation_result = state.clipboard.operation == ClipboardState::Operation::Copy
                                        ? blockOn(services.fileSystem->copy(source, destination, overwrite))
                                        : moveClipboardSourceTo(source, destination, overwrite);
            if (!operation_result) {
                return operation_result;
            }
        }

        if (state.clipboard.operation == ClipboardState::Operation::Cut) {
            clearClipboard();
        }

        clear_visual_selection(state.selection);
        return refresh();
    }

    [[nodiscard]] core::VoidResult copySelectedPathToSystemClipboard(bool absolute) const {
        auto text = selectedPathClipboardText(absolute);
        if (!text) {
            return std::unexpected(text.error());
        }
        return blockOn(services.clipboard->copyText(*text));
    }

    [[nodiscard]] core::VoidResult copyCurrentDirectoryPathToSystemClipboard(bool absolute) const {
        return blockOn(services.clipboard->copyText(currentDirectoryClipboardText(absolute)));
    }

    [[nodiscard]] core::VoidResult copySelectedFileNameToSystemClipboard() const {
        auto text = selectedFileNameClipboardText();
        if (!text) {
            return std::unexpected(text.error());
        }
        return blockOn(services.clipboard->copyText(*text));
    }

    [[nodiscard]] core::VoidResult copySelectedNameWithoutExtensionToSystemClipboard() const {
        auto text = selectedStemClipboardText();
        if (!text) {
            return std::unexpected(text.error());
        }
        return blockOn(services.clipboard->copyText(*text));
    }

    [[nodiscard]] core::Result<std::string> selectedPathClipboardText(bool absolute) const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path selected = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path;
        return formatPathForClipboard(selected, absolute);
    }

    [[nodiscard]] std::string currentDirectoryClipboardText(bool absolute) const {
        return formatPathForClipboard(state.currentDir, absolute);
    }

    [[nodiscard]] core::Result<std::string> selectedFileNameClipboardText() const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path name = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path.filename();
        if (name.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument, "Selected entry has no file name");
        }

        return to_utf8_string(name);
    }

    [[nodiscard]] core::Result<std::string> selectedStemClipboardText() const {
        if (state.entries.empty()) {
            return core::make_error(core::ErrorCategory::FileSystem, "No entry selected");
        }

        const fs::path stem = state.entries[static_cast<std::size_t>(state.selection.currentSelected)].path.stem();
        if (stem.empty()) {
            return core::make_error(core::ErrorCategory::InvalidArgument,
                                    "Selected entry has no name without extension");
        }

        return to_utf8_string(stem);
    }
};

Explorer::Explorer(std::filesystem::path start_path, ExplorerServices services)
    : impl_(std::make_unique<Impl>(std::move(start_path), std::move(services))) {}

core::Result<std::shared_ptr<Explorer>> Explorer::create(std::filesystem::path start_path, ExplorerServices services) {
    auto explorer = std::shared_ptr<Explorer>(new Explorer(std::move(start_path), std::move(services)));
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

const ExplorerServices& Explorer::services() const noexcept {
    return impl_->services;
}

std::optional<fs::path> Explorer::selectedPath() const {
    if (impl_->state.entries.empty()) {
        return std::nullopt;
    }
    return impl_->state.entries[static_cast<std::size_t>(impl_->state.selection.currentSelected)].path;
}

std::vector<fs::path> Explorer::selectedPaths() const {
    return selected_entry_paths(impl_->state);
}

bool Explorer::showHidden() const noexcept {
    return impl_->showHidden;
}

void Explorer::setShowHidden(bool show_hidden) noexcept {
    impl_->showHidden = show_hidden;
}

core::VoidResult Explorer::navigateTo(const fs::path& path) {
    return impl_->navigateTo(path);
}

core::VoidResult Explorer::goParent() {
    return impl_->goParent();
}

core::VoidResult Explorer::navigateToSelectedLinkTargetDirectory() {
    return impl_->navigateToSelectedLinkTargetDirectory();
}

core::VoidResult Explorer::enterSelected(bool open_file) {
    return impl_->enterSelected(open_file);
}

void Explorer::moveDown(int count) {
    if (!impl_->state.entries.empty()) {
        impl_->setSelectedIndexAndUpdate(std::min(impl_->state.selection.currentSelected + count,
                                                  static_cast<int>(impl_->state.entries.size()) - 1));
    }
}

void Explorer::moveUp(int count) {
    impl_->setSelectedIndexAndUpdate(std::max(impl_->state.selection.currentSelected - count, 0));
}

void Explorer::goToTop() {
    impl_->setSelectedIndexAndUpdate(0);
}

void Explorer::goToBottom() {
    if (!impl_->state.entries.empty()) {
        impl_->setSelectedIndexAndUpdate(static_cast<int>(impl_->state.entries.size()) - 1);
    }
}

void Explorer::goToLine(int line) {
    if (!impl_->state.entries.empty()) {
        impl_->setSelectedIndexAndUpdate(
            std::max(0, std::min(line - 1, static_cast<int>(impl_->state.entries.size()) - 1)));
    }
}

void Explorer::setViewportRows(int rows) {
    impl_->setViewportRows(rows);
}

void Explorer::setSortOrder(SortOrder::Field field, SortOrder::Direction direction) {
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

core::Result<std::string> Explorer::selectedPathClipboardText(bool absolute) const {
    return impl_->selectedPathClipboardText(absolute);
}

std::string Explorer::currentDirectoryClipboardText(bool absolute) const {
    return impl_->currentDirectoryClipboardText(absolute);
}

core::Result<std::string> Explorer::selectedFileNameClipboardText() const {
    return impl_->selectedFileNameClipboardText();
}

core::Result<std::string> Explorer::selectedStemClipboardText() const {
    return impl_->selectedStemClipboardText();
}

void Explorer::enterVisualMode() {
    if (impl_->state.entries.empty()) {
        return;
    }

    impl_->state.selection.visualModeActive = true;
    impl_->state.selection.visualAnchor = impl_->state.selection.currentSelected;
    update_visual_selection(impl_->state.selection, impl_->state.entries);
}

void Explorer::exitVisualMode() {
    clear_visual_selection(impl_->state.selection);
}

int Explorer::visualSelectionCount() const noexcept {
    return impl_->state.selection.visualModeActive
               ? static_cast<int>(impl_->state.selection.visualSelectedIndices.size())
               : 0;
}

void Explorer::search(const std::string& pattern) {
    impl_->search(pattern);
}

void Explorer::nextMatch() {
    if (impl_->state.search.highlightActive && !impl_->state.search.matches.empty()) {
        impl_->jumpToNextMatch();
    }
}

void Explorer::prevMatch() {
    if (impl_->state.search.highlightActive && !impl_->state.search.matches.empty()) {
        impl_->jumpToPrevMatch();
    }
}

void Explorer::clearSearch() {
    impl_->clearSearchState();
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

void Explorer::beginDirectoryListing(fs::path directory, const std::uint64_t generation) {
    impl_->beginDirectoryListing(std::move(directory), generation);
}

void Explorer::appendDirectoryChunk(std::vector<core::filesystem::FileEntry> entries,
                                    std::size_t loaded_entries,
                                    std::size_t total_entries,
                                    bool has_more,
                                    const std::uint64_t generation) {
    impl_->appendDirectoryChunk(std::move(entries), loaded_entries, total_entries, has_more, generation);
}

void Explorer::completeDirectoryListing(const std::uint64_t generation) {
    impl_->completeDirectoryListing(generation);
}

void Explorer::setParentEntries(std::vector<core::filesystem::FileEntry> entries) {
    impl_->setParentEntries(std::move(entries));
}

void Explorer::selectPathIfPresent(const fs::path& path) {
    if (path.empty()) {
        return;
    }

    const auto it = rng::find_if(impl_->state.entries, [&](const auto& entry) { return entry.path == path; });
    if (it == impl_->state.entries.end()) {
        return;
    }

    auto update_helper = SelectionUpdateHelper(impl_->state.selection, impl_->state.entries,
                                               static_cast<int>(impl_->state.entries.size()));
    impl_->state.selection.currentSelected = static_cast<int>(rng::distance(impl_->state.entries.begin(), it));
}

}  // namespace expp::app
