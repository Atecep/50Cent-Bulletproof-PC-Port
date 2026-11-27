#include "graphics_menu.h"
#include "input_system.h"
#include "native_menu_state.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "core/desktop_input.h"
#else
namespace input {
extern uint32_t mouseSensitivity;
extern bool invertMouseY;
}
#endif
#include "kernel/kernel.h"

// Modern Invert Camera Y option (default: false = modern standard, stick UP looks UP)
bool s_invert_y = false;

namespace {

struct ResPreset {
    uint32_t width;
    uint32_t height;
};

static constexpr ResPreset kPresets[] = {
    {0, 0}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160},
};

static const char* kAspectOptions[] = {"auto", "4:3", "16:9"};
static const char* kDisplayModeNames[] = {"Fullscreen", "Borderless", "Windowed"};

struct FpsPreset {
    const char* label;
    uint32_t cap;
};

// Frame rate presets: 30, 60, 90, 120 FPS.
// 60 FPS standard default. Unlimited removed from normal menu.
static const FpsPreset kFpsPresets[] = {
    {"30 FPS", 30},
    {"60 FPS", 60},
    {"90 FPS", 90},
    {"120 FPS", 120},
};

// Secondary debug overlay state (hotkeys / combo only)
static bool s_show_debug_menu = false;
static bool s_initialized = false;
static int s_debug_selection = 0;

// Native audio rows 0–3, followed by the seven display rows 4–10.
constexpr int kDisplayStart = 4;
constexpr int kDisplayRows = 7;
constexpr int kControllerReferenceRows = 7;
constexpr int kKeyboardReferenceRows = 6;

static int s_av_nav_index = 0;
static int s_av_page = 0;
static int s_native_select = -1;
static bool s_was_in_av_menu = false;

// Controller settings and control reference
static int s_controls_page = 0;
static int s_controls_nav_index = 0;
static bool s_was_in_controls_menu = false;

// Keyboard & Mouse submenu state
static uint32_t s_kbm_descriptor = 0;
static int s_kbm_transition = 0;

#if defined(__linux__) || defined(XBE_VK_TARGET)
bool s_binding_capture = false;
const char* s_binding_prompt = "Press a key...";
constexpr const char* kBindingActions[] = {
    "Move Forward", "Move Backward", "Move Left", "Move Right",
    "Fire", "Jump", "Reload / Action", "Crouch", "Evade / Dive",
    "Grab / Hostage / Melee", "Heal / Painkiller", "Weapon Inventory"
};
constexpr int kFirstBindingPage = 3;
constexpr int kBindingCount = static_cast<int>(input::DesktopAction::Count);
static_assert(std::size(kBindingActions) == kBindingCount);
constexpr int kBindingsPerPage = 7;
constexpr int kBindingRows = kBindingCount + 2;
constexpr int kBindingPages = (kBindingRows + kBindingsPerPage - 1) / kBindingsPerPage;

#endif
static int s_kbm_page = 0;
static int s_kbm_nav_index = 0;
static bool s_was_in_kbm_menu = false;
#if defined(__linux__) || defined(XBE_VK_TARGET)
int BindingPageStart() { return (s_kbm_page - kFirstBindingPage) * kBindingsPerPage; }
int BindingPageRows() { return std::min(kBindingsPerPage, kBindingRows - BindingPageStart()); }
#endif

int MoveMenuRow(int row, int count, bool up) {
    return (row + (up ? count - 1 : 1)) % count;
}

static int s_main_menu_row = -1;

// Options menu navigation state
static int s_options_nav_index = 0;
static bool s_was_in_options_menu = false;
static uint32_t s_options_controller = 0;

// Pending settings being edited in Audio/Video menu
static app::DisplayMode s_pending_mode = app::DisplayMode::Borderless;
static uint32_t s_pending_width = 0;
static uint32_t s_pending_height = 0;
static int s_pending_preset = 0;
static int s_pending_fps_idx = 1;  // 60 FPS standard default
static int s_pending_aspect_idx = 0;
static bool s_pending_vsync = true;
static int s_pending_scale = 100;

// Confirmation dialog state (15-second countdown)
static bool s_confirming = false;
static bool s_display_apply_failed = false;
static int s_confirm_selection = 0;  // 0 = Keep Changes, 1 = Revert Now
static app::Settings s_revert_settings;
static std::chrono::steady_clock::time_point s_confirm_deadline;

// Physical gamepad edge detection
static input::Pad s_prev_pad = {};
static native_menu::ActionReleaseGuard s_action_release;

enum class GameMenuState { None, AudioVideoMenu, CameraMenu, ControlsMenu, KeyboardMouseMenu, OptionsMenu, MainMenu };

static GameMenuState GetCurrentMenuState() {
    if (!kernel::HostPtr(0)) return GameMenuState::None;
    // Active descriptor belongs to the top controller on the MenuManager stack.
    const auto active_menu=native_menu::Descriptor([](uint32_t a){return kernel::R32(a);});
    if (active_menu == native_menu::MainMenuDescriptor) return GameMenuState::MainMenu;
    if (active_menu==0x00332B14) return GameMenuState::AudioVideoMenu;
    if (active_menu==0x003329AC) return GameMenuState::ControlsMenu;
    if (active_menu && active_menu == s_kbm_descriptor) return GameMenuState::KeyboardMouseMenu;
    if (active_menu==0x00333384 || active_menu==0x00332A60 || active_menu==0x0033321C) return GameMenuState::CameraMenu;
    if (active_menu==0x003328F8 || active_menu==0x00332844) return GameMenuState::OptionsMenu;
    return GameMenuState::None;
}

void SetPendingResolution(app::Mode mode) {
    const auto selected = app::ResolutionForAspect(mode, kAspectOptions[s_pending_aspect_idx]);
    s_pending_width = selected.width;
    s_pending_height = selected.height;
}

