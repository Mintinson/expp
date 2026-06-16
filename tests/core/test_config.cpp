#include "expp/core/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() / "expp_config_test";
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

class ScopedEnvVar {
public:
    ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
#ifdef _WIN32
        char* old_value = nullptr;
        size_t required_size = 0;
        if (_dupenv_s(&old_value, &required_size, name_.c_str()) == 0 && old_value != nullptr) {
            oldValue_ = old_value;
            free(old_value);
        }
#else
        const char* old_value = std::getenv(name_.c_str());
        if (old_value != nullptr) {
            oldValue_ = old_value;
        }
#endif
        set(value);
    }

    ~ScopedEnvVar() {
        if (oldValue_.has_value()) {
            set(*oldValue_);
        } else {
            unset();
        }
    }

private:
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void unset() {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> oldValue_;
};

[[nodiscard]] expp::core::filesystem::FileEntry make_entry(std::string name,
                                                           expp::core::filesystem::FileType type,
                                                           bool hidden = false) {
    return expp::core::filesystem::FileEntry{.path = fs::path{std::move(name)}, .type = type, .isHidden = hidden};
}

}  // namespace

TEST_CASE("Notification config loads from TOML", "[core][config]") {
    TempDirectory tmp;
    const auto config_path = tmp.path() / "config.toml";

    std::ofstream out(config_path);
    out << R"(
[notifications]
duration_ms = 4200
show_success = false
show_info = false
)";
    out.close();

    expp::core::ConfigManager manager;
    auto result = manager.loadFrom(config_path);

    REQUIRE(result.has_value());
    CHECK(manager.config().notifications.durationMs == 4200);
    CHECK_FALSE(manager.config().notifications.showSuccess);
    CHECK_FALSE(manager.config().notifications.showInfo);
}

TEST_CASE("Notification config defaults are stable", "[core][config]") {
    const auto defaults = expp::core::ConfigManager::defaults();

    CHECK(defaults.notifications.durationMs == 2500);
    CHECK(defaults.notifications.showSuccess);
    CHECK(defaults.notifications.showInfo);
    CHECK_FALSE(defaults.versionControl.enabled);
    CHECK(defaults.versionControl.showIgnoredFiles);
    CHECK(defaults.versionControl.statusDetail == expp::core::VersionControlStatusDetail::Summary);
}

TEST_CASE("Config scalar sections save and load round-trip", "[core][config]") {
    TempDirectory tmp;

#ifdef _WIN32
    ScopedEnvVar appdata{"APPDATA", tmp.path().string()};
#else
    ScopedEnvVar xdg_config{"XDG_CONFIG_HOME", tmp.path().string()};
#endif

    expp::core::ConfigManager manager;
    auto config = expp::core::ConfigManager::defaults();
    config.preview.enabled = false;
    config.preview.maxLines = 18;
    config.preview.maxLineLength = 120;
    config.layout.parentPanelWidth = 31;
    config.layout.previewPanelWidth = 52;
    config.layout.showStatusBar = false;
    config.behavior.showHiddenFiles = true;
    config.behavior.confirmDelete = false;
    config.behavior.keyTimeoutMs = 1750;
    config.notifications.durationMs = 1800;
    config.notifications.showSuccess = false;
    config.notifications.showInfo = true;
    config.versionControl.enabled = true;
    config.versionControl.showIgnoredFiles = false;
    config.versionControl.statusDetail = expp::core::VersionControlStatusDetail::Full;
    manager.setConfig(config);

    auto save_result = manager.save();
    REQUIRE(save_result.has_value());

    expp::core::ConfigManager reloaded;
    auto load_result = reloaded.loadFrom(expp::core::ConfigManager::userConfigPath());
    REQUIRE(load_result.has_value());

    CHECK_FALSE(reloaded.config().preview.enabled);
    CHECK(reloaded.config().preview.maxLines == 18);
    CHECK(reloaded.config().preview.maxLineLength == 120);
    CHECK(reloaded.config().layout.parentPanelWidth == 31);
    CHECK(reloaded.config().layout.previewPanelWidth == 52);
    CHECK_FALSE(reloaded.config().layout.showStatusBar);
    CHECK(reloaded.config().behavior.showHiddenFiles);
    CHECK_FALSE(reloaded.config().behavior.confirmDelete);
    CHECK(reloaded.config().behavior.keyTimeoutMs == 1750);
    CHECK(reloaded.config().notifications.durationMs == 1800);
    CHECK_FALSE(reloaded.config().notifications.showSuccess);
    CHECK(reloaded.config().notifications.showInfo);
    CHECK(reloaded.config().versionControl.enabled);
    CHECK_FALSE(reloaded.config().versionControl.showIgnoredFiles);
    CHECK(reloaded.config().versionControl.statusDetail == expp::core::VersionControlStatusDetail::Full);
}

