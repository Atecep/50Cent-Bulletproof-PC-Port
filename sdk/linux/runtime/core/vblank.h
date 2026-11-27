#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
namespace gpu {
// A broadcast interrupt is an edge. A later waiter must not erase a previous
// waiter's wake-up by clearing the shared dispatcher event before it runs.
class VBlankSequence {
public:
    uint64_t Generation() { std::lock_guard lock(mutex_); return generation_; }
    uint64_t Notify() {
        std::lock_guard lock(mutex_);
        ++generation_; changed_.notify_all(); return generation_;
    }
    uint64_t WaitAfter(uint64_t observed) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock,[&]{return generation_!=observed;});
        return generation_;
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    uint64_t generation_=0;
};
} // namespace gpu
