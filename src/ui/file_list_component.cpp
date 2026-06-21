#include "expp/core/filesystem.hpp"
#include "expp/core/version_control.hpp"
#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>

#include <format>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace expp::ui {
namespace {

/**
 * @brief Returns the properly formatted display name for a file entry and whether it should be
 * highlighted as an error.
 *
 * @param entry The file entry for which to generate a display name.
 * @return std::pair<std::string, bool> The formatted display name and a boolean indicating whether
 * it should be highlighted as an error.
 */
[[nodiscard]] std::pair<std::string, bool> proper_display_name(
    const core::filesystem::FileEntry& entry) {
    std::string name = entry.filename();
    if (entry.isSymlink()) {
        if (!entry.symlinkTarget.empty()) {
            name += " -> " + entry.symlinkTarget.string();
        }
        if (entry.isRecursiveSymlink || entry.isBrokenSymlink) {
            name += entry.isRecursiveSymlink ? " [loop]" : " [broken]";
            return {name, true};
        }
    }
    return {name, false};
}

}  // namespace

struct FileListComponent::Impl {
    explicit Impl(FileListConfig file_config) : config(std::move(file_config)) {}

    FileListConfig config;

    [[nodiscard]] ftxui::Element render(std::span<const core::filesystem::FileEntry> entries,
                                        int selected,
                                        const std::vector<int>& search_matches,
                                        int current_match_index,
                                        const std::vector<int>& selected_indices) const {
        using namespace ftxui;

        Elements elements;
        std::unordered_set<int> search_match_set(search_matches.begin(), search_matches.end());
        std::unordered_set<int> selected_index_set(selected_indices.begin(),
                                                   selected_indices.end());

        elements.reserve(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const bool is_selected = static_cast<int>(index) == selected;
            const bool is_visual_selected = selected_index_set.contains(static_cast<int>(index));
            const bool is_search_match =
                !search_matches.empty() && search_match_set.contains(static_cast<int>(index));
            const bool is_current_match =
                is_search_match && current_match_index >= 0 &&
                search_matches[static_cast<std::size_t>(current_match_index)] ==
                    static_cast<int>(index);

            const std::string prefix = is_selected ? config.selectionPrefix : config.normalPrefix;
            const auto [display_name, should_highlight] = proper_display_name(entry);
            auto base_color =
                should_highlight ? Color::Red : config.theme->getFileEntryColor(entry);
            auto left_content = text(std::format(
                "{}{} {}", prefix, config.showIcons ? config.theme->getFileTypeIcon(entry) : "",
                display_name));
            auto right_content = text(std::format("{}", core::status_marker(entry.versionStatus)));
            if (entry.versionStatus != core::VersionStatus::Clean) {
                right_content |= color(config.theme->getVersionStatusColor(entry.versionStatus));
            }

            auto element = hbox({left_content, filler(), right_content}) | color(base_color);

            if (config.boldDirectories && entry.isDirectory()) {
                element |= bold;
            }
            if (config.enableHighlight && is_current_match) {
                element |= bgcolor(config.theme->getSearchHighlightColor()) | color(Color::Black);
            }
            if (is_selected) {
                element |= inverted;
            } else if (is_visual_selected) {
                element |= bgcolor(Color::Blue) | color(Color::White);
            }

            elements.push_back(std::move(element));
        }

        if (elements.empty()) {
            elements.push_back(text("[Empty]") | dim | center);
        }

        return vbox(std::move(elements));
    }
};

FileListComponent::FileListComponent(const FileListConfig& config)
    : impl_{std::make_unique<Impl>(config)} {}

FileListComponent::~FileListComponent() = default;
FileListComponent::FileListComponent(FileListComponent&&) noexcept = default;
FileListComponent& FileListComponent::operator=(FileListComponent&&) noexcept = default;

ftxui::Element FileListComponent::render(std::span<const core::filesystem::FileEntry> entries,
                                         int selected,
                                         const std::vector<int>& search_matches,
                                         int current_match_index,
                                         const std::vector<int>& selected_indices) const {
    return impl_->render(entries, selected, search_matches, current_match_index, selected_indices);
}

void FileListComponent::setConfig(FileListConfig config) {
    impl_->config = std::move(config);
}

}  // namespace expp::ui