std::string PendingResolutionLabel() {
    if (!s_pending_width || !s_pending_height) return "Auto";
    return std::to_string(s_pending_width) + "x" + std::to_string(s_pending_height);
}

void SyncPendingWithActive() {
    const auto& cur = app::Get();
    s_pending_mode = cur.display_mode;
    s_pending_width = cur.width;
    s_pending_height = cur.height;
    s_pending_vsync = cur.vsync;
    s_pending_scale = int(cur.render_percent);

    s_pending_preset = -1;
    for (int i = 0; i < int(std::size(kPresets)); i++) {
        if (kPresets[i].height == cur.height && (kPresets[i].width == cur.width ||
            (cur.height && cur.width == cur.height * 4 / 3))) {
            s_pending_preset = i;
            break;
        }
    }

    s_pending_fps_idx = 1;  // Default 60 FPS
    for (int i = 0; i < int(std::size(kFpsPresets)); i++) {
        if (kFpsPresets[i].cap == cur.fps_cap) {
            s_pending_fps_idx = i;
            break;
        }
    }

    s_pending_aspect_idx = 0;
    for (int i = 0; i < int(std::size(kAspectOptions)); i++) {
        if (cur.aspect_ratio == kAspectOptions[i]) {
            s_pending_aspect_idx = i;
            break;
        }
    }
    SetPendingResolution({cur.width, cur.height});
}

bool HasPendingChanges() {
    const auto& cur = app::Get();
    if (s_pending_mode != cur.display_mode) return true;
    const auto mode = app::ResolutionForAspect({cur.width, cur.height}, cur.aspect_ratio);
    if (s_pending_width != mode.width || s_pending_height != mode.height) return true;
    if (kFpsPresets[s_pending_fps_idx].cap != cur.fps_cap) return true;
    if (std::string(kAspectOptions[s_pending_aspect_idx]) != cur.aspect_ratio) return true;
    if (s_pending_vsync != cur.vsync) return true;
    if (uint32_t(s_pending_scale) != cur.render_percent) return true;
    return false;
}

void ApplyPendingSettings() {
    s_display_apply_failed = false;
    s_revert_settings = app::Get();

    auto& cur = app::Get();
    cur.display_mode = s_pending_mode;
    cur.width = s_pending_width;
    cur.height = s_pending_height;
    cur.fps_cap = kFpsPresets[s_pending_fps_idx].cap;
    cur.aspect_ratio = kAspectOptions[s_pending_aspect_idx];
    cur.vsync = s_pending_vsync;
    cur.render_percent = uint32_t(s_pending_scale);

    LOGI("graphics_menu: applying settings (mode=%s, %ux%u, fps_cap=%u, aspect=%s, vsync=%d, scale=%u%%)",
         kDisplayModeNames[uint32_t(cur.display_mode)], cur.width, cur.height, cur.fps_cap,
         cur.aspect_ratio.c_str(), int(cur.vsync), cur.render_percent);

    app::ApplyDisplay();

    // Start 15-second confirmation countdown
    s_confirming = true;
    s_confirm_selection = 0;  // Default to Keep Changes
    s_confirm_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
}

void ConfirmNewSettings() {
#if !defined(_WIN32) || defined(XBE_VK_TARGET)
    if (app::LastDisplayApply() != app::DisplayApplyResult::Applied) return;
#endif
    LOGI("graphics_menu: new display settings confirmed by user; saving to settings.toml");
    app::Save();
    s_confirming = false;
    SyncPendingWithActive();
}

void RevertSettings() {
    LOGI("graphics_menu: reverting display settings to previous configuration");
    app::Get() = s_revert_settings;
    app::ApplyDisplay();
    s_confirming = false;
    SyncPendingWithActive();
}

void AdjustVideoSlot(int slot, int direction) {
    s_display_apply_failed = false;
    switch (slot) {
        case 4: {  // Resolution
            int num = int(std::size(kPresets));
            if (s_pending_preset < 0) s_pending_preset = 0;
            s_pending_preset = (s_pending_preset + direction + num) % num;
            SetPendingResolution({kPresets[s_pending_preset].width, kPresets[s_pending_preset].height});
            break;
        }
        case 5: {  // Window Mode
            int mode = int(s_pending_mode);
            mode = (mode + direction + 3) % 3;
            s_pending_mode = app::DisplayMode(mode);
            break;
        }
        case 6: {  // Frame Rate Lock
            int num = int(std::size(kFpsPresets));
            s_pending_fps_idx = (s_pending_fps_idx + direction + num) % num;
            break;
        }
        case 7: {  // V-Sync
            s_pending_vsync = !s_pending_vsync;
            break;
        }
        case 8: {  // Aspect Ratio
            int num = int(std::size(kAspectOptions));
            s_pending_aspect_idx = (s_pending_aspect_idx + direction + num) % num;
            if (s_pending_preset >= 0)
                SetPendingResolution({kPresets[s_pending_preset].width, kPresets[s_pending_preset].height});
            break;
        }
        case 9: {  // 3D Render Scale
            s_pending_scale += direction * 10;
            if (s_pending_scale < 50) s_pending_scale = 50;
            if (s_pending_scale > 200) s_pending_scale = 200;
            break;
        }
        case 10:  // Actions are activated by A, not by left/right.
            break;
        default:
            break;
    }
}

void CycleRenderScalePreset() {
    const int kScalePresets[] = {50, 75, 100, 125, 150, 200};
    int next_idx = 0;
    for (int i = 0; i < int(std::size(kScalePresets)); i++) {
        if (s_pending_scale < kScalePresets[i]) {
            next_idx = i;
            break;
        }
    }
    s_pending_scale = kScalePresets[next_idx];
}

