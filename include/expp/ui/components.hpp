/**
 * @file components.hpp
 * @brief Reusable FTXUI components for the file explorer
 *
 * These components provide a modular, composable architecture for the UI:
 * - FileListComponent: Displays lists of files with proper styling
 * - PreviewComponent: Shows file content previews
 * - StatusBarComponent: Display status information
 * - DialogComponent: Base for modals and confirmations
 *
 * EXTENSION POINT: Custom components can be created following these patterns
 *
 * @copyright Copyright (c) 2026
 */

#ifndef EXPP_UI_COMPONENTS_HPP
#define EXPP_UI_COMPONENTS_HPP

#include "expp/core/filesystem.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace expp::ui {

// ============================================================================
// FileListComponent: Renders a list of files with styling
// ============================================================================

/**
 * @brief Configuration for FileListComponent
 */
struct FileListConfig {
    const Theme* theme{&global_theme()};
    bool showIcons{true};
    bool boldDirectories{true};
    bool enableHighlight{true};
    std::string selectionPrefix{"➤ "};  // support unicode icons or custom markers
    std::string normalPrefix{"  "};
};

class FileListComponent {
public:
    explicit FileListComponent(const FileListConfig& config = {});
    ~FileListComponent();

    // Non-copyable, movable
    FileListComponent(FileListComponent&&) noexcept;
    FileListComponent& operator=(FileListComponent&&) noexcept;
    FileListComponent(const FileListComponent&) = delete;
    FileListComponent& operator=(const FileListComponent&) = delete;

    /**
     * @brief Renders the file list
     * @param entries File entries to display
     * @param selected Index of selected entry
     * @param searchMatches Optional indices of search matches
     * @param currentMatchIndex Currently highlighted match index (-1 if none)
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element render(std::span<const core::filesystem::FileEntry> entries,
                                        int selected,
                                        const std::vector<int>& search_matches = {},
                                        int current_match_index = -1) const;

    /**
     * @brief Updates configuration
     */
    void setConfig(FileListConfig config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// StatusBarComponent: Status and info bar
// ============================================================================

/**
 * @brief Status bar information
 */
// TODO: make it more generic and extensible for future features (e.g. git status, notifications)
struct StatusBarInfo {
    std::string currentPath;
    std::string keyBuffer;
    std::string searchStatus;
    std::string helpText{"j/k to move, h/l to navigate, q to quit"};
    bool showPath{true};
    bool showHelp{true};
};

/**
 * @brief Displays status bar at bottom of the screen
 */
class StatusBarComponent {
public:
    explicit StatusBarComponent(const Theme* theme = &global_theme());
    ~StatusBarComponent();

    // Non-copyable, movable
    StatusBarComponent(StatusBarComponent&&) noexcept;
    StatusBarComponent& operator=(StatusBarComponent&&) noexcept;
    StatusBarComponent(const StatusBarComponent&) = delete;
    StatusBarComponent& operator=(const StatusBarComponent&) = delete;

    /**
     * @brief Renders the status bar
     * @param info Status information to display
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element render(const StatusBarInfo& info) const;

    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// DialogComponent: Modal dialogs
// ============================================================================

/**
 * @brief Dialog button configuration
 */
struct DialogButton {
    std::string label;
    ftxui::Color color{ftxui::Color::White};
    bool primary{false};  // primary button gets focused by default
};

/**
 * @brief Dialog types
 */
enum class DialogType : std::uint8_t {
    Confirmation,
    Input,
    Message
};

/**
 * @brief Dialog configuration
 */
struct DialogConfig {
    DialogType type{DialogType::Message};
    std::string title;
    std::string message;
    std::vector<DialogButton> buttons;
    int width{50};
    const Theme* theme{&global_theme()};
};

/**
 * @brief Generic dialog component
 *
 * Can be used for:
 * - Confirmation dialogs (delete, trash)
 * - Input dialogs (create, rename, search)
 * - Message dialogs (errors, info)
 */
class DialogComponent {
public:
    DialogComponent();
    ~DialogComponent();

    DialogComponent(DialogComponent&&) noexcept;
    DialogComponent& operator=(DialogComponent&&) noexcept;
    DialogComponent(const DialogComponent&) = delete;
    DialogComponent& operator=(const DialogComponent&) = delete;

    /**
     * @brief Renders a confirmation dialog
     * @param title Dialog title
     * @param message Confirmation message
     * @param target_name Name of target (e.g., file being deleted)
     * @param target_color Color to highlight target name
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element renderConfirmation(const std::string& title,
                                                    const std::string& message,
                                                    const std::string& target_name,
                                                    ftxui::Color target_color) const;

    /**
     * @brief Renders an input dialog with a text input component
     * @param title Dialog title
     * @param message Prompt message
     * @param input_component FTXUI input component
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element renderInput(const std::string& title,
                                             const std::string& message,
                                             ftxui::Element input_component) const;

    /**
     * @brief Renders a message dialog
     * @param title Dialog title
     * @param message Message content
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element renderMessage(const std::string& title, const std::string& message) const;

    void setTheme(const Theme* theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// PanelComponent: Three-column panel layout
// ============================================================================

/**
 * @brief Configuration for the three-column panel layout
 */
struct PanelConfig {
    bool showParent{true};
    bool showPreview{true};
    int parentWidth{30};
    int previewWidth{60};
    const Theme* theme{&global_theme()};
};

/**
 * @brief Three-column panel layout component
 *
 * Manages the parent | current | preview layout
 */
class PanelComponent {
public:
    explicit PanelComponent(const PanelConfig& config = {});
    ~PanelComponent();

    PanelComponent(PanelComponent&&) noexcept;
    PanelComponent& operator=(PanelComponent&&) noexcept;
    PanelComponent(const PanelComponent&) = delete;
    PanelComponent& operator=(const PanelComponent&) = delete;

    void setConfig(const PanelConfig& config);

    /**
     * @brief Renders the three-column panel layout
     * @param parent_title Title for parent column
     * @param parent_content Content for parent column
     * @param current_title Title for current column
     * @param current_content Content for current column
     * @param preview_title Title for preview column
     * @param preview_content Content for preview column
     * @return FTXUI Element
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

// ============================================================================
// PreviewComponent: File content preview
// ============================================================================

/**
 * @brief Configuration for PreviewComponent
 */
struct PreviewConfig {
    const Theme* theme{&global_theme()};
    int maxLines{50};
    std::string emptyMessage{"[Empty]"};
    std::string errorPrefix{"[Error: "};
};

/**
 * @brief Displays file content preview
 *
 * Handles:
 * - Text file previews
 * - Directory content lists
 * - Binary file indicators
 * - Error messages
 * - TODO: future support for image previews, git status, etc.
 * - TODO: using async component with loading state for large files or slow operations
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
     * @brief Renders preview for the selected file
     * @param entry File entry to preview
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element render(const core::filesystem::FileEntry& entry) const;

    /**
     * @brief Renders custom preview lines
     * @param lines Custom preview lines
     * @return FTXUI Element
     */
    [[nodiscard]] ftxui::Element renderLines(const std::vector<std::string>& lines) const;

    void setConfig(const PreviewConfig& config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace expp::ui

#endif  // EXPP_UI_COMPONENTS_HPP