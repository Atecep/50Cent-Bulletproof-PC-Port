#include "framerate_timing.h"
#include "timing_telemetry.h"
#include <algorithm>
#include <cmath>
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

// Phase 6D: Frame rate independence for animations, takedown clock overrides,
// scrolling textures (steam/blood/fluids), and environmental particle effects.
//
// Golden Reference: 30 FPS console behavior is the standard.
// At 60, 90, and 120 FPS:
// - Locomotion remains rock-stable (~0.833s run_class1)
// - Takedowns advance at 1.0 guest second per 1.0 real second (eliminating 2x-4x speedup)
// - Scrolling UVs and blood drips advance per wall second identical to 30 FPS
// - Particle emitters receive real-time delta instead of hardcoded 1/30s steps

namespace {

static float s_target_dt = 1.0f / 30.0f;
static float s_current_unscaled = 1.0f / 30.0f;
static float s_ramp_step = 0.0f;
static bool s_override_active = false;

static float s_headshot_pre_time = 0.0f;
static bool s_headshot_active = false;

} // namespace

namespace timing {

bool IsClockOverrideActive() {
    return s_override_active;
}

float GetCurrentTimeScale() {
    return s_target_dt * 30.0f;
}

float GetEffectiveDeltaTime() {
    return s_current_unscaled;
}

bool IsHeadshotCameraActive() {
    return s_headshot_active;
}

float GetHeadshotPreImpactTime() {
    return s_headshot_pre_time;
}

} // namespace timing

// sub_000178A0: Setup fixed timestep override (used by takedowns, e.g. 0x0006FAEC, and headshots, e.g. 0x00067D50)
extern "C" __attribute__((weak)) void __imp__sub_000178A0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_000178A0) {
    const float target_dt = *reinterpret_cast<const float*>(base + ctx.esp + 4);
    const uint32_t ramp = kernel::R32(ctx.esp + 8);
    const bool already_overridden = s_override_active && base[0x00323044] != 0;

    *reinterpret_cast<float*>(base + 0x002E0D3C) = target_dt;
    *reinterpret_cast<uint8_t*>(base + 0x00323044) = 1;

    s_target_dt = target_dt;
    s_override_active = true;

    if (!ramp) {
        s_current_unscaled = target_dt;
        s_ramp_step = 0.0f;
        *reinterpret_cast<float*>(base + 0x00323040) = 0.0f;

        const float real_dt = *reinterpret_cast<const float*>(base + 0x0030E648);
        const float dt_clamped = std::min(std::max(real_dt, 0.0001f), 0.05f);
        const float frame_factor = dt_clamped * 30.0f;
        *reinterpret_cast<float*>(base + 0x002E0D38) = target_dt * frame_factor;
    } else {
        // Only a transition from the normal clock starts at the 30 Hz reference.
        // A new target during slow motion starts from the active ramp value.
        if (!already_overridden) {
            s_current_unscaled = 1.0f / 30.0f;
        }
        s_ramp_step = (target_dt - s_current_unscaled) / 3.0f;
        *reinterpret_cast<float*>(base + 0x00323040) = s_ramp_step;
    }

    ctx.esp += 4;
}

// sub_00017900: Reset fixed timestep override (called e.g. at 0x0007023C when takedown finishes)
extern "C" __attribute__((weak)) void __imp__sub_00017900(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00017900) {
    s_override_active = false;
    s_ramp_step = 0.0f;
    *reinterpret_cast<uint8_t*>(base + 0x00323044) = 0;
    *reinterpret_cast<float*>(base + 0x00323040) = 0.0f;

    ctx.esp += 4;
}