void SyncNativeCameraInvertY() {
    auto* base=static_cast<uint8_t*>(kernel::HostPtr(0));
    if(!base)return;
    auto& flag=base[0x0032346E];
    static bool was_camera=false;
    const bool camera=GetCurrentMenuState()==GameMenuState::CameraMenu;
    if(!camera || !was_camera) {
        // Guest initialization writes the console default later than host
        // startup. It must not silently turn a saved modern preference on.
        flag=s_invert_y?1:0;
    } else if((flag!=0)!=s_invert_y) {
        s_invert_y=flag!=0;app::Save();
        LOGI("camera: native Controls preference saved: invert_y=%d",int(s_invert_y));
    }
    was_camera=camera;
}

}  // namespace

uint32_t GraphicsMenu_KeyboardDescriptor() { return s_kbm_descriptor; }
void GraphicsMenu_SetKeyboardDescriptor(uint32_t desc) { s_kbm_descriptor = desc; }
int GraphicsMenu_TakeKeyboardTransition() {
    const auto result = s_kbm_transition;
    s_kbm_transition = 0;
    return result;
}

void GraphicsMenu_Init() {
    SyncPendingWithActive();
    s_initialized = true;
}

bool GraphicsMenu_IsOpen() {
    return s_show_debug_menu || s_confirming;
}

void GraphicsMenu_SetOpen(bool open) {
    s_show_debug_menu = open;
    if (open) {
        SyncPendingWithActive();
    }
}

bool GraphicsMenu_GetInvertY() {
    return s_invert_y;
}

void GraphicsMenu_SetInvertY(bool invert) {
    s_invert_y = invert;
    app::Save();

    uint8_t* base = static_cast<uint8_t*>(kernel::HostPtr(0));
    if (base) {
        *reinterpret_cast<uint8_t*>(base + 0x0032346E) = s_invert_y ? 1 : 0;
    }
}

