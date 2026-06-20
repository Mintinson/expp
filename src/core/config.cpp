/**
 * @file config.cpp
 * @brief Implementation of the TOML-based configuration manager
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/core/config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// #define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

namespace expp::core {

namespace {

// These descriptors keep scalar TOML fields defined once for both load and save
// paths, which reduces drift when config sections evolve.
template <typename ConfigT, typename MemberT>
struct ScalarFieldSpec {
    std::string_view key;
    MemberT ConfigT::* member;
};

template <typename ConfigT, std::size_t N>
void load_bool_fields(const toml::table& tbl,
                      ConfigT& config,
                      const std::array<ScalarFieldSpec<ConfigT, bool>, N>& specs) {
    for (const auto& spec : specs) {
        if (auto value = tbl[spec.key].template value<bool>()) {
            config.*(spec.member) = *value;
        }
    }
}

template <typename ConfigT, std::size_t N>
void load_int_fields(const toml::table& tbl,
                     ConfigT& config,
                     const std::array<ScalarFieldSpec<ConfigT, int>, N>& specs) {
    for (const auto& spec : specs) {
        if (auto value = tbl[spec.key].template value<int64_t>()) {
            config.*(spec.member) = static_cast<int>(*value);
        }
    }
}

template <typename ConfigT, std::size_t N>
void insert_bool_fields(toml::table& tbl,
                        const ConfigT& config,
                        const std::array<ScalarFieldSpec<ConfigT, bool>, N>& specs) {
    for (const auto& spec : specs) {
        tbl.insert(spec.key, config.*(spec.member));
    }
}

template <typename ConfigT, std::size_t N>
void insert_int_fields(toml::table& tbl,
                       const ConfigT& config,
                       const std::array<ScalarFieldSpec<ConfigT, int>, N>& specs) {
    for (const auto& spec : specs) {
        tbl.insert(spec.key, static_cast<int64_t>(config.*(spec.member)));
    }
}

constexpr auto kPreviewBoolFields = std::array{
    ScalarFieldSpec<PreviewConfig, bool>{"enabled",          &PreviewConfig::enabled        },
    ScalarFieldSpec<PreviewConfig, bool>{"syntax_highlight", &PreviewConfig::syntaxHighlight},
};

constexpr auto kPreviewIntFields = std::array{
    ScalarFieldSpec<PreviewConfig, int>{"max_lines",       &PreviewConfig::maxLines     },
    ScalarFieldSpec<PreviewConfig, int>{"max_line_length", &PreviewConfig::maxLineLength},
};

constexpr auto kLayoutBoolFields = std::array{
    ScalarFieldSpec<LayoutConfig, bool>{"show_preview_panel", &LayoutConfig::showPreviewPanel},
    ScalarFieldSpec<LayoutConfig, bool>{"show_parent_panel",  &LayoutConfig::showParentPanel },
    ScalarFieldSpec<LayoutConfig, bool>{"show_status_bar",    &LayoutConfig::showStatusBar   },
};

constexpr auto kLayoutIntFields = std::array{
    ScalarFieldSpec<LayoutConfig, int>{"parent_panel_width",  &LayoutConfig::parentPanelWidth },
    ScalarFieldSpec<LayoutConfig, int>{"preview_panel_width", &LayoutConfig::previewPanelWidth},
};

constexpr auto kBehaviorBoolFields = std::array{
    ScalarFieldSpec<BehaviorConfig, bool>{"show_hidden_files",      &BehaviorConfig::showHiddenFiles},
    ScalarFieldSpec<BehaviorConfig, bool>{"confirm_delete",         &BehaviorConfig::confirmDelete  },
    ScalarFieldSpec<BehaviorConfig, bool>{"confirm_trash",          &BehaviorConfig::confirmTrash   },
    ScalarFieldSpec<BehaviorConfig, bool>{"sort_directories_first",
                                          &BehaviorConfig::sortDirectoriesFirst                     },
    ScalarFieldSpec<BehaviorConfig, bool>{"case_sensitive_search",
                                          &BehaviorConfig::caseSensitiveSearch                      },
};

constexpr auto kBehaviorIntFields = std::array{
    ScalarFieldSpec<BehaviorConfig, int>{"key_timeout_ms", &BehaviorConfig::keyTimeoutMs},
};

constexpr auto kNotificationBoolFields = std::array{
    ScalarFieldSpec<NotificationConfig, bool>{"show_success", &NotificationConfig::showSuccess},
    ScalarFieldSpec<NotificationConfig, bool>{"show_info",    &NotificationConfig::showInfo   },
};

constexpr auto kNotificationIntFields = std::array{
    ScalarFieldSpec<NotificationConfig, int>{"duration_ms", &NotificationConfig::durationMs},
};

constexpr auto kRuntimeIntFields = std::array{
    ScalarFieldSpec<RuntimeConfig, int>{"io_threads",  &RuntimeConfig::ioThreads },
    ScalarFieldSpec<RuntimeConfig, int>{"cpu_threads", &RuntimeConfig::cpuThreads},
};

constexpr auto kListingIntFields = std::array{
    ScalarFieldSpec<ListingConfig, int>{"chunk_entries", &ListingConfig::chunkEntries},
    ScalarFieldSpec<ListingConfig, int>{"preload_pages", &ListingConfig::preloadPages},
};

constexpr auto kAnalysisBoolFields = std::array{
    ScalarFieldSpec<AnalysisConfig, bool>{"mime_sniffing",      &AnalysisConfig::mimeSniffing     },
    ScalarFieldSpec<AnalysisConfig, bool>{"highlight_previews", &AnalysisConfig::highlightPreviews},
};

constexpr auto kVersionControlBoolFields = std::array{
    ScalarFieldSpec<VersionControlConfig, bool>{"enabled",            &VersionControlConfig::enabled},
    ScalarFieldSpec<VersionControlConfig, bool>{"show_ignored_files",
                                                &VersionControlConfig::showIgnoredFiles             },
};

[[nodiscard]] std::optional<VersionControlStatusDetail> parse_status_detail(
    std::string_view value) noexcept {
    if (value == "compact") {
        return VersionControlStatusDetail::Compact;
    }
    if (value == "summary") {
        return VersionControlStatusDetail::Summary;
    }
    if (value == "full") {
        return VersionControlStatusDetail::Full;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view format_status_detail(VersionControlStatusDetail detail) noexcept {
    switch (detail) {
        case VersionControlStatusDetail::Compact:
            return "compact";
        case VersionControlStatusDetail::Summary:
            return "summary";
        case VersionControlStatusDetail::Full:
            return "full";
        default:
            return "summary";
    }
}

/**
 * @brief Parses a hex color string ("0xRRGGBB" or "#RRGGBB") to uint32_t
 */