// sub_00017910: Per-frame timestep updater
extern "C" __attribute__((weak)) void __imp__sub_00017910(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00017910) {
    const float real_dt = *reinterpret_cast<const float*>(base + 0x0030E648);
    const float dt_clamped = std::min(std::max(real_dt, 0.0001f), 0.05f);
    const uint8_t override_flag = *reinterpret_cast<const uint8_t*>(base + 0x00323044);

    if (!override_flag) {
        s_override_active = false;
        s_ramp_step = 0.0f;
        s_current_unscaled = 1.0f / 30.0f;
        *reinterpret_cast<float*>(base + 0x002E0D38) = dt_clamped;
        *reinterpret_cast<float*>(base + 0x00323040) = 0.0f;
    } else {
        // Override active: advance time proportionally to presentation rate
        // calibrated against the original 30 FPS console reference.
        const float frame_factor = dt_clamped * 30.0f;

        if (s_ramp_step != 0.0f) {
            s_current_unscaled += s_ramp_step * frame_factor;
            if ((s_ramp_step > 0.0f && s_current_unscaled >= s_target_dt) ||
                (s_ramp_step < 0.0f && s_current_unscaled <= s_target_dt)) {
                s_current_unscaled = s_target_dt;
                s_ramp_step = 0.0f;
                *reinterpret_cast<float*>(base + 0x00323040) = 0.0f;
            }
        }

        const float effective_dt = s_current_unscaled * frame_factor;
        *reinterpret_cast<float*>(base + 0x002E0D38) = effective_dt;
    }

    ctx.esp += 4;
}

// sub_000D9930: Texture UV scrolling and blood drips updater.
// Original code advances offsets directly per visual frame without delta time.
// Scaled by (dt * 30.0f) so visual scrolling speed is identical to 30 FPS reference.
extern "C" __attribute__((weak)) void __imp__sub_000D9930(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_000D9930) {
    if (!ctx.ecx) {
        ctx.esp += 4;
        return;
    }

    const float real_dt = *reinterpret_cast<const float*>(base + 0x0030E648);
    const float dt_clamped = std::min(std::max(real_dt, 0.0001f), 0.05f);
    const float scale = dt_clamped * 30.0f;

    // List 1: standard scrolling UV textures
    const uint32_t count1 = x86_read32(base, ctx.ecx);
    const uint32_t list1 = x86_read32(base, ctx.ecx + 0x10);
    if (count1 && list1) {
        uint32_t item = list1 + 0x0C;
        for (uint32_t i = 0; i < count1; ++i) {
            const float cur = *reinterpret_cast<const float*>(base + item);
            if (cur > 0.0f) {
                const float delta = *reinterpret_cast<const float*>(base + item + 4);
                *reinterpret_cast<float*>(base + item) = cur + delta * scale;
            }
            item += 0x18;
        }
    }

    // List 2: decaying drip textures
    const uint32_t count2 = x86_read32(base, ctx.ecx + 0x04);
    const uint32_t list2 = x86_read32(base, ctx.ecx + 0x14);
    if (count2 && list2) {
        const float max_val = *reinterpret_cast<const float*>(base + 0x002B8270);
        uint32_t item = list2 + 0x18;
        for (uint32_t i = 0; i < count2; ++i) {
            const float cur = *reinterpret_cast<const float*>(base + item);
            if (cur > 0.0f) {
                const float delta = *reinterpret_cast<const float*>(base + item + 4);
                *reinterpret_cast<float*>(base + item) = cur + delta * scale;

                const float pos = *reinterpret_cast<const float*>(base + item - 0x0C);
                const float speed = *reinterpret_cast<const float*>(base + item - 0x08);
                float new_pos = pos + speed * scale;
                if (new_pos > max_val) new_pos = max_val;
                *reinterpret_cast<float*>(base + item - 0x0C) = new_pos;

                const float decay = *reinterpret_cast<const float*>(base + item - 0x04);
                if (decay > 0.0f && decay < 1.0f) {
                    *reinterpret_cast<float*>(base + item - 0x08) = speed * std::pow(decay, scale);
                } else {
                    *reinterpret_cast<float*>(base + item - 0x08) = speed * decay;
                }
            }
            item += 0x20;
        }
    }

    ctx.esp += 4;
}