void GraphicsMenu_ProcessPad(int port, input::Pad& pad) {
    if (!pad.connected || port != 0) return;

    // Always process centralized input system (optional L3 shortcut) first
    InputSystem_ProcessPad(port, pad);

    // At Title Screen, pressing Enter/A triggers START so the game begins
    const auto active_desc = native_menu::Descriptor([](uint32_t a){return kernel::R32(a);});
    bool gameplay = false;
#if defined(__linux__) || defined(XBE_VK_TARGET)
    gameplay = input::DesktopGameplayActive();
    if (!gameplay && native_menu::PdaHasKeyboardShift(active_desc) && input::DesktopMenuShift()) {
        pad.analog[3] = 255;
    }
#endif
    if (!gameplay && active_desc == 0 && pad.analog[0] > 128) {
        pad.buttons |= 0x0010;
    }

    const auto physical_pad = pad;
    const bool block_actions=s_action_release.Block(physical_pad.analog[0]>128,physical_pad.analog[1]>128);

    // Toggle secondary debug overlay via Start + Back combo (only if D-pad is not pressed)
    constexpr uint16_t kToggleCombo = 0x0030;
    bool combo_down = ((pad.buttons & kToggleCombo) == kToggleCombo) && ((pad.buttons & 0x000F) == 0);
    bool prev_down = ((s_prev_pad.buttons & kToggleCombo) == kToggleCombo) && ((s_prev_pad.buttons & 0x000F) == 0);
    if (combo_down && !prev_down) {
        s_show_debug_menu = !s_show_debug_menu;
        if (s_show_debug_menu) {
            SyncPendingWithActive();
        }
    }

    // Synchronize native Controls menu Camera Invert Y flag with settings.toml
    SyncNativeCameraInvertY();

    // Standard Right Stick Camera Y:
    // In SDL3 input (input.cpp:106), pushing stick UP produces positive ry, DOWN produces negative ry.
    // In native exploration (sub_00091000) and aiming (sub_00094000):
    // - Invert Y Axis OFF (flag 0x0032346E == 0, clean install default): guest applies ry directly -> Stick UP tilts camera UP (Modern Standard).
    // - Invert Y Axis ON  (flag 0x0032346E == 1): guest negates ry (0 - ry) -> Stick UP tilts camera DOWN (Classic Inverted).
    // No artificial bridge inversion is needed or desired.

    // Edge detection for D-pad & Left Stick
    bool up = ((pad.buttons & 0x0001) && !(s_prev_pad.buttons & 0x0001)) ||
              (pad.ly > 16000 && s_prev_pad.ly <= 16000);
    bool down = ((pad.buttons & 0x0002) && !(s_prev_pad.buttons & 0x0002)) ||
                (pad.ly < -16000 && s_prev_pad.ly >= -16000);
    bool left = ((pad.buttons & 0x0004) && !(s_prev_pad.buttons & 0x0004)) ||
                (pad.lx < -16000 && s_prev_pad.lx >= -16000);
    bool right = ((pad.buttons & 0x0008) && !(s_prev_pad.buttons & 0x0008)) ||
                 (pad.lx > 16000 && s_prev_pad.lx <= 16000);

    // Edge detection for action buttons
    bool btn_a = !block_actions && (pad.analog[0] > 128 && s_prev_pad.analog[0] <= 128);
    bool btn_b = !block_actions && (pad.analog[1] > 128 && s_prev_pad.analog[1] <= 128);

    // Confirmation Modal Navigation
    if (s_confirming) {
        if (left && s_show_debug_menu) s_confirm_selection = 0;   // Keep Changes
        if (right && s_show_debug_menu) s_confirm_selection = 1;  // Revert Now

        if (btn_a) {
            if (s_confirm_selection == 0) ConfirmNewSettings();
            else RevertSettings();
        } else if (btn_b) {
            RevertSettings();
        }

        if (!s_confirming) s_action_release.Arm();

        // Swallow gameplay inputs during confirmation
        pad.buttons &= kToggleCombo;
        std::memset(pad.analog, 0, sizeof(pad.analog));
        pad.lx = pad.ly = pad.rx = pad.ry = 0;
        s_prev_pad = physical_pad;
        return;
    }

    // Secondary Debug Overlay Navigation
    if (s_show_debug_menu) {
        if (up) s_debug_selection = (s_debug_selection - 1 + 8) % 8;
        if (down) s_debug_selection = (s_debug_selection + 1) % 8;
        if (left) AdjustVideoSlot(s_debug_selection + 4, -1);
        if (right) AdjustVideoSlot(s_debug_selection + 4, 1);
        if (btn_a) {
            if (s_debug_selection == 6 && HasPendingChanges()) ApplyPendingSettings();
            else if (s_debug_selection == 5) CycleRenderScalePreset();
            else AdjustVideoSlot(s_debug_selection + 4, 1);
        }
        if (btn_b) s_show_debug_menu = false;

        pad.buttons &= kToggleCombo;
        std::memset(pad.analog, 0, sizeof(pad.analog));
        pad.lx = pad.ly = pad.rx = pad.ry = 0;
        s_prev_pad = physical_pad;
        return;
    }

    // Native widgets own the audio and controller settings. Expanded text
    // lists use logical rows and position the native highlight on the UI thread.
    GameMenuState state = GetCurrentMenuState();
    // The title controller can stay on the guest stack during a mission.
    if (gameplay && state == GameMenuState::MainMenu) state = GameMenuState::None;
#if defined(__linux__) || defined(XBE_VK_TARGET)
    if (state != GameMenuState::KeyboardMouseMenu && s_binding_capture) {
        input::EndDesktopBindingCapture();
        s_binding_capture = false;
    }
#endif
    if (state != GameMenuState::OptionsMenu) s_was_in_options_menu = false;
    if (state != GameMenuState::MainMenu) s_main_menu_row = -1;
    if (state == GameMenuState::MainMenu) {
        if (s_main_menu_row < 0) {
            s_main_menu_row = std::max(0, native_menu::SelectedSlot(
                [](uint32_t a){return kernel::R32(a);},
                [](uint32_t a){return *static_cast<const uint8_t*>(kernel::HostPtr(a));}));
        }
        if (up || down) {
            constexpr int count = native_menu::ExitGameRow + 1;
            s_main_menu_row = (s_main_menu_row + (up ? -1 : 1) + count) % count;
            s_native_select = std::min(s_main_menu_row, native_menu::ExitGameRow - 1);
        }
        pad.buttons &= ~0xF;
        pad.lx = pad.ly = 0;
        if (s_main_menu_row == native_menu::ExitGameRow) {
            pad.analog[0] = 0;
            if (btn_a) app::RequestQuit();
        }
    } else if (state == GameMenuState::AudioVideoMenu) {
        if (!s_was_in_av_menu) {
            s_was_in_av_menu = true; s_av_page = 0; s_av_nav_index = 0;
            SyncPendingWithActive();
        }
        pad.buttons &= ~0xF;
        pad.lx = pad.ly = 0;
        if (s_av_page == 0) {
            const int slot = native_menu::SelectedSlot(
                [](uint32_t a){return kernel::R32(a);},
                [](uint32_t a){return *static_cast<const uint8_t*>(kernel::HostPtr(a));});
            if (slot >= 0) s_av_nav_index = slot;
            if ((down && s_av_nav_index == 3) || (up && s_av_nav_index == 0)) {
                s_av_page = 1;
                s_av_nav_index = kDisplayStart + (up ? kDisplayRows - 1 : 0);
                s_native_select = 0;
                pad.analog[0] = 0;
            } else {
                if (up) pad.buttons |= 1;
                if (down) pad.buttons |= 2;
                if (left) pad.buttons |= 4;
                if (right) pad.buttons |= 8;
            }
            if (btn_b) SyncPendingWithActive();
        } else {
            pad.analog[0] = 0;
            if (btn_b) {
                // The display page's Back action returns to the native audio rows.
                s_av_page = 0;
                s_av_nav_index = s_native_select = 3;
                pad.analog[1] = 0;
                s_action_release.Arm();
            } else if (up || down) {
                const int row = s_av_nav_index - kDisplayStart;
                if ((up && row == 0) || (down && row == kDisplayRows - 1)) {
                    s_av_page = 0;
                    s_av_nav_index = s_native_select = up ? 3 : 0;
                } else {
                    s_av_nav_index += up ? -1 : 1;
                    s_native_select = 0;
                }
            } else {
                if (left) AdjustVideoSlot(s_av_nav_index,-1);
                if (right) AdjustVideoSlot(s_av_nav_index,1);
                if (btn_a && HasPendingChanges()) ApplyPendingSettings();
            }
        }
    } else if (state == GameMenuState::ControlsMenu) {
        if (!s_was_in_controls_menu) {
            s_was_in_controls_menu = true;
            s_controls_page = 0;
            s_controls_nav_index = 0;
        }
        pad.buttons &= ~0xF;
        pad.lx = pad.ly = 0;
        if (s_controls_page == 0) {
            const int slot = native_menu::SelectedSlot(
                [](uint32_t a){return kernel::R32(a);},
                [](uint32_t a){return *static_cast<const uint8_t*>(kernel::HostPtr(a));});
            if (slot >= 0) s_controls_nav_index = slot;
            if (!btn_b && ((btn_a && s_controls_nav_index == 3) ||
                           (down && s_controls_nav_index == 3) || (up && s_controls_nav_index == 0))) {
                s_controls_page = 1;
                s_controls_nav_index = up ? kControllerReferenceRows - 1 : 0;
                s_native_select = 0;
                pad.analog[0] = 0;
                if (btn_a) s_action_release.Arm();
            } else {
                if (up) pad.buttons |= 1;
                if (down) pad.buttons |= 2;
                if (s_controls_nav_index == 1 || s_controls_nav_index == 2) {
                    if (left) pad.buttons |= 4;
                    if (right) pad.buttons |= 8;
                }
                if (s_controls_nav_index == 0) {
                    if (left || right || btn_a) InputSystem_SetL3Crouch(!InputSystem_GetL3Crouch());
                    pad.analog[0] = 0;
                }
            }
        } else {
            pad.analog[0] = pad.analog[1] = 0;
            if (btn_a || btn_b || left || right) {
                s_controls_page = 0;
                s_controls_nav_index = s_native_select = 3;
                if (btn_a || btn_b) s_action_release.Arm();
            } else if (up || down) {
                s_controls_nav_index = MoveMenuRow(s_controls_nav_index, kControllerReferenceRows, up);
                s_native_select = 0;
            }
        }
    } else if (state == GameMenuState::KeyboardMouseMenu) {
        if (!s_was_in_kbm_menu) {
            s_was_in_kbm_menu = true;
            s_kbm_page = 0;
            s_kbm_nav_index = 0;
        }
        pad.buttons &= ~0xF;
        pad.lx = pad.ly = 0;
        pad.analog[0] = pad.analog[1] = 0;

#if defined(__linux__) || defined(XBE_VK_TARGET)
        if (s_kbm_page >= kFirstBindingPage) {
            if (s_binding_capture) {
                const int binding = input::TakeDesktopBindingCapture();
                if (btn_b || binding == input::DesktopBindingCancelled) {
                    input::EndDesktopBindingCapture();
                    s_binding_capture = false;
                } else if (binding) {
                    const int action = BindingPageStart() + s_kbm_nav_index;
                    const auto result = input::AssignDesktopBinding(static_cast<input::DesktopAction>(action), binding);
                    if (result == input::BindingResult::Applied) {
                        input::EndDesktopBindingCapture();
                        s_binding_capture = false;
                        app::Save();
                    } else {
                        s_binding_prompt = result == input::BindingResult::InUse
                            ? "Key in use. Try another" : "Reserved. Try another";
                    }
                }
            } else if (btn_b) {
                s_kbm_page = 0;
                s_kbm_nav_index = s_native_select = 2;
            } else if (left || right) {
                const int page = s_kbm_page - kFirstBindingPage;
                s_kbm_page = kFirstBindingPage + (page + (left ? -1 : 1) + kBindingPages) % kBindingPages;
                s_kbm_nav_index = s_native_select = 0;
            } else if (up || down) {
                s_kbm_nav_index = MoveMenuRow(s_kbm_nav_index, BindingPageRows(), up);
                s_native_select = 0;
            } else if (btn_a) {
                const int action = BindingPageStart() + s_kbm_nav_index;
                if (action < kBindingCount) {
                    input::BeginDesktopBindingCapture();
                    s_binding_capture = true;
                    s_binding_prompt = "Press a key...";
                } else if (action == kBindingCount) {
                    input::ResetDesktopBindings();
                    app::Save();
                } else {
                    s_kbm_page = 1;
                    s_kbm_nav_index = s_native_select = 0;
                }
            }
        } else
#endif
        if (s_kbm_page == 0) {
            if (up || down) {
                s_kbm_nav_index = (s_kbm_nav_index + (up ? 2 : 1)) % 3;
                s_native_select = s_kbm_nav_index;
            }

            if (s_kbm_nav_index == 0) {
                // Mouse Sensitivity: adjust by 5% on Left/Right
                if (left) {
                    if (input::mouseSensitivity > 10) {
                        input::mouseSensitivity = (input::mouseSensitivity <= 15) ? 10 : input::mouseSensitivity - 5;
                        app::Save();
                        LOGI("kbm_menu: mouse_sensitivity decreased to %u%%", input::mouseSensitivity);
                    }
                }
                if (right) {
                    if (input::mouseSensitivity < 400) {
                        input::mouseSensitivity = std::min(400u, input::mouseSensitivity + 5);
                        app::Save();
                        LOGI("kbm_menu: mouse_sensitivity increased to %u%%", input::mouseSensitivity);
                    }
                }
            } else if (s_kbm_nav_index == 1) {
                // Invert Mouse Y
                if (left || right || btn_a) {
                    input::invertMouseY = !input::invertMouseY;
                    app::Save();
                    LOGI("kbm_menu: invert_mouse_y toggled to %s", input::invertMouseY ? "On" : "Off");
                }
            } else if (s_kbm_nav_index == 2) {
                // View Controls
                if (btn_a) {
#if defined(__linux__) || defined(XBE_VK_TARGET)
                    s_kbm_page = kFirstBindingPage;
#else
                    s_kbm_page = 1;
#endif
                    s_kbm_nav_index = s_native_select = 0;
                    s_action_release.Arm();
                }
            }

            if (btn_b) {
                s_kbm_page = 0;
                s_kbm_transition = -1;
                s_action_release.Arm();
            }
        } else {
            if (left || right || btn_a || btn_b) {
#if defined(__linux__) || defined(XBE_VK_TARGET)
                s_kbm_page = kFirstBindingPage + kBindingPages - 1;
                s_kbm_nav_index = BindingPageRows() - 1;
                s_native_select = 0;
#else
                s_kbm_page = 0;
                s_kbm_nav_index = s_native_select = 2;
#endif
                if (btn_a || btn_b) s_action_release.Arm();
            } else if (up || down) {
                s_kbm_nav_index = MoveMenuRow(s_kbm_nav_index, kKeyboardReferenceRows, up);
                s_native_select = 0;
            }
        }
    } else if (state == GameMenuState::OptionsMenu) {
        const auto desc = native_menu::Descriptor([](uint32_t a){return kernel::R32(a);});
        const bool is_pause = (desc == 0x00332844);
        const int max_visible = native_menu::OptionsOriginalCount(is_pause);
        const int total_items = max_visible + 1;

        if (!s_was_in_options_menu) {
            s_was_in_options_menu = true;
            const auto controller = native_menu::Controller([](uint32_t a){return kernel::R32(a);});
            if (controller != s_options_controller) s_options_nav_index = 0;
            s_options_controller = controller;
            if (s_options_nav_index < 0 || s_options_nav_index >= total_items) {
                s_options_nav_index = 0;
            }
            s_native_select = native_menu::OptionsWidgetRow(s_options_nav_index);
        }

        if (up || down) {
            s_options_nav_index = (s_options_nav_index + (up ? -1 : 1) + total_items) % total_items;
            s_native_select = native_menu::OptionsWidgetRow(s_options_nav_index);
        }
        // The native menu must not also process held directions on subsequent polls.
        pad.buttons &= ~0xF;
        pad.lx = pad.ly = 0;

        if (btn_b) s_options_controller = 0;
        if (btn_a && s_options_nav_index == native_menu::OptionsKeyboardRow) {
            s_kbm_transition = 1;
            pad.analog[0] = 0;
            s_action_release.Arm();
            s_was_in_kbm_menu = false;
        }
    } else {
        s_was_in_av_menu = false; s_av_page = 0; s_av_nav_index = 0;
        s_was_in_controls_menu = false; s_controls_page = 0; s_controls_nav_index = 0;
        s_was_in_kbm_menu = false; s_kbm_page = 0; s_kbm_nav_index = 0;
        s_was_in_options_menu = false;
    }

    if(block_actions) pad.analog[0]=pad.analog[1]=0;
    s_prev_pad = physical_pad;
}

