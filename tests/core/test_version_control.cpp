#include "expp/core/version_control.hpp"

#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

TEST_CASE("Git porcelain status parser handles common states", "[core][version_control]") {
    std::string output;
    output += " M src/main.cpp";
    output.push_back('\0');
    output += "A  include/expp/new.hpp";
    output.push_back('\0');
    output += "?? scratch.txt";
    output.push_back('\0');
    output += "!! build/cache.obj";
    output.push_back('\0');
    output += "R  src/new.cpp";
    output.push_back('\0');
    output += "src/old.cpp";
    output.push_back('\0');
    output += "UU conflict.txt";
    output.push_back('\0');

    auto result = expp::core::version_control::parse_porcelain_status(output);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 6);
    CHECK((*result)[0].path == fs::path{"src/main.cpp"});
    CHECK((*result)[0].status == expp::core::version_control::VersionStatus::Modified);
    CHECK((*result)[1].status == expp::core::version_control::VersionStatus::Added);
    CHECK((*result)[2].status == expp::core::version_control::VersionStatus::Untracked);
    CHECK((*result)[3].status == expp::core::version_control::VersionStatus::Ignored);
    CHECK((*result)[4].path == fs::path{"src/new.cpp"});
    CHECK((*result)[4].status == expp::core::version_control::VersionStatus::Renamed);
    CHECK((*result)[5].status == expp::core::version_control::VersionStatus::Conflicted);
}

TEST_CASE("Version status merge keeps the most important nested state", "[core][version_control]") {
    using expp::core::version_control::merge_status;
    using expp::core::version_control::VersionStatus;

    CHECK(merge_status(VersionStatus::Ignored, VersionStatus::Modified) == VersionStatus::Modified);
    CHECK(merge_status(VersionStatus::Untracked, VersionStatus::Conflicted) == VersionStatus::Conflicted);
    CHECK(merge_status(VersionStatus::Modified, VersionStatus::Ignored) == VersionStatus::Modified);
}

TEST_CASE("Version status markers are compact and stable", "[core][version_control]") {
    using expp::core::version_control::status_marker;
    using expp::core::version_control::VersionStatus;

    CHECK(status_marker(VersionStatus::Clean).empty());
    CHECK(status_marker(VersionStatus::Modified) == "[M]");
    CHECK(status_marker(VersionStatus::Added) == "[A]");
    CHECK(status_marker(VersionStatus::Untracked) == "[?]");
    CHECK(status_marker(VersionStatus::Ignored) == "[I]");
    CHECK(status_marker(VersionStatus::Conflicted) == "[!]");
}
