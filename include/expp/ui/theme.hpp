#ifndef EXPP_UI_THEME_CPP
#define EXPP_UI_THEME_CPP

#include "expp/core/config.hpp"
#include "expp/core/filesystem.hpp"

#include <ftxui/screen/color.hpp>

#include <cstdint>

namespace expp::ui {
/**
 * @brief Converts hex color to ftxui::Color
 * @param hex RGB hex value (e.g., 0xFF5555)
 * @return `ftxui::Color`
 */
[[nodiscard]] inline ftxui::Color hex_to_color(std::uint32_t hex) noexcept {
    return {
        
        static_cast<std::uint8_t>((hex >> 16) & 0xFF),  // Red // NOLINT
        static_cast<std::uint8_t>((hex >> 8) & 0xFF),   // Green // NOLINT
        static_cast<std::uint8_t>(hex & 0xFF)           // Blue // NOLINT
    };
}

/**
 * @brief Theme provider for UI components
 *
 * Caches converted colors for performance.
 */
class Theme {
public:
    explicit Theme(const core::ColorTheme& config_theme = {});

    /**
     * @brief Gets color for a file type
     * @param type File type to get color for
     * @return ftxui::Color for the file type
     */
    [[nodiscard]] ftxui::Color getFileTypeColor(core::filesystem::FileType type) const noexcept;

    [[nodiscard]] std::string_view getFileTypeIcon(const core::filesystem::FileEntry& entry) const noexcept;

    /**
     * @brief Gets color for a file entry (considers hidden status)
     * @param entry File entry to get color for
     * @return ftxui::Color for the entry
     */
    [[nodiscard]] ftxui::Color getFileEntryColor(core::filesystem::FileEntry entry) const noexcept;

    [[nodiscard]] ftxui::Color getBackgroundColor() const noexcept { return backgroundColor_; }
    [[nodiscard]] ftxui::Color getForegroundColor() const noexcept { return foregroundColor_; }
    [[nodiscard]] ftxui::Color getSelectionColor() const noexcept { return selectionColor_; }
    [[nodiscard]] ftxui::Color getBorderColor() const noexcept { return borderColor_; }
    [[nodiscard]] ftxui::Color getStatusBarColor() const noexcept { return statusBarColor_; }
    [[nodiscard]] ftxui::Color getSearchHighlightColor() const noexcept { return searchHighlightColor_; }

    /**
     * @brief Reloads theme from configuration
     * @param config New color theme configuration
     */
    void reload(const core::ColorTheme& config);

private:
    // File type colors
    ftxui::Color directoryColor_;
    ftxui::Color regularFileColor_;
    ftxui::Color executableColor_;
    ftxui::Color symlinkColor_;
    ftxui::Color archiveColor_;
    ftxui::Color sourceCodeColor_;
    ftxui::Color imageColor_;
    ftxui::Color documentColor_;
    ftxui::Color configColor_;
    ftxui::Color hiddenColor_;

    // UI color
    ftxui::Color backgroundColor_;
    ftxui::Color foregroundColor_;
    ftxui::Color selectionColor_;
    ftxui::Color borderColor_;
    ftxui::Color statusBarColor_;
    ftxui::Color searchHighlightColor_;
};

/**
 * @brief Gets the global theme instance
 * @return Reference to global Theme
 */
[[nodiscard]] Theme& global_theme();

}  // namespace expp::ui

#endif  // EXPP_UI_THEME_CPP