void GraphicsMenu_RenderUi(double fps) {
    if (!s_initialized) {
        GraphicsMenu_Init();
    }

#if !defined(_WIN32) || defined(XBE_VK_TARGET)
    if (s_confirming && app::LastDisplayApply() == app::DisplayApplyResult::Failed) {
        RevertSettings();
        s_display_apply_failed = true;
    }
#endif

    // Automated test hook for live resolution switching & confirmation validation
    static bool s_test_triggered = false;
    static auto s_launch_time = std::chrono::steady_clock::now();
    if (!s_test_triggered) {
        const char* test_target = std::getenv("BULLETPROOF_TEST_SWITCH_TO");
        if (test_target) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_launch_time).count();
            if (el > 66.0) {  // reached 3D gameplay
                s_test_triggered = true;
                if (std::strcmp(test_target, "1080p") == 0) {
                    s_pending_preset = 2;
                    SetPendingResolution({1920, 1080});
                    ApplyPendingSettings();
                } else if (std::strcmp(test_target, "720p") == 0) {
                    s_pending_preset = 1;
                    SetPendingResolution({1280, 720});
                    ApplyPendingSettings();
                } else if (std::strcmp(test_target, "1440p") == 0) {
                    s_pending_preset = 3;
                    SetPendingResolution({2560, 1440});
                    ApplyPendingSettings();
                } else if (std::strcmp(test_target, "4k") == 0) {
                    s_pending_preset = 4;
                    SetPendingResolution({3840, 2160});
                    ApplyPendingSettings();
                }
            }
        }
    }

    // Secondary debug overlay hotkey toggles: F1, F2, F10
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false) ||
        ImGui::IsKeyPressed(ImGuiKey_F2, false) ||
        ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
        s_show_debug_menu = !s_show_debug_menu;
        if (s_show_debug_menu) {
            SyncPendingWithActive();
        }
    }

    if (s_show_debug_menu && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !s_confirming) {
        s_show_debug_menu = false;
    }

    // Confirmation Modal Window (15-second auto-revert countdown)
    if (s_confirming) {
        auto now = std::chrono::steady_clock::now();
        int remaining_sec = int(std::chrono::duration_cast<std::chrono::seconds>(s_confirm_deadline - now).count());
        if (remaining_sec < 0) remaining_sec = 0;

        if (now >= s_confirm_deadline) {
            RevertSettings();
        } else if (s_show_debug_menu) {
            ImGui::SetNextWindowSize(ImVec2(480, 230), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("Confirm Display Settings", nullptr, flags)) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Display settings have been updated!");
                ImGui::Spacing();
                ImGui::Text("Do you want to keep these changes?");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "Reverting automatically in %d seconds...", remaining_sec);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Keep Changes button (Selection 0)
                bool sel_keep = (s_confirm_selection == 0);
                if (sel_keep) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.70f, 0.35f, 1.0f));
                if (ImGui::Button("(A) Keep Changes", ImVec2(210, 40)) || (sel_keep && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                    ConfirmNewSettings();
                }
                if (sel_keep) ImGui::PopStyleColor();

                ImGui::SameLine();

                // Revert Now button (Selection 1)
                bool sel_revert = (s_confirm_selection == 1);
                if (sel_revert) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));
                if (ImGui::Button("(B) Revert Now", ImVec2(180, 40)) || (sel_revert && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                    RevertSettings();
                }
                if (sel_revert) ImGui::PopStyleColor();

                ImGui::End();
            }
        }
    }



    // Secondary Debug Overlay Window (F1/F2/F10 or Start + Back only)
    if (s_show_debug_menu) {
        ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f),
                                ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin("50 Cent: Bulletproof – PC Port - Debug Graphics Overlay", &s_show_debug_menu, win_flags)) {
            const auto& cur = app::Get();
            app::Mode out = app::Output();
            app::Mode desk = app::Desktop();
            float scale = app::RenderScale(480);
            uint32_t target_w = uint32_t(std::lround(640 * scale));
            uint32_t target_h = uint32_t(std::lround(480 * scale));

            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Performance: %.1f FPS", fps);
            ImGui::SameLine(320);
            ImGui::Text("Desktop: %ux%u", desk.width, desk.height);

            ImGui::Text("Output Resolution: %ux%u", out.width, out.height);
            ImGui::SameLine(320);
            ImGui::Text("3D Render Target: %ux%u", target_w, target_h);

            ImGui::Text("Aspect: %s (%s)", cur.aspect_ratio.c_str(), app::Widescreen() ? "Widescreen 16:9" : "Standard 4:3");
            ImGui::SameLine(320);
            ImGui::Text("VSync: %s", cur.vsync ? "Enabled" : "Disabled");

            ImGui::Separator();
            ImGui::Spacing();

            auto DrawMenuRow = [&](int item, const char* label, const char* value_text, const char* extra = nullptr) {
                bool selected = (item == s_debug_selection);
                ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0, 32));
                ImGui::SameLine(20);

                ImGui::TextColored(selected ? ImVec4(1.0f, 0.92f, 0.4f, 1.0f) : ImVec4(0.85f, 0.88f, 0.92f, 1.0f),
                                   "%s%s:", selected ? "> " : "  ", label);
                ImGui::SameLine(240);
                ImGui::TextColored(selected ? ImVec4(0.4f, 0.95f, 1.0f, 1.0f) : ImVec4(0.7f, 0.85f, 0.95f, 1.0f),
                                   "< %s >", value_text);
                if (extra) {
                    ImGui::SameLine(420);
                    ImGui::TextDisabled("%s", extra);
                }
            };

            const auto current_res_preview = PendingResolutionLabel();
            DrawMenuRow(0, "1. Resolution", current_res_preview.c_str());

            int mode_idx = int(s_pending_mode);
            const char* current_mode_preview = (mode_idx >= 0 && mode_idx < 3) ? kDisplayModeNames[mode_idx] : "Unknown";
            DrawMenuRow(1, "2. Display Mode", current_mode_preview);

            DrawMenuRow(2, "3. Frame Rate Lock", kFpsPresets[s_pending_fps_idx].label);

            DrawMenuRow(3, "4. Vertical Sync", s_pending_vsync ? "Enabled" : "Disabled");

            const char* aspect_extra = (s_pending_aspect_idx == 1) ? "(4:3 Pillarboxed)" :
                                       ((s_pending_aspect_idx == 2) ? "(16:9 Widescreen)" : "(Follows monitor)");
            DrawMenuRow(4, "5. Aspect Ratio", kAspectOptions[s_pending_aspect_idx], aspect_extra);

            char scale_buf[32];
            std::snprintf(scale_buf, sizeof(scale_buf), "%d%%", s_pending_scale);
            DrawMenuRow(5, "6. 3D Render Scale", scale_buf, "(50% - 200%)");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool has_changes = HasPendingChanges();
            if (has_changes) {
                if (ImGui::Button("(A) Apply Changes", ImVec2(180, 40))) {
                    ApplyPendingSettings();
                }
                ImGui::SameLine();
                if (ImGui::Button("Discard", ImVec2(100, 40))) {
                    SyncPendingWithActive();
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("No Changes", ImVec2(180, 40));
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (ImGui::Button("(B) Close Overlay", ImVec2(160, 40))) {
                s_show_debug_menu = false;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                               "Gamepad: (D-Pad) Navigate/Adjust | (A) Select/Apply | (B) Close");
            ImGui::TextDisabled("Toggle Hotkeys: F1, F2, F10 | Controller Combo: Start + Back");
        }
        ImGui::End();
    }
}