Result<uint32_t> parse_hex_color(std::string_view str) {
    // Strip prefix
    if (str.starts_with("0x") || str.starts_with("0X")) {
        str.remove_prefix(2);
    } else if (str.starts_with('#')) {
        str.remove_prefix(1);
    } else {
        return make_error(
            ErrorCategory::Config,
            std::format("Invalid color format '{}': expected 0xRRGGBB or #RRGGBB", str));
    }

    if (str.size() != 6) {
        return make_error(ErrorCategory::Config,
                          std::format("Invalid color '{}': expected 6 hex digits", str));
    }

    uint32_t value = 0;
    static constexpr int kHexBase = 16;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, kHexBase);
    if (ec != std::errc{} || ptr != str.data() + str.size()) {  // NOLINT
        return make_error(ErrorCategory::Config,
                          std::format("Failed to parse hex color '{}'", str));
    }

    return value;
}

/**
 * @brief Formats a uint32_t color as "0xRRGGBB"
 */
std::string format_hex_color(uint32_t color) {
    return std::format("0x{:06X}", color);
}

// [[nodiscard]] std::string ascii_lower(std::string value) {
//     std::ranges::transform(value, value.begin(), [](unsigned char ch) {
//         return static_cast<char>(std::tolower(ch));
//     });
//     return value;
// }

