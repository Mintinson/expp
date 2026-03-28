#ifndef EXPP_CORE_CONFIG_HPP
#define EXPP_CORE_CONFIG_HPP

#include "expp/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace expp::core {
using namespace std::literals;

constexpr std::string_view kDefaultFolderIcon = "\uf07b"sv;
constexpr std::string_view kDefaultFileIcon = "\uf15b"sv;

/**
 * @brief Returns the built-in default icon map
 */
inline std::unordered_map<std::string, std::string> default_icon_map() {
    return {
        {"default", "\uf15b"},
        {    "exe", "\ue795"},
        {   "link", "\uf481"},
        {   ".lib", "\ueb9c"},
        {  ".bash", "\ue795"},
        {     ".c", "\ue61e"},
        { ".cmake", "\ue794"},
        {   ".cpp", "\ue61d"},
        {    ".cs", "\ue737"},
        {    ".go", "\ue627"},
        {     ".h", "\ue615"},
        {   ".hpp", "\uf0fd"},
        {  ".html", "\ue736"},
        {    ".js", "\ue74e"},
        {  ".json", "\ue60b"},
        {   ".lua", "\ue620"},
        {    ".md", "\uf48a"},
        {    ".py", "\ue73c"},
        {    ".rs", "\ue7a8"},
        {   ".zig", "\ue8ef"},
        {    ".ts", "\ue628"},
        {  ".toml", "\ue6b2"},
        {   ".log", "\uf4ed"},
        {  ".yaml", "\ue8eb"},
        {   ".yml", "\ue6a8"},
        {   ".xml",   "󰗀"},
        {   ".zip", "\ue6aa"},
        {   ".rar", "\ue6aa"},
        {    ".7z", "\ue6aa"},
        {   ".tar", "\ue6aa"},
        { "folder", "\uf07b"},
    };
}

/**
 * @brief Icon configuration
 */
struct IconConfig {
    std::string defaultFileIcon{kDefaultFileIcon};
    std::string defaultFolderIcon{kDefaultFolderIcon};
    std::unordered_map<std::string, std::string> icons{default_icon_map()};
};

/**
 * @brief Color theme configuration
 */
struct ColorTheme {
    std::string name{"default"};

    // File type colors (hex RGB values)
    uint32_t directory{0x0F9ED5};    // Blue
    uint32_t regularFile{0xFFFFFF};  // White
    uint32_t executable{0x55FF55};   // Green
    uint32_t symlink{0x55FFFF};      // Cyan
    uint32_t archive{0xFF5555};      // Red
    uint32_t sourceCode{0xFFFF55};   // Yellow
    uint32_t image{0xFF55FF};        // Magenta
    uint32_t document{0xFFAA55};     // Orange
    uint32_t config{0xAAAAFF};       // Light blue
    uint32_t hidden{0x888888};       // Gray

    // UI colors
    uint32_t background{0x000000};
    uint32_t foreground{0xFFFFFF};
    uint32_t selection{0x444444};
    uint32_t border{0x666666};
    uint32_t statusBar{0x333333};
    uint32_t searchHighlight{0xFFFF00};
};

/**
 * @brief Preview panel configuration
 */
struct PreviewConfig {
    bool enabled{true};
    int maxLines{50};
    int maxLineLength{80};
    bool syntaxHighlight{false};  // EXTENSION POINT: future syntax highlighting
};

/**
 * @brief UI layout configuration
 */
struct LayoutConfig {
    int parentPanelWidth{25};
    int previewPanelWidth{40};
    bool showPreviewPanel{true};
    bool showParentPanel{true};
    bool showStatusBar{true};
};

/**
 * @brief Behavior configuration
 */
struct BehaviorConfig {
    bool showHiddenFiles{false};
    bool confirmDelete{true};
    bool confirmTrash{true};
    bool sortDirectoriesFirst{true};
    bool caseSensitiveSearch{true};
    int keyTimeoutMs{1000};
};

/**
 * @brief Notification toast configuration
 */
struct NotificationConfig {
    int durationMs{2500};
    bool showSuccess{true};
    bool showInfo{true};
};

/**
 * @brief Main configuration container
 */
struct Config {
    ColorTheme theme;
    IconConfig icons;
    PreviewConfig preview;
    LayoutConfig layout;
    BehaviorConfig behavior;
    NotificationConfig notifications;
};

/**
 * @brief Configuration manager with layered config support
 *
 * Configuration layers (highest priority first):
 * 1. Runtime overrides
 * 2. User config (~/.config/expp/config.toml)
 * 3. System config (/etc/expp/config.toml)
 * 4. Built-in defaults
 *
 * Thread-safety: Thread-safe for read operations after initialization.
 * Use setConfig() for thread-safe updates.
 */
class ConfigManager {
public:
    using ConfigChangeCallback = std::function<void(const Config&)>;

    ConfigManager();
    ~ConfigManager();

    // Non-copyable, non-movable (singleton-like usage)
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;
    ConfigManager(ConfigManager&&) = delete;

    /**
     * @brief Loads configuration from default locations
     * @return Success or Error
     *
     * Searches in order:
     * 1. $XDG_CONFIG_HOME/expp/config.toml
     * 2. ~/.config/expp/config.toml
     * 3. /etc/expp/config.toml (Linux/macOS)
     * 4. %APPDATA%\expp\config.toml (Windows)
     *
     * If no config file is found, uses built-in defaults (not an error).
     */
    [[nodiscard]] VoidResult load();

    /**
     * @brief Loads configuration from a specific file
     * @param path Path to config file
     * @return Success or Error
     */
    [[nodiscard]] VoidResult loadFrom(const std::filesystem::path& path);

    /**
     * @brief Saves current configuration to user config file
     * @return Success or Error
     */
    [[nodiscard]] VoidResult save() const;

    /**
     * @brief Gets the current configuration (thread-safe)
     * @return Const reference to current config
     */
    [[nodiscard]] const Config& config() const noexcept;

    /**
     * @brief Updates configuration (thread-safe)
     * @param newConfig New configuration to apply
     */
    void setConfig(Config new_config);

    /**
     * @brief Resets to built-in defaults
     */
    void resetToDefaults();

    /**
     * @brief Registers a callback for config changes
     * @param callback Function to call when config changes
     * @return Callback ID for unregistering
     */
    size_t onConfigChange(ConfigChangeCallback callback);

    /**
     * @brief Unregisters a config change callback
     * @param id Callback ID from onConfigChange
     */
    void removeConfigChangeCallback(size_t id);

    /**
     * @brief Enables hot-reloading of config file
     * @param enable True to enable monitoring
     */
    void enableHotReload(bool enable);

    /**
     * @brief Gets the user config file path
     * @return Path to user config file
     */
    [[nodiscard]] static std::filesystem::path userConfigPath();

    /**
     * @brief Gets built-in default configuration
     * @return Default Config
     */
    [[nodiscard]] static Config defaults() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Global config manager instance
 * @return Reference to global ConfigManager
 */
[[nodiscard]] ConfigManager& global_config();

}  // namespace expp::core
#endif  // EXPP_CORE_CONFIG_HPP
