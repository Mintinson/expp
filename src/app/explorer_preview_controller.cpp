/**
 * @file explorer_preview_controller.cpp
 * @brief Implementation of preview target synchronization and loading.
 */

#include "expp/app/explorer_preview_controller.hpp"

#include "expp/core/config.hpp"

namespace expp::app {

void ExplorerPreviewController::sync(const std::optional<std::filesystem::path>& current_target, bool force_refresh) {
    // Fast path: avoid recomputing preview if the target is unchanged.
    if (!force_refresh && current_target == previewTarget_) {
        return;
    }

    // Selection can move faster than preview loading; cancel stale work first.
    previewCancellation_.cancel();
    previewCancellation_.reset();
    previewTarget_ = current_target;

    // No selection means there is nothing to preview.
    if (!current_target.has_value()) {
        previewModel_ = ui::PreviewIdleState{};
        return;
    }

    loadPreview(*current_target);
}

void ExplorerPreviewController::loadPreview(const std::filesystem::path& target) {
    // Publish loading state immediately so UI feedback is instant.
    previewModel_ = ui::PreviewLoadingState{.target = target};

    const int max_lines = std::max(1, core::global_config().config().preview.maxLines);

    const auto result = scheduler_.execute(
        core::TaskContext{
            .name = "preview.load",
            .priority = core::TaskPriority::UserVisible,
            .taskClass = core::TaskClass::Micro,
            .cancellation = previewCancellation_.token(),
        },
        [&](const core::TaskContext& context) {
            return explorer_->services().preview->loadPreview({
                .target = target,
                .maxLines = max_lines,
                .cancellation = context.cancellation,
            });
        });

    // If a newer sync canceled this request, do not publish stale output.
    if (previewCancellation_.token().isCancellationRequested()) {
        return;
    }

    if (!result) {
        // Preserve the target in error state so UI can show contextual messages.
        previewModel_ = ui::PreviewErrorState{
            .target = target,
            .message = result.error().message(),
        };
        return;
    }

    // Ready state is rendered directly by the composer/preview component.
    previewModel_ = ui::PreviewReadyState{
        .target = target,
        .lines = result->lines,
    };
}

}  // namespace expp::app