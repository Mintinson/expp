#include "expp/core/config.hpp"
#include "expp/ui/components.hpp"
#include "expp/ui/theme.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

[[nodiscard]] std::string render_preview(const expp::app::PreviewModel& model) {
    expp::ui::PreviewComponent component{expp::ui::PreviewRenderConfig{.maxRenderLines = 8}};
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60), ftxui::Dimension::Fixed(8));
    ftxui::Render(screen, component.render(model));
    return screen.ToString();
}

[[nodiscard]] std::string render_preview(expp::ui::PreviewComponent& component,
                                         const expp::app::PreviewModel& model) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(60), ftxui::Dimension::Fixed(8));
    ftxui::Render(screen, component.render(model));
    return screen.ToString();
}

[[nodiscard]] bool has_visible_output(const std::string& output) {
    return output.find_first_not_of(" \r\n") != std::string::npos;
}

[[nodiscard]] ftxui::Color render_role_color(const expp::ui::Theme& theme,
                                             expp::app::PreviewTextRole role) {
    expp::ui::PreviewComponent component{
        expp::ui::PreviewRenderConfig{
                                      .theme = &theme,
                                      .maxRenderLines = 1,
                                      }
    };
    const expp::app::PreviewModel model = expp::app::PreviewReadyState{
        .target = std::filesystem::path{"sample"},
        .lines = {"x"},
        .content = expp::app::RichTextPreview{.lines = {{{.text = "x", .role = role}}}},
        .mimeType = "text/plain",
        .diagnostic = {},
    };
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ftxui::Render(screen, component.render(model));
    return screen.PixelAt(0, 0).foreground_color;
}

}  // namespace

TEST_CASE("Preview component renders loading and errors", "[ui][preview]") {
    const auto loading =
        render_preview(expp::app::PreviewLoadingState{.target = std::filesystem::path{"file.txt"}});
    const auto error = render_preview(expp::app::PreviewErrorState{
        .target = std::filesystem::path{"file.txt"}, .message = "permission denied"});

    CHECK(loading.find("[Loading...]") != std::string::npos);
    CHECK(error.find("[Error: permission denied]") != std::string::npos);
}

TEST_CASE("Preview component renders every typed content variant", "[ui][preview]") {
    std::vector<expp::app::PreviewContent> contents;
    contents.emplace_back(expp::app::PlainTextPreview{.lines = {"plain text"}});
    contents.emplace_back(expp::app::RichTextPreview{
        .lines = {{{.text = "keyword", .role = expp::app::PreviewTextRole::Keyword}}}});
    contents.emplace_back(
        expp::app::ImagePreview{.protocol = "kitty", .escapeStream = {}, .renderedInline = false});
    contents.emplace_back(expp::app::ListingPreview{.entries = {"[F] entry.txt"}});
    contents.emplace_back(
        expp::app::HexDumpPreview{.lines = {"00000000  41 |A|"}, .baseOffset = 0});
    contents.emplace_back(expp::app::MetadataPreview{.lines = {"Type: binary data"}});

    for (auto& content : contents) {
        const auto output = render_preview(expp::app::PreviewReadyState{
            .target = std::filesystem::path{"target"},
            .lines = {"image metadata fallback"},
            .content = std::move(content),
            .mimeType = "application/octet-stream",
            .diagnostic = {},
        });
        CHECK(has_visible_output(output));
    }
}

TEST_CASE("Preview component emits an inline image stream", "[ui][preview][image]") {
    constexpr std::string_view image_stream{"\x1b_Ga=T,f=100;AQ==\x1b\\"};
    const expp::app::PreviewModel model = expp::app::PreviewReadyState{
        .target = std::filesystem::path{"sample.png"},
        .lines = {},
        .content =
            expp::app::ImagePreview{
                                        .protocol = "kitty",
                                        .escapeStream = std::string{image_stream},
                                        .renderedInline = true,
                                        },
        .mimeType = "image/png",
        .diagnostic = {},
    };

    CHECK(render_preview(model).find(image_stream) != std::string::npos);
}

