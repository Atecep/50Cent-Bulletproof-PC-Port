// Thread synchronization hooks for XACT priority queue and sound event processing.
// Resolves host multi-core race condition where guest single-core IRQL-based
// synchronization allowed concurrent heap modification between main thread and
// XACT worker thread (signal 11 in sub_0026375E / sub_00263E57).

#include <mutex>
#include "xdk/hle.h"
#include "kernel/kernel.h"
#include "core/log.h"

static std::recursive_mutex g_xact_queue_mutex;

// sub_0026375E: 64-bit timestamp/priority comparator for sound events.
// Protects against NULL element pointers causing SIGSEGV in guest thread 34.
extern "C" __attribute__((weak)) void __imp__sub_0026375E(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0026375E) {
    const uint32_t item1 = ctx.ecx;
    const uint32_t item2 = kernel::R32(ctx.esp + 4);
    if (!item1 || !item2) {
        if (!item1 && item2) ctx.eax = 0xFFFFFFFFu;
        else if (item1 && !item2) ctx.eax = 1;
        else ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_0026375E) {
        __imp__sub_0026375E(ctx, base);
    } else {
        ctx.esp += 8;
    }
}

// Heap mutators and accessors protected by host recursive mutex:
extern "C" __attribute__((weak)) void __imp__sub_00263EDF(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00263EDF) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00263EDF) __imp__sub_00263EDF(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00263E57(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00263E57) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00263E57) __imp__sub_00263E57(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00263F3D(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00263F3D) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00263F3D) __imp__sub_00263F3D(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00263E34(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00263E34) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00263E34) __imp__sub_00263E34(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00264273(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00264273) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00264273) __imp__sub_00264273(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00264420(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00264420) {
    std::lock_guard<std::recursive_mutex> lock(g_xact_queue_mutex);
    if (__imp__sub_00264420) __imp__sub_00264420(ctx, base);
}

// Hardware voice accessor guards.
// On original uniprocessor Xbox, page 0 was identity-mapped and readable/writable,
// so accessing hardware channel properties when a voice was unallocated, stopped,
// or software-only (voice_hw == 0) did not raise an unhandled page fault.
// On modern host OS with PROT_NONE guard pages, dereferencing voice_hw (offset 0x40, etc.)
// triggers signal 11 (SIGSEGV) in guest worker threads (e.g. guest thread 14).

extern "C" __attribute__((weak)) void __imp__sub_00272659(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272659) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_00272659) __imp__sub_00272659(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_0027252A(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_0027252A) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_0027252A) __imp__sub_0027252A(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_002727EB(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_002727EB) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_002727EB) __imp__sub_002727EB(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00272DBF(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272DBF) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    if (__imp__sub_00272DBF) __imp__sub_00272DBF(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00272E57(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272E57) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
    if (__imp__sub_00272E57) __imp__sub_00272E57(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_002722D5(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_002722D5) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_002722D5) __imp__sub_002722D5(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_002723BB(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_002723BB) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 12;
        return;
    }
    if (__imp__sub_002723BB) __imp__sub_002723BB(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00272EE9(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272EE9) {
    if (!ctx.ecx) {
        ctx.esp += 4;
        return;
    }
    if (__imp__sub_00272EE9) __imp__sub_00272EE9(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00272F05(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272F05) {
    if (!ctx.ecx) {
        ctx.esp += 4;
        return;
    }
    if (__imp__sub_00272F05) __imp__sub_00272F05(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00272FD3(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00272FD3) {
    if (!ctx.ecx) {
        ctx.eax = 0;
        ctx.esp += 8;
        return;
    }
    if (__imp__sub_00272FD3) __imp__sub_00272FD3(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00049F20(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00049F20) {
    const uint32_t ctrl = ctx.esp ? kernel::R32(ctx.esp + 4) : 0;
    const uint32_t desc = ctrl ? kernel::R32(ctrl + 0x0C) : 0;
    LOGR("PUSH_MENU: manager=0x%08X, ctrl=0x%08X, desc=0x%08X", ctx.ecx, ctrl, desc);
    if (__imp__sub_00049F20) __imp__sub_00049F20(ctx, base);
}

extern "C" __attribute__((weak)) void __imp__sub_00049FC0(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_00049FC0) {
    const uint32_t depth = ctx.ecx ? kernel::R32(ctx.ecx + 0x0C) : 0;
    LOGR("POP_MENU: manager=0x%08X, depth=%u", ctx.ecx, depth);
    if (__imp__sub_00049FC0) __imp__sub_00049FC0(ctx, base);
}

// -----------------------------------------------------------------------------
// Lua 5.0 Protected Call & Exception Unwinding Bridge
// -----------------------------------------------------------------------------
// The original Xbox game uses Lua 5.0's setjmp/longjmp error handling mechanism:
//   luaD_rawrunprotected (sub_000E7A00) sets up struct lua_longjmp on the guest stack
//   and calls _setjmp3 (sub_001E0E28).
//   When a script error occurs, luaD_throw (sub_000E79C0) calls longjmp (sub_001E0DAC).
//
// In static recompilation, longjmp attempted an indirect call to 0x000E7A28 (the return
// site of _setjmp3 inside sub_000E7A00). Because 0x000E7A28 is an internal instruction
// rather than a recompiled function entry point, x86_icall aborted with:
//   "indirect call to 000E7A28 has no recompiled function"
// Furthermore, even if 0x000E7A28 were a function, a regular C++ function call cannot
// unwind intervening recompiled C++ frames.
//
// We bridge this using native C++ exception unwinding:
//   - sub_001E0DAC throws LuaLongJmpException with the target lua_longjmp and status code.
//   - sub_000E7A00 establishes the protected frame in a try/catch block, catching
//     LuaLongJmpException, unwinding host C++ frames, restoring callee-saved registers
//     and L->errorJmp, and returning the status code to luaD_pcall.
// -----------------------------------------------------------------------------

struct LuaLongJmpException {
    uint32_t target_lj;
    int status;
};

extern "C" __attribute__((weak)) void __imp__sub_001E0DAC(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_001E0DAC) {
    const uint32_t jmp_buf = kernel::R32(ctx.esp + 4);
    const int val = static_cast<int>(kernel::R32(ctx.esp + 8));
    const uint32_t target_lj = jmp_buf - 4;
    int status = static_cast<int>(kernel::R32(target_lj + 0x44));
    if (status == 0) {
        status = (val != 0) ? val : 1;
    }
    LOGW("Lua longjmp intercepted: target_lj=0x%08X status=%d val=%d", target_lj, status, val);
    throw LuaLongJmpException{target_lj, status};
}

extern "C" __attribute__((weak)) void __imp__sub_000E7A00(X86Context& __restrict ctx, uint8_t* base);
X86_FUNC(sub_000E7A00) {
    const uint32_t saved_esp = ctx.esp;
    const uint32_t saved_ebp = ctx.ebp;
    const uint32_t saved_esi = ctx.esi;
    const uint32_t saved_edi = ctx.edi;
    const uint32_t saved_ebx = ctx.ebx;

    const uint32_t L = kernel::R32(saved_esp + 4);
    const uint32_t f = kernel::R32(saved_esp + 8);
    const uint32_t ud = kernel::R32(saved_esp + 12);

    // Emulate original guest stack allocation for struct lua_longjmp (0x48 bytes)
    // [ebp-0x48]: previous
    // [ebp-0x44]..[ebp-0x08]: jmp_buf (64 bytes)
    // [ebp-0x04]: status
    ctx.esp -= 4; kernel::W32(ctx.esp, saved_ebp);
    ctx.ebp = ctx.esp;
    ctx.esp -= 0x48;
    const uint32_t my_lj = ctx.ebp - 0x48;
    const uint32_t prev_lj = kernel::R32(L + 0x50);

    kernel::W32(my_lj, prev_lj);
    kernel::W32(my_lj + 0x44, 0);
    kernel::W32(L + 0x50, my_lj);

    ctx.esp -= 4; kernel::W32(ctx.esp, saved_esi);

    // Populate jmp_buf
    const uint32_t jmp_buf = my_lj + 4;
    kernel::W32(jmp_buf + 0x00, ctx.ebp);
    kernel::W32(jmp_buf + 0x04, ctx.ebx);
    kernel::W32(jmp_buf + 0x08, ctx.edi);
    kernel::W32(jmp_buf + 0x0C, ctx.esi);
    kernel::W32(jmp_buf + 0x10, ctx.esp);
    kernel::W32(jmp_buf + 0x14, 0x000E7A28u);
    kernel::W32(jmp_buf + 0x20, 0x56433230u); // "VC20"

    int status = 0;
    try {
        // Call f(L, ud) - cdecl convention: push ud, push L, push ret_addr
        ctx.esp -= 4; kernel::W32(ctx.esp, ud);
        ctx.esp -= 4; kernel::W32(ctx.esp, L);
        ctx.esp -= 4; kernel::W32(ctx.esp, 0x000E7A3Au);
        x86_icall(ctx, base, f);
        ctx.esp += 8; // clean up ud, L
        status = static_cast<int>(kernel::R32(my_lj + 0x44));
    } catch (const LuaLongJmpException& ex) {
        if (ex.target_lj != my_lj) {
            // Belongs to an outer protected frame: restore errorJmp and rethrow
            kernel::W32(L + 0x50, prev_lj);
            ctx.esp = saved_esp;
            ctx.ebp = saved_ebp;
            ctx.esi = saved_esi;
            ctx.edi = saved_edi;
            ctx.ebx = saved_ebx;
            throw;
        }
        status = ex.status;
        const uint32_t top = kernel::R32(L + 0x08);
        if (top >= 8) {
            const uint32_t str_obj = kernel::R32(top - 8);
            const uint32_t type_tag = kernel::R32(top - 4);
            if (type_tag == 4 && str_obj) {
                const char* err_str = reinterpret_cast<const char*>(base + str_obj + 0x10);
                LOGW("Lua protected call caught error (%d): %s", status, err_str);
            } else {
                LOGW("Lua protected call caught error (%d)", status);
            }
        } else {
            LOGW("Lua protected call caught error (%d)", status);
        }
    }

    // Restore L->errorJmp
    kernel::W32(L + 0x50, prev_lj);

    // Restore callee-saved registers and pop ret_addr
    ctx.esp = saved_esp + 4;
    ctx.ebp = saved_ebp;
    ctx.esi = saved_esi;
    ctx.edi = saved_edi;
    ctx.ebx = saved_ebx;
    ctx.eax = static_cast<uint32_t>(status);
}