[[nodiscard]] std::string normalize_extension_rule_key(std::string_view key) {
    // Remove leading dots and convert to lowercase
    auto normalized = key | std::views::drop_while([](char ch) { return ch == '.'; }) |
                      std::views::transform(
                          [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }) |
                      std::ranges::to<std::string>();
    return normalized;
}

[[nodiscard]] std::string normalized_entry_extension(const filesystem::FileEntry& entry) {
    return normalize_extension_rule_key(entry.extension());
}

[[nodiscard]] std::string_view icon_or_fallback(const IconConfig& config,
                                                std::string_view icon_id,
                                                std::string_view fallback_id) noexcept {
    if (auto it = config.iconTheme.find(icon_id); it != config.iconTheme.end()) {
        return it->second;
    }
    if (auto it = config.iconTheme.find(fallback_id); it != config.iconTheme.end()) {
        return it->second;
    }
    return fallback_id == config.folderFallbackIconId ? default_icon_theme().at("folder_default")
                                                      : default_icon_theme().at("file_default");
}

void load_string_map(const toml::table& tbl,
                     StringHashMap<std::string>& target,
                     bool normalize_extension_keys) {
    for (const auto& [key, val] : tbl) {
        if (auto str = val.value<std::string>()) {
            std::string map_key{key};
            if (normalize_extension_keys) {
                map_key = normalize_extension_rule_key(map_key);
            }
            target[std::move(map_key)] = *str;
        }
    }
}

void load_icon_theme(const toml::table& tbl, IconConfig& icons) {
    load_string_map(tbl, icons.iconTheme, false);
}

void load_color_theme(const toml::table& tbl, ColorTheme& theme) {
    if (auto val = tbl["name"].value<std::string>()) {
        theme.name = *val;
    }

    if (const auto* ft = tbl["filetype_colors"].as_table()) {
        // Color parsing is tolerant here by design: invalid individual color
        // fields fall back to the existing default instead of rejecting the
        // entire theme section.
        auto try_color = [&](std::string_view key, uint32_t& target) {
            if (auto v = (*ft)[key].value<std::string>()) {
                if (auto parsed = parse_hex_color(*v)) {
                    target = *parsed;
                }
            }
        };
        try_color("directory", theme.directory);
        try_color("regular_file", theme.regularFile);
        try_color("executable", theme.executable);
        try_color("symlink", theme.symlink);
        try_color("archive", theme.archive);
        try_color("source_code", theme.sourceCode);
        try_color("image", theme.image);
        try_color("document", theme.document);
        try_color("config", theme.config);
        try_color("hidden", theme.hidden);
    }

    if (const auto* ui = tbl["ui_colors"].as_table()) {
        auto try_color = [&](std::string_view key, uint32_t& target) {
            if (auto v = (*ui)[key].value<std::string>()) {
                if (auto parsed = parse_hex_color(*v)) {
                    target = *parsed;
                }
            }
        };
        try_color("background", theme.background);
        try_color("foreground", theme.foreground);
        try_color("selection", theme.selection);
        try_color("border", theme.border);
        try_color("status_bar", theme.statusBar);
        try_color("search_highlight", theme.searchHighlight);
    }

    if (const auto* vc = tbl["version_control_colors"].as_table()) {
        auto try_color = [&](std::string_view key, uint32_t& target) {
            if (auto v = (*vc)[key].value<std::string>()) {
                if (auto parsed = parse_hex_color(*v)) {
                    target = *parsed;
                }
            }
        };
        try_color("modified", theme.modified);
        try_color("added", theme.added);
        try_color("deleted", theme.deleted);
        try_color("renamed", theme.renamed);
        try_color("copied", theme.copied);
        try_color("untracked", theme.untracked);
        try_color("ignored", theme.ignored);
        try_color("conflicted", theme.conflicted);
    }
}

