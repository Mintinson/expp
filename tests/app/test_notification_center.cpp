#include "expp/app/notification_center.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

TEST_CASE("NotificationCenter obeys success and info suppression", "[app][notifications]") {
    using Clock = expp::app::NotificationCenter::Clock;

    expp::app::NotificationCenter center({
        .durationMs = 1000,
        .showSuccess = false,
        .showInfo = false,
    });

    center.publish(expp::ui::ToastSeverity::Success, "done", Clock::time_point{});
    CHECK_FALSE(center.current().has_value());

    center.publish(expp::ui::ToastSeverity::Info, "info", Clock::time_point{});
    CHECK_FALSE(center.current().has_value());

    center.publish(expp::ui::ToastSeverity::Warning, "warn", Clock::time_point{});
    REQUIRE(center.current().has_value());
    CHECK(center.current()->message == "warn");
}

TEST_CASE("NotificationCenter replaces older toast with newer toast", "[app][notifications]") {
    using Clock = expp::app::NotificationCenter::Clock;

    expp::app::NotificationCenter center;
    center.publish(expp::ui::ToastSeverity::Info, "first", Clock::time_point{});
    center.publish(expp::ui::ToastSeverity::Error, "second", Clock::time_point{});

    REQUIRE(center.current().has_value());
    CHECK(center.current()->severity == expp::ui::ToastSeverity::Error);
    CHECK(center.current()->message == "second");
}

TEST_CASE("NotificationCenter expires toast after configured duration", "[app][notifications]") {
    using Clock = expp::app::NotificationCenter::Clock;

    expp::app::NotificationCenter center({
        .durationMs = 50,
        .showSuccess = true,
        .showInfo = true,
    });

    center.publish(expp::ui::ToastSeverity::Info, "temporary", Clock::time_point{});
    REQUIRE(center.current().has_value());

    center.expire(Clock::time_point{} + std::chrono::milliseconds(49));
    CHECK(center.current().has_value());

    center.expire(Clock::time_point{} + std::chrono::milliseconds(50));
    CHECK_FALSE(center.current().has_value());
}

TEST_CASE("severity_for_error maps recoverable and serious errors", "[app][notifications]") {
    CHECK(expp::app::severity_for_error(
              expp::core::Error{expp::core::ErrorCategory::InvalidArgument, "bad input"}) ==
          expp::ui::ToastSeverity::Warning);
    CHECK(expp::app::severity_for_error(expp::core::Error{expp::core::ErrorCategory::System, "boom"}) ==
          expp::ui::ToastSeverity::Error);
}