int GraphicsMenu_TakeNativeSelection() {
    const int result=s_native_select; s_native_select=-1; return result;
}
bool GraphicsMenu_NativeText(int role, bool value, std::string& text) {
    if (GetCurrentMenuState()!=GameMenuState::AudioVideoMenu) return false;
    if (role==4) {
        text=s_confirming ? "Confirm Display" : "Audio/Video ("+std::to_string(s_av_page+1)+"/2)";
        return true;
    }
    if (role==6 && s_av_page && !s_confirming) { text="Back to Audio"; return true; }
    if (role==5) {
        if (s_confirming) { text="Keep Changes"; return true; }
        if (s_av_page==0) return false;
        text="Apply";
        return true;
    }
    if (role<0 || role>3 || (s_av_page==0 && !s_confirming)) return false;
    if (s_confirming) {
        int seconds=std::max(0,int(std::chrono::duration_cast<std::chrono::seconds>(s_confirm_deadline-std::chrono::steady_clock::now()).count())+1);
        const auto& cur=app::Get();
        const bool high_fps = (cur.fps_cap > 60);
        if (high_fps) {
            const std::string labels[]={
                "Frame Rate",
                "Higher frame rates may affect gameplay.",
                "Keep Changes (A)",
                "Revert (B)"
            };
            const std::string values[]={
                std::to_string(cur.fps_cap) + " FPS",
                "",
                std::to_string(seconds) + " sec",
                ""
            };
            text=value?values[role]:labels[role];
            return true;
        } else {
            const std::string labels[]={"Resolution","Window Mode","Keep Changes (A)","Revert (B)"};
            const std::string values[]={
                (cur.width>0 && cur.height>0?std::to_string(cur.width)+"x"+std::to_string(cur.height):"Auto"),
                kDisplayModeNames[int(cur.display_mode)],
                std::to_string(seconds)+" sec",
                ""
            };
            text=value?values[role]:labels[role];
            return true;
        }
    }
    return false;
}

