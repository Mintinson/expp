#include "expp/ui/theme.hpp"

namespace expp::ui {

expp::ui::Theme::Theme(const core::ColorTheme& config_theme) {
    reload(config_theme);
}

ftxui::Color expp::ui::Theme::getFileEntryColor(core::filesystem::FileEntry entry) const noexcept {
    if (entry.isHidden) {
        return hiddenColor_;
    }
    return getFileTypeColor(entry.type);
}

ftxui::Color expp::ui::Theme::getFileTypeColor(core::filesystem::FileType type) const noexcept {
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
        auto it = core::kIConMap.find("folder");
        return it != core::kIConMap.end() ? it->second : core::kDefaultDirectIcon;
    }
    if (entry.type == core::filesystem::FileType::Executable) {
        auto it = core::kIConMap.find("exe");
        return it != core::kIConMap.end() ? it->second : core::kDefaultFileIcon;
    }
    if (entry.type == core::filesystem::FileType::Symlink) {
        auto it = core::kIConMap.find("link");
        return it != core::kIConMap.end() ? it->second : core::kDefaultFileIcon;
    }
    if (auto it = core::kIConMap.find(entry.extension()); it != core::kIConMap.end()) {
        return it->second;
    }
    auto it2 = core::kIConMap.find("default");
    return it2 != core::kIConMap.end() ? it2->second : core::kDefaultFileIcon;
}

void expp::ui::Theme::reload(const core::ColorTheme& config) {
    // TODO: refactor it once c++26 reflection is available
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
// Global theme instance
Theme& global_theme() {
    static Theme instance;
    return instance;
}

}  // namespace expp::ui