// sub_00032950: Primary particle system updater (wall steam vents P_DO_AcVent01, etc.).
// Adjusts hardcoded 1/30s inputs (e.g. from 0x00089A00) to actual frame presentation time.
extern "C" __attribute__((weak)) void __imp__sub_00032950(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00032950) {
    float dt = *reinterpret_cast<float*>(base + ctx.esp + 4);
    const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);

    // If caller pushed hardcoded 0x3D088889 (1.0f / 30.0f), adjust to effective frame delta
    if (std::abs(dt - (1.0f / 30.0f)) < 0.0002f && eff_dt > 0.0001f) {
        dt = std::min(eff_dt, 1.0f / 30.0f);
        *reinterpret_cast<float*>(base + ctx.esp + 4) = dt;
    }

    if (__imp__sub_00032950) {
        __imp__sub_00032950(ctx, base);
    } else {
        ctx.esp += 4;
    }
}

// sub_00044EE0: Secondary particle buffer ([0x0032F7C0]), initialized with bullet_decal.tex.
// Original code pushes hardcoded 0x3D088889 (1/30s) every visual frame.
// Use the clamped effective game delta, which includes any active clock override.
extern "C" __attribute__((weak)) void __imp__sub_001245F0(X86Context& __restrict ctx, uint8_t* base);
extern "C" __attribute__((weak)) void __imp__sub_00044EE0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00044EE0) {
    const uint32_t buf = *reinterpret_cast<const uint32_t*>(base + 0x0032F7C0);
    if (buf && __imp__sub_001245F0) {
        const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);
        const float dt_clamped = std::min(std::max(eff_dt, 0.0001f), 1.0f / 30.0f);
        ctx.ecx = buf;
        ctx.esp -= 4;
        *reinterpret_cast<float*>(base + ctx.esp) = dt_clamped;
        ctx.esp -= 4;
        *reinterpret_cast<uint32_t*>(base + ctx.esp) = 0x44EF0u;
        __imp__sub_001245F0(ctx, base);
    }
    ctx.esp += 4;
}

// sub_001245F0: Core particle integration subroutine called by sub_00032950 and sub_00044EE0.
// Normalizes any hardcoded 1/30s (0x3D088889) input to the effective frame delta.
X86_FUNC(sub_001245F0) {
    const uint32_t buf = ctx.ecx;
    const float dt_in = *reinterpret_cast<float*>(base + ctx.esp + 4);
    float dt = dt_in;
    const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);
    if (std::abs(dt - (1.0f / 30.0f)) < 0.0002f && eff_dt > 0.0001f) {
        dt = std::min(eff_dt, 1.0f / 30.0f);
        *reinterpret_cast<float*>(base + ctx.esp + 4) = dt;
    }
    if (timing::IsTelemetryEnabled && timing::IsTelemetryEnabled()) {
        if (timing::OnParticleIntegrate) {
            timing::OnParticleIntegrate(buf, dt_in, dt, base);
        }
    }
    if (__imp__sub_001245F0) {
        __imp__sub_001245F0(ctx, base);
    } else {
        ctx.esp += 8;
    }
    if (timing::IsTelemetryEnabled && timing::IsTelemetryEnabled()) {
        if (timing::OnParticleIntegratePost) {
            timing::OnParticleIntegratePost(buf, dt, base);
        }
    }
}

