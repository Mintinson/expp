#include "expp/ui/key_handler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() / "expp_keymap_test";
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

}  // namespace

TEST_CASE("KeyMap loadFromFile reports skipped invalid bindings", "[ui][keymap]") {
    TempDirectory tmp;
    const auto config_path = tmp.path() / "keys.toml";

    std::ofstream out(config_path);
    out << R"(
[keys.normal]
move_down = "j"
bad_action = "<<"
numeric_value = 123

[keys.weird]
noop = "x"
)";
    out.close();

    expp::ui::KeyMap keymap;
    auto result = keymap.loadFromFile(config_path);

    REQUIRE(result.has_value());
    CHECK(result->loadedBindings.size() == 1);
    CHECK(result->loadedBindings.front().actionName == "move_down");
    CHECK(result->warnings.size() == 3);

    const auto* binding = keymap.findExact({expp::ui::Key::fromChar('j')}, expp::ui::Mode::Normal);
    REQUIRE(binding != nullptr);
    CHECK(binding->actionName == "move_down");
}

TEST_CASE("KeyMap loadFromFile returns parse failure for invalid TOML", "[ui][keymap]") {
    TempDirectory tmp;
    const auto config_path = tmp.path() / "invalid.toml";

    std::ofstream out(config_path);
    out << R"(
[keys.normal
move_down = "j"
)";
    out.close();

    expp::ui::KeyMap keymap;
    auto result = keymap.loadFromFile(config_path);

    CHECK_FALSE(result.has_value());
}
