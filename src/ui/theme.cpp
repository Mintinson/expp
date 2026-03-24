#include "expp/ui/theme.hpp"

namespace expp::ui {

Theme::Theme(const core::ColorTheme& config_theme, const core::IconConfig& icon_config) {
    reload(config_theme);
    reloadIcons(icon_config);
}

ftxui::Color Theme::getFileEntryColor(core::filesystem::FileEntry entry) const noexcept {
    if (entry.isHidden) {
        return hiddenColor_;
    }
    return getFileTypeColor(entry.type);
}

ftxui::Color Theme::getFileTypeColor(core::filesystem::FileType type) const noexcept {
    switch (type) {
        case core::filesystem::FileType::Directory:
            return directoryColor_;
        case core::filesystem::FileType::RegularFile:
            return regularFileColor_;
        case core::filesystem::FileType::Executable:
            return executableColor_;
        case core::filesystem::FileType::Symlink:
            return symlinkColor_;
        case core::filesystem::FileType::Archive:
            return archiveColor_;
        case core::filesystem::FileType::SourceCode:
            return sourceCodeColor_;
        case core::filesystem::FileType::Image:
            return imageColor_;
        case core::filesystem::FileType::Document:
            return documentColor_;
        case core::filesystem::FileType::Config:
            return configColor_;
        case core::filesystem::FileType::Unknown:
            return regularFileColor_;
        default:
            return regularFileColor_;
    }
}

std::string_view Theme::getFileTypeIcon(const core::filesystem::FileEntry& entry) const noexcept {
    if (entry.type == core::filesystem::FileType::Directory) {
        if (auto it = iconMap_.find("folder"); it != iconMap_.end()) {
            return it->second;
        }
        return defaultFolderIcon_;
    }
    if (entry.type == core::filesystem::FileType::Executable) {
        if (auto it = iconMap_.find("exe"); it != iconMap_.end()) {
            return it->second;
        }
        return defaultFileIcon_;
    }
    if (entry.type == core::filesystem::FileType::Symlink) {
        if (auto it = iconMap_.find("link"); it != iconMap_.end()) {
            return it->second;
        }
        return defaultFileIcon_;
    }
    if (auto it = iconMap_.find(entry.extension()); it != iconMap_.end()) {
        return it->second;
    }
    if (auto it = iconMap_.find("default"); it != iconMap_.end()) {
        return it->second;
    }
    return defaultFileIcon_;
}

void Theme::reload(const core::ColorTheme& config) {
    directoryColor_ = hex_to_color(config.directory);
    regularFileColor_ = hex_to_color(config.regularFile);
    executableColor_ = hex_to_color(config.executable);
    symlinkColor_ = hex_to_color(config.symlink);
    archiveColor_ = hex_to_color(config.archive);
    sourceCodeColor_ = hex_to_color(config.sourceCode);
    imageColor_ = hex_to_color(config.image);
    documentColor_ = hex_to_color(config.document);
    configColor_ = hex_to_color(config.config);
    hiddenColor_ = hex_to_color(config.hidden);

    backgroundColor_ = hex_to_color(config.background);
    foregroundColor_ = hex_to_color(config.foreground);
    selectionColor_ = hex_to_color(config.selection);
    borderColor_ = hex_to_color(config.border);
    statusBarColor_ = hex_to_color(config.statusBar);
    searchHighlightColor_ = hex_to_color(config.searchHighlight);
}

void Theme::reloadIcons(const core::IconConfig& iconConfig) {
    iconMap_ = iconConfig.icons;
    defaultFileIcon_ = iconConfig.defaultFileIcon;
    defaultFolderIcon_ = iconConfig.defaultFolderIcon;
}

// Global theme instance
Theme& global_theme() {
    static Theme instance;
    return instance;
}

}  // namespace expp::ui
