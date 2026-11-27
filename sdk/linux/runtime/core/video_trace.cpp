#ifdef _WIN32
#include "core/video_trace.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>

#include "core/log.h"

namespace video_trace {
namespace {

constexpr size_t kEventCapacity = 1u << 20;

struct RenderEventRecord {
    const char* name;
    uint64_t time, a, b, c, d, thread;
    std::atomic<bool> ready{false};
};

struct RenderBuffer {
    std::unique_ptr<RenderEventRecord[]> events{new (std::nothrow) RenderEventRecord[kEventCapacity]};
    std::atomic<size_t> next{0};
    std::atomic<bool> sealed{false};
};

RenderBuffer& Buffer() {
    static RenderBuffer buffer;
    return buffer;
}

}  // namespace

void BufferRenderEvent(const char* event, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    auto& buffer = Buffer();
    if (!buffer.events || buffer.sealed.load(std::memory_order_relaxed)) return;
    const auto now = Now();
    const size_t index = buffer.next.fetch_add(1, std::memory_order_relaxed);
    if (index >= kEventCapacity) return;

    auto& record = buffer.events[index];
    record.name = event;
    record.time = now;
    record.a = a;
    record.b = b;
    record.c = c;
    record.d = d;
    static thread_local const uint64_t thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    record.thread = thread;
    record.ready.store(true, std::memory_order_release);
}

void FlushRenderTrace() {
    if (!RenderEnabled()) return;
    auto& buffer = Buffer();
    if (buffer.sealed.exchange(true, std::memory_order_relaxed)) return;
    if (!buffer.events) {
        LOGE("render trace: could not allocate event buffer");
        return;
    }
    const char* path = std::getenv("XBE_RENDER_TRACE");
    auto* file = path ? std::fopen(path, "w") : nullptr;
    if (!file) {
        LOGE("render trace: could not open output");
        return;
    }
    std::setvbuf(file, nullptr, _IOFBF, 65536);
    std::fputs("ns,event,a,b,c,d,host_thread\n", file);
    const size_t attempted = buffer.next.load(std::memory_order_relaxed);
    size_t written = 0;
    for (size_t i = 0; i < std::min(attempted, kEventCapacity); ++i) {
        const auto& record = buffer.events[i];
        // Shutdown can race a final writer. Skip unpublished records rather
        // than waiting for a guest or graphics thread to make progress.
        if (!record.ready.load(std::memory_order_acquire)) continue;
        if (std::fprintf(file, "%llu,%s,%llu,%llu,%llu,%llu,%llu\n",
                         static_cast<unsigned long long>(record.time), record.name,
                         static_cast<unsigned long long>(record.a), static_cast<unsigned long long>(record.b),
                         static_cast<unsigned long long>(record.c), static_cast<unsigned long long>(record.d),
                         static_cast<unsigned long long>(record.thread)) < 0) break;
        ++written;
    }
    if (std::fclose(file) || written < std::min(attempted, kEventCapacity))
        LOGE("render trace: output may be incomplete");
    LOGR("render trace: wrote %zu events, dropped %zu", written, attempted - written);
}

}  // namespace video_trace
#endif
