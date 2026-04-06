/**
 * @file components.hpp
 * @brief Reusable FTXUI view components and rendering models for the explorer UI.
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_UI_COMPONENTS_HPP
#define EXPP_UI_COMPONENTS_HPP

#include "expp/core/filesystem.hpp"
#include "expp/ui/help_menu_model.hpp"
#include "expp/ui/key_handler.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace expp::ui {

/**
 * @brief Configuration for file-list rendering.
 */
struct FileListConfig {
    /// Theme used to derive colors and icons.
    const Theme* theme{&global_theme()};
    /// Whether file-type icons are shown before names.
    bool showIcons{true};
    /// Whether directory entries are rendered in bold.
    bool boldDirectories{true};
    /// Whether search and selection highlighting is applied.
    bool enableHighlight{true};
    /// Prefix inserted before the selected row.
    std::string selectionPrefix{"➤ "};
    /// Prefix inserted before non-selected rows.
    std::string normalPrefix{"  "};
};

/**
 * @brief Renders a scrollable list of filesystem entries.
 */
class FileListComponent {
public:
    explicit FileListComponent(const FileListConfig& config = {});
    ~FileListComponent();

    FileListComponent(FileListComponent&&) noexcept;
    FileListComponent& operator=(FileListComponent&&) noexcept;
    FileListComponent(const FileListComponent&) = delete;
    FileListComponent& operator=(const FileListComponent&) = delete;

