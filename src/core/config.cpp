/**
 * @file config.cpp
 * @brief Implementation of the TOML-based configuration manager
 *
 * @copyright Copyright (c) 2026
 */

#include "expp/core/config.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

namespace expp::core {

namespace {

// These descriptors keep scalar TOML fields defined once for both load and save
// paths, which reduces drift when config sections evolve.
template <typename ConfigT, typename MemberT>
struct ScalarFieldSpec {
    std::string_view key;
    MemberT ConfigT::*member;
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
    ScalarFieldSpec<PreviewConfig, bool>{"enabled", &PreviewConfig::enabled},
    ScalarFieldSpec<PreviewConfig, bool>{"syntax_highlight", &PreviewConfig::syntaxHighlight},
};

constexpr auto kPreviewIntFields = std::array{
    ScalarFieldSpec<PreviewConfig, int>{"max_lines", &PreviewConfig::maxLines},
    ScalarFieldSpec<PreviewConfig, int>{"max_line_length", &PreviewConfig::maxLineLength},
};

constexpr auto kLayoutBoolFields = std::array{
    ScalarFieldSpec<LayoutConfig, bool>{"show_preview_panel", &LayoutConfig::showPreviewPanel},
    ScalarFieldSpec<LayoutConfig, bool>{"show_parent_panel", &LayoutConfig::showParentPanel},
    ScalarFieldSpec<LayoutConfig, bool>{"show_status_bar", &LayoutConfig::showStatusBar},
};

constexpr auto kLayoutIntFields = std::array{
    ScalarFieldSpec<LayoutConfig, int>{"parent_panel_width", &LayoutConfig::parentPanelWidth},
    ScalarFieldSpec<LayoutConfig, int>{"preview_panel_width", &LayoutConfig::previewPanelWidth},
};

constexpr auto kBehaviorBoolFields = std::array{
    ScalarFieldSpec<BehaviorConfig, bool>{"show_hidden_files", &BehaviorConfig::showHiddenFiles},
    ScalarFieldSpec<BehaviorConfig, bool>{"confirm_delete", &BehaviorConfig::confirmDelete},
    ScalarFieldSpec<BehaviorConfig, bool>{"confirm_trash", &BehaviorConfig::confirmTrash},
    ScalarFieldSpec<BehaviorConfig, bool>{"sort_directories_first", &BehaviorConfig::sortDirectoriesFirst},
    ScalarFieldSpec<BehaviorConfig, bool>{"case_sensitive_search", &BehaviorConfig::caseSensitiveSearch},
};

constexpr auto kBehaviorIntFields = std::array{
    ScalarFieldSpec<BehaviorConfig, int>{"key_timeout_ms", &BehaviorConfig::keyTimeoutMs},
};

constexpr auto kNotificationBoolFields = std::array{
    ScalarFieldSpec<NotificationConfig, bool>{"show_success", &NotificationConfig::showSuccess},
    ScalarFieldSpec<NotificationConfig, bool>{"show_info", &NotificationConfig::showInfo},
};

constexpr auto kNotificationIntFields = std::array{
    ScalarFieldSpec<NotificationConfig, int>{"duration_ms", &NotificationConfig::durationMs},
};

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
        return make_error(ErrorCategory::Config,
                          std::format("Invalid color format '{}': expected 0xRRGGBB or #RRGGBB", str));
    }

    if (str.size() != 6) {
        return make_error(ErrorCategory::Config, std::format("Invalid color '{}': expected 6 hex digits", str));
    }

    uint32_t value = 0;
    static constexpr int kHexBase = 16;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, kHexBase);
    if (ec != std::errc{} || ptr != str.data() + str.size()) { // NOLINT
        return make_error(ErrorCategory::Config, std::format("Failed to parse hex color '{}'", str));
    }

    return value;
}

/**
 * @brief Formats a uint32_t color as "0xRRGGBB"
 */
std::string format_hex_color(uint32_t color) {
    return std::format("0x{:06X}", color);
}

void load_color_theme(const toml::table& tbl, ColorTheme& theme) {
    if (auto val = tbl["name"].value<std::string>()) {
        theme.name = *val;
    }

    if (const auto *ft = tbl["filetype_colors"].as_table()) {
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

    if (const auto *ui = tbl["ui_colors"].as_table()) {
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
}

void load_icon_config(const toml::table& tbl, IconConfig& icons) {
    for (auto&& [key, val] : tbl) {
        if (auto str = val.value<std::string>()) {
            std::string k{key};
            if (k == "default") {
                icons.defaultFileIcon = *str;
                icons.icons["default"] = *str;
            } else if (k == "folder") {
                icons.defaultFolderIcon = *str;
                icons.icons["folder"] = *str;
            } else {
                icons.icons[k] = *str;
            }
        }
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

    return tbl;
}

toml::table serialize_icon_config(const IconConfig& icons) {
    toml::table tbl;
    for (const auto& [key, val] : icons.icons) {
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

    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            // Stop at the first discovered config path so layer precedence is
            // explicit and deterministic.
            return loadFrom(path);
        }
    }

    // No config file found — use defaults (this is not an error)
    return {};
}

VoidResult ConfigManager::loadFrom(const std::filesystem::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& err) {
        return make_error(ErrorCategory::Config,
                          std::format("Failed to parse config file '{}': {}", path.string(), err.description()));
    }

    Config cfg = defaults();
    // Load into a temporary config so parse success is all-or-nothing from the
    // caller's perspective.

    if (auto *theme = tbl["theme"].as_table()) {
        load_color_theme(*theme, cfg.theme);
    }

    if (auto *icons = tbl["icons"].as_table()) {
        load_icon_config(*icons, cfg.icons);
    }

    if (auto *preview = tbl["preview"].as_table()) {
        load_preview_config(*preview, cfg.preview);
    }

    if (auto *layout = tbl["layout"].as_table()) {
        load_layout_config(*layout, cfg.layout);
    }

    if (auto *behavior = tbl["behavior"].as_table()) {
        load_behavior_config(*behavior, cfg.behavior);
    }

    if (auto *notifications = tbl["notifications"].as_table()) {
        load_notification_config(*notifications, cfg.notifications);
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
                          std::format("Failed to create config directory '{}': {}", parent_dir.string(), ec.message()));
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
    }

    std::ofstream ofs(path);
    if (!ofs) {
        return make_error(ErrorCategory::IO, std::format("Failed to open config file '{}' for writing", path.string()));
    }

    ofs << "# Expp Configuration File\n"
        << "# See documentation for available options.\n\n"
        << root << '\n';

    if (!ofs.good()) {
        return make_error(ErrorCategory::IO, std::format("Failed to write config file '{}'", path.string()));
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

Config ConfigManager::defaults() noexcept {
    return Config{};
}

ConfigManager& global_config() {
    static ConfigManager instance;
    return instance;
}

}  // namespace expp::core
