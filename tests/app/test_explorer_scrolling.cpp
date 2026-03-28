#include "expp/app/explorer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {
class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() / "expp_scroll_test";
        fs::remove_all(path_);
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }

    void createFiles(int count) const {
        for (int i = 0; i < count; ++i) {
            const fs::path file_path = path_ / ("file_" + std::to_string(i) + ".txt");
            std::ofstream out(file_path);
            out << "line\n";
        }
    }

private:
    fs::path path_;
};
}  // namespace

TEST_CASE("Explorer bottom selection shows last page", "[app][explorer][scroll]") {
    TempDirectory tmp;
    tmp.createFiles(30);

    auto explorer_result = expp::app::Explorer::create(tmp.path());
    REQUIRE(explorer_result.has_value());
    auto explorer = *explorer_result;
    explorer->setViewportRows(7);
    explorer->goToBottom();

    const auto& state = explorer->state();
    REQUIRE(state.entries.size() == 30);
    CHECK(state.currentSelected == 29);
    CHECK(state.currentScrollOffset == 23);
}

TEST_CASE("Explorer scroll offset clamps after viewport expansion", "[app][explorer][scroll]") {
    TempDirectory tmp;
    tmp.createFiles(24);

    auto explorer_result = expp::app::Explorer::create(tmp.path());
    REQUIRE(explorer_result.has_value());
    auto explorer = *explorer_result;
    explorer->setViewportRows(5);
    explorer->goToBottom();

    {
        const auto& state = explorer->state();
        CHECK(state.currentSelected == 23);
        CHECK(state.currentScrollOffset == 19);
    }

    explorer->setViewportRows(40);

    const auto& state = explorer->state();
    CHECK(state.currentSelected == 23);
    CHECK(state.currentScrollOffset == 0);
}

TEST_CASE("Explorer create fails for missing startup directory", "[app][explorer][errors]") {
    TempDirectory tmp;
    const auto missing = tmp.path() / "missing";

    auto explorer_result = expp::app::Explorer::create(missing);

    CHECK_FALSE(explorer_result.has_value());
}

TEST_CASE("Explorer refresh surfaces directory disappearance", "[app][explorer][errors]") {
    TempDirectory tmp;
    tmp.createFiles(3);

    auto explorer_result = expp::app::Explorer::create(tmp.path());
    REQUIRE(explorer_result.has_value());
    auto explorer = *explorer_result;

    std::error_code ec;
    fs::remove_all(tmp.path(), ec);
    REQUIRE_FALSE(ec);

    auto refresh_result = explorer->refresh();
    CHECK_FALSE(refresh_result.has_value());
}