void load_legacy_icon_config(const toml::table& tbl, IconConfig& icons) {
    for (auto&& [key, val] : tbl) {
        if (auto str = val.value<std::string>()) {
            std::string k{key};
            if (k == "default") {
                icons.iconTheme[icons.fileFallbackIconId] = *str;
            } else if (k == "folder") {
                icons.iconTheme[icons.folderFallbackIconId] = *str;
            } else if (k == "exe") {
                icons.iconTheme[icons.executableIconId] = *str;
            } else if (k == "link") {
                icons.iconTheme[icons.symlinkIconId] = *str;
            } else if (k.starts_with('.')) {
                icons.iconTheme[k] = *str;
                icons.rules.extensions[normalize_extension_rule_key(k)] = k;
            } else {
                icons.iconTheme[k] = *str;
            }
        }
    }
}

void load_icon_config(const toml::table& tbl, IconConfig& icons) {
    if (const auto* icon_theme = tbl["icon_theme"].as_table()) {
        load_icon_theme(*icon_theme, icons);
    }

    if (const auto* rules = tbl["rules"].as_table()) {
        if (const auto* exact_files = (*rules)["exact_files"].as_table()) {
            load_string_map(*exact_files, icons.rules.exactFiles, false);
        }
        if (const auto* extensions = (*rules)["extensions"].as_table()) {
            load_string_map(*extensions, icons.rules.extensions, true);
        }
        if (const auto* exact_folders = (*rules)["exact_folders"].as_table()) {
            load_string_map(*exact_folders, icons.rules.exactFolders, false);
        }
        if (const auto* attributes = (*rules)["attributes"].as_table()) {
            if (const auto& value = (*attributes)["executable"].value<std::string>()) {
                icons.executableIconId = *value;
            }
            if (const auto& value = (*attributes)["symlink"].value<std::string>()) {
                icons.symlinkIconId = *value;
            }
            if (const auto& value = (*attributes)["hidden"].value<std::string>()) {
                icons.hiddenIconId = *value;
            }
        }
        if (const auto* fallbacks = (*rules)["fallbacks"].as_table()) {
            if (const auto& value = (*fallbacks)["file"].value<std::string>()) {
                icons.fileFallbackIconId = *value;
            }
            if (const auto& value = (*fallbacks)["folder"].value<std::string>()) {
                icons.folderFallbackIconId = *value;
            }
        }
    }

    if (const auto* legacy_icons = tbl["icons"].as_table()) {
        load_legacy_icon_config(*legacy_icons, icons);
    }
}

void load_preview_config(const toml::table& tbl, PreviewConfig& preview) {
    load_bool_fields(tbl, preview, kPreviewBoolFields);
    load_int_fields(tbl, preview, kPreviewIntFields);
}

void load_layout_config(const toml::table& tbl, LayoutConfig& layout) {
    load_bool_fields(tbl, layout, kLayoutBoolFields);
    load_int_fields(tbl, layout, kLayoutIntFields);
}

void load_behavior_config(const toml::table& tbl, BehaviorConfig& behavior) {
    load_bool_fields(tbl, behavior, kBehaviorBoolFields);
    load_int_fields(tbl, behavior, kBehaviorIntFields);
}

void load_notification_config(const toml::table& tbl, NotificationConfig& notifications) {
    load_bool_fields(tbl, notifications, kNotificationBoolFields);
    load_int_fields(tbl, notifications, kNotificationIntFields);
}

void load_runtime_config(const toml::table& tbl, RuntimeConfig& runtime) {
    load_int_fields(tbl, runtime, kRuntimeIntFields);
}

