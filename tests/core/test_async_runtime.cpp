#include "expp/core/async_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

namespace {

#if defined(__cpp_exceptions)
expp::core::Task<expp::core::Result<int>> failing_result_task() {
    throw std::runtime_error("boom");
    co_return 7;
}

expp::core::Task<void> failing_detached_task() {
    throw std::runtime_error("background boom");
    co_return;
}
#endif

}  // namespace

TEST_CASE("UiMailbox drains posted closures", "[core][async]") {
    expp::core::UiMailbox mailbox;
    int value = 0;

    mailbox.post([&] { value = 42; });

    REQUIRE(mailbox.drain());
    CHECK(value == 42);
    CHECK_FALSE(mailbox.drain());
}

TEST_CASE("AsioRuntime scheduleAfter posts through the UI mailbox", "[core][async]") {
    auto runtime = std::make_shared<expp::core::AsioRuntime>(1, 1);
    std::atomic_bool fired = false;

    runtime->scheduleAfter(10ms, [&] { fired.store(true); });

    for (int attempt = 0; attempt < 40 && !fired.load(); ++attempt) {
        std::this_thread::sleep_for(5ms);
        (void)runtime->mailbox().drain();
    }

    CHECK(fired.load());
}

#if defined(__cpp_exceptions)
TEST_CASE("AsioRuntime blockOn converts coroutine exceptions into core errors", "[core][async]") {
    auto runtime = std::make_shared<expp::core::AsioRuntime>(1, 1);

    auto result = runtime->blockOn(failing_result_task());

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().category() == expp::core::ErrorCategory::System);
    CHECK(result.error().message().contains("blocking async task failed"));
    CHECK(result.error().message().contains("boom"));
}

TEST_CASE("AsioRuntime spawnDetached reports coroutine failures on the UI thread", "[core][async]") {
    auto runtime = std::make_shared<expp::core::AsioRuntime>(1, 1);
    std::optional<expp::core::Error> captured_error;

    runtime->spawnDetached(runtime->ioExecutor(), failing_detached_task(), "detached test",
                           [&](expp::core::Error error) { captured_error = std::move(error); });

    for (int attempt = 0; attempt < 40 && !captured_error.has_value(); ++attempt) {
        std::this_thread::sleep_for(5ms);
        (void)runtime->mailbox().drain();
    }

    REQUIRE(captured_error.has_value());
    CHECK(captured_error->category() == expp::core::ErrorCategory::System);
    CHECK(captured_error->message().contains("detached test failed"));
    CHECK(captured_error->message().contains("background boom"));
}
#endif
