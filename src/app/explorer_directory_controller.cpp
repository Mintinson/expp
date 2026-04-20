#include "expp/app/explorer_directory_controller.hpp"

#include "expp/app/navigation_utils.hpp"
#include "expp/core/config.hpp"
#include "expp/core/task.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/this_coro.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <utility>

namespace expp::app {

namespace fs = std::filesystem;

ExplorerDirectoryController::ExplorerDirectoryController(std::shared_ptr<Explorer> explorer,
                                                         NotificationCenter& notifications)
    : explorer_(std::move(explorer))
    , notifications_(notifications) {}

void ExplorerDirectoryController::reloadCurrentDirectory(std::optional<fs::path> reselect) {
    startDirectoryLoad(explorer_->state().currentDir, std::move(reselect));
}

void ExplorerDirectoryController::navigateTo(fs::path directory, std::optional<fs::path> reselect) {
    startDirectoryLoad(std::move(directory), std::move(reselect));
}

void ExplorerDirectoryController::navigateToInput(std::string input) {
    const auto runtime = explorer_->services().runtime;
    const auto base_directory = explorer_->state().currentDir;

    // Spawn a task because path resolution (handling ~, symlinks, etc.) touches the disk
    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, input = std::move(input), base_directory]() -> core::Task<void> {
            // Context switch to the Disk Executor for blocking I/O operation
            auto resolved = co_await core::invoke_on(runtime->diskExecutor(), [input, base_directory] {
                return resolve_directory_input(input, base_directory);
            });
            if (!resolved) {
                // Switch back to UI to show error toast
                runtime->postToUi([this, error = resolved.error()] {
                    notifications_.publish(severity_for_error(error), error.message());
                });
                co_return;
            }

            // Switch to UI to start actual loading process
            runtime->postToUi([this, resolved_path = *resolved] { startDirectoryLoad(resolved_path); });
        },
        asio::detached);
}

void ExplorerDirectoryController::navigateToHomeDirectory() {
    auto home_result = resolve_home_directory();
    if (!home_result) {
        notifications_.publish(severity_for_error(home_result.error()), home_result.error().message());
        return;
    }
    startDirectoryLoad(*home_result);
}

void ExplorerDirectoryController::navigateToConfigDirectory() {
    const auto config_dir = core::ConfigManager::userConfigPath().parent_path();
    if (config_dir.empty()) {
        notifications_.publish(ui::ToastSeverity::Warning, "Config directory is not available");
        return;
    }
    startDirectoryLoad(config_dir);
}

void ExplorerDirectoryController::navigateToSelectedLinkTargetDirectory() {
    const auto selected = explorer_->selectedPath();
    if (!selected.has_value()) {
        notifications_.publish(ui::ToastSeverity::Warning, "No entry selected");
        return;
    }

    const auto& state = explorer_->state();
    const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];

    // Ensure the target is actually a symlink
    if (!entry.isSymlink()) {
        notifications_.publish(ui::ToastSeverity::Warning,
                               std::format("'{}' is not a symbolic link", entry.filename()));
        return;
    }
    if (entry.symlinkTarget.empty()) {
        notifications_.publish(ui::ToastSeverity::Warning,
                               std::format("Cannot resolve link target for '{}'", entry.filename()));
        return;
    }

    // Resolve relative symlink targets based on their current parent directory
    fs::path target_path = entry.symlinkTarget;
    if (target_path.is_relative()) {
        target_path = entry.path.parent_path() / target_path;
    }

    startDirectoryLoad(explorer_->services().fileSystem->normalize(target_path));
}

void ExplorerDirectoryController::goParent() {
    const auto& current_dir = explorer_->state().currentDir;
    // Prevent navigating past the root directory
    if (!current_dir.has_parent_path() || current_dir.parent_path() == current_dir) {
        return;
    }
    // Navigate up, and automatically reselect the folder we just came from
    startDirectoryLoad(current_dir.parent_path(), current_dir);
}

