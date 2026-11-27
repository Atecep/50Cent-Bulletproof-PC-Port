#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "linux_gameplay_prompts.h"

#include "prompt_bindings.h"
#include "core/desktop_input.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

#include <array>
#include <cstring>
#include <string_view>

X86_EXTERN_FUNC(__imp__sub_0004DDF0);
X86_EXTERN_FUNC(__imp__sub_000F8870);
X86_EXTERN_FUNC(__imp__sub_0004C3C0);

namespace {
using prompts::Context;
using prompts::Control;
using kernel::R32;

constexpr uint32_t firstInteractionSlot = 10;
constexpr uint32_t interactionPriorities = 0x002E39FC;
constexpr uint32_t measureLabelCaller = 0x0004DEC1;
constexpr uint32_t textBoxStringCaller = 0x000551C0;
constexpr uint32_t textBoxHeaderCaller = 0x00053708;
constexpr size_t textCapacity = 256;

struct Prompt {
    uint32_t node = 0;
    uint32_t localized = 0;
    uint32_t scratch = 0;
    std::string text;
};
thread_local std::array<Prompt, 4> hudPrompts;
struct Measurement {
    Prompt& prompt;
    const std::string& binding;
};
thread_local Measurement* measuring = nullptr;

void AdaptTextBox(X86Context& ctx, uint8_t* base) {
    const auto* start = reinterpret_cast<const char*>(base + ctx.eax);
    if (!start) return;
    const auto* end = static_cast<const char*>(std::memchr(start, 0, textCapacity));
    if (!end) return;
    const std::string_view source(start, end - start);
    std::string text;
    if (!prompts::AdaptGlyphText(source, text)) return;
    if (text.size() >= textCapacity) return;
    static thread_local uint32_t scratch = mem::AllocSystem(textCapacity, 16);
    if (!scratch) return;
    std::memcpy(base + scratch, text.c_str(), text.size() + 1);
    ctx.eax = scratch;
}
}

X86_FUNC(sub_0004DDF0) {
    const auto stack = ctx.esp;
    const auto slot = R32(stack + 4);
    const auto index = slot - firstInteractionSlot;
    // The guest accepts equal priority and uses smaller values for stronger cues.
    if (index >= hudPrompts.size() ||
        static_cast<int32_t>(R32(stack + 8)) > static_cast<int32_t>(R32(interactionPriorities + slot * 4))) {
        __imp__sub_0004DDF0(ctx, base);
        return;
    }

    auto& prompt = hudPrompts[index];
    prompt.node = 0;
    const auto label = R32(stack + 12);
    const auto icon = R32(stack + 16);
    if (label && R32(stack + 28) == 0xFFFFFFFF) {
        if (auto control = prompts::ControlFromHudIcon(icon)) {
            auto binding = prompts::KeyboardLabel({Context::Gameplay, *control});
            if (!binding.empty()) {
                Measurement measurement{prompt, binding};
                auto* previous = measuring;
                measuring = &measurement;
                __imp__sub_0004DDF0(ctx, base);
                measuring = previous;
                kernel::W32(stack + 16, icon);
                return;
            }
        }
    }
    __imp__sub_0004DDF0(ctx, base);
}

X86_FUNC(sub_000F8870) {
    const auto caller = R32(ctx.esp);
    const bool measurePrompt = measuring && caller == measureLabelCaller;
    const auto localized = (measurePrompt || caller == textBoxStringCaller || caller == textBoxHeaderCaller) ? R32(ctx.esp + 4) : 0;
    __imp__sub_000F8870(ctx, base);
    if ((caller == textBoxStringCaller || caller == textBoxHeaderCaller) && localized && ctx.eax) {
        AdaptTextBox(ctx, base);
        return;
    }
    if (!measurePrompt || !ctx.eax) return;

    const auto& binding = measuring->binding;
    const auto* label = reinterpret_cast<const char*>(base + ctx.eax);
    const auto* end = static_cast<const char*>(std::memchr(label, 0, textCapacity));
    if (binding.empty() || !end || size_t(end - label) + binding.size() + 3 >= textCapacity) return;

    auto& prompt = measuring->prompt;
    if (!prompt.scratch) prompt.scratch = mem::AllocSystem(textCapacity, 16);
    if (!prompt.scratch) return;

    prompt.text = "[" + binding + "] ";
    prompt.text.append(label, end);
    prompt.node = ctx.esi; // 4DEAA/4DEBC keep the caption TextNode in ESI.
    prompt.localized = localized;
    std::memcpy(base + prompt.scratch, prompt.text.c_str(), prompt.text.size() + 1);
    ctx.eax = prompt.scratch;
    // 4DDF0 reads the icon argument after measuring the text. Its native
    // icon-less path lays out the bar and hides the Xbox texture together.
    kernel::W32(ctx.ebp + 0x14, 0xFFFFFFFF);
}

