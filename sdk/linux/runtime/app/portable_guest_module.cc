// Compiled locally into guest.dll, outside the host target.
#include "app/portable_guest_abi.h"

namespace {
const PortableGuestServices* host;
enum Hook {
#define XBE_GUEST_HOOK(name) Hook_##name,
#include "guest_hook_list.inc"
#undef XBE_GUEST_HOOK
    HookCount
};
}

#define XBE_GUEST_HOOK(name) X86_FUNC(name) { host->hooks[Hook_##name](ctx, base); }
#include "guest_hook_list.inc"
#undef XBE_GUEST_HOOK

void x86_icall(X86Context& ctx, uint8_t* base, uint32_t target) { host->icall(ctx, base, target); }
void x86_resume(X86Context& ctx, uint8_t* base, const X86Resume& r) { host->resume(ctx, base, r); }
void x86_kernel(X86Context& ctx, uint8_t* base, uint32_t ordinal) { host->kernel(ctx, base, ordinal); }
[[noreturn]] void x86_trap(X86Context& ctx, uint32_t va, const char* what) {
    host->trap(ctx, va, what);
    __builtin_unreachable();
}
void x86_priv(X86Context& ctx, uint32_t va, const char* what) { host->priv(ctx, va, what); }
uint32_t x86_trace(X86Context& ctx, uint32_t va, uint32_t ret) { return host->trace(ctx, va, ret); }
uint64_t x86_rdtsc() { return host->rdtsc(); }
void x86_cpuid(X86Context& ctx) { host->cpuid(ctx); }
uint32_t apu::SampleTime() { return host->sample_time(); }

extern "C" __declspec(dllexport) bool XbeBindGuest(
    uint32_t version, const PortableGuestServices* services, PortableGuestImage* image) {
    if (version != kPortableGuestAbi || services->hook_count != HookCount) return false;
    host = services;
    *image = {X86FuncMappings, X86Continuations, X86CodeBase, X86CodeEnd, X86EntryPoint};
    return true;
}

extern "C" __declspec(dllexport) bool XbeProbeGuest(void (*raise)()) {
    volatile bool unwound = false;
    try {
        struct Guard {
            volatile bool& unwound;
            ~Guard() { unwound = true; }
        } guard{unwound};
        raise();
    } catch (const X86Resume& resume) {
        if (unwound && resume.target == kPortableGuestAbi) throw X86Resume{resume.target, 1};
    }
    return false;
}
