#include "expp/core/config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

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
}

TEST_CASE("Notification config save and load round-trip", "[core][config]") {
    TempDirectory tmp;

#ifdef _WIN32
    ScopedEnvVar appdata{"APPDATA", tmp.path().string()};
#else
    ScopedEnvVar xdg_config{"XDG_CONFIG_HOME", tmp.path().string()};
#endif

    expp::core::ConfigManager manager;
    auto config = expp::core::ConfigManager::defaults();
    config.notifications.durationMs = 1800;
    config.notifications.showSuccess = false;
    config.notifications.showInfo = true;
    manager.setConfig(config);

    auto save_result = manager.save();
    REQUIRE(save_result.has_value());

    expp::core::ConfigManager reloaded;
    auto load_result = reloaded.loadFrom(expp::core::ConfigManager::userConfigPath());
    REQUIRE(load_result.has_value());

    CHECK(reloaded.config().notifications.durationMs == 1800);
    CHECK_FALSE(reloaded.config().notifications.showSuccess);
    CHECK(reloaded.config().notifications.showInfo);
}
