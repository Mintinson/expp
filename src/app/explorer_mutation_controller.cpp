#include "expp/app/explorer_mutation_controller.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace expp::app {

namespace fs = std::filesystem;

namespace {

/**
 * @brief Helper function to format pluralized nouns (e.g., "1 item", "2 items").
 */
[[nodiscard]] std::string noun_with_count(int count, std::string_view singular) {
    return std::format("{} {}{}", count, singular, count == 1 ? "" : "s");
}

}  // namespace

ExplorerMutationController::ExplorerMutationController(std::shared_ptr<Explorer> explorer,
                                                       NotificationCenter& notifications,
                                                       ExplorerDirectoryController& directory_controller)
    : explorer_(std::move(explorer))
    , notifications_(notifications)
    , directoryController_(directory_controller) {}

void ExplorerMutationController::create(std::string name) {
    if (name.empty()) {
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;
    const fs::path current_dir = explorer_->state().currentDir;

    // Heuristic: If the name ends with a slash, treat it as a directory creation request.
    const bool create_directory = name.back() == '/' || name.back() == '\\';
    const fs::path target = current_dir / name;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, target, create_directory, name = std::move(name)]() -> core::Task<void> {
            // 1. Perform disk I/O in the background
            auto result = create_directory ? co_await file_system->createDirectory(target)
                                           : co_await file_system->createFile(target);

            // 2. Post result back to the UI thread safely
            runtime->postToUi([this, target, name, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Success, std::format("Created '{}'", name));

                // 3. Trigger a directory reload to show the newly created item
                directoryController_.reloadCurrentDirectory(target);
            });
        },
        asio::detached);
}

void ExplorerMutationController::rename(std::string new_name) {
    const auto selected = explorer_->selectedPath();
    if (!selected.has_value() || new_name.empty()) {
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;
    const fs::path current_dir = explorer_->state().currentDir;
    const fs::path target = current_dir / new_name;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, source = *selected, target, new_name = std::move(new_name)]() -> core::Task<void> {
            auto result = co_await file_system->rename(source, target);
            runtime->postToUi([this, target, new_name, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Success, std::format("Renamed to '{}'", new_name));
                directoryController_.reloadCurrentDirectory(target);
            });
        },
        asio::detached);
}

void ExplorerMutationController::deleteSelected() {
    const auto targets = explorer_->selectedPaths();
    if (targets.empty()) {
        return;
    }

    const int selection_count = static_cast<int>(targets.size());
    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, targets, selection_count]() -> core::Task<void> {
            for (const auto& target : targets) {
                auto result = fs::is_directory(target) ? co_await file_system->removeDirectory(target)
                                                       : co_await file_system->removeFile(target);
                if (!result) {
                    runtime->postToUi([this, error = result.error()] {
                        notifications_.publish(severity_for_error(error), error.message());
                    });
                    co_return;
                }
            }

            runtime->postToUi([this, selection_count] {
                explorer_->exitVisualMode();
                notifications_.publish(ui::ToastSeverity::Success,
                                       std::format("Deleted {}", noun_with_count(selection_count, "item")));
                directoryController_.reloadCurrentDirectory();
            });
        },
        asio::detached);
}

void ExplorerMutationController::trashSelected() {
    const auto targets = explorer_->selectedPaths();
    if (targets.empty()) {
        return;
    }

    const int selection_count = static_cast<int>(targets.size());
    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, targets, selection_count]() -> core::Task<void> {
            for (const auto& target : targets) {
                auto result = co_await file_system->moveToTrash(target);
                if (!result) {
                    runtime->postToUi([this, error = result.error()] {
                        notifications_.publish(severity_for_error(error), error.message());
                    });
                    co_return;
                }
            }

            runtime->postToUi([this, selection_count] {
                explorer_->exitVisualMode();
                notifications_.publish(ui::ToastSeverity::Success,
                                       std::format("Trashed {}", noun_with_count(selection_count, "item")));
                directoryController_.reloadCurrentDirectory();
            });
        },
        asio::detached);
}

void ExplorerMutationController::openSelected() {
    const auto selected = explorer_->selectedPath();
    if (!selected.has_value()) {
        return;
    }

    const auto& state = explorer_->state();
    const auto& entry = state.entries[static_cast<std::size_t>(state.selection.currentSelected)];
    if (entry.isDirectory()) {
        directoryController_.navigateTo(entry.path);
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, path = *selected]() -> core::Task<void> {
            auto result = co_await file_system->openWithDefault(path);
            runtime->postToUi([this, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Success, "Opened with default application");
            });
        },
        asio::detached);
}