void load_listing_config(const toml::table& tbl, ListingConfig& listing) {
    load_int_fields(tbl, listing, kListingIntFields);
}

void load_analysis_config(const toml::table& tbl, AnalysisConfig& analysis) {
    load_bool_fields(tbl, analysis, kAnalysisBoolFields);
}

void load_version_control_config(const toml::table& tbl, VersionControlConfig& version_control) {
    load_bool_fields(tbl, version_control, kVersionControlBoolFields);
    if (auto value = tbl["status_detail"].value<std::string>()) {
        if (auto detail = parse_status_detail(*value)) {
            version_control.statusDetail = *detail;
        }
    }
}

toml::table serialize_color_theme(const ColorTheme& theme) {
    toml::table tbl;
    tbl.insert("name", theme.name);

    toml::table ft;
    ft.insert("directory", format_hex_color(theme.directory));
    ft.insert("regular_file", format_hex_color(theme.regularFile));
    ft.insert("executable", format_hex_color(theme.executable));
    ft.insert("symlink", format_hex_color(theme.symlink));
    ft.insert("archive", format_hex_color(theme.archive));
    ft.insert("source_code", format_hex_color(theme.sourceCode));
    ft.insert("image", format_hex_color(theme.image));
    ft.insert("document", format_hex_color(theme.document));
    ft.insert("config", format_hex_color(theme.config));
    ft.insert("hidden", format_hex_color(theme.hidden));
    tbl.insert("filetype_colors", std::move(ft));

    toml::table ui;
    ui.insert("background", format_hex_color(theme.background));
    ui.insert("foreground", format_hex_color(theme.foreground));
    ui.insert("selection", format_hex_color(theme.selection));
    ui.insert("border", format_hex_color(theme.border));
    ui.insert("status_bar", format_hex_color(theme.statusBar));
    ui.insert("search_highlight", format_hex_color(theme.searchHighlight));
    tbl.insert("ui_colors", std::move(ui));

    toml::table vc;
    vc.insert("modified", format_hex_color(theme.modified));
    vc.insert("added", format_hex_color(theme.added));
    vc.insert("deleted", format_hex_color(theme.deleted));
    vc.insert("renamed", format_hex_color(theme.renamed));
    vc.insert("copied", format_hex_color(theme.copied));
    vc.insert("untracked", format_hex_color(theme.untracked));
    vc.insert("ignored", format_hex_color(theme.ignored));
    vc.insert("conflicted", format_hex_color(theme.conflicted));
    tbl.insert("version_control_colors", std::move(vc));

    return tbl;
}

toml::table serialize_icon_config(const IconConfig& icons) {
    toml::table tbl;
    for (const auto& [key, val] : icons.iconTheme) {
        tbl.insert(key, val);
    }
    return tbl;
}

toml::table serialize_preview_config(const PreviewConfig& preview) {
    toml::table tbl;
    insert_bool_fields(tbl, preview, kPreviewBoolFields);
    insert_int_fields(tbl, preview, kPreviewIntFields);
    return tbl;
}

toml::table serialize_layout_config(const LayoutConfig& layout) {
    toml::table tbl;
    insert_bool_fields(tbl, layout, kLayoutBoolFields);
    insert_int_fields(tbl, layout, kLayoutIntFields);
    return tbl;
}

toml::table serialize_behavior_config(const BehaviorConfig& behavior) {
    toml::table tbl;
    insert_bool_fields(tbl, behavior, kBehaviorBoolFields);
    insert_int_fields(tbl, behavior, kBehaviorIntFields);
    return tbl;
}

toml::table serialize_notification_config(const NotificationConfig& notifications) {
    toml::table tbl;
    insert_bool_fields(tbl, notifications, kNotificationBoolFields);
    insert_int_fields(tbl, notifications, kNotificationIntFields);
    return tbl;
}