// sub_00124090: Emitter Force Field / Radial Turbulence update routine.
// Phase 6D.6: Frame rate independence for emitter force fields and particle turbulence.
// Called by sub_0003A0A0 for active emitters to apply radial impulse forces to particles.
// In the original code, when branch 0x001244F2 is taken (condition [esp+0x19] == 0),
// the force vector calculation does not scale by dt, leaving [esp+0x24] = 1.0f.
// As a result, a raw velocity impulse (~0.05 m/s) is subtracted directly per frame update.
// At 30 FPS: 30 impulses/sec -> terminal horizontal velocity ~0.54 m/s.
// At 120 FPS: 120 impulses/sec -> 4x accumulation (6.38 m/s^2), driving velocity to ~1.73 m/s
// and lateral dispersion from 0.335m up to 1.893m.
//
// Calibration: We hook sub_00124090, record pre-force particle positions and velocities,
// invoke __imp__sub_00124090, and when running above 30 FPS (scale = dt * 30.0f < 1.0f),
// scale the discrete deltas:
//   v_final = v_old + (v_post - v_old) * scale
//   p_final = p_old + (p_post - p_old) * scale
// At 30 FPS, scale = 1.0f (exact byte-level original behavior).
// At 60/90/120 FPS, the velocity change accumulated per real second is identical to 30 FPS.
extern "C" __attribute__((weak)) void __imp__sub_00124090(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00124090) {
    const uint32_t buf = ctx.ecx;
    if (!buf) {
        ctx.eax = 0;
        ctx.esp += 24;
        return;
    }

    const uint16_t first_p = *reinterpret_cast<const uint16_t*>(base + ctx.esp + 4);
    if (first_p == 0xFFFF) {
        if (__imp__sub_00124090) {
            __imp__sub_00124090(ctx, base);
        } else {
            ctx.eax = 0;
            ctx.esp += 24;
        }
        return;
    }

    const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);
    const float dt_arg = *reinterpret_cast<const float*>(base + ctx.esp + 20);
    const float effective_dt = (eff_dt > 0.0001f && eff_dt < 0.05f) ? eff_dt : dt_arg;
    float scale = effective_dt * 30.0f;
    if (scale > 1.0f || scale <= 0.0f) {
        scale = 1.0f;
    }

    struct ParticleState {
        uint16_t idx;
        float px, py, pz;
        float vx, vy, vz;
    };
    constexpr uint32_t kMaxTrack = 512;
    ParticleState tracked[kMaxTrack];
    uint32_t track_count = 0;

    const uint32_t pool = *reinterpret_cast<const uint32_t*>(base + buf + 0x0C);
    if (pool && scale < 0.999f) {
        uint16_t curr = first_p;
        while (curr != 0xFFFF && track_count < kMaxTrack) {
            const uint32_t p = pool + curr * 0x60;
            tracked[track_count].idx = curr;
            tracked[track_count].px = *reinterpret_cast<const float*>(base + p + 0x10);
            tracked[track_count].py = *reinterpret_cast<const float*>(base + p + 0x14);
            tracked[track_count].pz = *reinterpret_cast<const float*>(base + p + 0x18);
            tracked[track_count].vx = *reinterpret_cast<const float*>(base + p + 0x20);
            tracked[track_count].vy = *reinterpret_cast<const float*>(base + p + 0x24);
            tracked[track_count].vz = *reinterpret_cast<const float*>(base + p + 0x28);
            ++track_count;
            curr = *reinterpret_cast<const uint16_t*>(base + p + 0x3E);
            if (curr == first_p) break;
        }
    }

    if (timing::IsTelemetryEnabled && timing::IsTelemetryEnabled()) {
        if (timing::OnEmitterForceFieldBefore) {
            timing::OnEmitterForceFieldBefore(buf, first_p, effective_dt, base);
        }
    }

    if (__imp__sub_00124090) {
        __imp__sub_00124090(ctx, base);
    } else {
        ctx.eax = track_count;
        ctx.esp += 24;
    }

    if (pool && scale < 0.999f && track_count > 0) {
        for (uint32_t i = 0; i < track_count; ++i) {
            const uint32_t p = pool + tracked[i].idx * 0x60;
            float* px = reinterpret_cast<float*>(base + p + 0x10);
            float* py = reinterpret_cast<float*>(base + p + 0x14);
            float* pz = reinterpret_cast<float*>(base + p + 0x18);
            float* vx = reinterpret_cast<float*>(base + p + 0x20);
            float* vy = reinterpret_cast<float*>(base + p + 0x24);
            float* vz = reinterpret_cast<float*>(base + p + 0x28);

            *px = tracked[i].px + (*px - tracked[i].px) * scale;
            *py = tracked[i].py + (*py - tracked[i].py) * scale;
            *pz = tracked[i].pz + (*pz - tracked[i].pz) * scale;

            *vx = tracked[i].vx + (*vx - tracked[i].vx) * scale;
            *vy = tracked[i].vy + (*vy - tracked[i].vy) * scale;
            *vz = tracked[i].vz + (*vz - tracked[i].vz) * scale;
        }
    }

    if (timing::IsTelemetryEnabled && timing::IsTelemetryEnabled()) {
        if (timing::OnEmitterForceFieldAfter) {
            timing::OnEmitterForceFieldAfter(buf, first_p, effective_dt, base);
        }
    }
}

