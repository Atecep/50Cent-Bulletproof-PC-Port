// Bulletproof XDK 5849 bindings verified against this image's DSOUND bytes.
// Keep the Windows SDK and config/game.toml unchanged.
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "audio/dsound_voice.h"
#include "xdk/hle.h"

// stdcall(interface, BOOL), ret 8. Original: interface-0x1C -> +0x10
// voice -> byte +0x0B bit 0, enabling precomputed 3D voice data.
X86_FUNC(sub_0026CF8D) {
    (void)base;
    kernel::Return(ctx, dsound::Use3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8) != 0), 2);
}
// stdcall(interface, DS3DVOICEDATA*), ret 8: 0026CF71 -> 0026C945 -> 0026BA2C.
X86_FUNC(sub_0026CF71) {
    (void)base;
    kernel::Return(ctx, dsound::Set3DVoiceData(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)), 2);
}
// stdcall(interface, DSVOICEPROPS*), ret 8: 0026CF55 -> 0026C8F7.
X86_FUNC(sub_0026CF55) {
    (void)base;
    kernel::Return(ctx, dsound::GetVoiceProperties(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)), 2);
}

// stdcall(interface, LONG pitch), ret 8. IDirectSoundBuffer::SetPitch
X86_FUNC(sub_0026CDA5) {
    (void)base;
    kernel::Return(ctx, dsound::SetPitch(kernel::R32(ctx.esp + 4), static_cast<int32_t>(kernel::R32(ctx.esp + 8))), 2);
}

// stdcall(interface, LONG pitch), ret 8. IDirectSoundStream::SetPitch
X86_FUNC(sub_0026CFAE) {
    (void)base;
    kernel::Return(ctx, dsound::SetPitch(kernel::R32(ctx.esp + 4), static_cast<int32_t>(kernel::R32(ctx.esp + 8))), 2);
}

// stdcall(interface, LPCDSLFODESC), ret 8. IDirectSoundBuffer::SetLFO
X86_FUNC(sub_0026CDC1) {
    (void)base;
    kernel::Return(ctx, dsound::SetLFO(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)), 2);
}

// stdcall(interface, LPCDSLFODESC), ret 8. IDirectSoundStream::SetLFO
X86_FUNC(sub_0026CFA9) {
    (void)base;
    kernel::Return(ctx, dsound::SetLFO(kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8)), 2);
}

// Safe fallback for internal guest voice register pitch setter.
// [esp+4] = obj (CDirectSoundBuffer or CDirectSoundVoiceBuffer)
// [esp+8] = val
X86_EXTERN_FUNC(sub_00272E57);
X86_FUNC(sub_0026B95A) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t voice = kernel::R32(obj + 0x10);
    if (voice) {
        kernel::W32(voice + 0x18, val);
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 4;
        kernel::W32(ctx.esp, 0);
        ctx.ecx = sub;
        sub_00272E57(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register LFO setter.
// [esp+4] = obj
// [esp+8] = val
X86_EXTERN_FUNC(sub_00272DBF);
X86_FUNC(sub_0026B973) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t voice = kernel::R32(obj + 0x10);
    if (voice) {
        uint32_t cur = kernel::R32(voice + 0x20);
        kernel::W32(voice + 0x1C, val - cur);
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 4;
        kernel::W32(ctx.esp, 0);
        ctx.ecx = sub;
        sub_00272DBF(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register filter setter (sub_0026B9A2).
// [esp+4] = obj (CDirectSoundVoiceBuffer*)
// [esp+8] = filter_param
X86_EXTERN_FUNC(sub_00272659);
X86_FUNC(sub_0026B9A2) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t filter = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 8;
        kernel::W32(ctx.esp, 0);
        kernel::W32(ctx.esp + 4, filter);
        ctx.ecx = sub;
        sub_00272659(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register format setter (sub_0026B98F).
// [esp+4] = obj
// [esp+8] = val
X86_EXTERN_FUNC(sub_0027252A);
X86_FUNC(sub_0026B98F) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 8;
        kernel::W32(ctx.esp, 0);
        kernel::W32(ctx.esp + 4, val);
        ctx.ecx = sub;
        sub_0027252A(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register 3D setter (sub_0026B9B5).
// [esp+4] = obj
// [esp+8] = val
X86_EXTERN_FUNC(sub_002727EB);
X86_FUNC(sub_0026B9B5) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 8;
        kernel::W32(ctx.esp, 0);
        kernel::W32(ctx.esp + 4, val);
        ctx.ecx = sub;
        sub_002727EB(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register LFO delta setter (sub_0026B9C8).
// [esp+4] = obj
// [esp+8] = val
X86_FUNC(sub_0026B9C8) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t voice = kernel::R32(obj + 0x10);
    if (voice) {
        uint32_t cur = kernel::R32(voice + 0x20);
        uint32_t prev_delta = kernel::R32(voice + 0x1C);
        kernel::W32(voice + 0x1C, prev_delta + (cur - val));
        kernel::W32(voice + 0x20, val);
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 4;
        kernel::W32(ctx.esp, 0);
        ctx.ecx = sub;
        sub_00272DBF(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}

// Safe fallback for internal guest voice register volume setter (sub_0026B9EB).
// [esp+4] = obj
// [esp+8] = val
X86_EXTERN_FUNC(sub_0026B87A);
X86_FUNC(sub_0026B9EB) {
    const uint32_t obj = kernel::R32(ctx.esp + 4);
    const uint32_t val = kernel::R32(ctx.esp + 8);
    if (!obj) {
        kernel::Return(ctx, 0, 2);
        return;
    }
    const uint32_t voice = kernel::R32(obj + 0x10);
    if (voice) {
        ctx.esp -= 8;
        kernel::W32(ctx.esp, 0);
        kernel::W32(ctx.esp + 4, val);
        ctx.ecx = voice;
        sub_0026B87A(ctx, base);
    }
    const uint32_t sub = kernel::R32(obj + 0x0C);
    if (sub) {
        ctx.esp -= 4;
        kernel::W32(ctx.esp, 0);
        ctx.ecx = sub;
        sub_00272DBF(ctx, base);
    }
    kernel::Return(ctx, 0, 2);
}
#endif

#if defined(__linux__) || defined(XBE_VK_TARGET)
// These verified guest initializers contain only stores of existing function /
// constant-table addresses and RET. Running their own bodies preserves the
// exact tables that DirectSoundCreate's replaced console bootstrap would fill.
X86_EXTERN_FUNC(__imp__sub_0026F32A);
X86_EXTERN_FUNC(__imp__sub_0026EAFC);
X86_EXTERN_FUNC(__imp__sub_0026EB07);
void BulletproofLinuxLoaded() {
    X86Context ctx{};
    __imp__sub_0026F32A(ctx, kernel::g_base);
    __imp__sub_0026EAFC(ctx, kernel::g_base);
    __imp__sub_0026EB07(ctx, kernel::g_base);
}
#endif
