#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <functional>
#include <thread>

// Opt-in observation only. CSV events share a monotonic host clock across
// guest decoder, recorder, audio and Vulkan threads; no pacing is performed.
namespace video_trace {
inline bool Enabled() {
    static const bool enabled = std::getenv("XBE_VIDEO_TRACE") != nullptr;
    return enabled;
}
inline uint64_t Now() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline bool RenderEnabled() {
    static const bool enabled = std::getenv("XBE_RENDER_TRACE") != nullptr;
    return enabled;
}

#ifdef _WIN32
// Windows render events are buffered until Shutdown. Event names must have
// static storage, as they do at all render trace call sites.
void BufferRenderEvent(const char* event, uint64_t a, uint64_t b, uint64_t c, uint64_t d);
void FlushRenderTrace();
#endif

template <bool Render>
inline void Emit(const char* event, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    const auto time = Now();
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static FILE* file = [] {
        auto f = std::fopen(std::getenv(Render ? "XBE_RENDER_TRACE" : "XBE_VIDEO_TRACE"), "w");
        if (f) { std::setvbuf(f, nullptr, _IOFBF, 65536); std::fputs("ns,event,a,b,c,d,host_thread\n", f); }
        return f;
    }();
    if (!file) return;
    std::fprintf(file, "%llu,%s,%llu,%llu,%llu,%llu,%llu\n", (unsigned long long)time, event,
        (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, (unsigned long long)d,
        (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()));
    static unsigned pending=0;
    if (++pending == 128) { std::fflush(file); pending=0; }
}
inline void Event(const char* event, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0) {
    if (Enabled()) Emit<false>(event, a, b, c, d);
}

inline void RenderEvent(const char* event, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0) {
#ifdef _WIN32
    if (RenderEnabled()) BufferRenderEvent(event, a, b, c, d);
#else
    if (RenderEnabled()) Emit<true>(event, a, b, c, d);
#endif
}

inline void PresentEvent(const char* event, uint64_t a=0, uint64_t b=0, uint64_t c=0, uint64_t d=0) {
    Event(event, a, b, c, d);
    RenderEvent(event, a, b, c, d);
}
} // namespace video_trace