// sub_000EF7D0: AnimStateMachine::Update.
// Dynamic objects updated by sub_0008C340 push hardcoded
// 0x3D088889 (1/30s) each frame, causing 2x speed at 60 FPS and 4x speed at 120 FPS.
// Normalizes hardcoded 1/30s input to effective presentation delta while leaving character
// actor animation calls untouched.
extern "C" __attribute__((weak)) void __imp__sub_000EF7D0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_000EF7D0) {
    float dt = *reinterpret_cast<float*>(base + ctx.esp + 4);
    const float eff_dt = *reinterpret_cast<const float*>(base + 0x002E0D38);
    if (std::abs(dt - (1.0f / 30.0f)) < 0.0002f && eff_dt > 0.0001f) {
        dt = std::min(eff_dt, 1.0f / 30.0f);
        *reinterpret_cast<float*>(base + ctx.esp + 4) = dt;
    }
    if (__imp__sub_000EF7D0) {
        __imp__sub_000EF7D0(ctx, base);
    } else {
        ctx.esp += 8;
    }
}

// sub_0008F550: Camera Mode 4 (bullet / headshot follow camera) Init/Enter
extern "C" __attribute__((weak)) void __imp__sub_0008F550(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0008F550) {
    s_headshot_pre_time = 0.0f;
    s_headshot_active = true;
    if (__imp__sub_0008F550) {
        __imp__sub_0008F550(ctx, base);
    } else {
        if (ctx.ecx) {
            *reinterpret_cast<float*>(base + ctx.ecx + 0x1E4) = 0.0f;
            *reinterpret_cast<uint8_t*>(base + ctx.ecx + 0x200) = 1;
            *reinterpret_cast<uint32_t*>(base + ctx.ecx + 0x214) = 0;
            *reinterpret_cast<float*>(base + 0x00337440) = 0.0f;
        }
        ctx.esp += 8;
    }
}

// sub_0008F700: Camera Mode 4 (bullet / headshot follow camera) Exit
extern "C" __attribute__((weak)) void __imp__sub_0008F700(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0008F700) {
    s_headshot_pre_time = 0.0f;
    s_headshot_active = false;
    if (__imp__sub_0008F700) {
        __imp__sub_0008F700(ctx, base);
    } else {
        ctx.esp += 8;
    }
}