toml::table serialize_runtime_config(const RuntimeConfig& runtime) {
    toml::table tbl;
    insert_int_fields(tbl, runtime, kRuntimeIntFields);
    return tbl;
}

toml::table serialize_listing_config(const ListingConfig& listing) {
    toml::table tbl;
    insert_int_fields(tbl, listing, kListingIntFields);
    return tbl;
}

toml::table serialize_analysis_config(const AnalysisConfig& analysis) {
    toml::table tbl;
    insert_bool_fields(tbl, analysis, kAnalysisBoolFields);
    return tbl;
}

toml::table serialize_version_control_config(const VersionControlConfig& version_control) {
    toml::table tbl;
    insert_bool_fields(tbl, version_control, kVersionControlBoolFields);
    tbl.insert("status_detail", std::string{format_status_detail(version_control.statusDetail)});
    return tbl;
}

[[nodiscard]] VoidResult load_config_table_from_file(const std::filesystem::path& path,
                                                     toml::table& tbl) {
    toml::parse_result result = toml::parse_file(path.string());
    if (!result) {
        return make_error(ErrorCategory::Config,
                          std::format("Failed to parse config file '{}': {}", path.string(),
                                      result.error().description()));
    }
    tbl = std::move(result).table();
    return {};
}

void load_config_table(const toml::table& tbl, Config& cfg) {
    if (const auto* theme = tbl["theme"].as_table()) {
        load_color_theme(*theme, cfg.theme);
    }

    // if (const auto* icons = tbl["icons"].as_table()) {
    //     load_legacy_icon_config(*icons, cfg.icons);
    // }

    if (const auto* preview = tbl["preview"].as_table()) {
        load_preview_config(*preview, cfg.preview);
    }

    if (const auto* layout = tbl["layout"].as_table()) {
        load_layout_config(*layout, cfg.layout);
    }

    if (const auto* behavior = tbl["behavior"].as_table()) {
        load_behavior_config(*behavior, cfg.behavior);
    }

    if (const auto* notifications = tbl["notifications"].as_table()) {
        load_notification_config(*notifications, cfg.notifications);
    }

    if (const auto* runtime = tbl["runtime"].as_table()) {
        load_runtime_config(*runtime, cfg.runtime);
    }

    if (const auto* listing = tbl["listing"].as_table()) {
        load_listing_config(*listing, cfg.listing);
    }

    if (const auto* analysis = tbl["analysis"].as_table()) {
        load_analysis_config(*analysis, cfg.analysis);
    }

    if (const auto* version_control = tbl["version_control"].as_table()) {
        load_version_control_config(*version_control, cfg.versionControl);
    }
}

[[nodiscard]] VoidResult load_config_from_file(const std::filesystem::path& path, Config& cfg) {
    toml::table tbl;
    auto result = load_config_table_from_file(path, tbl);
    if (!result) {
        return result;
    }
    load_config_table(tbl, cfg);
    return {};
}

[[nodiscard]] VoidResult merge_icon_config_from_file(const std::filesystem::path& path,
                                                     IconConfig& icons) {
    toml::table tbl;
    auto result = load_config_table_from_file(path, tbl);
    if (!result) {
        return result;
    }
    load_icon_config(tbl, icons);
    return {};
}

}  // namespace

// ============================================================================
// ConfigManager Implementation
// ============================================================================

struct ConfigManager::Impl {
    Config config;
    mutable std::mutex mutex;
    std::vector<std::pair<size_t, ConfigChangeCallback>> callbacks;
    size_t nextCallbackId{0};
    std::filesystem::path loadedPath;
    bool hotReloadEnabled{false};
};

ConfigManager::ConfigManager() : impl_(std::make_unique<Impl>()) {}

ConfigManager::~ConfigManager() = default;

