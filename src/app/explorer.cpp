#include "expp/app/explorer.hpp"

#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace expp::app {

namespace fs = std::filesystem;
namespace rng = std::ranges;

struct Explorer::Impl {
    explicit Impl(fs::path start_path) {
        state.currentDir = std::move(start_path);
        refresh();
    }

    ExplorerState state;
    RefreshCallback refreshCallback;
    bool showHidden = false;

    void refresh() {
        state.entries.clear();
        auto result = core::filesystem::list_directory(state.currentDir, showHidden);
        if (result) {
            state.entries = std::move(*result);
        }

        // Get Parent Directory Contents
        state.parentEntries.clear();
        if (state.currentDir.has_parent_path() && state.currentDir.parent_path() != state.currentDir) {
            auto parent_result = core::filesystem::list_directory(state.currentDir.parent_path(), showHidden);
            if (parent_result) {
                state.parentEntries = std::move(*parent_result);
            }
        }

        // clamp selection
        state.currentSelected =
            std::min(state.currentSelected, std::max(0, static_cast<int>(state.entries.size()) - 1));

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
        if (!fs::is_directory(path)) {
            return core::make_error(core::ErrorCategory::FileSystem, std::format("Not a directory: {}", path.string()));
        }
        auto canonical_result = core::filesystem::canonicalize(path);
        if (!canonical_result) {
            return std::unexpected(canonical_result.error());
        }

        state.currentDir = *canonical_result;
        state.currentSelected = 0;

        refresh();
        return {};
    }

    core::VoidResult goParent() {
        if (!state.currentDir.has_parent_path() || state.currentDir.parent_path() == state.currentDir) {
            return {};  // Already at root
        }

        auto old_path = state.currentDir.filename();
        state.currentDir = state.currentDir.parent_path();
        state.currentSelected = 0;

        refresh();

        // Try to select the directory we came from
        for (size_t i = 0; i < state.entries.size(); ++i) {
            if (state.entries[i].path.filename() == old_path) {
                state.currentSelected = static_cast<int>(i);
                break;
            }
        }

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
        refresh();
        return {};
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
        refresh();
        return {};
    }

    core::VoidResult deleteSelected() {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<size_t>(state.currentSelected)];

        // Use core::filesystem::remove_directory or core::filesystem::remove_file
        core::VoidResult result;
        if (fs::is_directory(entry.path)) {
            result = core::filesystem::remove_directory(entry.path);
        } else {
            result = core::filesystem::remove_file(entry.path);
        }

        if (!result) {
            return result;
        }
        refresh();
        return {};
    }

    core::VoidResult trashSelected() {
        if (state.entries.empty()) {
            return {};
        }

        const auto& entry = state.entries[static_cast<size_t>(state.currentSelected)];

        auto result = core::filesystem::move_to_trash(entry.path);
        if (!result) {
            return result;
        }
        refresh();
        return {};
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
                return;
            }
        }
        // Wrap around to the first match
        state.currentMatchIndex = 0;
        state.currentSelected = state.searchMatches[0];
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
                return;
            }
        }
        // Wrap around to the last match
        state.currentMatchIndex = static_cast<int>(state.searchMatches.size()) - 1;
        state.currentSelected = state.searchMatches[static_cast<size_t>(state.currentMatchIndex)];
    }

};  // Explorer::Impl

Explorer::Explorer(std::filesystem::path start_path) : impl_(std::make_unique<Impl>(std::move(start_path))) {}

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
    }
}

void Explorer::moveUp(int count) {
    impl_->state.currentSelected = std::max(impl_->state.currentSelected - count, 0);
}

void Explorer::goToTop() {
    impl_->state.currentSelected = 0;
}

void Explorer::goToBottom() {
    if (!impl_->state.entries.empty()) {
        impl_->state.currentSelected = static_cast<int>(impl_->state.entries.size()) - 1;
    }
}

void Explorer::goToLine(int line) {
    if (!impl_->state.entries.empty()) {
        impl_->state.currentSelected = std::min(line - 1,  // 1-indexed to 0-indexed
                                                static_cast<int>(impl_->state.entries.size()) - 1);
        impl_->state.currentSelected = std::max(impl_->state.currentSelected, 0);
    }
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
        impl_->state.targetPath = impl_->state.entries[static_cast<size_t>(impl_->state.currentSelected)].path;
        impl_->state.showDeleteDialog = true;
    }
}

void Explorer::showTrashDialog() {
    if (!impl_->state.entries.empty()) {
        impl_->state.targetPath = impl_->state.entries[static_cast<size_t>(impl_->state.currentSelected)].path;
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

void Explorer::refresh() {
    impl_->refresh();
}

void Explorer::onRefresh(RefreshCallback callback) {
    impl_->refreshCallback = std::move(callback);
}
}  // namespace expp::app