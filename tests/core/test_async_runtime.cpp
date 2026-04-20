#include "expp/core/async_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

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
