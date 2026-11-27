#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "core/desktop_input.h"
#include "core/input.h"
#include "core/log.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_0011F1F0);
X86_EXTERN_FUNC(__imp__sub_00017890);
X86_EXTERN_FUNC(__imp__sub_00094000);
X86_EXTERN_FUNC(__imp__sub_00090DF0);

X86_FUNC(sub_00017890) {
    const auto caller = kernel::R32(ctx.esp);
    __imp__sub_00017890(ctx, base);
    static const bool trace = std::getenv("XBE_MOUSE_TRACE") != nullptr;
    // This camera call stores the x87 result in its local time multiplier.
    if (!trace || (caller != 0x94047 && caller != 0x90E06)) return;

    using Clock = std::chrono::steady_clock;
    struct Sample {
        Clock::time_point last = Clock::now();
        unsigned updates = 0;
        double sum = 0;
    };
    thread_local Sample samples[2];
    auto& [last, updates, sum] = samples[caller == 0x94047 ? 0 : 1];
    sum += X87_ST(0);
    ++updates;
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - last).count();
    if (elapsed >= 1) {
        LOGR("mouse audit: camera=%08X updates=%u wall=%.6f guest_sum=%.6f",
             caller, updates, elapsed, sum);
        last = now;
        updates = 0;
        sum = 0;
    }
}

// Only intercept the camera's own normalized stick reads. Movement, menus
// and original controller dead zones continue through the unmodified getter.
X86_FUNC(sub_0011F1F0) {
    const auto caller = kernel::R32(ctx.esp);
    const auto axis = kernel::R32(ctx.esp + 4);
    switch (caller) {
        case 0x91121: case 0x9115A:
        case 0x94088: case 0x940C1:
            input::DesktopCamera();
            break;
        case 0x91193: case 0x911CF:
        case 0x940FA: case 0x94136:
            break;
        default:
            __imp__sub_0011F1F0(ctx, base);
            return;
    }
    float value = 0.0f;
    if ((axis == 2 || axis == 3) && input::DesktopLook(axis, value)) {
        ctx.eax = axis;
        x87_push(ctx, value);
        ctx.esp += 8;
    } else {
        __imp__sub_0011F1F0(ctx, base);
    }
}

// Active gameplay camera routine (Mission 0)
X86_FUNC(sub_00094000) {
    if (!input::IsMouseActive()) {
        __imp__sub_00094000(ctx, base);
        return;
    }

    input::DesktopCamera();
    const auto camera = ctx.ecx;
    const auto anglesPtr = kernel::R32(ctx.esp + 4);

    // Clear analog inertia so there is zero residual drift
    if (camera != 0) {
        *reinterpret_cast<float*>(base + camera + 0x1C8) = 0.0f;
        *reinterpret_cast<float*>(base + camera + 0x1CC) = 0.0f;
    }

    // Run guest camera logic (distance, obstruction, tracking) with stick reads returning 0.0f
    __imp__sub_00094000(ctx, base);

    // Clear analog inertia post-update
    if (camera != 0) {
        *reinterpret_cast<float*>(base + camera + 0x1C8) = 0.0f;
        *reinterpret_cast<float*>(base + camera + 0x1CC) = 0.0f;
    }

    if (anglesPtr != 0) {
        float dx = 0.0f, dy = 0.0f;
        if (input::ConsumeMouseDelta(dx, dy) && (dx != 0.0f || dy != 0.0f)) {
            const float sens = input::GetMouseSensitivityFactor();
            const float dYaw = dx * sens;
            const float dPitch = (input::invertMouseY ? -dy : dy) * sens;

            float& pitch = *reinterpret_cast<float*>(base + anglesPtr + 0x00);
            float& yaw   = *reinterpret_cast<float*>(base + anglesPtr + 0x04);

            pitch += dPitch;
            yaw   += dYaw;

            // Natural vertical pitch limits: -50 deg (-0.8726646f) to +60 deg (+1.0471976f)
            constexpr float kMinPitch = -0.8726646f;
            constexpr float kMaxPitch =  1.0471976f;
            pitch = std::clamp(pitch, kMinPitch, kMaxPitch);

            static const bool trace = std::getenv("XBE_MOUSE_TRACE") != nullptr;
            if (trace) {
                LOGR("mouse camera 94000: dx=%.2f dy=%.2f yaw=%.4f pitch=%.4f (dYaw=%.4f dPitch=%.4f)",
                     dx, dy, yaw, pitch, dYaw, dPitch);
            }
        }
    }
}

// Free / exploration camera routine
X86_FUNC(sub_00090DF0) {
    if (!input::IsMouseActive()) {
        __imp__sub_00090DF0(ctx, base);
        return;
    }

    input::DesktopCamera();
    const auto camera = ctx.ecx;

    __imp__sub_00090DF0(ctx, base);

    if (camera != 0) {
        float dx = 0.0f, dy = 0.0f;
        if (input::ConsumeMouseDelta(dx, dy) && (dx != 0.0f || dy != 0.0f)) {
            const float sens = input::GetMouseSensitivityFactor();
            const float dYaw = dx * sens;
            const float dPitch = (input::invertMouseY ? -dy : dy) * sens;

            float& pitch = *reinterpret_cast<float*>(base + camera + 0x254);
            float& yaw   = *reinterpret_cast<float*>(base + camera + 0x258);

            pitch += dPitch;
            yaw   += dYaw;

            constexpr float kMinPitch = -0.8726646f;
            constexpr float kMaxPitch =  1.0471976f;
            pitch = std::clamp(pitch, kMinPitch, kMaxPitch);

            // Synchronize with global Euler angles
            *reinterpret_cast<float*>(base + 0x00322FE4) = pitch;
            *reinterpret_cast<float*>(base + 0x00322FE8) = yaw;

            static const bool trace = std::getenv("XBE_MOUSE_TRACE") != nullptr;
            if (trace) {
                LOGR("mouse camera 90DF0: dx=%.2f dy=%.2f yaw=%.4f pitch=%.4f (dYaw=%.4f dPitch=%.4f)",
                     dx, dy, yaw, pitch, dYaw, dPitch);
            }
        }
    }
}
#endif
