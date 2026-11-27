#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "core/desktop_input.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00086400);
X86_EXTERN_FUNC(__imp__sub_00050900);

namespace {
constexpr uint32_t selectorMode = 0x0032FAE4;
constexpr uint32_t selectorIndex = 0x0032F980;
constexpr uint32_t pauseMode = 0x00329A10;

bool WeaponSelectorActive() {
    return kernel::R32(selectorMode) == 1 && kernel::R32(pauseMode) == 0;
}
}

// This original input update opens the HUD with 4EA90 and closes it with
// 4C800 on Black release. Publish the resulting state to the desktop bridge.
X86_FUNC(sub_00086400) {
    __imp__sub_00086400(ctx, base);
    input::DesktopWeaponSelector(WeaponSelectorActive());
}

X86_FUNC(sub_00050900) {
    const auto step = input::PeekWeaponWheelStep();
    if (!step.direction || !WeaponSelectorActive()) {
        __imp__sub_00050900(ctx, base);
        return;
    }

    const auto saved = ctx;
    const auto controller = kernel::CallGuest(ctx, 0x0011F3F0, {0});
    ctx = saved;
    if (!controller) {
        __imp__sub_00050900(ctx, base);
        return;
    }

    // 11F200 returns the normalized held mask at +2C. Only the selector
    // receives these directions, and its native repeat timer still applies.
    const auto address = controller + 0x2C;
    const auto held = kernel::R32(address);
    const auto index = kernel::R32(selectorIndex);
    kernel::W32(address, (held & ~0xF0u) | (step.direction < 0 ? 0x20u : 0x10u));
    __imp__sub_00050900(ctx, base);
    kernel::W32(address, held);
    if (kernel::R32(selectorIndex) != index) input::CompleteWeaponWheelStep(step.id);
}
#endif
