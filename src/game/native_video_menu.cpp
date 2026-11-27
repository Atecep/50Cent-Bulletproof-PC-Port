#include "xdk/hle.h"
#include "kernel/memory.h"
#include "native_menu_state.h"
#include "graphics_menu.h"
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "prompt_bindings.h"
#include "linux_gameplay_prompts.h"
#include "core/desktop_input.h"
#endif
#include <unordered_map>
#include <string>
#include <cstring>
#include <bit>
using kernel::R32;
X86_EXTERN_FUNC(__imp__sub_00053680);
X86_EXTERN_FUNC(__imp__sub_000FB700);
#if defined(__linux__) || defined(XBE_VK_TARGET)
X86_EXTERN_FUNC(__imp__sub_00051B40);
#endif
namespace {
struct Role { int row; bool value; uint32_t style; };
std::unordered_map<uint32_t,Role> roles;
std::unordered_map<uint32_t,uint32_t> strings;
thread_local uint32_t text_node=0;
int last_label=-1;
uint32_t previous_controller=0;
float menu_row_spacing = 0;
bool compact_highlight = false;
uint32_t Read(uint32_t a){return R32(a);}
#if defined(__linux__) || defined(XBE_VK_TARGET)
enum class PdaButton { Confirm, Back, Secondary, Tertiary };
struct PdaCaption {
    PdaButton button;
    uint32_t style;
};
std::unordered_map<uint32_t, PdaCaption> pdaCaptions;

const PdaCaption* FindPdaCaption(uint32_t node) {
    const auto found = pdaCaptions.find(node);
    if (found == pdaCaptions.end() || found->second.style != R32(node + 0xDC)) return nullptr;
    return &found->second;
}

void DiscoverHostCaption(uint32_t descriptor, uint32_t node, const PdaCaption& caption) {
    if (descriptor == 0x00332B14 && (caption.button == PdaButton::Confirm || caption.button == PdaButton::Back)) {
        roles.insert_or_assign(node, Role{caption.button == PdaButton::Confirm ? 5 : 6, false, caption.style});
    } else if (descriptor == 0x003329AC || descriptor == GraphicsMenu_KeyboardDescriptor()) {
        if (caption.button == PdaButton::Confirm || caption.button == PdaButton::Back) {
            roles.insert_or_assign(node, Role{caption.button == PdaButton::Confirm ? 11 : 12, false, caption.style});
        }
    }
}

bool AddPdaKey(X86Context& ctx, uint8_t* base, uint32_t descriptor, PdaButton button, uint32_t raw,
               uint32_t localized, bool hasReplacement, std::string& text) {
    using prompts::Context;
    using prompts::Control;
    auto context = Context::Menu;
    switch (descriptor) {
    case native_menu::CodesTextEntry: context = Context::PdaTextEntry; break;
    case native_menu::PdaMessages: context = Context::PdaMessages; break;
    case native_menu::PdaMusicList: context = Context::PdaMusicList; break;
    default: break;
    }
    Control control = Control::A;
    switch (button) {
    case PdaButton::Confirm:
        if (hasReplacement && text == "Adjust" && descriptor == GraphicsMenu_KeyboardDescriptor())
            control = Control::Adjust;
        break;
    case PdaButton::Back: control = Control::B; break;
    case PdaButton::Secondary: control = Control::X; break;
    case PdaButton::Tertiary: control = Control::Y; break;
    }
    const auto binding = prompts::KeyboardLabel({context, control});
    if (binding.empty()) return false;

    if (!hasReplacement) {
        const auto saved = ctx;
        const auto stringManager = R32(0x0032304C);
        if (localized) {
            ctx.ecx = stringManager;
            raw = kernel::CallGuest(ctx, 0x000F8870, {localized});
            ctx = saved;
        }
        if (raw) {
            const auto* caption = reinterpret_cast<const char*>(base + raw);
            const auto* end = static_cast<const char*>(std::memchr(caption, 0, 128));
            if (end) text.assign(caption, end);
        }
        if (localized) {
            ctx.ecx = stringManager;
            kernel::CallGuest(ctx, 0x000F8230, {localized});
            ctx = saved;
        }
    }
    if (text.empty()) return false;
    text += " (" + binding + ")";
    return true;
}
#endif

bool Active(){
    const auto desc = native_menu::Descriptor(Read);
    return desc == native_menu::MainMenuDescriptor || desc == 0x00332B14 || desc == 0x003329AC || (desc && desc == GraphicsMenu_KeyboardDescriptor()) || desc == 0x003328F8 || desc == 0x00332844;
}
uint32_t FindWidget(X86Context& ctx, uint32_t controller, int index) {
    const auto saved = ctx;
    ctx.ecx = R32(controller + 8);
    const auto name = R32(R32(controller + 12) + 0x28 + index * 4);
    const auto widget = kernel::CallGuest(ctx, 0x000495C0, {name});
    ctx = saved;
    return widget;
}

float MenuRowSpacing(X86Context& ctx, uint32_t controller, int index) {
    const auto current = FindWidget(ctx, controller, index);
    const auto previous = FindWidget(ctx, controller, index - 1);
    if (!current || !previous) return 0;
    // 489D0 uses the widget's scene node position to place the highlight.
    const auto currentNode = R32(current + 4);
    const auto previousNode = R32(previous + 4);
    if (!currentNode || !previousNode) return 0;
    return std::bit_cast<float>(R32(currentNode + 8)) -
           std::bit_cast<float>(R32(previousNode + 8));
}

void Select(X86Context& ctx, const GraphicsMenuRows& rows) {
    int index = GraphicsMenu_TakeNativeSelection();
    if (compact_highlight != (rows.count != 0)) {
        compact_highlight = rows.count != 0;
        if (index < 0) index = 0;
    }
    if(index<0)return;
    const auto controller=native_menu::Controller(Read);
    if(!controller)return;
    const auto saved=ctx;
    const auto widget = FindWidget(ctx, controller, index);
    if (widget) {
        ctx.ecx = controller;
        const auto descriptor = R32(controller + 12);
        const bool shift = (descriptor == native_menu::MainMenuDescriptor && MainMenu_ExitSelected()) ||
                           (native_menu::IsOptions(descriptor) && OptionsMenu_ShiftHighlight());
        const int offset = rows.count ? rows.selected - rows.top_margin_rows : int(shift);
        const auto node = R32(widget + 4);
        if (offset && node) {
            // The native selection routine positions the existing highlight.
            const auto y = node + 8;
            const auto original = R32(y);
            kernel::W32(y, std::bit_cast<uint32_t>(std::bit_cast<float>(original) + offset * menu_row_spacing));
            kernel::CallGuest(ctx, 0x00049220, {widget, 1});
            kernel::W32(y, original);
        } else {
            kernel::CallGuest(ctx, 0x00049220, {widget, 1});
        }
    }
    ctx=saved;
}
void KeyboardTransition(X86Context& ctx, uint8_t* base) {
    const int transition = GraphicsMenu_TakeKeyboardTransition();
    if (!transition) return;
    constexpr uint32_t manager = 0x0032A170;
    constexpr uint32_t pagerDescriptor = 0x00332BC8;
    const auto parent = native_menu::Controller(Read);
    const auto active = native_menu::Descriptor(Read);
    const auto saved = ctx;
    if (transition < 0 && active == GraphicsMenu_KeyboardDescriptor()) {
        ctx.ecx = manager;
        kernel::CallGuest(ctx, 0x00049FC0, {});
        ctx = saved;
        return;
    }
    if (transition != 1 || !parent || !native_menu::IsOptions(active) ||
        R32(manager + 0xC) >= R32(manager + 0x10)) return;

    // Resolve the scene's pager template exactly as the original open-widget
    // handler 4A220 does, without activating the Customize widget.
    ctx.ecx = R32(parent + 8);
    const auto widget = kernel::CallGuest(ctx, 0x000495C0, {R32(active + 0x34)});
    ctx = saved;
    if (!widget) return;
    const auto registryEntry = R32(widget + 0x1C);
    const auto widgetManager = R32(widget + 0xC);
    if (!registryEntry || widgetManager != manager) return;
    ctx.ecx = manager;
    const auto original = kernel::CallGuest(ctx, 0x00049610, {registryEntry + 0xC4});
    ctx = saved;
    if (!original || R32(original + 0xC) != pagerDescriptor) return;

    static uint32_t descriptor = mem::AllocSystem(0xAC, 16);
    static uint32_t controller = mem::AllocSystem(0x18, 16);
    if (!descriptor || !controller) return;
    std::memcpy(base + descriptor, base + pagerDescriptor, 0xAC);
    std::memcpy(base + controller, base + original, 0x18);
    kernel::W32(controller + 4, R32(original));
    kernel::W32(controller + 0xC, descriptor);
    // Settings actions belong to the host. The original pager callbacks
    // remain on its own controller, including wallpaper and ring mode.
    kernel::W32(controller + 0x10, 0);
    kernel::W32(controller + 0x14, 0);
    GraphicsMenu_SetKeyboardDescriptor(descriptor);
    ctx.ecx = manager;
    kernel::CallGuest(ctx, 0x00049F20, {controller});
    ctx = saved;
}

}
#if defined(__linux__) || defined(XBE_VK_TARGET)
X86_FUNC(sub_00051B40) {
    // Localized caption setters in the master PDA (1E850) and other PDA scenes (56290).
    const auto node = ctx.ecx;
    PdaButton button;
    switch (R32(ctx.esp)) {
        case 0x1E8A1: case 0x562FF: button = PdaButton::Back; break;
        case 0x1E929: case 0x56347: button = PdaButton::Confirm; break;
        case 0x1E9AC: case 0x5639D: button = PdaButton::Secondary; break;
        case 0x1EA2F: case 0x563FA: button = PdaButton::Tertiary; break;
        default:
            __imp__sub_00051B40(ctx, base);
            return;
    }
    pdaCaptions.insert_or_assign(node, PdaCaption{button, R32(node + 0xDC)});
    __imp__sub_00051B40(ctx, base);
}
#endif

