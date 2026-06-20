#ifndef EXPP_CORE_CONFIG_HPP
#define EXPP_CORE_CONFIG_HPP

#include "expp/core/error.hpp"
#include "expp/core/filesystem.hpp"
#include "expp/core/helper.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace expp::core {
using namespace std::literals;

/**
 * @brief Returns the built-in default icon theme map
 *
 * Uses function-local static to avoid repeated allocations when multiple
 * Config objects are default-constructed.
 */
inline const StringHashMap<std::string>& default_icon_theme() {
    static const StringHashMap<std::string> map = {
        {"file_default",    "\uf15b"    },
        {"file_executable", "\ue795"    },
        {"file_symlink",    "\uf481"    },
        {"file_hidden",     "\uf15b"    },
        {"file_binary",     "\ueae8"    },
        {"file_library",    "\ueb9c"    },
        {"file_shell",      "\ue795"    },
        {"file_c",          "\ue61e"    },
        {"file_cmake",      "\ue794"    },
        {"file_cpp",        "\ue61d"    },
        {"file_csharp",     "\ue737"    },
        {"file_go",         "\ue627"    },
        {"file_header",     "\ued83"    },
        {"file_hpp",        "\uf0fd"    },
        {"file_html",       "\ue736"    },
        {"file_javascript", "\ue74e"    },
        {"file_json",       "\ue60b"    },
        {"file_lua",        "\ue620"    },
        {"file_markdown",   "\uf48a"    },
        {"file_python",     "\ue73c"    },
        {"file_rust",       "\ue7a8"    },
        {"file_zig",        "\ue8ef"    },
        {"file_typescript", "\ue628"    },
        {"file_toml",       "\ue6b2"    },
        {"file_log",        "\uf4ed"    },
        {"file_yaml",       "\ue8eb"    },
        {"file_xml",        "\U000f05c0"},
        {"file_archive",    "\ue6aa"    },
        {"file_git",        "\ue702"    },
        {"file_ninja",      "\U000f0774"},
        {"folder_default",  "\uf07b"    },
        {"folder_build",    "\U000f107f"},
        {"folder_docs",     "\U000f19f6"},
        {"folder_git",      "\ue5fb"    },
        {"folder_test",     "\uf07b"    },
        {"folder_github",   "\ue5fd"    },
        {"folder_config",   "\ue5fc"    }
    };
    return map;
}

/**
 * @brief Returns the built-in default file extension to icon ID rules.
 *
 * Uses function-local static to avoid repeated allocations.
 */
inline const StringHashMap<std::string>& default_icon_extension_rules() {
    static const StringHashMap<std::string> map = {
        {"lib",   "file_library"   },
        {"bash",  "file_shell"     },
        {"c",     "file_c"         },
        {"cmake", "file_cmake"     },
        {"cpp",   "file_cpp"       },
        {"cxx",   "file_cpp"       },
        {"cc",    "file_cpp"       },
        {"cs",    "file_csharp"    },
        {"go",    "file_go"        },
        {"h",     "file_header"    },
        {"hpp",   "file_hpp"       },
        {"html",  "file_html"      },
        {"js",    "file_javascript"},
        {"json",  "file_json"      },
        {"lua",   "file_lua"       },
        {"md",    "file_markdown"  },
        {"py",    "file_python"    },
        {"rs",    "file_rust"      },
        {"zig",   "file_zig"       },
        {"ts",    "file_typescript"},
        {"toml",  "file_toml"      },
        {"log",   "file_log"       },
        {"yaml",  "file_yaml"      },
        {"yml",   "file_yaml"      },
        {"xml",   "file_xml"       },
        {"ninja", "file_ninja"     },
        {"zip",   "file_archive"   },
        {"rar",   "file_archive"   },
        {"7z",    "file_archive"   },
        {"tar",   "file_archive"   },
    };
    return map;
}

/**
 * @brief Returns the built-in exact file name to icon ID rules.
 *
 * Uses function-local static to avoid repeated allocations.
 */
