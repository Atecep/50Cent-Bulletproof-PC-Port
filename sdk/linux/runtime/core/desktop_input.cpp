#include "core/desktop_input.h"
#include "core/log.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace input {
uint32_t mouseSensitivity = 100;
bool invertMouseY = false;

namespace {
using Clock = std::chrono::steady_clock;
std::mutex lock;
std::array<bool, SDL_SCANCODE_COUNT> keys{};
std::array<bool, SDL_SCANCODE_COUNT> pressed{};
constexpr DesktopBindings defaults{SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A,
    SDL_SCANCODE_D, -SDL_BUTTON_LEFT, SDL_SCANCODE_SPACE, SDL_SCANCODE_R,
    SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_F, SDL_SCANCODE_Q, SDL_SCANCODE_TAB};
DesktopBindings bindings = defaults;
std::array<bool, SDL_BUTTON_X2 + 1> mouseDown{};
std::array<bool, SDL_BUTTON_X2 + 1> mousePressed{};
bool bindingCapture = false;
int capturedBinding = 0;

bool CompatibleBinding(int binding) {
    if (binding < 0) return binding >= -SDL_BUTTON_X2;
    if (binding == 0 || binding >= SDL_SCANCODE_COUNT) return false;
    switch (binding) {
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_E: return false;
    default: return true;
    }
}
Clock::time_point lastCamera{};
bool focused = false;
bool gameplay = false;
bool keyboard = true;
bool keyboardPrompts = true;
bool menuShift = false;
bool captured = false;
bool weaponSelector = false;
bool inventoryHeld = false;
float wheelRemainder = 0;
uint64_t nextWheelId = 0;
std::deque<WeaponWheelStep> weaponWheel;
float pendingX = 0, pendingY = 0;

void ClearWeaponInput() {
    inventoryHeld = false;
    wheelRemainder = 0;
    weaponWheel.clear();
}

void UpdateInventoryHold(int binding, bool down, bool wasDown) {
    if (binding != bindings[static_cast<unsigned>(DesktopAction::Inventory)]) return;
    if (!down) ClearWeaponInput();
    else if (!wasDown && gameplay) inventoryHeld = true;
}

void ClearButtons() {
    menuShift = false;
    keys.fill(false);
    pressed.fill(false);
    mouseDown.fill(false);
    mousePressed.fill(false);
}

void ClearMotion() {
    pendingX = pendingY = 0;
}
}

void DesktopEvent(const SDL_Event& e) {
    std::lock_guard guard(lock);
    switch (e.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused = false;
        if (bindingCapture) capturedBinding = DesktopBindingCancelled;
        ClearWeaponInput();
        ClearButtons();
        ClearMotion();
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!focused || e.key.repeat || e.key.scancode >= keys.size()) break;
        if (e.key.down) keyboardPrompts = true;
        if (bindingCapture && e.key.down) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) capturedBinding = DesktopBindingCancelled;
            else if (!capturedBinding) capturedBinding = e.key.scancode;
            break;
        }
        if (e.key.down && e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) break;
        UpdateInventoryHold(e.key.scancode, e.key.down, keys[e.key.scancode]);
        if (e.key.down && !keys[e.key.scancode]) pressed[e.key.scancode] = true;
        keys[e.key.scancode] = e.key.down;
        if (e.key.down) keyboard = true;
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        if (!focused || !keyboard || !inventoryHeld || !weaponSelector || !std::isfinite(e.wheel.y)) break;
        const float amount = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        constexpr int maxQueuedSteps = 64;
        wheelRemainder += std::clamp(amount, -float(maxQueuedSteps), float(maxQueuedSteps));
        const int ticks = int(wheelRemainder);
        wheelRemainder -= float(ticks);
        for (int i = 0; i < std::abs(ticks) && weaponWheel.size() < maxQueuedSteps; ++i) {
            weaponWheel.push_back({ticks > 0 ? -1 : 1, ++nextWheelId});
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (focused && e.button.button > 0 && e.button.button < mouseDown.size()) {
            const auto button = e.button.button;
            if (e.button.down && e.button.which != SDL_TOUCH_MOUSEID) keyboardPrompts = true;
            if (bindingCapture && e.button.down) {
                if (!capturedBinding) capturedBinding = -int(button);
                break;
            }
            UpdateInventoryHold(-int(button), e.button.down, mouseDown[button]);
            if (captured && (button == SDL_BUTTON_LEFT ||
                std::find(bindings.begin(), bindings.end(), -int(button)) != bindings.end())) {
                keyboard = true;
                if (e.button.down && !mouseDown[button]) mousePressed[button] = true;
                mouseDown[button] = e.button.down;
            }
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (focused && captured && gameplay) {
            keyboard = true;
            // Only a deliberate relative movement takes over the captions.
            if (e.motion.which != SDL_TOUCH_MOUSEID &&
                std::abs(e.motion.xrel) + std::abs(e.motion.yrel) >= 4.f)
                keyboardPrompts = true;
            pendingX += e.motion.xrel;
            pendingY += e.motion.yrel;
        }
        break;
    default:
        break;
    }
}

