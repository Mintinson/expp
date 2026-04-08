
#ifndef EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP
#define EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP

#include "expp/app/explorer.hpp"
#include "expp/core/task.hpp"
#include "expp/ui/components.hpp"

#include <filesystem>
#include <optional>

namespace expp::app {
class ExplorerPreviewController {
public:
    explicit ExplorerPreviewController(std::shared_ptr<Explorer> explorer) : explorer_(std::move(explorer)) {}

    [[nodiscard]] const ui::PreviewModel& model() const { return previewModel_; }

    void sync(const std::optional<std::filesystem::path>& current_target, bool force_refresh = false);

private:
    std::shared_ptr<Explorer> explorer_;
    core::InlineScheduler scheduler_;
    core::CancellationSource previewCancellation_;

    ui::PreviewModel previewModel_{ui::PreviewIdleState{}};
    std::optional<std::filesystem::path> previewTarget_;

    void loadPreview(const std::filesystem::path& target);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP
