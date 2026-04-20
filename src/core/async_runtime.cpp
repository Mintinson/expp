#include "expp/core/async_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace expp::core {

void UiMailbox::setWakeCallback(WakeCallback callback) {
    std::scoped_lock lock(mutex_);
    wakeCallback_ = std::move(callback);
}

void UiMailbox::post(Closure closure) {
    WakeCallback wake;
    {
        std::scoped_lock lock(mutex_);
        queue_.push(std::move(closure));
        wake = wakeCallback_;
    }
    // Trigger the wake mechanism outside the lock to prevent deadlocks 
    // or unnecessary blocking of the UI thread's wake handler.
    if (wake) {
        wake();
    }
}

bool UiMailbox::drain() {
    std::queue<Closure> pending;
    {
        std::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        // CRITICAL PERFORMANCE OPTIMIZATION:
        // Swap the queue to local variable 'pending' in O(1) time.
        // This releases the lock immediately, allowing background threads
        // to continue posting new tasks while we execute the current batch.
        pending.swap(queue_);
    }
    // Execute all tasks sequentially on the caller's thread (expected to be UI thread)
    while (!pending.empty()) {
        auto closure = std::move(pending.front());
        pending.pop();
        if (closure) {
            closure();
        }
    }
    return true;
}

AsioRuntime::AsioRuntime(std::size_t io_threads, std::size_t cpu_threads)
    : ioContext_(std::max(1, static_cast<int>(io_threads)))
    , ioWork_(asio::make_work_guard(ioContext_))
    , diskPool_(std::max(static_cast<std::size_t>(1), io_threads))
    , cpuPool_(std::max(static_cast<std::size_t>(1), cpu_threads)) {
    ioThreads_.reserve(static_cast<std::size_t>(std::max(static_cast<std::size_t>(1), io_threads)));
    for (std::size_t index = 0; index < std::max(static_cast<std::size_t>(1), io_threads); ++index) {
        ioThreads_.emplace_back([this] { ioContext_.run(); });
    }
}

AsioRuntime::~AsioRuntime() {
    // 1. Drop the work guard. This tells ioContext it can exit once pending work is done.
    ioWork_.reset();
    
    // 2. Forcefully stop ioContext_ (aborts pending IO and timers immediately).
    ioContext_.stop();
    
    // Note: ioThreads_ (jthreads) will automatically join here upon destruction 
    // because ioContext_.run() will have returned due to the .stop() above.
    
    // 3. Gracefully wait for all background thread pools to finish currently executing tasks.
    diskPool_.join();
    cpuPool_.join();
}

AsioRuntime::IoExecutor AsioRuntime::ioExecutor() noexcept {
    return ioContext_.get_executor();
}

AsioRuntime::DiskExecutor AsioRuntime::diskExecutor() noexcept {
    return diskPool_.get_executor();
}

AsioRuntime::CpuExecutor AsioRuntime::cpuExecutor() noexcept {
    return cpuPool_.get_executor();
}

UiMailbox& AsioRuntime::mailbox() noexcept {
    return mailbox_;
}

const UiMailbox& AsioRuntime::mailbox() const noexcept {
    return mailbox_;
}

}  // namespace expp::core