void DesktopTick(SDL_Window* window) {
    if (!window) return;
    std::lock_guard guard(lock);
    const bool active = Clock::now() - lastCamera < std::chrono::milliseconds(110);
    if (gameplay != active) {
        gameplay = active;
        if (!active && !weaponSelector) ClearWeaponInput();
        ClearButtons();
        ClearMotion();
    }
    focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!focused) ClearWeaponInput();
    const bool want = focused && (gameplay || (weaponSelector && inventoryHeld));
    if (want != captured && SDL_SetWindowRelativeMouseMode(window, want)) {
        captured = want;
        LOGR("desktop input: relative mouse %s", captured ? "captured" : "released");
        mouseDown.fill(false);
        mousePressed.fill(false);
        ClearMotion();
    }
}

void DesktopCamera() {
    std::lock_guard guard(lock);
    lastCamera = Clock::now();
    if (pendingX != 0 || pendingY != 0) {
        keyboard = true;
        LOGR("desktop input: relative delta %.2f %.2f", pendingX, pendingY);
    }
}

bool IsMouseActive() {
    std::lock_guard guard(lock);
    return focused && captured && keyboard;
}

bool ConsumeMouseDelta(float& dx, float& dy) {
    std::lock_guard guard(lock);
    if (!focused || !captured || !keyboard) {
        dx = dy = 0.0f;
        return false;
    }
    dx = pendingX;
    dy = pendingY;
    pendingX = 0.0f;
    pendingY = 0.0f;
    return true;
}

float GetMouseSensitivityFactor() {
    // 1:1 X/Y effective angular rotation per unit of relative mouse displacement.
    // Base sensitivity 0.000625f radians (~0.03581 deg) per mouse count at 100%.
    // Configurable via mouse_sensitivity in settings.toml (10% to 400%).
    const uint32_t pct = std::clamp(mouseSensitivity, 10u, 400u);
    constexpr float kBaseRadiansPerCount = 0.000625f;
    return kBaseRadiansPerCount * (static_cast<float>(pct) / 100.0f);
}

void SetMouseCapturedForTest(bool c) {
    std::lock_guard guard(lock);
    captured = c;
    gameplay = c;
}

bool DesktopLook(int axis, float& value) {
    std::lock_guard guard(lock);
    if (!focused || !captured || !keyboard) return false;
    (void)axis;
    value = 0.0f;
    return true;
}

void PreferController() {
    std::lock_guard guard(lock);
    keyboardPrompts = false;
    if (!keyboard) return;
    keyboard = false;
    ClearWeaponInput();
    ClearButtons();
    ClearMotion();
}

bool DesktopUsesKeyboardPrompts() {
    std::lock_guard guard(lock);
    return keyboardPrompts;
}

void DesktopWeaponSelector(bool active) {
    std::lock_guard guard(lock);
    if (!active && weaponSelector) ClearWeaponInput();
    weaponSelector = active;
}

WeaponWheelStep PeekWeaponWheelStep() {
    std::lock_guard guard(lock);
    if (!weaponSelector || weaponWheel.empty()) return {};
    return weaponWheel.front();
}

void CompleteWeaponWheelStep(uint64_t id) {
    std::lock_guard guard(lock);
    if (!weaponWheel.empty() && weaponWheel.front().id == id) weaponWheel.pop_front();
}

DesktopBindings GetDesktopBindings() {
    std::lock_guard guard(lock);
    return bindings;
}

void LoadDesktopBindings(const DesktopBindings& candidate) {
    std::lock_guard guard(lock);
    ClearWeaponInput();
    bindings = defaults;
    for (auto it = candidate.begin(); it != candidate.end(); ++it) {
        if (!CompatibleBinding(*it) || std::find(candidate.begin(), it, *it) != it) return;
    }
    bindings = candidate;
}

void ResetDesktopBindings() { LoadDesktopBindings(defaults); }

BindingResult AssignDesktopBinding(DesktopAction action, int binding) {
    std::lock_guard guard(lock);
    if (!CompatibleBinding(binding)) return BindingResult::Reserved;
    auto& current = bindings[static_cast<unsigned>(action)];
    if (binding != current && std::find(bindings.begin(), bindings.end(), binding) != bindings.end()) {
        return BindingResult::InUse;
    }
    if (action == DesktopAction::Inventory && current != binding) ClearWeaponInput();
    current = binding;
    return BindingResult::Applied;
}

