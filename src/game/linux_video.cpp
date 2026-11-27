// Bindings established from this image's original player bodies.
// Original acquire/release, MPEG references and stack cleanup are preserved.
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "core/video_trace.h"
#include "xdk/hle.h"
#include "kernel/memory.h"
#include "video_frame_snapshot.h"
using kernel::R32;
namespace {
sofdec::FrameSnapshots video_frames;
bool ReadableFrame(uint32_t address, uint32_t bytes) {
    for (uint64_t p = address & ~uint64_t(mem::kPage - 1);
         p < uint64_t(address) + bytes; p += mem::kPage)
        if (!mem::IsCommitted(uint32_t(p))) return false;
    return true;
}
}
X86_EXTERN_FUNC(__imp__sub_0014B960);
X86_EXTERN_FUNC(__imp__sub_0014B3E0);
X86_EXTERN_FUNC(__imp__sub_0014C580);
X86_EXTERN_FUNC(__imp__sub_0014BBC0);
X86_EXTERN_FUNC(sub_00156A20);
X86_FUNC(sub_0014B960) {
    const auto player = R32(ctx.esp + 4);
    const auto out = R32(ctx.esp + 8);
    const auto core = player ? R32(player + 0x3C) : 0;
    if (core && R32(core + 0x48) != 0 && R32(core + 0xB28) == 1) {
        const auto saved_ctx = ctx;
        ctx.esi = core;
        X86_PUSH32(0x156C25u);
        sub_00156A20(ctx, base);
        ctx = saved_ctx;
    }

    const auto start = video_trace::Now();
    __imp__sub_0014B960(ctx, base);
    const bool ready = out && R32(out);
    if (ready && R32(player + 0x7C) > R32(player + 0x80)) {
        const auto source = R32(out);
        const auto snapshot = video_frames.Capture(player, base, source,
            R32(out + 8), R32(out + 12), R32(out + 16),
            [](uint32_t size) { return mem::AllocSystem(size, 32); },
            mem::FreeSystem, ReadableFrame);
        if (snapshot) {
            // 14B3E0 releases player+78, not this public output descriptor.
            // Both early and late 14C580 conversions now use owned pixels.
            X86_MEM32(out) = snapshot;
            video_trace::Event("frame_snapshot", player, R32(out + 0x34), source, snapshot);
        } else {
            std::fprintf(stderr, "[Sofdec] Cannot snapshot frame: player=%08X mode=%u size=%ux%u source=%08X\n",
                         player, R32(out + 8), R32(out + 12), R32(out + 16), source);
        }
    }
    if (!video_trace::Enabled()) return;
    video_trace::Event("frame_acquire", player, ready, ready ? R32(out + 0x34) : 0, video_trace::Now() - start);
    video_trace::Event("frame_counts", player, R32(player + 0x7C), R32(player + 0x80), R32(player + 0x84));
    if (core) video_trace::Event("core_clock", player, R32(core + 0xFBC), R32(core + 0xFC0));
    if (ready) video_trace::Event("frame_info", player, R32(out + 0x20), R32(out + 0x24), R32(out + 0x34));
}
X86_FUNC(sub_0014B3E0) {
    if (!video_trace::Enabled()) { __imp__sub_0014B3E0(ctx, base); return; }
    const auto player=R32(ctx.esp+4), frame=R32(player+0x78);
    const auto id=frame ? R32(frame+0x30) : 0;
    __imp__sub_0014B3E0(ctx, base);
    video_trace::Event("frame_release", player, id, R32(player+0x80));
}
X86_FUNC(sub_0014C580) {
    if (!video_trace::Enabled()) { __imp__sub_0014C580(ctx, base); return; }
    const auto player=R32(ctx.esp+4), info=R32(ctx.esp+8), id=R32(info+0x34);
    const auto start=video_trace::Now();
    video_trace::Event("convert_begin", player, id, R32(info), R32(info+0x30));
    __imp__sub_0014C580(ctx, base);
    video_trace::Event("convert_end", player, id, video_trace::Now()-start);
}
X86_FUNC(sub_0014BBC0) {
    if (!video_trace::Enabled()) { __imp__sub_0014BBC0(ctx, base); return; }
    const auto player=R32(ctx.esp+4), ticks=R32(ctx.esp+8), scale=R32(ctx.esp+12);
    __imp__sub_0014BBC0(ctx, base);
    video_trace::Event("playback_clock", player, ticks ? R32(ticks) : 0, scale ? R32(scale) : 0);
}
#endif
#if defined(__linux__) || defined(XBE_VK_TARGET)
X86_EXTERN_FUNC(__imp__sub_00157270);
X86_EXTERN_FUNC(__imp__sub_00157280);
X86_EXTERN_FUNC(__imp__sub_0015F2F0);
X86_FUNC(sub_00157270) {
    if (!video_trace::Enabled()) { __imp__sub_00157270(ctx, base); return; }
    const auto frame=R32(ctx.esp+4);
    __imp__sub_00157270(ctx, base);
    video_trace::Event("decoder_ready", frame, R32(frame+0x48), R32(frame+0x4C), R32(frame+0x50));
    video_trace::Event("decoder_scale", frame, R32(frame+0x3C));
    video_trace::Event("decoder_buffer", frame, R32(frame+8));
}
X86_FUNC(sub_00157280) {
    if (!video_trace::Enabled()) { __imp__sub_00157280(ctx, base); return; }
    const auto frame=R32(ctx.esp+4);
    __imp__sub_00157280(ctx, base);
    video_trace::Event("decoder_ready4", frame, R32(frame+0x48), R32(frame+0x4C), R32(frame+0x50));
    video_trace::Event("decoder_scale", frame, R32(frame+0x3C));
    video_trace::Event("decoder_buffer", frame, R32(frame+8));
}
X86_FUNC(sub_0015F2F0) {
    if (!video_trace::Enabled()) { __imp__sub_0015F2F0(ctx, base); return; }
    const auto decoder=R32(ctx.esp+4), info=R32(ctx.esp+8);
    const auto start=video_trace::Now();
    video_trace::Event("mpeg_decode_begin", decoder);
    __imp__sub_0015F2F0(ctx, base);
    const uint64_t deltas=uint64_t(R32(info+0x28))<<32 | R32(info+0x2C);
    video_trace::Event("mpeg_decode_end", decoder, ctx.eax, deltas, video_trace::Now()-start);
}
#endif
#if defined(__linux__) || defined(XBE_VK_TARGET)
X86_EXTERN_FUNC(__imp__sub_00153280);
X86_FUNC(sub_00153280) {
    const auto ecx_arg = R32(ctx.esp + 4);
    if (!ecx_arg) {
        ctx.eax = 1;
        ctx.esp += 4;
        return;
    }
    __imp__sub_00153280(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_001556A0);
X86_FUNC(sub_001556A0) {
    const auto record = R32(ctx.esp + 4);
    if (!record || !R32(record + 0x3C)) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    __imp__sub_001556A0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00155650);
X86_FUNC(sub_00155650) {
    const auto record = R32(ctx.esp + 4);
    if (video_trace::Enabled()) {
        video_trace::Event("sync_barrier_begin", record, record ? R32(record + 0x3C) : 0, record ? R32(record + 0x60) : 0);
    }
    __imp__sub_00155650(ctx, base);
    if (video_trace::Enabled()) {
        video_trace::Event("sync_barrier_end", record, record ? R32(record + 0x3C) : 0, record ? R32(record + 0x60) : 0);
    }
}

X86_EXTERN_FUNC(__imp__sub_0014AE20);
X86_FUNC(sub_0014AE20) {
    const auto record = R32(ctx.esp + 4);
    if (video_trace::Enabled()) {
        video_trace::Event("stream_close_begin", record, record ? R32(record+0x3C) : 0, record ? R32(record+0x5C) : 0, record ? R32(record+0x60) : 0);
    }
    __imp__sub_0014AE20(ctx, base);
    video_frames.Close(record, mem::FreeSystem);
    if (video_trace::Enabled()) {
        video_trace::Event("stream_close_end", record, record ? R32(record+0x3C) : 0, record ? R32(record+0x5C) : 0, record ? R32(record+0x60) : 0);
    }
}
#endif
#if defined(__linux__) || defined(XBE_VK_TARGET)
X86_EXTERN_FUNC(__imp__sub_0012F020);
X86_EXTERN_FUNC(sub_001324C0);
X86_FUNC(sub_0012F020) {
    const uint32_t flag_addr = 0x00369F14u;
    const uint32_t worker_handle_addr = 0x003AC588u;
    const uint32_t high_prio_addr = 0x00369EF8u;
    const uint32_t orig_prio_addr = 0x00369F0Cu;

    const uint32_t worker_handle = x86_read32(base, worker_handle_addr);
    kernel::ObjectRef obj = kernel::FromHandle(worker_handle);
    if (!obj || obj->type != kernel::ObjType::Thread) {
        __imp__sub_0012F020(ctx, base);
        return;
    }

    auto worker = std::static_pointer_cast<kernel::Thread>(obj);
    const int target_prio = (int)x86_read32(base, high_prio_addr);
    const int orig_prio = (int)x86_read32(base, orig_prio_addr);

    worker->priority = target_prio;
    if (video_trace::Enabled()) {
        video_trace::Event("border_sync_begin", worker->id, worker->suspend.load(), worker->parked.load());
    }

    // Signal border processing request
    X86_MEM32(flag_addr) = 1u;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    bool timed_out = false;

    {
        std::unique_lock<std::mutex> lock(worker->apc_lock);

        // Resume worker if parked/suspended so it runs the border cycle
        if (worker->parked.load() || worker->suspend.load() > 0) {
            if (worker->suspend.load() > 0) {
                worker->suspend--;
            }
            worker->resume_cv.notify_all();
        }

        while (x86_read32(base, flag_addr) != 0 || !worker->parked.load()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                timed_out = true;
                break;
            }

            worker->park_cv.wait_for(lock, std::chrono::milliseconds(10), [&] {
                return x86_read32(base, flag_addr) == 0 && worker->parked.load();
            });

            if (x86_read32(base, flag_addr) == 0 && worker->parked.load()) {
                break;
            }

            if (worker->parked.load() && x86_read32(base, flag_addr) != 0) {
                if (worker->suspend.load() > 0) {
                    worker->suspend--;
                }
                worker->resume_cv.notify_all();
            }
        }
    }

    if (timed_out) {
        X86_PUSH32(0x2D45D4u);
        X86_PUSH32(0x12F073u);
        sub_001324C0(ctx, base);
        ctx.esp += 4;
    }

    worker->priority = orig_prio;
    if (video_trace::Enabled()) {
        video_trace::Event("border_sync_end", worker->id, worker->suspend.load(), worker->parked.load(), timed_out ? 1 : 0);
    }

    ctx.eax = 1;
    ctx.esp += 4;
    return;
}
#endif
