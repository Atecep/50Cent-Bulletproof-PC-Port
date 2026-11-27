#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

X86_FUNC(sub_000178A0);
X86_FUNC(sub_00017900);
X86_FUNC(sub_00017910);
X86_FUNC(sub_0008F550);
X86_FUNC(sub_0008F700);
X86_FUNC(sub_0008F770);

uint8_t* kernel::g_base = nullptr;

uint32_t apu::SampleTime() { std::abort(); }

int main() {
    std::vector<uint8_t> memory(0x400000);
    kernel::g_base = memory.data();
    X86Context ctx{};
    constexpr uint32_t stack = 0x1000;
    constexpr float headshot = 1.0f / 90.0f;
    const auto call = [&](auto hook) {
        ctx.esp = stack;
        hook(ctx, memory.data());
        if (ctx.esp != stack + 4) std::abort();
    };
    const auto writeFloat = [&](uint32_t address, float value) {
        std::memcpy(memory.data() + address, &value, sizeof(value));
    };
    const auto check = [](float actual, float expected) {
        if (!(std::abs(actual - expected) < 0.00001f)) std::abort();
    };
    for (const int fps : {30, 60, 90, 120}) {
        const float dt = 1.0f / fps;
        writeFloat(0x30E648, dt);
        call(sub_00017900);
        call(sub_00017910);
        check(kernel::G<float>(0x2E0D38), dt);

        writeFloat(stack + 4, headshot);
        kernel::W32(stack + 8, 1);
        call(sub_000178A0);
        // Replay the two calls seen on a single guest clock sample.
        for (int i = 0; i < fps / 5; ++i) {
            call(sub_00017910);
            call(sub_00017910);
        }
        check(kernel::G<float>(0x2E0D38), dt / 3.0f);

        // Re-enter the same headshot ramp from the settled slow-motion rate.
        call(sub_000178A0);
        call(sub_00017910);
        check(kernel::G<float>(0x2E0D38), dt / 3.0f);

        // Guest debug controls can set the flag without setting up a host ramp.
        call(sub_00017900);
        memory[0x323044] = 1;
        call(sub_000178A0);
        call(sub_00017910);
        constexpr float step = (headshot - 1.0f / 30.0f) / 3.0f;
        check(kernel::G<float>(0x323040), step);

        call(sub_00017900);
        call(sub_00017910);
        check(kernel::G<float>(0x2E0D38), dt);

        // The takedown callsite requests the 30 Hz reference with a ramp.
        writeFloat(stack + 4, 1.0f / 30.0f);
        call(sub_000178A0);
        call(sub_00017910);
        check(kernel::G<float>(0x2E0D38), dt);

        // Phase 6D.4: Headshot bullet camera progression test (sub_0008F550 / sub_0008F770)
        // Camera object at offset 0x200000
        constexpr uint32_t cam = 0x200000;
        constexpr float total_dist = 2.10180562f;
        constexpr float factor = 1.0f;

        // Init Mode 4
        ctx.ecx = cam;
        ctx.esp = stack;
        sub_0008F550(ctx, memory.data());
        writeFloat(cam + 0x1E8, total_dist);
        writeFloat(cam + 0x1E0, factor);

        float real_elapsed = 0.0f;
        float impact_time = 0.0f;
        int impact_frame = 0;
        float finish_time = 0.0f;
        int finish_frame = 0;

        for (int frame = 1; frame <= 500; ++frame) {
            real_elapsed += dt;
            ctx.ecx = cam;
            ctx.esp = stack;
            sub_0008F770(ctx, memory.data());

            const uint32_t phase = kernel::G<uint32_t>(cam + 0x214);
            if (phase == 1 && impact_frame == 0) {
                impact_frame = frame;
                impact_time = real_elapsed;
            }

            if (ctx.eax == 0) {
                finish_frame = frame;
                finish_time = real_elapsed;
                break;
            }
        }

        // Verify pre-impact phase: must reach impact at ~1.200 real seconds across all framerates
        if (impact_frame == 0) std::abort();
        if (std::abs(impact_time - 1.200f) > 0.05f) std::abort();

        // Verify post-impact phase: must last ~1.000 real second across all framerates
        if (finish_frame == 0) std::abort();
        const float post_impact_dur = finish_time - impact_time;
        if (std::abs(post_impact_dur - 1.000f) > 0.05f) std::abort();

        // Total sequence duration must be ~2.200 seconds across all framerates
        if (std::abs(finish_time - 2.200f) > 0.08f) std::abort();

        // Clean up Mode 4
        ctx.ecx = cam;
        ctx.esp = stack;
        sub_0008F700(ctx, memory.data());
    }
}
