// explorer_preview_controller.cpp
#include "expp/app/explorer_preview_controller.hpp"

#include "expp/core/config.hpp"

namespace expp::app {

void ExplorerPreviewController::sync(const std::optional<std::filesystem::path>& current_target, bool force_refresh) {
    if (!force_refresh && current_target == previewTarget_) {
        return;
    }

    previewCancellation_.cancel();
    previewCancellation_.reset();
    previewTarget_ = current_target;

    if (!current_target.has_value()) {
        previewModel_ = ui::PreviewIdleState{};
        return;
    }

    loadPreview(*current_target);
}

void ExplorerPreviewController::loadPreview(const std::filesystem::path& target) {
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

    if (previewCancellation_.token().isCancellationRequested()) {
        return;
    }

    if (!result) {
        previewModel_ = ui::PreviewErrorState{
            .target = target,
            .message = result.error().message(),
        };
        return;
    }

    previewModel_ = ui::PreviewReadyState{
        .target = target,
        .lines = result->lines,
    };
}

}  // namespace expp::app