void ExplorerMutationController::pasteYanked(bool overwrite) {
    const auto clipboard = explorer_->state().clipboard;
    if (clipboard.empty()) {
        notifications_.publish(ui::ToastSeverity::Warning, "Clipboard is empty");
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto file_system = explorer_->services().fileSystem;
    const fs::path current_dir = explorer_->state().currentDir;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, file_system, clipboard, current_dir, overwrite]() -> core::Task<void> {
            // Iterate through all items currently yanked/cut
            for (const auto& source : clipboard.paths) {
                if (!fs::exists(source)) {
                    runtime->postToUi([this] {
                        notifications_.publish(ui::ToastSeverity::Error, "Clipboard source does not exist");
                    });
                    co_return;  // Abort coroutine
                }

                const fs::path destination = current_dir / source.filename();

                std::error_code ec;
                // --- Guard 1: Overwrite Protection ---
                if (fs::exists(destination, ec)) {
                    if (!overwrite) {
                        runtime->postToUi([this, destination] {
                            notifications_.publish(ui::ToastSeverity::Error,
                                                   std::format("Destination already exists: {}", destination.string()));
                        });
                        co_return;
                    }
                    // If overwrite is true, delete the existing entity first
                    auto remove_result = fs::is_directory(destination, ec)
                                             ? co_await file_system->removeDirectory(destination)
                                             : co_await file_system->removeFile(destination);
                    if (!remove_result) {
                        runtime->postToUi([this, error = remove_result.error()] {
                            notifications_.publish(severity_for_error(error), error.message());
                        });
                        co_return;
                    }
                }

                // --- Guard 2: Identity Protection ---
                // Prevents copying a file onto exactly itself
                std::error_code equivalent_ec;
                if (fs::equivalent(source, destination, equivalent_ec)) {
                    runtime->postToUi([this] {
                        notifications_.publish(ui::ToastSeverity::Warning,
                                               "Source and destination resolve to the same path");
                    });
                    co_return;
                }

                // --- Guard 3: Recursion Protection ---
                if (clipboard.operation == ClipboardState::Operation::Copy &&
                    isSourceParentOfDestination(source, destination)) {
                    runtime->postToUi([this] {
                        notifications_.publish(ui::ToastSeverity::Warning,
                                               "Cannot copy a directory into itself or its subdirectory");
                    });
                    co_return;
                }

                // --- Execute Actual Copy or Move ---
                if (clipboard.operation == ClipboardState::Operation::Copy) {
                    auto result = co_await file_system->copy(source, destination, overwrite);
                    if (!result) {
                        runtime->postToUi([this, error = result.error()] {
                            notifications_.publish(severity_for_error(error), error.message());
                        });
                        co_return;
                    }
                } else {  // Operation is Cut (Move)
                    auto rename_result = co_await file_system->rename(source, destination);
                    if (!rename_result) {
                        auto copy_result = co_await file_system->copy(source, destination, overwrite);
                        // Fallback: If rename fails (e.g., EXDEV when moving across partitions/drives)
                        if (!copy_result) {
                            runtime->postToUi([this, error = copy_result.error()] {
                                notifications_.publish(severity_for_error(error), error.message());
                            });
                            co_return;
                        }
                        // Clean up original source after a successful deep copy
                        auto remove_result = fs::is_directory(source) ? co_await file_system->removeDirectory(source)
                                                                      : co_await file_system->removeFile(source);
                        if (!remove_result) {
                            runtime->postToUi([this, error = remove_result.error()] {
                                notifications_.publish(severity_for_error(error), error.message());
                            });
                            co_return;
                        }
                    }
                }
            }
            // --- Finalize State ---
            runtime->postToUi([this, clipboard, overwrite] {
                if (clipboard.operation == ClipboardState::Operation::Cut) {
                    (void)explorer_->discardYank();
                }
                explorer_->exitVisualMode();
                notifications_.publish(
                    ui::ToastSeverity::Success,
                    overwrite
                        ? std::format("Pasted {} with overwrite",
                                      noun_with_count(static_cast<int>(clipboard.paths.size()), "item"))
                        : std::format("Pasted {}", noun_with_count(static_cast<int>(clipboard.paths.size()), "item")));
                // Refresh UI to show pasted items
                directoryController_.reloadCurrentDirectory();
            });
        },
        asio::detached);
}

