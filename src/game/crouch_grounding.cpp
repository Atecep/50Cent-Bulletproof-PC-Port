#if defined(__linux__) || defined(XBE_VK_TARGET)
#include <bit>

#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_0011DD30);

X86_FUNC(sub_0011DD30) {
    // The crouch jog state normalizes its motion vector at 0x0006A787.
    // Its next physics update replaces X/Z but integrates gravity on the
    // retained Y, so normalizing that component each step damps a real fall.
    const auto actor = kernel::R32(ctx.esp) == 0x0006A78C
        ? kernel::R32(0x0032DCFC) : 0;
    const auto output = kernel::R32(ctx.esp + 4);
    const bool crouchFall = actor && output == actor + 0x31E0 &&
                            output == kernel::R32(ctx.esp + 8) &&
                            kernel::R32(actor + 0x32A0) == 2 &&
                            base[actor + 0x18] == 0;
    const auto verticalBits = crouchFall ? kernel::R32(output + 4) : 0;

    __imp__sub_0011DD30(ctx, base);

    if (crouchFall && std::bit_cast<float>(verticalBits) < 0.0f) {
        kernel::W32(output + 4, verticalBits);
    }
}
#endif
