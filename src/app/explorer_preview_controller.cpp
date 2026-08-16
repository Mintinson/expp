/**
 * @file explorer_preview_controller.cpp
 * @brief Implementation of preview target synchronization and loading.
 */

#include "expp/app/explorer_preview_controller.hpp"

#include "expp/app/explorer.hpp"
#include "expp/core/config.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>
#include <variant>

namespace expp::app {

void ExplorerPreviewController::sync(const std::optional<std::filesystem::path>& current_target,
                                     bool force_refresh,
                                     app::PreviewViewport viewport) {
    const bool viewport_changed = viewport != previewViewport_;
    if (!force_refresh && current_target == previewTarget_ && !viewport_changed) {
        return;
    }

    const bool target_changed = current_target != previewTarget_;
    previewCancellation_.cancel();
    previewCancellation_.reset();
    previewTarget_ = current_target;
    previewViewport_ = viewport;
    ++previewGeneration_;

    if (!current_target.has_value()) {
        previewModel_ = app::PreviewIdleState{};
        previewOffset_ = {};
        return;
    }

    if (target_changed) {
        previewOffset_ = {};
    }
    previewModel_ = app::PreviewLoadingState{.target = *current_target};

    const auto runtime = explorer_->services().runtime;
    const auto preview_service = explorer_->services().preview;
    const auto token = previewCancellation_.token();
    const auto generation = previewGeneration_;
    const auto& target = *current_target;

    // Take a single config snapshot so maxLines and maxLineLength come from the
    // same configuration version (prevents races with concurrent setConfig()).
    const auto cfg = core::global_config().config();

    // Negative maxLines means "auto": read generously so the display side
    // (which resolves negative to terminal height) has enough data.
    const int config_max_lines = cfg.preview.maxLines;
    const int max_read_lines = config_max_lines < 0 ? 500 : std::max(1, config_max_lines);
    const int max_line_length = std::max(1, cfg.preview.maxLineLength);
    const auto max_text_bytes = static_cast<std::size_t>(std::max(1, cfg.preview.maxTextBytes));
    const int chunk_lines = std::max(1, cfg.preview.chunkLines);
    const auto header_bytes = static_cast<std::size_t>(std::max(1, cfg.preview.headerBytes));
    const auto debounce = std::chrono::milliseconds(std::max(0, cfg.preview.debounceMs));
    const auto offset = previewOffset_;

    runtime->scheduleAfter(debounce, [this, runtime, preview_service = preview_service, token,
                                      generation, target = target, max_read_lines, max_line_length,
                                      max_text_bytes, chunk_lines, header_bytes, viewport,
                                      offset]() mutable {
        if (token.isCancellationRequested()) {
            return;
        }

        asio::co_spawn(
            runtime->ioExecutor(),
            [this, runtime, preview_service = std::move(preview_service), token, generation,
             target = std::move(target), max_read_lines, max_line_length, max_text_bytes,
             chunk_lines, header_bytes, viewport, offset]() -> core::Task<void> {
                auto result = co_await preview_service->loadPreview(PreviewRequest{
                    .target = target,
                    .maxLines = max_read_lines,
                    .maxLineLength = max_line_length,
                    .maxBytes = max_text_bytes,
                    .chunkLines = chunk_lines,
                    .headerBytes = header_bytes,
                    .viewport = viewport,
                    .offset = offset,
                    .cancellation = token,
                });

                runtime->postToUi([this, generation, target, result = std::move(result)]() mutable {
                    if (generation != previewGeneration_) {
                        return;
                    }

                    if (!result) {
                        previewModel_ = app::PreviewErrorState{
                            .target = target,
                            .message = result.error().message(),
                        };
                        return;
                    }

                    previewModel_ = app::PreviewReadyState{
                        .target = target,
                        .lines = std::move(result->lines),
                        .content = std::move(result->content),
                        .mimeType = std::move(result->mimeType),
                        .truncated = result->truncated,
                        .canScrollUp = result->canScrollUp,
                        .canScrollDown = result->canScrollDown,
                        .diagnostic = std::move(result->diagnostic),
                    };
                });
            },
            asio::detached);
    });
}

void ExplorerPreviewController::scroll(int line_delta) {
    if (line_delta == 0 || !previewTarget_.has_value()) {
        return;
    }

    const auto* ready = std::get_if<app::PreviewReadyState>(&previewModel_);
    if (line_delta > 0 && ready != nullptr && !ready->canScrollDown) {
        return;
    }
    if (line_delta < 0 && ready != nullptr && !ready->canScrollUp) {
        return;
    }

    const auto magnitude = static_cast<std::uint64_t>(std::abs(line_delta));
    if (ready != nullptr && std::holds_alternative<app::HexDumpPreview>(ready->content)) {
        const std::uint64_t bytes_per_line = previewViewport_.width >= 96 ? 16U : 8U;
        const std::uint64_t byte_delta = magnitude * bytes_per_line;
        if (line_delta > 0) {
            previewOffset_.byte += byte_delta;
        } else {
            previewOffset_.byte =
                previewOffset_.byte > byte_delta ? previewOffset_.byte - byte_delta : 0;
        }
    } else if (line_delta > 0) {
        previewOffset_.row += magnitude;
    } else {
        previewOffset_.row = previewOffset_.row > magnitude ? previewOffset_.row - magnitude : 0;
    }

    sync(previewTarget_, true, previewViewport_);
}

}  // namespace expp::app
