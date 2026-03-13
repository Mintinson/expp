/**
 * @file test_error.cpp
 * @brief Unit tests for error handling system
 */

#include "expp/core/error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace expp::core;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Error construction", "[core][error]") {
    SECTION("with category and message") {
        Error err{ErrorCategory::FileSystem, "test error"};

        CHECK(err.category() == ErrorCategory::FileSystem);
        CHECK(err.message() == "test error");
        CHECK(err.is_category(ErrorCategory::FileSystem));
        CHECK_FALSE(err.is_category(ErrorCategory::IO));
    }

    SECTION("with message only (Unknown category)") {
        Error err{"just a message"};

        CHECK(err.category() == ErrorCategory::Unknown);
        CHECK(err.message() == "just a message");
        CHECK(err.is_category(ErrorCategory::Unknown));
    }

    SECTION("format() includes all info") {
        Error err{ErrorCategory::IO, "read failed"};
        auto formatted = err.format();

        CHECK_THAT(formatted, ContainsSubstring("IO"));
        CHECK_THAT(formatted, ContainsSubstring("read failed"));
        CHECK_THAT(formatted, ContainsSubstring("test_error.cpp"));
    }
}

TEST_CASE("category_to_string", "[core][error]") {
    CHECK(category_to_string(ErrorCategory::None) == "None");
    CHECK(category_to_string(ErrorCategory::FileSystem) == "FileSystem");
    CHECK(category_to_string(ErrorCategory::Permission) == "Permission");
    CHECK(category_to_string(ErrorCategory::NotFound) == "NotFound");
    CHECK(category_to_string(ErrorCategory::InvalidArgument) == "InvalidArgument");
    CHECK(category_to_string(ErrorCategory::InvalidState) == "InvalidState");
    CHECK(category_to_string(ErrorCategory::IO) == "IO");
    CHECK(category_to_string(ErrorCategory::Config) == "Config");
    CHECK(category_to_string(ErrorCategory::UI) == "UI");
    CHECK(category_to_string(ErrorCategory::System) == "System");
    CHECK(category_to_string(ErrorCategory::Unknown) == "Unknown");
}

TEST_CASE("Result type usage", "[core][error]") {
    SECTION("success case") {
        Result<int> result = 42;

        CHECK(result.has_value());
        CHECK(*result == 42);
    }

    SECTION("error case") {
        Result<char> result = make_error(ErrorCategory::InvalidArgument, "bad value");

        CHECK_FALSE(result.has_value());
        CHECK(result.error().category() == ErrorCategory::InvalidArgument);
    }

    SECTION("VoidResult success") {
        VoidResult result{};

        CHECK(result.has_value());
    }

    SECTION("VoidResult error") {
        VoidResult result = make_error("operation failed");

        CHECK_FALSE(result.has_value());
        CHECK(result.error().message() == "operation failed");
    }
}
TEST_CASE("make_error factory functions", "[core][error]") {
    SECTION("with category") {
        auto err = make_error(ErrorCategory::NotFound, "file missing");
        
        CHECK(err.error().category() == ErrorCategory::NotFound);
        CHECK(err.error().message() == "file missing");
    }

    SECTION("without category") {
        auto err = make_error("generic error");
        
        CHECK(err.error().category() == ErrorCategory::Unknown);
        CHECK(err.error().message() == "generic error");
    }
}