bool ControlsMenu_NativeText(int role, bool is_value, std::string& text) {
    if (GetCurrentMenuState() != GameMenuState::ControlsMenu) return false;
    switch (role) {
    case 10: text = s_controls_page ? "View Controls (2/2)" : "Controller (1/2)"; return true;
    case 11:
        text = s_controls_page ? "Done" : s_controls_nav_index == 3 ? "View"
                                     : s_controls_nav_index == 0 ? "Change" : "Toggle";
        return true;
    case 12: text = s_controls_page ? "Back to Controller" : "Back"; return true;
    case 4: text.clear(); return true;
    case 0: text = is_value ? (InputSystem_GetL3Crouch() ? "On" : "Off") : "L3 Crouch"; return true;
    case 3: text = is_value ? "(A) View" : "View Controls"; return true;
    default: return false; // Native Vibration and Crouch Mode labels and values.
    }
}

bool MainMenu_ExitSelected() {
    return s_main_menu_row == native_menu::ExitGameRow;
}

bool OptionsMenu_ShiftHighlight() {
    return s_options_nav_index > native_menu::OptionsKeyboardRow;
}

bool KeyboardMouseMenu_NativeText(int role, bool is_value, std::string& text) {
    if (GetCurrentMenuState() != GameMenuState::KeyboardMouseMenu) return false;

#if defined(__linux__) || defined(XBE_VK_TARGET)
    if (s_kbm_page >= kFirstBindingPage) {
        const int page = s_kbm_page - kFirstBindingPage;
        if (role == 10) {
            text = s_binding_capture ? s_binding_prompt
                : "Bindings (" + std::to_string(page + 1) + "/" + std::to_string(kBindingPages) + ")";
        } else if (role == 11) {
            text = s_binding_capture ? "" : "Edit / Left-Right Pages";
        } else if (role == 12) {
            text = s_binding_capture ? "Cancel" : "Back";
        } else return false;
        return true;
    }
#endif

    // Role 10: Menu Title
    if (role == 10) {
        if (s_kbm_page == 0) text = "Keyboard & Mouse";
        else text = "Other Controls";
        return true;
    }

    // Role 11: Button Hint (A)
    if (role == 11) {
        if (s_kbm_page == 0) {
            if (s_kbm_nav_index == 0) text = "Adjust";
            else if (s_kbm_nav_index == 1) text = "Toggle";
            else text = "View";
        } else {
            text = "Done";
        }
        return true;
    }

    // Role 12: Button Hint (B)
    if (role == 12) {
        if (s_kbm_page == 0) text = "Back";
        else text = "Back to Menu";
        return true;
    }

    // Roles 0..2
    if (role < 0 || role > 2) return false;

    if (s_kbm_page == 0) {
        if (role == 0) {
            // Mouse Sensitivity: 10% to 400%
            if (!is_value) {
                text = "Mouse Sensitivity";
            } else {
                text = std::to_string(input::mouseSensitivity) + "%";
            }
            return true;
        } else if (role == 1) {
            // Invert Mouse Y: Off / On
            if (!is_value) {
                text = "Invert Mouse Y";
            } else {
                text = input::invertMouseY ? "On" : "Off";
            }
            return true;
        } else if (role == 2) {
            // View Controls
            if (!is_value) {
#if defined(__linux__) || defined(XBE_VK_TARGET)
                text = "View / Edit Controls";
#else
                text = "View Controls";
#endif
            } else {
                text = "(A) View";
            }
            return true;
        }
    }
    return false;
}

