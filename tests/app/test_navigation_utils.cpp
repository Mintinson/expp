#include "expp/app/navigation_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
    TempDirectory() {
        path_ = fs::temp_directory_path() / "expp_navigation_utils_test";
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

TEST_CASE("resolveDirectoryInput resolves and validates user-entered directories", "[app][navigation]") {
    TempDirectory tmp;
    const fs::path current_dir = tmp.path() / "current";
    const fs::path nested_dir = current_dir / "nested";
    const fs::path home_dir = tmp.path() / "home";
    const fs::path home_target = home_dir / "projects";
    const fs::path file_path = current_dir / "note.txt";

    fs::create_directories(nested_dir);
    fs::create_directories(home_target);

    std::ofstream out(file_path);
    out << "hello\n";
    out.close();

    SECTION("relative path resolves from current directory") {
        auto result = expp::app::resolve_directory_input("nested", current_dir, home_dir);
        REQUIRE(result.has_value());
        CHECK(*result == fs::weakly_canonical(nested_dir));
    }

    SECTION("home expansion uses supplied home directory") {
        auto result = expp::app::resolve_directory_input("~/projects", current_dir, home_dir);
        REQUIRE(result.has_value());
        CHECK(*result == fs::weakly_canonical(home_target));
    }

    SECTION("missing directory is rejected") {
        auto result = expp::app::resolve_directory_input("missing", current_dir, home_dir);
        CHECK_FALSE(result.has_value());
    }

    SECTION("non-directory path is rejected") {
        auto result = expp::app::resolve_directory_input("note.txt", current_dir, home_dir);
        CHECK_FALSE(result.has_value());
    }
}