void ExplorerDirectoryController::toggleHidden() {
    const auto reselect = explorer_->selectedPath();
    explorer_->setShowHidden(!explorer_->showHidden());
    startDirectoryLoad(explorer_->state().currentDir, reselect);
}

void ExplorerDirectoryController::updateViewportInterest() {
    scheduleMimePreload();
}

std::optional<ExplorerDirectoryController::CachedMime> ExplorerDirectoryController::cachedMime(
    const fs::path& path) const {
    if (const auto it = mimeCache_.find(path); it != mimeCache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// --- [ Core Orchestration logic ] ---
void ExplorerDirectoryController::startDirectoryLoad(fs::path directory, std::optional<fs::path> reselect) {
    // 1. Reset state & Cancel previous in-flight operations
    listingCancellation_.cancel();
    listingCancellation_.reset();
    reselectPath_ = std::move(reselect);
    mimeCache_.clear();
    preloadStart_ = -1;
    preloadEnd_ = -1;
    preloadEntryCount_ = 0;

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;

    // 2. Increment generation counter to track this specific request
    const auto generation = ++listingGeneration_;
    const auto include_hidden = explorer_->showHidden();
    const auto token = listingCancellation_.token();

    // How many items to process at a time (prevents UI stuttering)
    const std::size_t chunk_entries =
        static_cast<std::size_t>(std::max(1, core::global_config().config().listing.chunkEntries));

    // 3. Launch async operation
    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, directory = std::move(directory), generation, include_hidden, token,
         chunk_entries]() -> core::Task<void> {
            // Step A: Canonicalize the directory path (resolve symlinks/absolute path)
            auto canonical_result = co_await file_system->canonicalize(directory);
            if (!canonical_result) {
                runtime->postToUi([this, generation, error = canonical_result.error()] {
                    if (generation != listingGeneration_) {
                        return;  // Generation Counters resolve race conditions.
                    }
                    notifications_.publish(severity_for_error(error), error.message());
                });
                co_return;
            }

            const fs::path canonical_directory = *canonical_result;

            // Step B: Notify UI that a valid directory is about to be loaded
            runtime->postToUi([this, generation, canonical_directory] {
                if (generation != listingGeneration_) {
                    return;
                }
                explorer_->beginDirectoryListing(canonical_directory, generation);
                scheduleMimePreload();
            });

            // Step C: Stream directory contents progressively (Generator pattern)
            auto list_result = co_await file_system->streamDirectory(
                DirectoryListRequest{
                    .directory = canonical_directory,
                    .includeHidden = include_hidden,
                    .chunkEntries = chunk_entries,
                    .cancellation = token,
                },
                // Chunk callback: Called by backend every time a chunk is ready
                [this, runtime, generation](DirectoryListChunk chunk) {
                    runtime->postToUi([this, generation, chunk = std::move(chunk)]() mutable {
                        if (generation != listingGeneration_) {
                            return;
                        }
                        explorer_->appendDirectoryChunk(std::move(chunk.entries), chunk.loadedEntries,
                                                        chunk.totalEntries, chunk.hasMore, generation);
                        if (reselectPath_.has_value()) {
                            explorer_->selectPathIfPresent(*reselectPath_);
                        }
                        scheduleMimePreload();
                    });
                });

            // Step D: Handle stream failure (e.g., permissions denied mid-read)
            if (!list_result) {
                runtime->postToUi([this, generation, error = list_result.error()] {
                    if (generation != listingGeneration_) {
                        return;
                    }
                    notifications_.publish(severity_for_error(error), error.message());
                    explorer_->completeDirectoryListing(generation);
                });
                co_return;
            }

            // Step E: Request parent directory info in the background (for UI breadcrumbs)
            scheduleParentEntries(canonical_directory, generation);

            // Step F: Finalize UI state
            runtime->postToUi([this, generation] {
                if (generation != listingGeneration_) {
                    return;
                }
                explorer_->completeDirectoryListing(generation);
                if (reselectPath_.has_value()) {
                    explorer_->selectPathIfPresent(*reselectPath_);
                }
                scheduleMimePreload();
            });
        },
        asio::detached);
}

