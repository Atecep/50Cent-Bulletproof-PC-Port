#if defined(XBE_VK_TARGET)
// NWFSX's scan ends at the yield call at 0x00145054 on the supported XBE.
#include <chrono>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#include <SDL3/SDL_timer.h>
#endif

#include "core/video_trace.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

extern "C" __attribute__((weak)) void __imp__sub_00011C14(X86Context&, uint8_t*);

namespace {

bool NwfsxIdle() {
    using kernel::R32;
    // Leave queued/processing work and shutdown on the original path. The next
    // guest scan also checks the separate activity table, so no signal is needed.
    if (R32(0x0036B89C) == 0) return false;
    for (uint32_t entry = 0x00393760; entry < 0x003944E0; entry += 0x6C) {
        if (R32(entry) != 1) continue;
        const uint32_t status = R32(entry + 4);
        if (status == 1 || status == 2) return false;
    }
    return true;
}

}  // namespace

X86_FUNC(sub_00011C14) {
    const bool nwfsx = kernel::R32(ctx.esp) == 0x00145059;
    __imp__sub_00011C14(ctx, base);
    if (!nwfsx) return;
    static const auto delay = [] {
        // Keep the old scheduling on Windows until its timer granularity has
        // been measured. This is local to the game's idle scan on Linux.
#ifdef _WIN32
        constexpr auto fallback = std::chrono::microseconds(0);
#else
        constexpr auto fallback = std::chrono::microseconds(100);
#endif
        const char* value = std::getenv("XBE_NWFSX_IDLE_US");
        if (!value) return fallback;
        char* end = nullptr;
        const long us = std::strtol(value, &end, 10);
        return end != value && *end == '\0' && us >= 0 && us <= 250
            ? std::chrono::microseconds(us) : fallback;
    }();
    if (!delay.count() || !NwfsxIdle()) return;
    static thread_local unsigned sleeps = 0;
    const bool trace = video_trace::RenderEnabled() && (++sleeps % 128 == 0);
    const auto start = trace ? video_trace::Now() : 0;
#ifdef _WIN32
    SDL_DelayNS(static_cast<uint64_t>(delay.count()) * SDL_NS_PER_US);
#else
    std::this_thread::sleep_for(delay);
#endif
    if (trace) video_trace::RenderEvent("nwfsx_idle", delay.count(), video_trace::Now() - start);
}

#endif
