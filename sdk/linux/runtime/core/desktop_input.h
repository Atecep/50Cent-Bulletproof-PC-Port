#pragma once
#include "core/input.h"
#include <cstdint>
#include <array>
#include <string>

namespace input {
extern uint32_t mouseSensitivity;
extern bool invertMouseY;

enum class DesktopAction { Forward, Backward, Left, Right, Fire, Jump, Reload, Crouch, Evade, Grab, Heal, Inventory, Count };
using DesktopBindings = std::array<int, static_cast<unsigned>(DesktopAction::Count)>;
enum class BindingResult { Applied, Reserved, InUse };
DesktopBindings GetDesktopBindings();
void LoadDesktopBindings(const DesktopBindings& bindings);
void ResetDesktopBindings();
BindingResult AssignDesktopBinding(DesktopAction action, int binding);
std::string DesktopBindingName(DesktopAction action);
void BeginDesktopBindingCapture();
void EndDesktopBindingCapture();
int TakeDesktopBindingCapture();
inline constexpr int DesktopBindingCancelled = -256;

bool DesktopGameplayActive();
Pad DesktopPad();
void DesktopWeaponSelector(bool active);
struct WeaponWheelStep {
    int direction = 0;
    uint64_t id = 0;
};
WeaponWheelStep PeekWeaponWheelStep();
void CompleteWeaponWheelStep(uint64_t id);
void PreferController();
bool DesktopUsesKeyboardPrompts();
bool DesktopMenuShift();
bool IsMouseActive();
bool ConsumeMouseDelta(float& dx, float& dy);
float GetMouseSensitivityFactor();
void SetMouseCapturedForTest(bool captured);
}