void ExplorerDirectoryController::scheduleParentEntries(const fs::path& directory, const std::uint64_t generation) {
    if (!directory.has_parent_path() || directory.parent_path() == directory) {
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;
    const auto include_hidden = explorer_->showHidden();

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, parent_directory = directory.parent_path(), generation,
         include_hidden]() -> core::Task<void> {
            auto parent_result = co_await file_system->listDirectory(DirectoryListRequest{
                .directory = parent_directory,
                .includeHidden = include_hidden,
            });
            if (!parent_result) {
                co_return;  // Silently fail, it's just for breadcrumbs
            }

            runtime->postToUi([this, generation, entries = std::move(parent_result->entries)]() mutable {
                if (generation != listingGeneration_) {
                    return;
                }
                explorer_->setParentEntries(std::move(entries));
            });
        },
        asio::detached);
}

void ExplorerDirectoryController::scheduleMimePreload() {
    const auto& state = explorer_->state();
    if (state.entries.empty()) {
        preloadStart_ = -1;
        preloadEnd_ = -1;
        preloadEntryCount_ = 0;
        return;
    }
    // 1. Calculate the visible sliding window (Viewport + Preload padding)
    const int viewport_rows = std::max(1, state.selection.currentViewportRows);
    const int preload_pages = std::max(0, core::global_config().config().listing.preloadPages);
    const int visible_offset =
        std::clamp(state.selection.currentScrollOffset, 0, static_cast<int>(state.entries.size()));
    // We want to load N pages before and N+1 pages after the current view
    const int preload_before = viewport_rows * preload_pages;
    const int preload_after = viewport_rows * (preload_pages + 1);
    const int start = std::max(0, visible_offset - preload_before);
    const int end = std::min(static_cast<int>(state.entries.size()), visible_offset + preload_after);

    // 2. Optimization: Don't spawn new tasks if the viewport hasn't significantly changed
    if (start == preloadStart_ && end == preloadEnd_ && preloadEntryCount_ == state.entries.size()) {
        return;
    }

    preloadStart_ = start;
    preloadEnd_ = end;
    preloadEntryCount_ = state.entries.size();

    // 3. Filter entries that already have their MIME cached
    std::vector<fs::path> targets;
    targets.reserve(static_cast<std::size_t>(std::max(0, end - start)));
    for (int index = start; index < end; ++index) {
        const auto& path = state.entries[static_cast<std::size_t>(index)].path;
        if (!mimeCache_.contains(path)) {
            targets.push_back(path);
        }
    }

    if (targets.empty()) {
        return;
    }

    // 4. Cancel previous MIME tasks.
    //    If the user scrolls rapidly, we don't want to waste CPU checking files they scrolled past.
    enrichmentCancellation_.cancel();
    enrichmentCancellation_.reset();

    const auto runtime = explorer_->services().runtime;
    const auto mime_service = explorer_->services().mime;
    const auto token = enrichmentCancellation_.token();
    const auto generation = ++enrichmentGeneration_;

    // 5. Spawn background task to detect MIME types one by one
    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, mime_service, targets = std::move(targets), token, generation]() -> core::Task<void> {
            for (const auto& path : targets) {
                // Early exit if the user scrolled away and triggered cancellation
                if (token.isCancellationRequested()) {
                    co_return;
                }

                auto mime_result = co_await mime_service->detectMime(MimeRequest{
                    .target = path,
                    .cancellation = token,
                });
                if (!mime_result) {
                    continue;  // Skip on failure
                }

                // Post successful result back to UI and update cache
                runtime->postToUi([this, generation, path, mime = *mime_result] {
                    if (generation != enrichmentGeneration_) {
                        return;
                    }
                    mimeCache_[path] = CachedMime{
                        .mimeType = mime.mimeType,
                        .previewable = mime.previewable,
                    };
                });
            }
        },
        asio::detached);
}

}  // namespace expp::app
