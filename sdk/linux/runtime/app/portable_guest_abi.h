#pragma once

#include "cpu/x86_ops.h"

// Both sides use Clang's Windows x64 MSVC ABI and the same C++ exception runtime.
struct PortableGuestServices {
    decltype(&x86_icall) icall;
    decltype(&x86_resume) resume;
    decltype(&x86_kernel) kernel;
    decltype(&x86_trap) trap;
    decltype(&x86_priv) priv;
    decltype(&x86_trace) trace;
    decltype(&x86_rdtsc) rdtsc;
    decltype(&x86_cpuid) cpuid;
    decltype(&apu::SampleTime) sample_time;
    X86Func* const* hooks;
    uint32_t hook_count;
};

struct PortableGuestImage {
    const X86FuncMapping* mappings;
    const uint32_t* continuations;
    uint32_t code_base, code_end, entry_point;
};

using PortableGuestBind = bool(uint32_t, const PortableGuestServices*, PortableGuestImage*);
using PortableGuestProbe = bool(void (*)());
inline constexpr uint32_t kPortableGuestAbi = 1;
