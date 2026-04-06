#include "expp/app/explorer_commands.hpp"
#include "expp/ui/key_handler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>

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
    auto result = keymap.loadFromFile(config_path, [](std::string_view name) -> std::optional<expp::ui::CommandId> {
        if (const auto command = expp::app::command_from_name(name)) {
            return expp::app::to_command_id(*command);
        }
        return std::nullopt;
    });

    REQUIRE(result.has_value());
    CHECK(result->loadedBindings.size() == 1);
    CHECK(result->loadedBindings.front().commandName == "move_down");
    CHECK(result->warnings.size() == 3);

    const auto* binding = keymap.findExact({expp::ui::Key::fromChar('j')}, expp::ui::Mode::Normal);
    REQUIRE(binding != nullptr);
    CHECK(binding->commandId == expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown));
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
    auto result = keymap.loadFromFile(config_path, [](std::string_view name) -> std::optional<expp::ui::CommandId> {
        if (const auto command = expp::app::command_from_name(name)) {
            return expp::app::to_command_id(*command);
        }
        return std::nullopt;
    });

    CHECK_FALSE(result.has_value());
}

TEST_CASE("Default binding catalog installs directory jump and help bindings", "[ui][keymap]") {
    expp::ui::KeyMap keymap;
    for (const auto& binding : expp::app::default_bindings()) {
        REQUIRE(keymap.bind(binding.keys,
                            expp::app::to_command_id(binding.command),
                            binding.mode,
                            std::string{binding.description})
                    .has_value());
    }

    CHECK(keymap.findExact({expp::ui::Key::fromChar('g'), expp::ui::Key::fromChar('h')}, expp::ui::Mode::Normal) !=
          nullptr);
    CHECK(keymap.findExact({expp::ui::Key::fromChar('g'), expp::ui::Key::fromChar('c')}, expp::ui::Mode::Normal) !=
          nullptr);
    CHECK(keymap.findExact({expp::ui::Key::fromChar('g'), expp::ui::Key::fromChar('l')}, expp::ui::Mode::Normal) !=
          nullptr);
    CHECK(keymap.findExact({expp::ui::Key::fromChar('g'), expp::ui::Key::fromChar(':')}, expp::ui::Mode::Normal) !=
          nullptr);
    CHECK(keymap.findExact({expp::ui::Key::fromChar('~')}, expp::ui::Mode::Normal) != nullptr);
}

TEST_CASE("Command catalog resolves every default binding", "[ui][keymap][catalog]") {
    for (const auto& binding : expp::app::default_bindings()) {
        const auto command = expp::app::command_from_id(expp::app::to_command_id(binding.command));
        REQUIRE(command.has_value());
        CHECK(expp::app::command_name(*command) == expp::app::command_spec(binding.command).name);
    }
}

TEST_CASE("KeyHandler executes numeric prefixes and multi-key sequences", "[ui][key_handler]") {
    expp::ui::KeyHandler handler;

    int move_down_count = 0;
    int go_top_count = 0;
    handler.actions().registerAction(expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown),
                                     [&](const expp::ui::ActionContext& ctx) { move_down_count = ctx.count; },
                                     "Move cursor down",
                                     "Navigation",
                                     true);
    handler.actions().registerAction(expp::app::to_command_id(expp::app::ExplorerCommand::GoTop),
                                     [&](const expp::ui::ActionContext&) { ++go_top_count; },
                                     "Go to top",
                                     "Navigation",
                                     false);

    REQUIRE(handler.keymap().bind("j",
                                  expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown),
                                  expp::ui::Mode::Normal)
                .has_value());
    REQUIRE(handler.keymap().bind("gg",
                                  expp::app::to_command_id(expp::app::ExplorerCommand::GoTop),
                                  expp::ui::Mode::Normal)
                .has_value());

    CHECK(handler.handle(ftxui::Event::Character('3')));
    CHECK(handler.handle(ftxui::Event::Character('j')));
    CHECK(move_down_count == 3);
    CHECK(handler.numericPrefix() == 1);

    CHECK(handler.handle(ftxui::Event::Character('g')));
    CHECK(handler.hasPendingSequence());
    CHECK(handler.handle(ftxui::Event::Character('g')));
    CHECK(go_top_count == 1);
}