std::string DesktopBindingName(DesktopAction action) {
    const int binding = GetDesktopBindings()[static_cast<unsigned>(action)];
    if (binding < 0) {
        constexpr const char* mouseNames[] = {"", "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2"};
        return mouseNames[-binding];
    }
    return SDL_GetScancodeName(static_cast<SDL_Scancode>(binding));
}

void BeginDesktopBindingCapture() {
    std::lock_guard guard(lock);
    ClearButtons();
    capturedBinding = 0;
    bindingCapture = true;
}

void EndDesktopBindingCapture() {
    std::lock_guard guard(lock);
    ClearButtons();
    capturedBinding = 0;
    bindingCapture = false;
}

int TakeDesktopBindingCapture() {
    std::lock_guard guard(lock);
    const int result = capturedBinding;
    capturedBinding = 0;
    return result;
}

bool DesktopGameplayActive() {
    std::lock_guard guard(lock);
    return gameplay || weaponSelector;
}

Pad DesktopPad() {
    std::lock_guard guard(lock);
    Pad pad;
    pad.connected = true;
    menuShift = false;
    if (!focused || !keyboard) return pad;
    const auto key = [](SDL_Scancode code) { return keys[code] || pressed[code]; };
    const auto bound = [&](DesktopAction action, bool pulse = true) {
        const int binding = bindings[static_cast<unsigned>(action)];
        if (binding < 0) return mouseDown[-binding] || (pulse && mousePressed[-binding]);
        return keys[binding] || (pulse && pressed[binding]);
    };
    if (gameplay) {
        const int x = int(bound(DesktopAction::Right, false)) - int(bound(DesktopAction::Left, false));
        const int y = int(bound(DesktopAction::Forward, false)) - int(bound(DesktopAction::Backward, false));
        const int magnitude = x && y ? 23170 : 32767;
        pad.lx = int16_t(x * magnitude);
        pad.ly = int16_t(y * magnitude);
        pad.analog[0] = bound(DesktopAction::Jump) ? 255 : 0;
        pad.analog[1] = bound(DesktopAction::Grab) ? 255 : 0;
        pad.analog[2] = bound(DesktopAction::Reload) || key(SDL_SCANCODE_E) ? 255 : 0;
        pad.analog[3] = bound(DesktopAction::Evade) ? 255 : 0;
        pad.analog[5] = bound(DesktopAction::Heal) ? 255 : 0;
        pad.analog[6] = bound(DesktopAction::Crouch) ? 255 : 0;
        pad.analog[7] = bound(DesktopAction::Fire) ? 255 : 0;
        if (key(SDL_SCANCODE_ESCAPE)) pad.buttons = 0x10;
    } else {
        menuShift = key(SDL_SCANCODE_LSHIFT) || key(SDL_SCANCODE_RSHIFT);
        if (key(SDL_SCANCODE_UP) || key(SDL_SCANCODE_W)) pad.buttons |= 1;
        if (key(SDL_SCANCODE_DOWN) || key(SDL_SCANCODE_S)) pad.buttons |= 2;
        if (key(SDL_SCANCODE_LEFT) || key(SDL_SCANCODE_A)) pad.buttons |= 4;
        if (key(SDL_SCANCODE_RIGHT) || key(SDL_SCANCODE_D)) pad.buttons |= 8;
        if (pressed[SDL_SCANCODE_RETURN] || pressed[SDL_SCANCODE_SPACE]) {
            pad.analog[0] = 255;
        }
        if (pressed[SDL_SCANCODE_ESCAPE] || pressed[SDL_SCANCODE_BACKSPACE]) pad.analog[1] = 255;
        if (bound(DesktopAction::Reload) || key(SDL_SCANCODE_R) || key(SDL_SCANCODE_E)) pad.analog[2] = 255;
    }
    // The HUD selector suspends the camera that identifies desktop gameplay.
    // Keep Black held through that transition, without enabling gameplay inputs in menus.
    if (weaponSelector && pressed[SDL_SCANCODE_ESCAPE]) {
        pad.buttons = 0x10;
        pad.analog[1] = 0;
    }
    if (pad.buttons & 0x10) ClearWeaponInput();
    if (inventoryHeld && (gameplay || weaponSelector)) pad.analog[4] = 255;
    pressed.fill(false);
    mousePressed.fill(false);
    return pad;
}

bool DesktopMenuShift() {
    std::lock_guard guard(lock);
    return menuShift;
}
}