// sub_0008F770: Camera Mode 4 (bullet / headshot follow camera) Per-Frame Update.
// Phase 6D.4: Frame rate independence for headshot pre-impact camera sequence.
// Golden Reference: At 30 FPS console reference, the pre-impact zoom sequence takes
// ~1.200 seconds (~36 frames) to reach impact. Post-impact sequence lasts ~1.000 second.
// The original code incremented distance and velocity recurrence once per visual frame,
// resulting in a 4x speedup (0.300s) at 120 FPS.
// We preserve the 30 FPS console progression across 30, 60, 90, and 120 FPS based on
// real elapsed presentation time, while leaving the already-correct post-impact timer untouched.
extern "C" __attribute__((weak)) void __imp__sub_0008F770(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0008F770) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 12;
        return;
    }

    const uint32_t cam = ctx.ecx;
    const uint32_t phase = *reinterpret_cast<const uint32_t*>(base + cam + 0x214);
    const uint8_t first_frame = *reinterpret_cast<const uint8_t*>(base + cam + 0x200);

    if (first_frame == 1) {
        s_headshot_pre_time = 0.0f;
        s_headshot_active = true;
    }

    const float real_dt = *reinterpret_cast<const float*>(base + 0x0030E648);
    const float dt_clamped = std::min(std::max(real_dt, 0.0001f), 0.05f);

    if (phase == 0) {
        // Pre-impact phase: camera zooms along trajectory toward the victim's head.
        // We calibrate the progression against the 30 FPS console standard:
        // D(N) = 0.003 * (1.2^N - 1) - 0.0006 * N, where N = s_headshot_pre_time * 30.0.
        s_headshot_pre_time += dt_clamped;

        const float N = s_headshot_pre_time * 30.0f;
        constexpr float r = 1.2000000476837158f;
        constexpr float N_cap = 44.492963f;
        constexpr float D_cap = 9.973304f;

        float target_D = 0.0f;
        if (N <= N_cap) {
            target_D = 0.003f * (std::pow(r, N) - 1.0f) - 0.0006f * N;
        } else {
            target_D = D_cap + 2.0f * (N - N_cap);
        }
        if (target_D < 0.0f) target_D = 0.0f;

        float factor = *reinterpret_cast<const float*>(base + cam + 0x1E0);
        if (factor <= 0.00001f) factor = 1.0f;

        const float cur_dist = *reinterpret_cast<const float*>(base + cam + 0x1E4);
        const float target_dist = target_D * factor;
        const float delta_dist = std::max(0.0f, target_dist - cur_dist);
        *reinterpret_cast<float*>(base + 0x00337440) = delta_dist / factor;

        const float scale = dt_clamped * 30.0f;
        const float cur_shake = *reinterpret_cast<const float*>(base + cam + 0x20C);
        *reinterpret_cast<float*>(base + cam + 0x20C) = cur_shake + 0.01f * (scale - 1.0f);
    } else {
        // Post-impact phase: sub_0008F770 already subtracts real_dt from [0x00337444],
        // lasting exactly 1.0 real second at all frame rates. Do not alter.
        s_headshot_active = false;
    }

    if (__imp__sub_0008F770) {
        __imp__sub_0008F770(ctx, base);
    } else {
        // Fallback for test environments without full recompiled game objects
        if (phase == 0) {
            float factor = *reinterpret_cast<const float*>(base + cam + 0x1E0);
            if (factor <= 0.00001f) factor = 1.0f;
            float step = *reinterpret_cast<const float*>(base + 0x00337440);
            float cur_dist = *reinterpret_cast<const float*>(base + cam + 0x1E4);
            float new_dist = cur_dist + step * factor;
            float total_dist = *reinterpret_cast<const float*>(base + cam + 0x1E8);
            if (new_dist >= total_dist) {
                new_dist = total_dist;
                *reinterpret_cast<uint32_t*>(base + cam + 0x214) = 1;
                *reinterpret_cast<float*>(base + 0x00337444) = 1.0f;
            }
            *reinterpret_cast<float*>(base + cam + 0x1E4) = new_dist;
            *reinterpret_cast<uint8_t*>(base + cam + 0x200) = 0;
            ctx.eax = 0xFFFFFFFF;
        } else {
            float timer = *reinterpret_cast<float*>(base + 0x00337444);
            timer -= dt_clamped;
            *reinterpret_cast<float*>(base + 0x00337444) = timer;
            if (timer <= 0.0f) {
                ctx.eax = 0;
            } else {
                ctx.eax = 0xFFFFFFFF;
            }
        }
        ctx.esp += 12;
    }
}