inline const StringHashMap<std::string>& default_icon_exact_file_rules() {
    static const StringHashMap<std::string> map = {
        {"CMakeLists.txt", "file_cmake"},
        {"Makefile",       "file_cmake"},
        {".gitignore",     "file_git"  },
    };
    return map;
}

/**
 * @brief Returns the built-in exact folder name to icon ID rules.
 *
 * Uses function-local static to avoid repeated allocations.
 */
inline const StringHashMap<std::string>& default_icon_exact_folder_rules() {
    static const StringHashMap<std::string> map = {
        {".git",         "folder_git"   },
        {"build",        "folder_build" },
        {"node_modules", "folder_build" },
        {"test",         "folder_test"  },
        {"tests",        "folder_test"  },
        {"docs",         "folder_docs"  },
        {"doc",          "folder_docs"  },
        {".github",      "folder_github"},
        {"config",       "folder_config"},
        {".config",      "folder_config"},
        {"configs",      "folder_config"},
        {"settings",     "folder_config"},
    };
    return map;
}

/**
 * @brief Icon selection rules ordered by resolver priority.
 */
struct IconRules {
    StringHashMap<std::string> exactFiles{default_icon_exact_file_rules()};
    StringHashMap<std::string> extensions{default_icon_extension_rules()};
    StringHashMap<std::string> exactFolders{default_icon_exact_folder_rules()};
};

/**
 * @brief Icon configuration
 */
struct IconConfig {
    StringHashMap<std::string> iconTheme{default_icon_theme()};
    IconRules rules;
    std::string fileFallbackIconId{"file_default"};
    std::string folderFallbackIconId{"folder_default"};
    std::string executableIconId{"file_executable"};
    std::string symlinkIconId{"file_symlink"};
    std::string hiddenIconId{"file_hidden"};
};

/**
 * @brief Resolves the rendered icon for a file entry using configured priority rules.
 */
[[nodiscard]] std::string_view resolve_icon(const IconConfig& config,
                                            const filesystem::FileEntry& entry) noexcept;

/**
 * @brief Color theme configuration
 */
struct ColorTheme {
    // NOLINTBEGIN
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

    // Git status colors
    uint32_t modified{0xFFA500};    // Orange
    uint32_t added{0x00FF00};       // Green
    uint32_t deleted{0xFF0000};     // Red
    uint32_t renamed{0x00FFFF};     // Cyan
    uint32_t copied{0xFF00FF};      // Magenta
    uint32_t untracked{0x5555FF};   // Blue
    uint32_t ignored{0x555555};     // Dark gray
    uint32_t conflicted{0xE32636};  // Amaranth red
    // NOLINTEND
};

// NOLINTBEGIN
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
 * @brief Async runtime worker configuration.
 */
struct RuntimeConfig {
    int ioThreads{2};
    int cpuThreads{2};
};

/**
 * @brief Progressive directory listing configuration.
 */
struct ListingConfig {
    int chunkEntries{512};
    int preloadPages{1};
};

// NOLINTEND

/**
 * @brief Background analysis configuration.
 */
struct AnalysisConfig {
    bool mimeSniffing{true};
    bool highlightPreviews{true};
};

/**
 * @brief Git-aware version tracking configuration.
 */
enum class VersionControlStatusDetail : std::uint8_t {
    Compact,  // only show whether git is on or not
    Summary,  // show modified/added/deleted status
    Full,     // add ahead /behind
};

struct VersionControlConfig {
    bool enabled{false};
    bool showIgnoredFiles{true};
    VersionControlStatusDetail statusDetail{VersionControlStatusDetail::Summary};
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
    RuntimeConfig runtime;
    ListingConfig listing;
    AnalysisConfig analysis;
    VersionControlConfig versionControl;
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
     * @brief Gets the user icon configuration file path
     * @return Path to user icons.toml file
     */
    [[nodiscard]] static std::filesystem::path userIconsPath();

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
