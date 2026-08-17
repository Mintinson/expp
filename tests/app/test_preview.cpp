#include "expp/app/explorer_services.hpp"
#include "expp/app/preview_provider.hpp"
#include "expp/core/config.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("expp_preview_test_" + std::to_string(suffix));
        std::error_code ec;
        fs::create_directories(path_, ec);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_bytes(const fs::path& path, std::span<const unsigned char> bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

class StubProvider final : public expp::app::PreviewProvider {
public:
    StubProvider(std::string pattern, int priority)
        : capabilities_{
              expp::app::PreviewProviderCapability{.mimePattern = std::move(pattern),
                                                   .priority = priority}
    } {}

    [[nodiscard]] std::span<const expp::app::PreviewProviderCapability> capabilities()
        const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] expp::core::Task<expp::core::Result<expp::app::PreviewPayload>> load(
        const expp::app::PreviewRequest&, const expp::app::MimePayload&) const override {
        co_return expp::app::PreviewPayload{};
    }

private:
    std::array<expp::app::PreviewProviderCapability, 1> capabilities_;
};

class ScopedPreviewConfig {
public:
    explicit ScopedPreviewConfig(expp::core::PreviewConfig preview) {
        oldConfig_ = expp::core::global_config().config();
        auto config = oldConfig_;
        config.preview = preview;
        expp::core::global_config().setConfig(std::move(config));
    }

    ~ScopedPreviewConfig() { expp::core::global_config().setConfig(oldConfig_); }

private:
    expp::core::Config oldConfig_;
};

