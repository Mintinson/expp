/**
 * @file explorer_preview_controller.hpp
 * @brief Preview-loading coordinator for explorer selection changes.
 *
 * `ExplorerPreviewController` keeps preview rendering responsive by:
 * - reloading only when the preview target changes (or when forced),
 * - canceling stale in-flight work before starting a newer request,
 * - exposing a single `ui::PreviewModel` state machine consumed by the renderer.
 */

#ifndef EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP
#define EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP

#include "expp/app/explorer.hpp"
#include "expp/core/task.hpp"
#include "expp/ui/components.hpp"

#include <filesystem>
#include <optional>

namespace expp::app {

/**
 * @brief Maintains preview state for the currently focused explorer entry.
 *
 * The controller is intentionally small and synchronous from the caller's
 * perspective. It still uses cancellation primitives so the implementation can
 * evolve to async execution without changing view-level call sites.
 */
class ExplorerPreviewController {
public:
    /**
     * @brief Constructs a preview controller bound to an explorer instance.
     */
    explicit ExplorerPreviewController(std::shared_ptr<Explorer> explorer) : explorer_(std::move(explorer)) {}

    /**
     * @brief Returns the latest preview model snapshot for rendering.
     */
    [[nodiscard]] const ui::PreviewModel& model() const { return previewModel_; }

    /**
     * @brief Synchronizes preview state with the current selection target.
     * @param current_target The currently selected path, or `std::nullopt` when nothing is selected.
     * @param force_refresh When true, reloads even if target did not change.
     */
    void sync(const std::optional<std::filesystem::path>& current_target, bool force_refresh = false);

private:
    /// Explorer facade used to access preview and filesystem services.
    std::shared_ptr<Explorer> explorer_;
    /// Scheduler used to execute preview loading tasks.
    core::InlineScheduler scheduler_;
    /// Cancellation source guarding preview task churn during rapid navigation.
    core::CancellationSource previewCancellation_;

    /// Last published preview model.
    ui::PreviewModel previewModel_{ui::PreviewIdleState{}};
    /// Last preview target used to suppress redundant reloads.
    std::optional<std::filesystem::path> previewTarget_;

    /**
     * @brief Loads preview data for a specific target and updates `previewModel_`.
     */
    void loadPreview(const std::filesystem::path& target);
};

}  // namespace expp::app

#endif  // EXPP_EXPLORER_PREVIEW_CONTROLLER_HPP
