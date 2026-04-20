#ifndef EXPP_CORE_TASK_HPP
#define EXPP_CORE_TASK_HPP

/**
 * @file task.hpp
 * @brief Lightweight task metadata, cancellation, and synchronous scheduling primitives.
 *
 * These types are intentionally small and allocation-light so the current
 * single-threaded implementation can use them today, while future async
 * schedulers can preserve the same call sites and task contracts.
 */

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace expp::core {

// In Asio, the Executor determines where the code runs 
// (e.g., in a single-threaded io_context or a multi-threaded thread_pool). 
// any_io_executor is a polymorphic wrapper that can accept any Asio-compliant executor, 
// making subsequent code not limited to a specific underlying implementation.
using AsyncExecutor = asio::any_io_executor;

/// `asio::awaitable` is a standard C++20 coroutine type provided by Asio. An alias `Task` is defined here.<T> This indicates that this is a coroutine that returns type T, 
///  and that the coroutine is bound by the previously defined generic executor AsyncExecutor by default.
template <typename T>
using Task = asio::awaitable<T, AsyncExecutor>;

/**
 * @brief Relative urgency for scheduler-aware work.
 *
 * The current inline scheduler executes immediately, but callers still attach
 * a priority so future schedulers can separate latency-sensitive UI work from
 * speculative background work without redesigning the API.
 */
enum class TaskPriority : std::uint8_t {
    Immediate,
    UserVisible,
    Background,
};

/**
 * @brief Coarse task size classification for future scheduling policies.
 */
enum class TaskClass : std::uint8_t {
    Micro,
    Interactive,
    Heavy,
};

/**
 * @brief Shared cancellation view passed to a running task.
 *
 * Tokens are cheap to copy and become cancelled when their originating
 * `CancellationSource` flips the shared state.
 */
class CancellationToken {
public:
    CancellationToken() = default;

    /**
     * @brief Returns whether the associated task should stop work.
     */
    [[nodiscard]] bool isCancellationRequested() const noexcept {
        const auto state = state_.lock();
        return state != nullptr && state->load(std::memory_order_relaxed);
    }

private:
    explicit CancellationToken(std::weak_ptr<std::atomic_bool> state) noexcept : state_(std::move(state)) {}

    std::weak_ptr<std::atomic_bool> state_;

    friend class CancellationSource;
};

/**
 * @brief Cancellation handle owned by the caller or scheduler.
 *
 * Resetting the source starts a fresh cancellation lifetime, which is useful
 * for replacing stale requests such as file previews.
 */
class CancellationSource {
public:
    CancellationSource() : state_(std::make_shared<std::atomic_bool>(false)) {}

    /**
     * @brief Marks the current task generation as cancelled.
     */
    void cancel() const noexcept {
        state_->store(true, std::memory_order_relaxed);
    }

    /**
     * @brief Creates a fresh uncancelled generation.
     */
    void reset() {
        state_ = std::make_shared<std::atomic_bool>(false);
    }

    /**
     * @brief Returns a token bound to the current generation.
     */
    [[nodiscard]] CancellationToken token() const noexcept {
        return CancellationToken{state_};
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};

/**
 * @brief Metadata attached to a task submission.
 */
struct TaskContext {
    /// Human-readable identifier used for diagnostics and future tracing.
    std::string_view name;
    /// Relative urgency seen by a scheduler.
    TaskPriority priority{TaskPriority::UserVisible};
    /// Coarse work-size hint seen by a scheduler.
    TaskClass taskClass{TaskClass::Interactive};
    /// Cooperative cancellation channel for the task body.
    CancellationToken cancellation;
};

/**
 * @brief Synchronous scheduler used by the current codebase.
 *
 * This preserves the eventual async-facing call shape while keeping behavior
 * unchanged today: work runs inline on the caller thread.
 */
class InlineScheduler {
public:
    /**
     * @brief Executes `callable` immediately.
     * @param context Metadata attached to the task.
     * @param callable Callable that either accepts `const TaskContext&` or no arguments.
     * @return Whatever `callable` returns.
     */
    template <typename Callable>
    decltype(auto) execute(TaskContext context, Callable&& callable) const {
        if constexpr (std::is_invocable_v<Callable, const TaskContext&>) {
            return std::invoke(std::forward<Callable>(callable), context);
        } else {
            return std::invoke(std::forward<Callable>(callable));
        }
    }
};

/**
 * @brief Suspends the current coroutine and resumes it on `executor`.
 * 
 * The code before `co_await switch_to(...)` runs on the original thread, 
 * while the code after it runs directly on the thread corresponding to the new executor.
 */
template <typename Executor>
Task<void> switch_to(Executor executor) {
    // suspend current coroutine, resume immediately on the specified executor
    co_await asio::post(executor, asio::use_awaitable);
}

/**
 * @brief Executes a callable on a specified executor and automatically resumes on the calling executor.
 *
 * This coroutine temporarily switches the execution context to the target `executor`,
 * invokes the provided `callable` (e.g., a lambda or function) to perform blocking
 * or CPU-bound work, and seamlessly switches back to the original calling executor
 * before returning the result.
 *
 * @tparam Executor The type of the target executor (must be compatible with Asio executors).
 * @tparam Callable The type of the callable object. Constrained by std::invocable.
 *
 * @param executor The target executor where the callable should be executed.
 * @param callable The zero-argument function, lambda, or functor to execute.
 *
 * @return A core::Task containing the result of the callable.
 *         If the callable returns void, the task will yield no value.
 */
template <typename Executor, std::invocable Callable>
auto invoke_on(Executor executor, Callable&& callable) -> core::Task<std::invoke_result_t<Callable>> {
    // Save the original executor (the caller's context)
    auto caller = co_await asio::this_coro::executor;

    // Jump to the target executor
    co_await core::switch_to(executor);

    // Execute the callable on the target executor
    // We use `if constexpr` to handle void-returning callables safely,
    // avoiding the "variable declared void" compilation error.
    using ResultType = std::invoke_result_t<Callable>;
    if constexpr (std::is_void_v<ResultType>) {
        std::invoke(std::forward<Callable>(callable));

        // Switch back to the caller's executor before returning
        co_await core::switch_to(caller);
        co_return;
    } else {
        auto result = std::invoke(std::forward<Callable>(callable));

        // Switch back to the caller's executor before returning
        co_await core::switch_to(caller);
        co_return result;
    }
}


}  // namespace expp::core

#endif  // EXPP_CORE_TASK_HPP
