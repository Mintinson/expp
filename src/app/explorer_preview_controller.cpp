/**
 * @file explorer_preview_controller.cpp
 * @brief Implementation of preview target synchronization and loading.
 */

#include "expp/app/explorer_preview_controller.hpp"

#include "expp/core/config.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

namespace expp::app {

void ExplorerPreviewController::sync(const std::optional<std::filesystem::path>& current_target, bool force_refresh) {
    if (!force_refresh && current_target == previewTarget_) {
        return;
    }

    previewCancellation_.cancel();
    previewCancellation_.reset();
    previewTarget_ = current_target;
    ++previewGeneration_;

    if (!current_target.has_value()) {
        previewModel_ = app::PreviewIdleState{};
        return;
    }

    previewModel_ = app::PreviewLoadingState{.target = *current_target};

    const auto runtime = explorer_->services().runtime;
    const auto preview_service = explorer_->services().preview;
    const auto token = previewCancellation_.token();
    const auto generation = previewGeneration_;
    const auto target = *current_target;

    // Negative maxLines means "auto": read generously so the display side
    // (which resolves negative to terminal height) has enough data.
    const int config_max_lines = core::global_config().config().preview.maxLines;
    const int max_read_lines = config_max_lines < 0 ? 500 : std::max(1, config_max_lines);

    asio::co_spawn(runtime->ioExecutor(),
                   [this, runtime, preview_service, token, generation, target, max_read_lines]() -> core::Task<void> {
                       auto result = co_await preview_service->loadPreview(PreviewRequest{
                           .target = target,
                           .maxLines = max_read_lines,
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
                           };
                       });
                   },
                   asio::detached);
}

}  // namespace expp::app