GraphicsMenuRows GraphicsMenu_CompactRows() {
    switch (GetCurrentMenuState()) {
    case GameMenuState::AudioVideoMenu:
        if (s_av_page && !s_confirming) return {kDisplayRows, s_av_nav_index - kDisplayStart, 1};
        break;
    case GameMenuState::ControlsMenu:
        if (s_controls_page) return {kControllerReferenceRows, s_controls_nav_index, 1};
        break;
    case GameMenuState::KeyboardMouseMenu:
#if defined(__linux__) || defined(XBE_VK_TARGET)
        if (s_kbm_page >= kFirstBindingPage) return {BindingPageRows(), s_kbm_nav_index, 2};
#endif
        if (s_kbm_page == 1) return {kKeyboardReferenceRows, s_kbm_nav_index, 2};
        break;
    default: break;
    }
    return {};
}

std::string GraphicsMenu_CompactRowText(int row, bool value) {
    switch (GetCurrentMenuState()) {
    case GameMenuState::AudioVideoMenu: {
        const char* labels[] = {"Resolution", "Window Mode", "Frame Rate Lock", "V-Sync",
                                "Aspect Ratio", "Render Scale", "Apply Changes"};
        const std::string values[] = {
            PendingResolutionLabel(),
            kDisplayModeNames[int(s_pending_mode)], kFpsPresets[s_pending_fps_idx].label,
            s_pending_vsync ? "On" : "Off", kAspectOptions[s_pending_aspect_idx],
            std::to_string(s_pending_scale)+"%", s_display_apply_failed ? "Mode unavailable" : HasPendingChanges() ? "Ready" : "--"
        };
        return value ? values[row] : labels[row];
    }
    case GameMenuState::ControlsMenu: {
        const char* labels[] = {"Move / Camera", "Crouch / Fire", "Reload / Cover", "Jump / Evade",
                                "Hostage / Grab", "Weapon Inventory", "Dual Wield / Counter-Kill"};
        const char* values[] = {"LS / RS", InputSystem_GetL3Crouch() ? "LT (L3) / RT" : "LT / RT",
                                "X", "A / Y", "B", "Black (RB)", "Y / X"};
        return value ? values[row] : labels[row];
    }
    case GameMenuState::KeyboardMouseMenu:
#if defined(__linux__) || defined(XBE_VK_TARGET)
        if (s_kbm_page >= kFirstBindingPage) {
            const int action = BindingPageStart() + row;
            if (action == kBindingCount) return value ? "" : "Reset to Defaults";
            if (action == kBindingCount + 1) return value ? "" : "View Other Controls";
            if (value) return input::DesktopBindingName(static_cast<input::DesktopAction>(action));
            return kBindingActions[action];
        } else {
            const char* labels[] = {"Context Action", "Look", "Pause", "Weapon Inventory", "Cycle Weapons", "Release to Confirm"};
            const std::string values[] = {"E", "Mouse", "Esc", input::DesktopBindingName(input::DesktopAction::Inventory), "Mouse Wheel", ""};
            return value ? values[row] : labels[row];
        }
#else
        {
            const char* labels[] = {"Move / Look", "Fire / Aim", "Reload / Cover", "Jump / Crouch", "Action / Grab", "Weapons / Pause"};
            const char* values[] = {"WASD / Mouse", "LMB / RMB", "R / Space", "Space / C", "E / F", "1-4, Wheel / Esc"};
            return value ? values[row] : labels[row];
        }
#endif
    default: return {};
    }
}
