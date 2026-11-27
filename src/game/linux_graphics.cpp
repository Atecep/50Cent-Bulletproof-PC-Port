// Bindings established from Bulletproof's own XDK 5849 bodies.
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "xdk/d3d8.h"
#include "xdk/hle.h"

// 250E10: stdcall(handle, UINT*), ret 8; copies object+8 (instruction
// count, populated by 2509F2..2509F6). The original reads handle+7 because
// console handles are object|1; HLE handles must be resolved by the host.
X86_FUNC(sub_00250E10) {
    (void)base;
    const uint32_t out = kernel::R32(ctx.esp + 8);
    if (out) {
        uint32_t instructions;
        if (d3d::VertexProgramSize(kernel::R32(ctx.esp + 4), instructions))
            kernel::W32(out, instructions);
    }
    // This entry is void; do not synthesize an HRESULT.
    ctx.eax = out;
    ctx.esp += 12;
}

// 24DC70: D3DDevice_SetPalette(stage, palette), stdcall ret 8.
// Updates the host stage palette so P8 texture fonts decode with their palette,
// and runs the guest function to update pushbuffer and device state.
extern "C" __attribute__((weak)) void __imp__sub_0024DC70(X86Context& __restrict ctx, uint8_t* base);

X86_FUNC(sub_0024DC70) {
    const uint32_t stage = kernel::R32(ctx.esp + 4);
    const uint32_t pal = kernel::R32(ctx.esp + 8);
    d3d::SetPalette(stage, pal);
    if (__imp__sub_0024DC70) {
        __imp__sub_0024DC70(ctx, base);
    } else {
        ctx.esp += 12;
    }
}
#endif