    /**
     * @brief Renders the visible file list.
     * @param entries Visible entry slice to render.
     * @param selected Selection index relative to `entries`.
     * @param search_matches Relative indices that should be marked as search matches.
     * @param current_match_index Active entry inside `search_matches`.
     * @param selected_indices Relative indices participating in visual selection.
     * @return FTXUI element for the file list.
     */
    [[nodiscard]] ftxui::Element render(std::span<const core::filesystem::FileEntry> entries,
                                        int selected,
                                        const std::vector<int>& search_matches = {},
                                        int current_match_index = -1,
                                        const std::vector<int>& selected_indices = {}) const;
    /**
     * @brief Replaces the rendering configuration.
     */
    void setConfig(FileListConfig config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Data rendered in the explorer status bar.
 */
struct StatusBarInfo {
    /// Current filesystem path shown on the left side.
    std::string currentPath;
    /// Pending key sequence buffer shown to the user.
    std::string keyBuffer;
    /// Search and sort status summary.
    std::string searchStatus;
    /// Default help text for the current mode.
    std::string helpText{"j/k to move, h/l to navigate, q to quit"};
    /// Whether the path segment is rendered.
    bool showPath{true};
    /// Whether the help text segment is rendered.
    bool showHelp{true};
};

/**
 * @brief Displays the bottom status line for the explorer.
 */
class StatusBarComponent {
public:
    explicit StatusBarComponent(const Theme* theme = &global_theme());
    ~StatusBarComponent();

    StatusBarComponent(StatusBarComponent&&) noexcept;
    StatusBarComponent& operator=(StatusBarComponent&&) noexcept;
    StatusBarComponent(const StatusBarComponent&) = delete;
    StatusBarComponent& operator=(const StatusBarComponent&) = delete;

    /**
     * @brief Renders the status bar.
     */
    [[nodiscard]] ftxui::Element render(const StatusBarInfo& info) const;
    /**
     * @brief Changes the theme used by the status bar.
     */
    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Severity levels for transient toast notifications.
 */
enum class ToastSeverity : std::uint8_t {
    Info,
    Success,
    Warning,
    Error,
};

/**
 * @brief Data rendered by the toast component.
 */
struct ToastInfo {
    ToastSeverity severity{ToastSeverity::Info};
    std::string message;
};

/**
 * @brief Renders transient notification toasts.
 */
class ToastComponent {
public:
    explicit ToastComponent(const Theme* theme = &global_theme());
    ~ToastComponent();

    ToastComponent(ToastComponent&&) noexcept;
    ToastComponent& operator=(ToastComponent&&) noexcept;
    ToastComponent(const ToastComponent&) = delete;
    ToastComponent& operator=(const ToastComponent&) = delete;

    /**
     * @brief Renders a toast message.
     */
    [[nodiscard]] ftxui::Element render(const ToastInfo& toast) const;
    /**
     * @brief Changes the theme used by the toast renderer.
     */
    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Builds help entries by joining command metadata with key bindings.
 *
 * Bindings without a matching registered action are omitted.
 */
[[nodiscard]] std::vector<HelpEntry> build_help_entries(std::span<const Action> actions,
                                                        std::span<const KeyBinding> bindings);

/**
 * @brief Renders the keyboard shortcut help overlay.
 */
class HelpMenuComponent {
public:
    explicit HelpMenuComponent(const Theme* theme = &global_theme());
    ~HelpMenuComponent();

    HelpMenuComponent(HelpMenuComponent&&) noexcept;
    HelpMenuComponent& operator=(HelpMenuComponent&&) noexcept;
    HelpMenuComponent(const HelpMenuComponent&) = delete;
    HelpMenuComponent& operator=(const HelpMenuComponent&) = delete;

    /**
     * @brief Renders the help overlay from the filtered help model.
     * @param model Filtered help data to display.
     * @param filter_mode Whether the filter input is currently active.
     * @param viewport Selection and scroll state for the overlay body.
     * @return FTXUI element for the help overlay.
     */
    [[nodiscard]] ftxui::Element render(const HelpMenuModel& model, bool filter_mode, HelpViewport viewport) const;
    /**
     * @brief Changes the theme used by the help overlay.
     */
    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Visual configuration for a dialog button.
 */
struct DialogButton {
    std::string label;
    ftxui::Color color{ftxui::Color::White};
    bool primary{false};
};

/**
 * @brief Supported dialog presentation styles.
 */
enum class DialogType : std::uint8_t {
    Confirmation,
    Input,
    Message
};

/**
 * @brief Generic dialog configuration container.
 */
struct DialogConfig {
    DialogType type{DialogType::Message};
    std::string title;
    std::string message;
    std::vector<DialogButton> buttons;
    int width{};
    const Theme* theme{&global_theme()};
};

/**
 * @brief Renders modal dialog variants used by the explorer.
 */
class DialogComponent {
public:
    DialogComponent();
    ~DialogComponent();

    DialogComponent(DialogComponent&&) noexcept;
    DialogComponent& operator=(DialogComponent&&) noexcept;
    DialogComponent(const DialogComponent&) = delete;
    DialogComponent& operator=(const DialogComponent&) = delete;

    /// Renders a confirmation dialog for destructive actions.
    [[nodiscard]] ftxui::Element renderConfirmation(const std::string& title,
                                                    const std::string& message,
                                                    const std::string& target_name,
                                                    ftxui::Color target_color) const;

    /// Renders an input dialog around an existing FTXUI input element.
    [[nodiscard]] ftxui::Element renderInput(const std::string& title,
                                             const std::string& message,
                                             ftxui::Element input_component) const;

    /// Renders a simple informational message dialog.
    [[nodiscard]] ftxui::Element renderMessage(const std::string& title, const std::string& message) const;
    /// Changes the theme used by dialogs.
    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Layout configuration for the three-panel explorer view.
 */
struct PanelConfig {
    /// Whether the parent directory panel is shown.
    bool showParent{true};
    /// Whether the preview panel is shown.
    bool showPreview{true};
    /// Fixed width for the parent panel when enabled.
    int parentWidth{};
    /// Fixed width for the preview panel when enabled.
    int previewWidth{};
    const Theme* theme{&global_theme()};
};

/**
 * @brief Renders the parent/current/preview panel layout.
 */
class PanelComponent {
public:
    explicit PanelComponent(const PanelConfig& config = {});
    ~PanelComponent();

    PanelComponent(PanelComponent&&) noexcept;
    PanelComponent& operator=(PanelComponent&&) noexcept;
    PanelComponent(const PanelComponent&) = delete;
    PanelComponent& operator=(const PanelComponent&) = delete;

    /**
     * @brief Replaces the panel layout configuration.
     */
    void setConfig(const PanelConfig& config);

    /**
     * @brief Renders the configured panel layout.
     */
    [[nodiscard]] ftxui::Element render(const std::string& parent_title,
                                        ftxui::Element parent_content,
                                        const std::string& current_title,
                                        ftxui::Element current_content,
                                        const std::string& preview_title,
                                        ftxui::Element preview_content) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Preview rendering configuration.
 */
struct PreviewConfig {
    const Theme* theme{&global_theme()};
    /// Maximum number of preview lines to display.
    int maxLines{};
    /// Message rendered when no preview is available.
    std::string emptyMessage{"[Empty]"};
    /// Prefix used for preview error output.
    std::string errorPrefix{"[Error: "};
};

/// Preview state when there is no active preview target.
struct PreviewIdleState {};

/// Preview state while content is being loaded.
struct PreviewLoadingState {
    std::filesystem::path target;
};

/// Preview state after successful content loading.
struct PreviewReadyState {
    std::filesystem::path target;
    std::vector<std::string> lines;
};

/// Preview state after a loading error.
struct PreviewErrorState {
    std::filesystem::path target;
    std::string message;
};

/**
 * @brief Preview state model consumed by the preview component.
 */
using PreviewModel = std::variant<PreviewIdleState, PreviewLoadingState, PreviewReadyState, PreviewErrorState>;

/**
 * @brief Renders preview content from a precomputed preview model.
 */
class PreviewComponent {
public:
    explicit PreviewComponent(const PreviewConfig& config = {});
    ~PreviewComponent();

    PreviewComponent(PreviewComponent&&) noexcept;
    PreviewComponent& operator=(PreviewComponent&&) noexcept;
    PreviewComponent(const PreviewComponent&) = delete;
    PreviewComponent& operator=(const PreviewComponent&) = delete;

    /**
     * @brief Renders the current preview model.
     */
    [[nodiscard]] ftxui::Element render(const PreviewModel& model) const;
    /**
     * @brief Renders raw preview lines directly.
     */
    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines) const;
    /**
     * @brief Replaces the preview rendering configuration.
     */
    void setConfig(const PreviewConfig& config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace expp::ui

#endif  // EXPP_UI_COMPONENTS_HPP