TEST_CASE("Version control config loads from TOML", "[core][config]") {
    TempDirectory tmp;
    const auto config_path = tmp.path() / "config.toml";

    std::ofstream out(config_path);
    out << R"(
[version_control]
enabled = true
show_ignored_files = false
status_detail = "compact"
)";
    out.close();

    expp::core::ConfigManager manager;
    auto result = manager.loadFrom(config_path);

    REQUIRE(result.has_value());
    CHECK(manager.config().versionControl.enabled);
    CHECK_FALSE(manager.config().versionControl.showIgnoredFiles);
    CHECK(manager.config().versionControl.statusDetail == expp::core::VersionControlStatusDetail::Compact);
}

TEST_CASE("Icon resolver applies priority rules", "[core][config][icons]") {
    auto config = expp::core::ConfigManager::defaults().icons;
    config.iconTheme["test_exact"] = "EXACT";
    config.iconTheme["test_txt"] = "TXT";
    config.iconTheme["test_cpp"] = "CPP";
    config.iconTheme["test_exec"] = "EXEC";
    config.iconTheme["test_link"] = "LINK";
    config.iconTheme["test_hidden"] = "HIDDEN";
    config.iconTheme["test_folder"] = "FOLDER";
    config.iconTheme["test_build"] = "BUILD";

    config.rules.exactFiles["CMakeLists.txt"] = "test_exact";
    config.rules.extensions["txt"] = "test_txt";
    config.rules.extensions["cpp"] = "test_cpp";
    config.rules.extensions["cxx"] = "test_cpp";
    config.rules.extensions["cc"] = "test_cpp";
    config.rules.exactFolders["build"] = "test_build";
    config.executableIconId = "test_exec";
    config.symlinkIconId = "test_link";
    config.hiddenIconId = "test_hidden";
    config.folderFallbackIconId = "test_folder";

    CHECK(expp::core::resolve_icon(config, make_entry("CMakeLists.txt", expp::core::filesystem::FileType::RegularFile))
          == "EXACT");
    CHECK(expp::core::resolve_icon(config, make_entry("main.CPP", expp::core::filesystem::FileType::RegularFile))
          == "CPP");
    CHECK(expp::core::resolve_icon(config, make_entry("unit.cxx", expp::core::filesystem::FileType::RegularFile))
          == "CPP");
    CHECK(expp::core::resolve_icon(config, make_entry("legacy.cc", expp::core::filesystem::FileType::RegularFile))
          == "CPP");
    CHECK(expp::core::resolve_icon(config, make_entry("tool", expp::core::filesystem::FileType::Executable)) == "EXEC");
    CHECK(expp::core::resolve_icon(config, make_entry("link", expp::core::filesystem::FileType::Symlink)) == "LINK");
    CHECK(expp::core::resolve_icon(config,
                                   make_entry(".env", expp::core::filesystem::FileType::RegularFile, true))
          == "HIDDEN");
    CHECK(expp::core::resolve_icon(config, make_entry("build", expp::core::filesystem::FileType::Directory))
          == "BUILD");
    CHECK(expp::core::resolve_icon(config, make_entry("src", expp::core::filesystem::FileType::Directory)) == "FOLDER");
}

TEST_CASE("Icon resolver falls back when configured icon IDs are missing", "[core][config][icons]") {
    auto config = expp::core::ConfigManager::defaults().icons;
    config.rules.exactFiles["unknown.icon"] = "missing_icon";
    config.fileFallbackIconId = "file_default";
    config.folderFallbackIconId = "folder_default";

    CHECK(expp::core::resolve_icon(config, make_entry("unknown.icon", expp::core::filesystem::FileType::RegularFile))
          == config.iconTheme.at("file_default"));

    config.rules.exactFolders["missing-folder"] = "missing_folder_icon";
    CHECK(expp::core::resolve_icon(config, make_entry("missing-folder", expp::core::filesystem::FileType::Directory))
          == config.iconTheme.at("folder_default"));
}