VoidResult ConfigManager::load() {
    // Search for config files in priority order
    std::vector<std::filesystem::path> search_paths;

#ifdef _WIN32
    char* appdata;
    size_t size;
    if (_dupenv_s(&appdata, &size, "APPDATA") == 0 && appdata != nullptr) {
        search_paths.emplace_back(std::filesystem::path(appdata) / "expp" / "config.toml");
        free(appdata);
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        search_paths.emplace_back(std::filesystem::path(xdg_config) / "expp" / "config.toml");
    }

    if (const char* home = std::getenv("HOME")) {
        search_paths.emplace_back(std::filesystem::path(home) / ".config" / "expp" / "config.toml");
    }

    search_paths.emplace_back("/etc/expp/config.toml");
#endif

    Config cfg = defaults();
    std::filesystem::path loaded_path;

    for (const auto& path : search_paths) {
        std::error_code exists_ec;
        if (std::filesystem::exists(path, exists_ec)) {
            // Stop at the first discovered config path so layer precedence is
            // explicit and deterministic.
            auto result = load_config_from_file(path, cfg);
            if (!result) {
                return result;
            }
            loaded_path = path;
            break;
        }
    }

    const auto icons_path = userIconsPath();
    std::error_code icons_exists_ec;
    if (std::filesystem::exists(icons_path, icons_exists_ec)) {
        auto result = merge_icon_config_from_file(icons_path, cfg.icons);
        if (!result) {
            return result;
        }
    }

    {
        std::lock_guard lock(impl_->mutex);
        impl_->config = std::move(cfg);
        impl_->loadedPath = std::move(loaded_path);
    }

    // Notify callbacks outside the lock.
    const auto& current_config = impl_->config;
    for (const auto& [id, callback] : impl_->callbacks) {
        callback(current_config);
    }

    // No config file found is not an error; defaults still become active.
    return {};
}

VoidResult ConfigManager::loadFrom(const std::filesystem::path& path) {
    Config cfg = defaults();
    // Load into a temporary config so parse success is all-or-nothing from the
    // caller's perspective.
    auto result = load_config_from_file(path, cfg);
    if (!result) {
        return result;
    }

    // Note: [keys] section is handled separately by KeyMap::loadFromFile()
    // because keybindings need the ActionRegistry context.

    {
        std::lock_guard lock(impl_->mutex);
        impl_->config = std::move(cfg);
        impl_->loadedPath = path;
    }

    // Notify callbacks outside the lock
    const auto& current_config = impl_->config;
    for (const auto& [id, callback] : impl_->callbacks) {
        callback(current_config);
    }

    return {};
}

VoidResult ConfigManager::save() const {
    auto path = userConfigPath();

    // Create parent directories
    auto parent_dir = path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent_dir, ec);
    if (ec) {
        return make_error(ErrorCategory::FileSystem,
                          std::format("Failed to create config directory '{}': {}",
                                      parent_dir.string(), ec.message()));
    }

    toml::table root;
    {
        std::lock_guard lock(impl_->mutex);
        // Serialize from a locked snapshot so all sections in the emitted file
        // come from the same logical configuration version.
        root.insert("theme", serialize_color_theme(impl_->config.theme));
        root.insert("icons", serialize_icon_config(impl_->config.icons));
        root.insert("preview", serialize_preview_config(impl_->config.preview));
        root.insert("layout", serialize_layout_config(impl_->config.layout));
        root.insert("behavior", serialize_behavior_config(impl_->config.behavior));
        root.insert("notifications", serialize_notification_config(impl_->config.notifications));
        root.insert("runtime", serialize_runtime_config(impl_->config.runtime));
        root.insert("listing", serialize_listing_config(impl_->config.listing));
        root.insert("analysis", serialize_analysis_config(impl_->config.analysis));
        root.insert("version_control",
                    serialize_version_control_config(impl_->config.versionControl));
    }

    std::ofstream ofs(path);
    if (!ofs) {
        return make_error(
            ErrorCategory::IO,
            std::format("Failed to open config file '{}' for writing", path.string()));
    }

    ofs << "# Expp Configuration File\n"
        << "# See documentation for available options.\n\n"
        << root << '\n';

    if (!ofs.good()) {
        return make_error(ErrorCategory::IO,
                          std::format("Failed to write config file '{}'", path.string()));
    }

    return {};
}

