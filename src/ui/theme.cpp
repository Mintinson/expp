#include "expp/ui/theme.hpp"

#include "expp/core/config.hpp"
#include "expp/core/filesystem.hpp"
#include "expp/core/version_control.hpp"

#include <memory>
#include <utility>

namespace expp::ui {
Theme::Theme() : Theme(core::ColorTheme{}, core::IconConfig{}) {}

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
        // case core::filesystem::FileType::SourceCode:
        //     return sourceCodeColor_;
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

ftxui::Color Theme::getVersionStatusColor(core::VersionStatus status) const noexcept {
    switch (status) {
        case core::VersionStatus::Modified:
            return modifiedColor_;
        case core::VersionStatus::Added:
            return addedColor_;
        case core::VersionStatus::Deleted:
            return deletedColor_;
        case core::VersionStatus::Renamed:
            return renamedColor_;
        case core::VersionStatus::Copied:
            return copiedColor_;
        case core::VersionStatus::Untracked:
            return untrackedColor_;
        case core::VersionStatus::Ignored:
            return ignoredColor_;
        case core::VersionStatus::Conflicted:
            return conflictedColor_;
        case core::VersionStatus::Clean:
        default:
            return getForegroundColor();
    }
}

std::string_view Theme::getFileTypeIcon(const core::filesystem::FileEntry& entry) const noexcept {
    return core::resolve_icon(*iconConfig_, entry);
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

    modifiedColor_ = hex_to_color(config.modified);
    addedColor_ = hex_to_color(config.added);
    deletedColor_ = hex_to_color(config.deleted);
    renamedColor_ = hex_to_color(config.renamed);
    copiedColor_ = hex_to_color(config.copied);
    untrackedColor_ = hex_to_color(config.untracked);
    ignoredColor_ = hex_to_color(config.ignored);
    conflictedColor_ = hex_to_color(config.conflicted);
}

void Theme::reloadIcons(core::IconConfig icon_config) {
    iconConfig_ = std::make_unique<core::IconConfig>(std::move(icon_config));
}

// Global theme instance
Theme& global_theme() {
    static Theme instance;
    return instance;
}

}  // namespace expp::ui