// The expanded lists replay the first native label/value TextNodes at the
// original row stride. No scene nodes are inserted into the guest controller.
bool DrawCompactRows(X86Context& ctx, uint8_t* base, uint32_t node, const Role& role, const GraphicsMenuRows& rows, uint32_t descriptor) {
    const int nativeRows = descriptor == GraphicsMenu_KeyboardDescriptor() ? 3 : 4;
    if (!rows.count || role.row < 0 || role.row >= nativeRows || !menu_row_spacing) return false;
    if (role.row != 0) return true;

    auto& address = strings[node];
    if (!address) address = mem::AllocSystem(256, 16);
    if (!address) return false;
    const auto saved = ctx;
    const uint32_t raw = R32(node + 0xB0), localized = R32(node + 0xB4);
    const uint32_t width = R32(node + 0xC0), y = R32(node + 0x44);
    for (int row = 0; row < rows.count; ++row) {
        const auto text = GraphicsMenu_CompactRowText(row, role.value);
        if (text.empty() || text.size() >= 256) continue;
        std::memcpy(base + address, text.c_str(), text.size() + 1);
        kernel::CallGuest(ctx, 0x00051A80, {address});
        ctx = saved;
        const float position = std::bit_cast<float>(y) + (row - rows.top_margin_rows) * menu_row_spacing;
        kernel::W32(node + 0x44, std::bit_cast<uint32_t>(position));
        __imp__sub_00053680(ctx, base);
        ctx = saved;
    }
    kernel::W32(node + 0xB0, raw);
    kernel::W32(node + 0xB4, localized);
    kernel::W32(node + 0xC0, width);
    kernel::W32(node + 0x44, y);
    return true;
}

