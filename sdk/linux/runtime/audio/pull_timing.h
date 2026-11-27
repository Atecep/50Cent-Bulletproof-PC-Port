#pragma once
#include <chrono>
#include <cstdint>
namespace audio {
// SDL's pull request includes input already queued and the chunks generated in
// this callback. This is a software submission clock, not hardware scanout time.
struct PullTiming {
    std::chrono::steady_clock::time_point time;
    uint32_t queued_frames = 0;
    uint32_t generated_frames = 0;
};
inline thread_local const PullTiming* current_pull = nullptr;
struct PullScope {
    const PullTiming* previous;
    explicit PullScope(const PullTiming& timing) : previous(current_pull) { current_pull = &timing; }
    ~PullScope() { current_pull = previous; }
};
}