void ExplorerMutationController::yankSelected() {
    const auto count =
        explorer_->state().selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
    auto result = explorer_->yankSelected();
    if (!result) {
        notifications_.publish(severity_for_error(result.error()), result.error().message());
        return;
    }
    notifications_.publish(ui::ToastSeverity::Success, std::format("Copied {}", noun_with_count(count, "item")));
}

void ExplorerMutationController::cutSelected() {
    const auto count =
        explorer_->state().selection.visualModeActive ? std::max(1, explorer_->visualSelectionCount()) : 1;
    auto result = explorer_->cutSelected();
    if (!result) {
        notifications_.publish(severity_for_error(result.error()), result.error().message());
        return;
    }
    notifications_.publish(ui::ToastSeverity::Success, std::format("Cut {}", noun_with_count(count, "item")));
}

void ExplorerMutationController::discardYank() {
    auto result = explorer_->discardYank();
    if (!result) {
        notifications_.publish(severity_for_error(result.error()), result.error().message());
        return;
    }
    notifications_.publish(ui::ToastSeverity::Info, "Clipboard cleared");
}

void ExplorerMutationController::copySelectedPath(bool absolute) {
    auto text = explorer_->selectedPathClipboardText(absolute);
    if (!text) {
        notifications_.publish(severity_for_error(text.error()), text.error().message());
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto clipboard = explorer_->services().clipboard;
    const std::string success_message = absolute ? "Copied absolute path" : "Copied relative path";

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, clipboard, text = *text, success_message]() -> core::Task<void> {
            auto result = co_await clipboard->copyText(text);
            runtime->postToUi([this, success_message, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Info, success_message);
            });
        },
        asio::detached);
}

void ExplorerMutationController::copyCurrentDirectoryPath(bool absolute) {
    const auto text = explorer_->currentDirectoryClipboardText(absolute);
    const auto runtime = explorer_->services().runtime;
    const auto clipboard = explorer_->services().clipboard;
    const std::string success_message = absolute ? "Copied absolute directory path" : "Copied relative directory path";

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, clipboard, text, success_message]() -> core::Task<void> {
            auto result = co_await clipboard->copyText(text);
            runtime->postToUi([this, success_message, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Info, success_message);
            });
        },
        asio::detached);
}

void ExplorerMutationController::copySelectedFileName() {
    auto text = explorer_->selectedFileNameClipboardText();
    if (!text) {
        notifications_.publish(severity_for_error(text.error()), text.error().message());
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto clipboard = explorer_->services().clipboard;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, clipboard, text = *text]() -> core::Task<void> {
            auto result = co_await clipboard->copyText(text);
            runtime->postToUi([this, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Info, "Copied file name");
            });
        },
        asio::detached);
}

void ExplorerMutationController::copySelectedStem() {
    auto text = explorer_->selectedStemClipboardText();
    if (!text) {
        notifications_.publish(severity_for_error(text.error()), text.error().message());
        return;
    }

    const auto runtime = explorer_->services().runtime;
    const auto clipboard = explorer_->services().clipboard;

    asio::co_spawn(
        runtime->ioExecutor(),
        [this, runtime, clipboard, text = *text]() -> core::Task<void> {
            auto result = co_await clipboard->copyText(text);
            runtime->postToUi([this, result = std::move(result)]() mutable {
                if (!result) {
                    notifications_.publish(severity_for_error(result.error()), result.error().message());
                    return;
                }
                notifications_.publish(ui::ToastSeverity::Info, "Copied name without extension");
            });
        },
        asio::detached);
}

bool ExplorerMutationController::isSourceParentOfDestination(const fs::path& source, const fs::path& destination) {
    // Resolve any symlinks, dot-dots (..), and format path separators uniformly
    std::error_code source_ec;
    std::error_code destination_ec;
    const fs::path source_normalized = fs::weakly_canonical(source, source_ec).lexically_normal();
    const fs::path destination_normalized = fs::weakly_canonical(destination, destination_ec).lexically_normal();
    if (source_ec || destination_ec || source_normalized.empty() || destination_normalized.empty()) {
        return false;
    }
    // Compare path components iteratively
    // E.g., Source: /usr/local/  Dest: /usr/local/bin/
    // It is a parent if Dest has all components of Source in the exact same order.
    auto source_it = source_normalized.begin();
    auto destination_it = destination_normalized.begin();
    for (; source_it != source_normalized.end(); ++source_it, ++destination_it) {
        if (destination_it == destination_normalized.end() || *source_it != *destination_it) {
            return false;
        }
    }
    return destination_it != destination_normalized.end();
}

}  // namespace expp::app
