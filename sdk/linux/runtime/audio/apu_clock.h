#pragma once
#include <cstdint>

namespace apu {

// NV_PAPU_XGSCNT: the free-running 32-bit count of 48 kHz samples.
constexpr uint32_t kSampleCounter = 0xFE80200C;
constexpr uint32_t SamplesAtNanoseconds(uint64_t ns) {
    // Divide first to avoid overflowing even for long process uptimes.
    return uint32_t((ns / 1000000000) * 48000 + (ns % 1000000000) * 48000 / 1000000000);
}
uint32_t SampleTime();

}  // namespace apu
