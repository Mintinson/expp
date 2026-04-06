#include "expp/app/explorer_commands.hpp"
#include "expp/ui/components.hpp"
#include "expp/ui/key_handler.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("buildHelpEntries groups bindings by action metadata", "[ui][help]") {
    expp::ui::ActionRegistry actions;
    actions.registerAction(expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown),
                           [](const expp::ui::ActionContext&) {},
                           "Move cursor down",
                           "Navigation",
                           true);
    actions.registerAction(expp::app::to_command_id(expp::app::ExplorerCommand::Search),
                           [](const expp::ui::ActionContext&) {},
                           "Search files",
                           "Search",
                           false);

    expp::ui::KeyMap keymap;
    REQUIRE(keymap.bind("j",
                        expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown),
                        expp::ui::Mode::Normal,
                        "Move down")
                .has_value());
    REQUIRE(
        keymap.bind("/", expp::app::to_command_id(expp::app::ExplorerCommand::Search), expp::ui::Mode::Normal)
            .has_value());
    REQUIRE(keymap.bind("J",
                        expp::app::to_command_id(expp::app::ExplorerCommand::MoveDown),
                        expp::ui::Mode::Visual,
                        "Move down (visual)")
                .has_value());

    const auto entries = expp::ui::build_help_entries(actions.actions(), keymap.bindings());

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].category == "Navigation");
    CHECK(entries[0].shortcut == "j");
    CHECK(entries[0].description == "Move down");
    CHECK(entries[1].category == "Navigation");
    CHECK(entries[1].mode == expp::ui::Mode::Visual);
    CHECK(entries[2].category == "Search");
}

TEST_CASE("HelpMenuModel filters shortcuts and descriptions", "[ui][help]") {
    expp::ui::HelpMenuModel model;
    model.setEntries({
        {.category = "Navigation", .shortcut = "gh", .description = "Go home", .mode = expp::ui::Mode::Normal},
        {.category = "Help", .shortcut = "~", .description = "Open help menu", .mode = expp::ui::Mode::Normal},
    });

    SECTION("filter matches shortcut text") {
        model.setFilter("gh");
        REQUIRE(model.filteredCount() == 1);
        CHECK(model.filteredEntry(0).description == "Go home");
        CHECK(model.filteredSourceIndex(0) == 0);
    }

    SECTION("filter matches description text") {
        model.setFilter("help");
        REQUIRE(model.filteredCount() == 1);
        CHECK(model.filteredEntry(0).shortcut == "~");
        CHECK(model.filteredSourceIndex(0) == 1);
    }
}

TEST_CASE("clampHelpViewport keeps selection and scroll valid", "[ui][help]") {
    SECTION("selection and scroll are clamped to available entries") {
        expp::ui::HelpViewport viewport{
            .selectedIndex = 8,
            .scrollOffset = 6,
            .viewportRows = 4,
        };

        const auto clamped = expp::ui::clamp_help_viewport(viewport, 3);
        CHECK(clamped.selectedIndex == 2);
        CHECK(clamped.scrollOffset == 0);
    }

    SECTION("scroll follows selection with threshold-style movement") {
        expp::ui::HelpViewport viewport{
            .selectedIndex = 9,
            .scrollOffset = 0,
            .viewportRows = 12,
        };

        const auto clamped = expp::ui::clamp_help_viewport(viewport, 40);
        CHECK(clamped.selectedIndex == 9);
        CHECK(clamped.scrollOffset == 1);
    }

    SECTION("scroll keeps the selected row visible near the end of the list") {
        expp::ui::HelpViewport viewport{
            .selectedIndex = 39,
            .scrollOffset = 20,
            .viewportRows = 12,
        };

        const auto clamped = expp::ui::clamp_help_viewport(viewport, 40);
        CHECK(clamped.selectedIndex == 39);
        CHECK(clamped.scrollOffset == 28);
    }

    SECTION("category headers are accounted for in filtered help viewports") {
        expp::ui::HelpMenuModel model;
        model.setEntries({
            {.category = "Navigation", .shortcut = "j", .description = "Move down", .mode = expp::ui::Mode::Normal},
            {.category = "Navigation", .shortcut = "k", .description = "Move up", .mode = expp::ui::Mode::Normal},
            {.category = "Search", .shortcut = "/", .description = "Search", .mode = expp::ui::Mode::Normal},
            {.category = "Search", .shortcut = "n", .description = "Next match", .mode = expp::ui::Mode::Normal},
            {.category = "View", .shortcut = ",n", .description = "Natural sort", .mode = expp::ui::Mode::Normal},
        });

        expp::ui::HelpViewport viewport{
            .selectedIndex = 4,
            .scrollOffset = 0,
            .viewportRows = 4,
        };

        const auto clamped = expp::ui::clamp_help_viewport(viewport, model);
        CHECK(clamped.selectedIndex == 4);
        CHECK(clamped.scrollOffset > 0);
    }
}
