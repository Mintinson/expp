#ifndef EXPP_CORE_ASYNC_RUNTIME_HPP
#define EXPP_CORE_ASYNC_RUNTIME_HPP

#include "expp/core/task.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>

#include <chrono>
#include <concepts>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace expp::core {

/**
 * @brief Thread-safe mailbox used to marshal work back onto the UI/Main thread.
 *
 * Provides a synchronized message queue where background threads can post closures
 * (e.g., UI update logic) to be executed safely on the main thread.
 */
class UiMailbox {
public:
    using Closure = std::function<void()>;
    using WakeCallback = std::function<void()>;

    UiMailbox() = default;

    /**
     * @brief Sets the callback invoked to wake up the UI event loop.
     * @param callback The platform-specific wake mechanism.
     */
    void setWakeCallback(WakeCallback callback);

    /**
     * @brief Posts a task to the mailbox and alerts the UI thread.
     * @param closure The task to be executed on the UI thread.
     */
    void post(Closure closure);

    /**
     * @brief Consumes and executes all pending tasks.
     * @note This MUST be called exclusively from the UI thread.
     * @return True if there were tasks processed; false if the mailbox was empty.
     */
    [[nodiscard]] bool drain();

private:
    std::mutex mutex_;
    std::queue<Closure> queue_;
    WakeCallback wakeCallback_;
};

/**
 * @brief Central Asio-based asynchronous runtime environment.
 *
 * Orchestrates multiple distinct execution contexts (I/O, Disk, CPU) to prevent
 * different types of workloads from blocking each other.
 */
class AsioRuntime : public std::enable_shared_from_this<AsioRuntime> {
public:
    using IoExecutor = asio::any_io_executor;
    using DiskExecutor = asio::thread_pool::executor_type;
    using CpuExecutor = asio::thread_pool::executor_type;

    /**
     * @brief Initializes the runtime and starts background thread pools.
     * @param io_threads Number of threads dedicated to non-blocking I/O.
     * @param cpu_threads Number of threads dedicated to CPU-bound background work.
     */
    explicit AsioRuntime(std::size_t io_threads = 2, std::size_t cpu_threads = 2);
    ~AsioRuntime();

    // Prevent copying and moving to ensure a stable runtime lifecycle
    AsioRuntime(const AsioRuntime&) = delete;
    AsioRuntime& operator=(const AsioRuntime&) = delete;
    AsioRuntime(AsioRuntime&&) = delete;
    AsioRuntime& operator=(AsioRuntime&&) = delete;

    /// Returns the executor for non-blocking operations (Network, Timers).
    [[nodiscard]] IoExecutor ioExecutor() noexcept;
    /// Returns the executor for blocking disk I/O operations.
    [[nodiscard]] DiskExecutor diskExecutor() noexcept;
    /// Returns the executor for heavy CPU computation operations.
    [[nodiscard]] CpuExecutor cpuExecutor() noexcept;

    /// Access the UI mailbox for thread-safe UI updates.
    [[nodiscard]] UiMailbox& mailbox() noexcept;
    [[nodiscard]] const UiMailbox& mailbox() const noexcept;

    /**
     * @brief Synchronously blocks the calling thread until the given coroutine task completes.
     *
     * Spawns the coroutine on the I/O executor and uses std::promise/future to wait.
     *
     * @tparam T The return type of the task (Note: requires specialization if T is void).
     * @param task The coroutine task to execute.
     * @return The result produced by the coroutine.
     */
    template <typename T>
    [[nodiscard]] T blockOn(Task<T> task) {
        std::promise<T> promise;
        auto future = promise.get_future();

        asio::co_spawn(ioContext_, std::move(task),
                       [promise = std::move(promise)](std::exception_ptr exception, T result) mutable {
                           if (exception) {
                               promise.set_exception(exception);
                               return;
                           }
                           promise.set_value(std::move(result));
                       });

        return future.get();
    }

    /**
     * @brief Posts a callable directly to the UI thread via the mailbox.
     * @tparam Closure The type of the callable.
     * @param closure The functional logic to run on UI.
     */
    template <typename Closure>
    void postToUi(Closure&& closure) {
        mailbox_.post(std::function<void()>(std::forward<Closure>(closure)));
    }

    /**
     * @brief Schedules a task to be executed on the UI thread after a specified delay.
     *
     * @tparam Rep, Period Duration traits.
     * @tparam Closure Type of the callable.
     * @param delay Time to wait before execution.
     * @param closure The task to run on the UI thread after the delay.
     */
    template <typename Rep, typename Period, typename Closure>
    void scheduleAfter(std::chrono::duration<Rep, Period> delay, Closure&& closure) {
        auto timer = std::make_shared<asio::steady_timer>(ioContext_);
        timer->expires_after(delay);
        // Wait asynchronously on the I/O thread...
        timer->async_wait([this, timer, closure = std::function<void()>(std::forward<Closure>(closure))](
                              const std::error_code& error) mutable {
            if (error) {
                return;
            }
            // ... then dispatch the work to the UI thread
            postToUi(std::move(closure));
        });
    }

private:
    asio::io_context ioContext_;
    /// Prevents ioContext_.run() from returning immediately when there is no active work
    asio::executor_work_guard<asio::io_context::executor_type> ioWork_;
    /// Dedicated to blocking disk interactions
    asio::thread_pool diskPool_;
    /// Dedicated to intensive compute
    asio::thread_pool cpuPool_;
    std::vector<std::jthread> ioThreads_;
    UiMailbox mailbox_;
};

/**
 * @brief Reports whether the Linux io_uring extension seam is compiled in.
 */
[[nodiscard]] constexpr bool io_uring_extension_enabled() noexcept {
#if defined(EXPP_PREPARE_IO_URING_BACKEND) && EXPP_PREPARE_IO_URING_BACKEND
    return true;
#else
    return false;
#endif
}

/**
 * @brief EXTENSION POINT: future Linux-specific async file backend abstraction.
 */
class LinuxIoBackend {
public:
    virtual ~LinuxIoBackend() = default;

    [[nodiscard]] virtual bool available() const noexcept = 0;
};


}  // namespace expp::core

#endif  // EXPP_CORE_ASYNC_RUNTIME_HPP