const Config& ConfigManager::config() const noexcept {
    return impl_->config;
}

void ConfigManager::setConfig(Config new_config) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->config = std::move(new_config);
    }

    const auto& current_config = impl_->config;
    for (const auto& [id, callback] : impl_->callbacks) {
        callback(current_config);
    }
}

void ConfigManager::resetToDefaults() {
    setConfig(defaults());
}

size_t ConfigManager::onConfigChange(ConfigChangeCallback callback) {
    std::lock_guard lock(impl_->mutex);
    size_t id = impl_->nextCallbackId++;
    impl_->callbacks.emplace_back(id, std::move(callback));
    return id;
}

void ConfigManager::removeConfigChangeCallback(size_t id) {
    std::lock_guard lock(impl_->mutex);
    auto& cbs = impl_->callbacks;
    std::erase_if(cbs, [id](const auto& pair) { return pair.first == id; });
}

void ConfigManager::enableHotReload(bool enable) {
    impl_->hotReloadEnabled = enable;
    // TODO: Implement file watching (inotify/kqueue/ReadDirectoryChangesW)
}

std::filesystem::path ConfigManager::userConfigPath() {
#ifdef _WIN32
    char* appdata;
    size_t size;
    if (_dupenv_s(&appdata, &size, "APPDATA") == 0 && appdata != nullptr) {
        std::filesystem::path path = std::filesystem::path(appdata) / "expp" / "config.toml";
        free(appdata);
        return path;
    }
    return "config.toml";
    // if (const char* appdata = std::getenv("APPDATA")) {

    //     return std::filesystem::path(appdata) / "expp" / "config.toml";
    // }
    // return "config.toml";
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg_config) / "expp" / "config.toml";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "expp" / "config.toml";
    }
    return "config.toml";
#endif
}

std::filesystem::path ConfigManager::userIconsPath() {
    auto path = userConfigPath();
    path.replace_filename("icons.toml");
    return path;
}

Config ConfigManager::defaults() noexcept {
    return Config{};
}

ConfigManager& global_config() {
    static ConfigManager instance;
    return instance;
}

std::string_view resolve_icon(const IconConfig& config,
                              const filesystem::FileEntry& entry) noexcept {
    const auto& filename = entry.filename();
    if (entry.type == filesystem::FileType::Directory) {
        if (auto it = config.rules.exactFolders.find(filename);
            it != config.rules.exactFolders.end()) {
            return icon_or_fallback(config, it->second, config.folderFallbackIconId);
        }
        return icon_or_fallback(config, config.folderFallbackIconId, config.folderFallbackIconId);
    }

    if (auto it = config.rules.exactFiles.find(filename); it != config.rules.exactFiles.end()) {
        return icon_or_fallback(config, it->second, config.fileFallbackIconId);
    }

    const auto extension = normalized_entry_extension(entry);
    if (!extension.empty()) {
        if (auto it = config.rules.extensions.find(extension);
            it != config.rules.extensions.end()) {
            return icon_or_fallback(config, it->second, config.fileFallbackIconId);
        }
    }

    if (entry.type == filesystem::FileType::Executable) {
        return icon_or_fallback(config, config.executableIconId, config.fileFallbackIconId);
    }

    if (entry.type == filesystem::FileType::Symlink) {
        return icon_or_fallback(config, config.symlinkIconId, config.fileFallbackIconId);
    }

    if (entry.isHidden) {
        return icon_or_fallback(config, config.hiddenIconId, config.fileFallbackIconId);
    }

    return icon_or_fallback(config, config.fileFallbackIconId, config.fileFallbackIconId);
}

}  // namespace expp::core
