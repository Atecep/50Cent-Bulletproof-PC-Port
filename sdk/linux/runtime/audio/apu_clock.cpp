#include "audio/apu_clock.h"
#include "audio/audio.h"
#include <chrono>

namespace apu {

uint32_t SampleTime() {
    static_assert(audio::kRate == 48000);
    // The host mixer is an HLE replacement for the hardware sample engine.
    // Keep its clock advancing even before an output device has been opened.
    using Clock = std::chrono::steady_clock;
    static const auto start = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return SamplesAtNanoseconds(uint64_t(ns));
}

}  // namespace apu