X86_FUNC(sub_00053680) {
#if defined(__linux__) || defined(XBE_VK_TARGET)
    SyncPagerPrompt(ctx, base);
#endif
    KeyboardTransition(ctx, base);
    const auto node=ctx.ecx;
    const auto descriptor = native_menu::Descriptor(Read);
    const bool active=Active();
    const auto compact = GraphicsMenu_CompactRows();
    if(active) {
        const auto controller=native_menu::Controller(Read);
        if (controller != previous_controller) {
            roles.clear();
            last_label = -1;
            previous_controller = controller;
            menu_row_spacing = 0;
            compact_highlight = false;
            if (descriptor == native_menu::MainMenuDescriptor) {
                menu_row_spacing = MenuRowSpacing(ctx, controller, native_menu::ExitGameRow - 1);
            } else if (native_menu::IsOptions(descriptor)) {
                menu_row_spacing = MenuRowSpacing(ctx, controller, native_menu::OptionsKeyboardRow);
            } else {
                menu_row_spacing = MenuRowSpacing(ctx, controller, 1);
            }
        }
        Select(ctx, compact);
    } else previous_controller=0;
    if (const auto it = roles.find(node); active && it != roles.end() && it->second.style == R32(node + 0xDC) &&
        DrawCompactRows(ctx, base, node, it->second, compact, descriptor)) {
        // The repeated draws restore their entry context. Consume the guest
        // return address once for the intercepted TextNode call (53B5A).
        ctx.esp += 4;
        return;
    }
    const auto old_node=text_node;text_node=active?node:0;
    const uint32_t raw=R32(node+0xB0),localized=R32(node+0xB4),width=R32(node+0xC0);
#if defined(__linux__) || defined(XBE_VK_TARGET)
    const auto* pdaCaption = FindPdaCaption(node);
    if (active && descriptor && pdaCaption) DiscoverHostCaption(descriptor, node, *pdaCaption);
#endif
    bool replaced=false;
    std::string value;
    bool has_replacement = false;
    if(active) if(auto it=roles.find(node);it!=roles.end() && it->second.style==R32(node+0xDC)) {
        const auto desc = native_menu::Descriptor(Read);
        if (desc == 0x00332B14) {
            has_replacement = GraphicsMenu_NativeText(it->second.row, it->second.value, value);
        } else if (desc == 0x003329AC) {
            has_replacement = ControlsMenu_NativeText(it->second.row, it->second.value, value);
        } else if ((desc && desc == GraphicsMenu_KeyboardDescriptor())) {
            has_replacement = KeyboardMouseMenu_NativeText(it->second.row, it->second.value, value);
        } else if (desc == 0x003328F8 || desc == 0x00332844) {
            const int physicalRow = it->second.row - 20;
            if (physicalRow >= 0 && physicalRow < 5) {
                const int row = physicalRow == 0 ? 0 : physicalRow + 1;
                value = native_menu::OptionsLabel(desc == 0x00332844, row);
                has_replacement = true;
            }
        }
    }
#if defined(__linux__) || defined(XBE_VK_TARGET)
    if (pdaCaption && AddPdaKey(ctx, base, descriptor, pdaCaption->button, raw, localized, has_replacement, value)) {
        has_replacement = true;
    }
    if (!has_replacement && GameplayPromptText(node, localized, value)) {
        has_replacement = true;
    }
    if (!has_replacement && raw) {
        const char* str = reinterpret_cast<const char*>(base + raw);
        if (raw == 0x002C0D30 || std::strcmp(str, "Press Start") == 0 || std::strcmp(str, "Press Enter") == 0) {
            value = input::DesktopUsesKeyboardPrompts() ? "Press Enter" : "Press Start";
            has_replacement = true;
            std::memcpy(base + 0x002C0D30, value.c_str(), 12);
        }
    }
    const uint32_t pagerLabel = R32(0x003306C0);
    if (!has_replacement && pagerLabel && node == pagerLabel && PagerUnreadActive()) {
        if (input::DesktopUsesKeyboardPrompts()) {
            value = "[Esc]";
            has_replacement = true;
        }
    }
#endif
    if (has_replacement) {
        auto& address = strings[node];
        if (!address) address = mem::AllocSystem(256, 16);
        if (address && value.size() < 256) {
            std::memcpy(base + address, value.c_str(), value.size() + 1);
            const auto saved = ctx;
            ctx.ecx = node;
            // 51A80 is the original raw-text setter and measures the new width.
            kernel::CallGuest(ctx, 0x00051A80, {address});
            ctx = saved;
            replaced = true;
        }
    }
    const auto role = active ? roles.find(node) : roles.end();
    const int row = role == roles.end() ? -1 : role->second.row;
    const bool options = native_menu::IsOptions(descriptor);
    const char* extraLabel = nullptr;
    float extraOffset = 0;
    if (options && row == 20 + native_menu::OptionsKeyboardRow) {
        extraLabel = native_menu::OptionsLabel(false, native_menu::OptionsKeyboardRow);
    } else if (descriptor == native_menu::MainMenuDescriptor && row == native_menu::ExitGameRow - 1) {
        extraLabel = native_menu::MainMenuLabel(native_menu::ExitGameRow);
        extraOffset = menu_row_spacing;
    }
    const auto y = R32(node + 0x44);
    const auto setY = [&](float offset) {
        kernel::W32(node + 0x44, std::bit_cast<uint32_t>(std::bit_cast<float>(y) + offset));
    };
    if (extraLabel && menu_row_spacing != 0) {
        static const uint32_t label = mem::AllocSystem(sizeof("Keyboard & Mouse"), 16);
        if (label) {
            std::memcpy(base + label, extraLabel, std::strlen(extraLabel) + 1);
            const auto draw = ctx;
            const auto currentRaw = R32(node + 0xB0);
            const auto currentLocalized = R32(node + 0xB4);
            const auto currentWidth = R32(node + 0xC0);
            kernel::CallGuest(ctx, 0x00051A80, {label});
            ctx = draw;
            setY(extraOffset);
            __imp__sub_00053680(ctx, base);
            kernel::W32(node + 0xB0, currentRaw);
            kernel::W32(node + 0xB4, currentLocalized);
            kernel::W32(node + 0xC0, currentWidth);
            ctx = draw;
        }
    }
    // 52D80 assembles the text transform using the local Y translation.
    const bool shift = (options && row > 20 && row <= 25) || extraOffset != 0;
    if (options && shift) setY(menu_row_spacing);
    else if (shift) kernel::W32(node + 0x44, y);
    __imp__sub_00053680(ctx,base);
    if (shift) kernel::W32(node + 0x44, y);
    if(replaced){kernel::W32(node+0xB0,raw);kernel::W32(node+0xB4,localized);kernel::W32(node+0xC0,width);}
    text_node=old_node;
}
// Discover TextNode roles from actual localized labels, not allocation addresses.
// Subsequent draws replace their raw-text property before the original renderer.
X86_FUNC(sub_000FB700) {
    if (text_node && !roles.contains(text_node) && Active()) {
        const auto address=R32(ctx.esp+16);
        if(address) {
            const char* value=reinterpret_cast<const char*>(base+address);
            const auto desc=native_menu::Descriptor(Read);
            if (desc == 0x00332844 &&
                (std::strcmp(value, "Enable Autosave") == 0 ||
                 std::strcmp(value, "Disable Autosave") == 0 ||
                 std::strcmp(value, "Autosave") == 0)) {
                roles.try_emplace(text_node, Role{25, false, R32(text_node+0xDC)});

            } else if (desc==0x00332B14) {
                // Audio/Video
                const char* labels[]={"Sound Effect Volume","Voice-Over Volume","Music Volume","Default"};
                int role=-1;bool is_value=false;
                for(int i=0;i<4;++i)if(std::strcmp(value,labels[i])==0){role=i;last_label=i;break;}
                if(std::strcmp(value,"Audio/Video")==0)role=4;
                if(std::strcmp(value,"Toggle On/Off")==0)role=5;
                if(std::strcmp(value,"Back")==0)role=6;
                const bool numeric=*value && std::strspn(value,"0123456789")==std::strlen(value);
                if(role<0 && numeric && last_label>=0 && last_label<3){role=last_label;is_value=true;}
                if(role>=0)roles[text_node]={role,is_value,R32(text_node+0xDC)};
            } else if (desc==0x003329AC) {
                // Controls Submenu
                const char* labels[]={"Dynamic HUD","Vibration","Crouch Mode","Game Hints","Default"};
                int role=-1;bool is_value=false;
                for(int i=0;i<5;++i)if(std::strcmp(value,labels[i])==0){role=i;last_label=i;break;}
                if(std::strcmp(value,"Game")==0)role=10; // Submenu title
                if(std::strcmp(value,"Toggle On/Off")==0)role=11; // (A) hint
                if(std::strcmp(value,"Back")==0)role=12; // (B) hint
                if(role<0 && last_label>=0 && last_label<=3 &&
                   (std::strcmp(value,"On")==0 || std::strcmp(value,"Off")==0 ||
                    std::strcmp(value,"Toggle")==0 || std::strcmp(value,"Hold")==0)) {
                    role=last_label;is_value=true;last_label=-1;
                }
                if(role>=0)roles[text_node]={role,is_value,R32(text_node+0xDC)};
            } else if ((desc && desc == GraphicsMenu_KeyboardDescriptor())) {
                // Keyboard & Mouse uses the original PDA scene text nodes.
                const char* labels[]={"Ring Mode","Wallpaper","Default"};
                int role=-1;bool is_value=false;
                // The wallpaper value and the reset action can both read "Default".
                if (last_label == 1 && std::strcmp(value, "Default") == 0) {
                    role = 1;
                    is_value = true;
                    last_label = -1;
                } else {
                    for (int i = 0; i < 3; ++i) {
                        if (std::strcmp(value, labels[i]) != 0) continue;
                        role = i;
                        last_label = i;
                        break;
                    }
                }
                if(std::strcmp(value,"Customize Pager")==0)role=10; // Submenu title
                if(std::strcmp(value,"Toggle On/Off")==0 || std::strcmp(value,"Select")==0 || std::strcmp(value,"Change")==0 || std::strcmp(value,"Toggle")==0)role=11; // (A) hint
                if(std::strcmp(value,"Back")==0)role=12; // (B) hint
                if(role<0 && last_label>=0 && last_label<2) {
                    role=last_label;is_value=true;last_label=-1;
                }
                if(role>=0)roles[text_node]={role,is_value,R32(text_node+0xDC)};
            } else if (desc == native_menu::MainMenuDescriptor) {
                for (int row = 0; row < native_menu::ExitGameRow; ++row) {
                    if (std::strcmp(value, native_menu::MainMenuLabel(row)) == 0) {
                        roles[text_node] = {row, false, R32(text_node+0xDC)};
                        break;
                    }
                }
            } else if (desc==0x003328F8 || desc==0x00332844) {
                // Options Menu (Main Menu Options & Pause Menu Options)
                if(std::strcmp(value,"Game")==0) {
                    roles[text_node]={20,false,R32(text_node+0xDC)};
                } else if(std::strcmp(value,"Camera")==0) {
                    roles[text_node]={21,false,R32(text_node+0xDC)};
                } else if(std::strcmp(value,"Audio")==0 || std::strcmp(value,"Audio/Video")==0) {
                    roles[text_node]={22,false,R32(text_node+0xDC)};
                } else if(std::strcmp(value,"Customize Pager")==0 || std::strcmp(value,"Customize")==0) {
                    roles[text_node]={23,false,R32(text_node+0xDC)};
                } else if(std::strcmp(value,"Credits")==0 || std::strcmp(value,"Codes")==0) {
                    roles[text_node]={24,false,R32(text_node+0xDC)};
                }
            }
        }
    }
    __imp__sub_000FB700(ctx,base);
}