TEST_CASE("Icon config accepts legacy icons section", "[core][config][icons]") {
    TempDirectory tmp;
    const auto config_path = tmp.path() / "config.toml";

    std::ofstream out(config_path);
    out << R"(
[icons]
".cpp" = "LEGACY_CPP"
folder = "LEGACY_FOLDER"
exe = "LEGACY_EXEC"
link = "LEGACY_LINK"
default = "LEGACY_DEFAULT"
)";
    out.close();

    expp::core::ConfigManager manager;
    auto result = manager.loadFrom(config_path);

    REQUIRE(result.has_value());
    const auto& icons = manager.config().icons;
    CHECK(expp::core::resolve_icon(icons, make_entry("main.cpp", expp::core::filesystem::FileType::RegularFile))
          == "LEGACY_CPP");
    CHECK(expp::core::resolve_icon(icons, make_entry("folder", expp::core::filesystem::FileType::Directory))
          == "LEGACY_FOLDER");
    CHECK(expp::core::resolve_icon(icons, make_entry("tool", expp::core::filesystem::FileType::Executable))
          == "LEGACY_EXEC");
    CHECK(expp::core::resolve_icon(icons, make_entry("target", expp::core::filesystem::FileType::Symlink))
          == "LEGACY_LINK");
    CHECK(expp::core::resolve_icon(icons, make_entry("unknown", expp::core::filesystem::FileType::RegularFile))
          == "LEGACY_DEFAULT");
}

TEST_CASE("User icons.toml deep merges with built-in icon defaults", "[core][config][icons]") {
    TempDirectory tmp;

#ifdef _WIN32
    ScopedEnvVar appdata{"APPDATA", tmp.path().string()};
#else
    ScopedEnvVar xdg_config{"XDG_CONFIG_HOME", tmp.path().string()};
#endif

    const auto config_dir = tmp.path() / "expp";
    fs::create_directories(config_dir);
    std::ofstream out(config_dir / "icons.toml");
    out << R"(
[icon_theme]
file_cpp = "USER_CPP"
file_document = "DOC"
folder_build = "USER_BUILD"

[rules.extensions]
txt = "file_document"
".cxx" = "file_cpp"

[rules.exact_folders]
build = "folder_build"
)";
    out.close();

    expp::core::ConfigManager manager;
    auto result = manager.load();

    REQUIRE(result.has_value());
    const auto& icons = manager.config().icons;
    CHECK(expp::core::resolve_icon(icons, make_entry("main.cpp", expp::core::filesystem::FileType::RegularFile))
          == "USER_CPP");
    CHECK(expp::core::resolve_icon(icons, make_entry("readme.txt", expp::core::filesystem::FileType::RegularFile))
          == "DOC");
    CHECK(expp::core::resolve_icon(icons, make_entry("module.cxx", expp::core::filesystem::FileType::RegularFile))
          == "USER_CPP");
    CHECK(expp::core::resolve_icon(icons, make_entry("build", expp::core::filesystem::FileType::Directory))
          == "USER_BUILD");
    CHECK(expp::core::resolve_icon(icons, make_entry("script.py", expp::core::filesystem::FileType::RegularFile))
          == icons.iconTheme.at("file_python"));
}

TEST_CASE("Invalid user icons.toml reports a config error", "[core][config][icons]") {
    TempDirectory tmp;

#ifdef _WIN32
    ScopedEnvVar appdata{"APPDATA", tmp.path().string()};
#else
    ScopedEnvVar xdg_config{"XDG_CONFIG_HOME", tmp.path().string()};
#endif

    const auto config_dir = tmp.path() / "expp";
    fs::create_directories(config_dir);
    std::ofstream out(config_dir / "icons.toml");
    out << "[icon_theme\n";
    out.close();

    expp::core::ConfigManager manager;
    auto result = manager.load();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message().find("icons.toml") != std::string::npos);
}
