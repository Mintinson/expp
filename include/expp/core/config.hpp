#ifndef EXPP_UI_CONFIG_HPP
#define EXPP_UI_CONFIG_HPP

#include "expp/core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace expp::core {
using namespace std::literals;
constexpr std::string_view kDefaultDirectIcon = "\uf07b"sv;
constexpr std::string_view kDefaultFileIcon = "\uf15b"sv;
const std::unordered_map<std::string, std::string> kIConMap = {
    {"default", "\uf15b"},
    {    "exe", "\ue795"},
    {   "link", "\uf481"},
    {  ".bash", "\ue795"},
    {     ".c", "\ue61e"},
    { ".cmake", "\ue706"},
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
    {    ".ts", "\ue628"},
    {  ".toml", "\ue6b2"},
    { "folder", "\uf07b"},
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
 * @brief Key binding configuration
 *
 * Keys are stored as string sequences (e.g., "gg", "j", "ctrl+d")
 */
struct KeyBindings {
    // Navigation
    std::string moveDown{"j"};
    std::string moveUp{"k"};
    std::string goParent{"h"};
    std::string enter{"l"};
    std::string goTop{"gg"};
    std::string goBottom{"G"};
    std::string pageDown{"ctrl+d"};
    std::string pageUp{"ctrl+u"};

    // Actions
    std::string quit{"q"};
    std::string open{"o"};
    std::string create{"a"};
    std::string rename{"r"};
    std::string trash{"d"};
    std::string deletePermanent{"D"};

    // Search
    std::string search{"/"};
    std::string nextMatch{"n"};
    std::string prevMatch{"N"};
    std::string clearSearch{"\\"};

    // Toggle
    std::string toggleHidden{"."};
    std::string refresh{"R"};
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
 * @brief Main configuration container
 */
struct Config {
    ColorTheme theme;
    KeyBindings keys;
    PreviewConfig preview;
    LayoutConfig layout;
    BehaviorConfig behavior;

    // EXTENSION POINT: Plugin configurations
    // Future: std::unordered_map<std::string, PluginConfig> plugins;
};

/**
 * @brief Configuration manager with layered config support
 *
 * Configuration layers (highest priority first):
 * 1. Runtime overrides
 * 2. User config (~/.config/playfxtui/config.toml)
 * 3. System config (/etc/playfxtui/config.toml)
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

    /**
     * @brief Loads configuration from default locations
     * @return Success or Error
     *
     * Searches in order:
     * 1. $XDG_CONFIG_HOME/playfxtui/config.toml
     * 2. ~/.config/playfxtui/config.toml
     * 3. /etc/playfxtui/config.toml (Linux/macOS)
     * 4. %APPDATA%\playfxtui\config.toml (Windows)
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
    void setConfig(Config newConfig);

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
#endif  // EXPP_UI_CONFIG_HPP