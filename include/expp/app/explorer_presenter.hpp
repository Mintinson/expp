#ifndef EXPP_APP_EXPLORER_PRESENTER_HPP
#define EXPP_APP_EXPLORER_PRESENTER_HPP

/**
 * @file explorer_presenter.hpp
 * @brief Declarative screen-model types for explorer rendering.
 *
 * The presenter translates domain state plus transient overlay state into a
 * compact rendering model so view code stays focused on FTXUI composition.
 */

#include "expp/app/explorer.hpp"
#include "expp/ui/help_menu_model.hpp"
#include "expp/ui/key_handler.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace expp::app {

namespace fs = std::filesystem;

/**
 * @brief Preview state when no target is selected.
 */
struct PreviewIdle {};

/**
 * @brief Preview state while a request is in flight.
 */
struct PreviewLoading {
    fs::path target;
};

/**
 * @brief Preview state after content has been loaded successfully.
 */
struct PreviewReady {
    fs::path target;
    std::vector<std::string> lines;
};

/**
 * @brief Preview state after a request fails.
 */
struct PreviewError {
    fs::path target;
    std::string message;
};

/**
 * @brief Discriminated preview model consumed by the UI layer.
 */
using PreviewModel = std::variant<PreviewIdle, PreviewLoading, PreviewReady, PreviewError>;

/**
 * @brief Overlay state for the help dialog.
 */
struct HelpOverlayState {
    bool filterMode{false};
    std::string filterText;
    ui::HelpMenuModel model;
    ui::HelpViewport viewport;
};

/**
 * @brief Overlay state for path-entry navigation.
 */
struct DirectoryJumpOverlayState {
    std::string input;
};

/**
 * @brief Overlay state for file or directory creation.
 */
struct CreateOverlayState {
    std::string input;
};

/**
 * @brief Overlay state for renaming the selected entry.
 */
struct RenameOverlayState {
    std::string input;
};

/**
 * @brief Overlay state for text search entry.
 */
struct SearchOverlayState {
    std::string input;
};

/**
 * @brief Overlay state for permanent deletion confirmation.
 */
struct DeleteConfirmOverlayState {
    std::string targetName;
    int selectionCount{0};
};

/**
 * @brief Overlay state for trash confirmation.
 */
struct TrashConfirmOverlayState {
    std::string targetName;
    int selectionCount{0};
};

/**
 * @brief All mutually exclusive overlays that the explorer view can show.
 */
using ExplorerOverlayState = std::variant<std::monostate,
                                          HelpOverlayState,
                                          DirectoryJumpOverlayState,
                                          CreateOverlayState,
                                          RenameOverlayState,
                                          SearchOverlayState,
                                          DeleteConfirmOverlayState,
                                          TrashConfirmOverlayState>;

/**
 * @brief List-specific rendering state after viewport projection.
 *
 * All indices in this model are relative to the currently visible slice so
 * rendering code can operate without repeatedly converting absolute indices.
 */
struct ExplorerListModel {
    /// Absolute offset into the full entry list.
    int offset{0};
    /// Absolute end offset for the visible slice.
    int visibleEnd{0};
    /// Selection index relative to the visible slice.
    int selectedIndex{-1};
    /// Search matches relative to the visible slice.
    std::vector<int> searchMatches;
    /// Active search-match index inside `searchMatches`.
    int currentMatchIndex{-1};
    /// Visual selections relative to the visible slice.
    std::vector<int> visualSelectedIndices;
};

/**
 * @brief Complete rendering model for the main explorer screen.
 *
 * The presenter computes this structure from domain state and overlay/mode
 * context. The composer then turns it into FTXUI elements without additional
 * domain lookups.
 */
struct ExplorerScreenModel {
    ExplorerListModel currentList;
    std::string parentTitle;
    std::string currentTitle;
    std::string statusPath;
    std::string searchStatus;
    std::string helpText;
    std::string keyBuffer;
};

/**
 * @brief Presenter that derives screen models from explorer state.
 *
 * `ExplorerPresenter` is intentionally side-effect free. It performs
 * projection/mapping only and does not mutate explorer state.
 */
class ExplorerPresenter {
public:
    /// Default paging step used by command dispatch and help text.
    static constexpr int kPageStep = 10;

    /**
     * @brief Chooses a stable screen height from the interactive screen and terminal fallback.
     *
     * Some platforms report a placeholder screen height before the first render loop iteration.
     * This helper keeps viewport initialization from collapsing to a one-row list on startup.
     */
    [[nodiscard]] static int resolveScreenRows(int screen_rows, int fallback_rows) noexcept;

    /**
     * @brief Computes the list viewport height from the current terminal height.
     *
     * The returned value is clamped to at least one row.
     */
    [[nodiscard]] static int listViewportRows(int screen_rows) noexcept;
    /**
     * @brief Computes the help overlay viewport height from the terminal height.
     *
     * The returned value is clamped to a readable range to avoid oversized
     * overlays on very large terminals and unusable overlays on tiny terminals.
     */
    [[nodiscard]] static int helpViewportRows(int screen_rows) noexcept;

    /**
     * @brief Projects explorer state into a render-friendly screen model.
     * @param state Domain explorer state.
     * @param overlay Active overlay state (influences help/status text).
     * @param key_buffer Pending key sequence buffer to surface in status UI.
     * @param mode Active key-handler mode (normal/visual/etc.).
     * @return Fully projected model for one render frame.
     */
    [[nodiscard]] ExplorerScreenModel present(const ExplorerState& state,
                                              const ExplorerOverlayState& overlay,
                                              std::string_view key_buffer,
                                              ui::Mode mode) const;
};

}  // namespace expp::app

#endif  // EXPP_APP_EXPLORER_PRESENTER_HPP