class ScopedEnvironment {
public:
    ScopedEnvironment(std::string name, std::string value) : name_(std::move(name)) {
        if (const char* previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
#ifdef _WIN32
        (void)_putenv_s(name_.c_str(), value.c_str());
#else
        (void)setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvironment() {
#ifdef _WIN32
        (void)_putenv_s(name_.c_str(), previous_.value_or("").c_str());
#else
        if (previous_) {
            (void)setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            (void)unsetenv(name_.c_str());
        }
#endif
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

[[nodiscard]] bool has_role(const expp::app::RichTextPreview& preview,
                            expp::app::PreviewTextRole role) {
    for (const auto& line : preview.lines) {
        for (const auto& fragment : line) {
            if (fragment.role == role) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::string plain_line(const std::vector<expp::app::RichTextFragment>& fragments) {
    std::string line;
    for (const auto& fragment : fragments) {
        line += fragment.text;
    }
    return line;
}

}  // namespace

TEST_CASE("MIME detection uses file content instead of extension", "[app][preview][mime]") {
    TempDirectory temp;
    const auto path = temp.path() / "misleading.txt";
    constexpr std::array<unsigned char, 16> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n',
                                                0,    0,   0,   0,   'I',  'H',  'D',  'R'};
    write_bytes(path, png);

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.mime->detectMime({.target = path}));

    REQUIRE(result.has_value());
    CHECK(result->mimeType == "image/png");
}

TEST_CASE("MIME header probing respects the request limit", "[app][preview][mime]") {
    TempDirectory temp;
    const auto path = temp.path() / "header.bin";
    std::array<unsigned char, 300> bytes{};
    bytes[257] = 'u';
    bytes[258] = 's';
    bytes[259] = 't';
    bytes[260] = 'a';
    bytes[261] = 'r';
    write_bytes(path, bytes);

    auto services = expp::app::make_default_explorer_services();
    const auto bounded =
        services.runtime->blockOn(services.mime->detectMime({.target = path, .headerBytes = 256}));
    const auto extended =
        services.runtime->blockOn(services.mime->detectMime({.target = path, .headerBytes = 262}));

    REQUIRE(bounded.has_value());
    REQUIRE(extended.has_value());
    CHECK(bounded->mimeType == "application/octet-stream");
    CHECK(extended->mimeType == "application/x-tar");
}

TEST_CASE("Preview registry chooses the highest matching priority", "[app][preview][registry]") {
    expp::app::PreviewProviderRegistry registry;
    const auto wildcard = std::make_shared<StubProvider>("*/*", 1);
    const auto generic_text = std::make_shared<StubProvider>("text/*", 20);
    const auto exact_text = std::make_shared<StubProvider>("text/plain", 50);
    registry.registerProvider(wildcard);
    registry.registerProvider(generic_text);
    registry.registerProvider(exact_text);

    CHECK(registry.findProvider("text/plain") == exact_text);
    CHECK(registry.findProvider("text/csv") == generic_text);
    CHECK(registry.findProvider("image/png") == wildcard);
}

TEST_CASE("Text preview filters controls and enforces limits", "[app][preview][text]") {
    TempDirectory temp;
    const auto path = temp.path() / "content.bin";
    std::ofstream output(path, std::ios::binary);
    output << "abc" << static_cast<char>(1) << "defghijklmnopqrstuvwxyz\nsecond\nthird\n";
    output.close();

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.preview->loadPreview({
        .target = path,
        .maxLineLength = 10,
        .maxBytes = 128,
        .chunkLines = 2,
        .viewport = {.width = 40, .height = 10},
    }));

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<expp::app::PlainTextPreview>(result->content));
    REQUIRE(result->lines.size() == 2);
    CHECK(result->lines.front() == "abc def...");
    CHECK(result->canScrollDown);
}

TEST_CASE("Text preview highlights common source and config files", "[app][preview][text]") {
    auto preview_config = expp::core::ConfigManager::defaults().preview;
    preview_config.syntaxHighlight = true;
    const ScopedPreviewConfig scoped{preview_config};

    TempDirectory temp;
    const auto cpp_path = temp.path() / "main.cpp";
    const auto cmake_path = temp.path() / "CMakeLists.txt";
    const auto json_path = temp.path() / "settings.json";
    const auto config_path = temp.path() / "app.toml";
    {
        std::ofstream out(cpp_path);
        out << "int main() { return 42; } // comment\n";
    }
    {
        std::ofstream out(cmake_path);
        out << "add_executable(app main.cpp)\n# comment\n";
    }
    {
        std::ofstream out(json_path);
        out << R"({"enabled": true, "count": 2})" << '\n';
    }
    {
        std::ofstream out(config_path);
        out << "enabled = true\n";
    }

    auto services = expp::app::make_default_explorer_services();
    const auto request = [](const fs::path& path) {
        return expp::app::PreviewRequest{
            .target = path,
            .maxBytes = 65536,
            .chunkLines = 20,
            .viewport = {.width = 80, .height = 10},
        };
    };

    const auto cpp = services.runtime->blockOn(services.preview->loadPreview(request(cpp_path)));
    const auto cmake =
        services.runtime->blockOn(services.preview->loadPreview(request(cmake_path)));
    const auto json = services.runtime->blockOn(services.preview->loadPreview(request(json_path)));
    const auto config =
        services.runtime->blockOn(services.preview->loadPreview(request(config_path)));

    REQUIRE(cpp.has_value());
    REQUIRE(cmake.has_value());
    REQUIRE(json.has_value());
    REQUIRE(config.has_value());
    REQUIRE(std::holds_alternative<expp::app::RichTextPreview>(cpp->content));
    REQUIRE(std::holds_alternative<expp::app::RichTextPreview>(cmake->content));
    REQUIRE(std::holds_alternative<expp::app::RichTextPreview>(json->content));
    REQUIRE(std::holds_alternative<expp::app::RichTextPreview>(config->content));
    CHECK(has_role(std::get<expp::app::RichTextPreview>(cpp->content),
                   expp::app::PreviewTextRole::Keyword));
    CHECK(has_role(std::get<expp::app::RichTextPreview>(cpp->content),
                   expp::app::PreviewTextRole::Comment));
    CHECK(has_role(std::get<expp::app::RichTextPreview>(cmake->content),
                   expp::app::PreviewTextRole::Keyword));
    CHECK(has_role(std::get<expp::app::RichTextPreview>(json->content),
                   expp::app::PreviewTextRole::String));
    CHECK(has_role(std::get<expp::app::RichTextPreview>(config->content),
                   expp::app::PreviewTextRole::Type));
}

TEST_CASE("Image preview uses Kitty graphics in Ghostty", "[app][preview][image]") {
    const ScopedEnvironment terminal{"TERM", "xterm-ghostty"};
    const ScopedPreviewConfig scoped{expp::core::ConfigManager::defaults().preview};

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.preview->loadPreview({
        .target = fs::path{SOURCE_PATH}
          / "resource" / "image.png",
        .maxBytes = 65536,
        .chunkLines = 20,
        .viewport = {.width = 40, .height = 12},
    }));

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<expp::app::ImagePreview>(result->content));
    const auto& image = std::get<expp::app::ImagePreview>(result->content);
    CHECK(image.protocol == "kitty");
    CHECK(image.renderedInline);
    CHECK(image.displayColumns == 23);
    CHECK(image.displayRows == 9);
    CHECK(image.escapeStream.starts_with("\x1b_Ga=d,d=I,i=1,q=2\x1b\\\x1b_Ga=T,f=100,i=1,p=1,q=2"));
    REQUIRE(result->lines.size() == 4);
    CHECK(result->lines.back().starts_with("Size: "));
}

TEST_CASE("Image preview fits its terminal viewport", "[app][preview][image]") {
    const ScopedEnvironment terminal{"TERM", "xterm-ghostty"};
    const ScopedPreviewConfig scoped{expp::core::ConfigManager::defaults().preview};

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.preview->loadPreview({
        .target = fs::path{SOURCE_PATH}
          / "resource" / "image.png",
        .maxBytes = 65536,
        .chunkLines = 20,
        .viewport = {.width = 60, .height = 40},
    }));

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<expp::app::ImagePreview>(result->content));
    const auto& image = std::get<expp::app::ImagePreview>(result->content);
    CHECK(image.escapeStream.find("\x1b_Ga=T,f=100,i=1,p=1,q=2,c=48,r=19,") != std::string::npos);
}