TEST_CASE("Preview component emits Kitty images only on change", "[ui][preview][image]") {
    constexpr std::string_view image_stream{"\x1b_Ga=T,f=100;AQ==\x1b\\"};
    constexpr std::string_view delete_stream{"\x1b_Ga=d,d=I,i=1,q=2\x1b\\"};
    const expp::app::PreviewModel image = expp::app::PreviewReadyState{
        .target = std::filesystem::path{"sample.png"},
        .lines = {"[Image]"},
        .content = expp::app::ImagePreview{.protocol = "kitty",
                                        .escapeStream = std::string{image_stream},
                                        .renderedInline = true},
        .mimeType = "image/png",
        .diagnostic = {},
    };
    const expp::app::PreviewModel text = expp::app::PreviewReadyState{
        .target = std::filesystem::path{"sample.txt"},
        .lines = {"plain text"},
        .content = expp::app::PlainTextPreview{.lines = {"plain text"}},
        .mimeType = "text/plain",
        .diagnostic = {},
    };
    expp::ui::PreviewComponent component{expp::ui::PreviewRenderConfig{.maxRenderLines = 8}};

    CHECK(render_preview(component, image).find(image_stream) != std::string::npos);
    CHECK(render_preview(component, image).find(image_stream) == std::string::npos);
    CHECK(render_preview(component, text).find(delete_stream) != std::string::npos);
    CHECK(render_preview(component, text).find(delete_stream) == std::string::npos);
}

TEST_CASE("Preview component renders image details above an inline image", "[ui][preview][image]") {
    constexpr std::string_view image_stream{"\x1b_Ga=T,f=100;AQ==\x1b\\"};
    const expp::app::PreviewModel model = expp::app::PreviewReadyState{
        .target = std::filesystem::path{"sample.png"},
        .lines = {"[Image]", "MIME: image/png", "Dimensions: 899x707", "Size: 24.7 KiB"},
        .content =
            expp::app::ImagePreview{
                                        .protocol = "kitty",
                                        .escapeStream = std::string{image_stream},
                                        .displayColumns = 20,
                                        .displayRows = 2,
                                        .renderedInline = true,
                                        },
        .mimeType = "image/png",
        .diagnostic = {},
    };

    const auto output = render_preview(model);
    CHECK(output.find("MIME: image/png") != std::string::npos);
    CHECK(output.find("Size: 24.7 KiB") != std::string::npos);
    CHECK(output.find(image_stream) != std::string::npos);
    CHECK(output.find(std::string{image_stream} + "\x1b[6;21H") != std::string::npos);
}

TEST_CASE("Preview syntax colors follow theme defaults and overrides", "[ui][preview][theme]") {
    expp::core::ColorTheme config;
    config.foreground = 0x010203;
    config.sourceCode = 0x111213;
    config.added = 0x212223;
    config.hidden = 0x313233;
    config.searchHighlight = 0x414243;
    config.config = 0x515253;
    config.conflicted = 0x616263;

    expp::ui::Theme theme{config, expp::core::IconConfig{}};
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Normal) ==
          expp::ui::hex_to_color(config.foreground));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Keyword) ==
          expp::ui::hex_to_color(config.sourceCode));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::String) ==
          expp::ui::hex_to_color(config.added));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Comment) ==
          expp::ui::hex_to_color(config.hidden));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Number) ==
          expp::ui::hex_to_color(config.searchHighlight));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Type) ==
          expp::ui::hex_to_color(config.config));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Diagnostic) ==
          expp::ui::hex_to_color(config.conflicted));

    config.syntax.base = 0xA0B0C0;
    config.syntax.string = 0x0A0B0C;
    theme.reload(config);
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::Keyword) ==
          expp::ui::hex_to_color(*config.syntax.base));
    CHECK(theme.getPreviewTextColor(expp::app::PreviewTextRole::String) ==
          expp::ui::hex_to_color(*config.syntax.string));
    CHECK(render_role_color(theme, expp::app::PreviewTextRole::Keyword) ==
          expp::ui::hex_to_color(*config.syntax.base));
    CHECK(render_role_color(theme, expp::app::PreviewTextRole::String) ==
          expp::ui::hex_to_color(*config.syntax.string));
}
