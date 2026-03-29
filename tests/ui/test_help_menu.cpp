#include "expp/ui/components.hpp"
#include "expp/ui/key_handler.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("buildHelpEntries groups bindings by action metadata", "[ui][help]") {
    expp::ui::ActionRegistry actions;
    actions.registerAction("move_down", [](const expp::ui::ActionContext&) {}, "Move cursor down", "Navigation",
                           true);
    actions.registerAction("search", [](const expp::ui::ActionContext&) {}, "Search files", "Search", false);

    expp::ui::KeyMap keymap;
    REQUIRE(keymap.bind("j", "move_down", expp::ui::Mode::Normal, "Move down").has_value());
    REQUIRE(keymap.bind("/", "search", expp::ui::Mode::Normal).has_value());
    REQUIRE(keymap.bind("J", "move_down", expp::ui::Mode::Visual, "Move down (visual)").has_value());

    const auto entries = expp::ui::build_help_entries(actions.actions(), keymap.bindings());

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].category == "Navigation");
    CHECK(entries[0].shortcut == "j");
    CHECK(entries[0].description == "Move down");
    CHECK(entries[1].category == "Navigation");
    CHECK(entries[1].mode == expp::ui::Mode::Visual);
    CHECK(entries[2].category == "Search");
}

TEST_CASE("filterHelpEntries matches shortcuts and descriptions", "[ui][help]") {
    const std::vector<expp::ui::HelpEntry> entries{
        {.category = "Navigation", .shortcut = "gh", .description = "Go home", .mode = expp::ui::Mode::Normal},
        {.category = "Help", .shortcut = "~", .description = "Open help menu", .mode = expp::ui::Mode::Normal},
    };

    SECTION("filter matches shortcut text") {
        const auto filtered = expp::ui::filter_help_entries(entries, "gh");
        REQUIRE(filtered.size() == 1);
        CHECK(filtered.front().description == "Go home");
    }

    SECTION("filter matches description text") {
        const auto filtered = expp::ui::filter_help_entries(entries, "help");
        REQUIRE(filtered.size() == 1);
        CHECK(filtered.front().shortcut == "~");
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
}