#if EXPP_HAS_TREE_SITTER
TEST_CASE("Tree-sitter highlights bundled language fixtures", "[app][preview][tree-sitter]") {
    auto preview_config = expp::core::ConfigManager::defaults().preview;
    preview_config.syntaxHighlight = true;
    const ScopedPreviewConfig scoped{preview_config};

    struct Fixture {
        std::string_view filename;
        std::array<expp::app::PreviewTextRole, 2> requiredRoles;
    };

    static constexpr std::array fixtures{
        Fixture{"sample.c",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::Type}   },
        Fixture{"sample.cpp",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::Type}   },
        Fixture{"sample.py",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.rs",
                {expp::app::PreviewTextRole::Comment, expp::app::PreviewTextRole::Number} },
        Fixture{"sample.js",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.go",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"Sample.java",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::Type}   },
        Fixture{"Sample.cs",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.rb",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::Comment}},
        Fixture{"sample.sh",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.ts",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.tsx",
                {expp::app::PreviewTextRole::Keyword, expp::app::PreviewTextRole::String} },
        Fixture{"sample.json",
                {expp::app::PreviewTextRole::String, expp::app::PreviewTextRole::Keyword} },
    };

    auto services = expp::app::make_default_explorer_services();
    const auto fixture_directory = fs::path{SOURCE_PATH} / "resource" / "syntax_samples";
    for (const auto& fixture : fixtures) {
        CAPTURE(fixture.filename);
        const auto result = services.runtime->blockOn(services.preview->loadPreview({
            .target = fixture_directory / fixture.filename,
            .maxBytes = 65536,
            .chunkLines = 50,
            .viewport = {.width = 100, .height = 20},
        }));

        REQUIRE(result.has_value());
        REQUIRE(std::holds_alternative<expp::app::RichTextPreview>(result->content));
        const auto& rich = std::get<expp::app::RichTextPreview>(result->content);
        REQUIRE(rich.lines.size() == result->lines.size());
        for (std::size_t index = 0; index < rich.lines.size(); ++index) {
            CHECK(plain_line(rich.lines[index]) == result->lines[index]);
        }
        for (const auto role : fixture.requiredRoles) {
            CAPTURE(static_cast<int>(role));
            CHECK(has_role(rich, role));
        }
    }
}
#endif

TEST_CASE("Renamed ZIP previews without extraction", "[app][preview][archive]") {
    TempDirectory temp;
    const auto path = temp.path() / "archive.data";
    fs::copy_file(fs::path{SOURCE_PATH} / "resource" / "archive.zip", path);

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.preview->loadPreview({
        .target = path,
        .maxBytes = 65536,
        .chunkLines = 20,
    }));

    REQUIRE(result.has_value());
    CHECK(result->mimeType == "application/zip");
    CHECK(std::holds_alternative<expp::app::ListingPreview>(result->content));
    CHECK_FALSE(result->lines.empty());
}

TEST_CASE("Unknown binary preview is an adaptive hex dump", "[app][preview][hex]") {
    TempDirectory temp;
    const auto path = temp.path() / "unknown.txt";
    constexpr std::array<unsigned char, 8> bytes{0, 1, 2, 3, 'A', 'B', 0xFF, 0x7F};
    write_bytes(path, bytes);

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(services.preview->loadPreview({
        .target = path,
        .maxBytes = 64,
        .chunkLines = 2,
        .viewport = {.width = 60, .height = 5},
    }));

    REQUIRE(result.has_value());
    REQUIRE(std::holds_alternative<expp::app::HexDumpPreview>(result->content));
    REQUIRE_FALSE(result->lines.empty());
    CHECK(result->lines.front().starts_with("00000000"));
    CHECK(result->lines.front().find("|....AB..|") != std::string::npos);
}

TEST_CASE("Failed advanced preview falls back to metadata", "[app][preview][fallback]") {
    TempDirectory temp;
    const auto path = temp.path() / "broken.gz";
    constexpr std::array<unsigned char, 6> bytes{0x1F, 0x8B, 0, 0, 0, 0};
    write_bytes(path, bytes);

    auto services = expp::app::make_default_explorer_services();
    const auto result = services.runtime->blockOn(
        services.preview->loadPreview({.target = path, .chunkLines = 10}));

    REQUIRE(result.has_value());
    CHECK(std::holds_alternative<expp::app::MetadataPreview>(result->content));
    CHECK_FALSE(result->lines.empty());
    CHECK_FALSE(result->diagnostic.empty());
}