bool GameplayPromptText(uint32_t node, uint32_t localized, std::string& text) {
    for (const auto& prompt : hudPrompts) {
        if (!prompt.node || node != prompt.node || localized != prompt.localized) continue;
        text = prompt.text;
        return true;
    }
    return false;
}

namespace {
bool s_pagerUnreadActive = false;
bool s_lastPagerKbm = false;
constexpr uint32_t pagerMessageIconPtr = 0x00330740;
constexpr uint32_t pagerUpdateLabelPtr = 0x003306C0;

void UpdatePagerPromptInternal(X86Context& ctx, uint8_t* base) {
    const uint32_t messageNode = R32(pagerMessageIconPtr);
    const uint32_t labelNode = R32(pagerUpdateLabelPtr);
    if (!s_pagerUnreadActive) {
        if (messageNode) {
            auto saved = ctx;
            ctx.ecx = messageNode;
            kernel::CallGuest(ctx, R32(R32(messageNode) + 0x28), {0});
            ctx = saved;
            kernel::W32(messageNode + 0x50, 0);
        }
        if (labelNode) {
            auto saved = ctx;
            ctx.ecx = labelNode;
            kernel::CallGuest(ctx, R32(R32(labelNode) + 0x28), {0});
            ctx = saved;
            kernel::W32(labelNode + 0x50, 0);
        }
        return;
    }

    const bool useKbm = input::DesktopUsesKeyboardPrompts();
    s_lastPagerKbm = useKbm;
    if (useKbm) {
        if (messageNode) {
            auto saved = ctx;
            ctx.ecx = messageNode;
            kernel::CallGuest(ctx, R32(R32(messageNode) + 0x28), {0});
            ctx = saved;
            kernel::W32(messageNode + 0x50, 0);
        }
        if (labelNode) {
            static uint32_t s_escText = 0;
            if (!s_escText) {
                s_escText = mem::AllocSystem(16, 16);
                std::memcpy(base + s_escText, "[Esc]", 6);
            }
            auto saved = ctx;
            ctx.ecx = labelNode;
            kernel::CallGuest(ctx, 0x00051A80, {s_escText});
            kernel::CallGuest(ctx, R32(R32(labelNode) + 0x28), {1});
            ctx = saved;
            kernel::W32(labelNode + 0x50, 1 | 0xFF0000);
        }
    } else {
        if (messageNode) {
            auto saved = ctx;
            ctx.ecx = messageNode;
            kernel::CallGuest(ctx, R32(R32(messageNode) + 0x28), {1});
            ctx = saved;
            kernel::W32(messageNode + 0x50, 1 | 0xFF0000);
        }
        if (labelNode) {
            static uint32_t s_emptyText = 0;
            if (!s_emptyText) {
                s_emptyText = mem::AllocSystem(16, 16);
                base[s_emptyText] = 0;
            }
            auto saved = ctx;
            ctx.ecx = labelNode;
            kernel::CallGuest(ctx, 0x00051A80, {s_emptyText});
            kernel::CallGuest(ctx, R32(R32(labelNode) + 0x28), {0});
            ctx = saved;
            kernel::W32(labelNode + 0x50, 0);
        }
    }
}
} // namespace

bool PagerUnreadActive() {
    return s_pagerUnreadActive;
}

void SyncPagerPrompt(X86Context& ctx, uint8_t* base) {
    if (!s_pagerUnreadActive) return;
    if (input::DesktopUsesKeyboardPrompts() != s_lastPagerKbm) {
        UpdatePagerPromptInternal(ctx, base);
    }
}

X86_FUNC(sub_0004C3C0) {
    const uint32_t show = R32(ctx.esp + 4);
    s_pagerUnreadActive = (show != 0);
    __imp__sub_0004C3C0(ctx, base);
    UpdatePagerPromptInternal(ctx, base);
}
#endif
