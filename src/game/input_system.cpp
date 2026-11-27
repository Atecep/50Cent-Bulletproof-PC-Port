#include "input_system.h"

#include <cctype>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

#include "app/app.h"
#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "core/desktop_input.h"
#endif

namespace {
bool s_l3_crouch = true;
#if defined(__linux__) || defined(XBE_VK_TARGET)
constexpr const char* bindingNames[] = {"move_forward", "move_backward", "move_left",
                                      "move_right", "fire", "jump", "reload", "crouch", "evade",
                                      "grab", "heal", "weapon_inventory"};
static_assert(std::size(bindingNames) == input::DesktopBindings{}.size());
#endif
}

bool InputSystem_GetL3Crouch() {
    return s_l3_crouch;
}

void InputSystem_SetL3Crouch(bool enabled) {
    if (s_l3_crouch == enabled) return;
    s_l3_crouch = enabled;
    app::Save();
}

void InputSystem_Load(const void* toml_table) {
    s_l3_crouch = true;
#if defined(__linux__) || defined(XBE_VK_TARGET)
    input::ResetDesktopBindings();
#endif
    if (!toml_table) return;
    const auto& table = *static_cast<const toml::table*>(toml_table);

    if (const auto enabled = table["l3_crouch"].value_exact<bool>()) {
        s_l3_crouch = *enabled;
    } else if (const auto profile = table["controller_profile"].value_exact<std::string_view>()) {
        std::string name(*profile);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s_l3_crouch = name != "classic" && name != "classic xbox";
    } else if (const auto profile = table["controller_profile"].value_exact<int64_t>()) {
        s_l3_crouch = *profile != 1;
    }
#if defined(__linux__) || defined(XBE_VK_TARGET)
    auto bindings = input::GetDesktopBindings();
    for (unsigned i = 0; i < bindings.size(); ++i) {
        const auto value = table["keyboard_bindings"][bindingNames[i]].value_exact<int64_t>();
        if (value && *value >= -256 && *value <= 1024) bindings[i] = static_cast<int>(*value);
    }
    input::LoadDesktopBindings(bindings);
#endif
}

void InputSystem_Save(void* toml_table) {
    if (!toml_table) return;
    auto& table = *static_cast<toml::table*>(toml_table);
    table.erase("controller_profile");
    table.insert_or_assign("l3_crouch", s_l3_crouch);
#if defined(__linux__) || defined(XBE_VK_TARGET)
    const auto bindings = input::GetDesktopBindings();
    toml::table keyboard;
    for (unsigned i = 0; i < bindings.size(); ++i) keyboard.insert(bindingNames[i], bindings[i]);
    table.insert_or_assign("keyboard_bindings", std::move(keyboard));
#endif
}

void InputSystem_ProcessPad(int port, input::Pad& pad) {
    constexpr uint16_t kLeftThumb = 0x0040;
    constexpr int kLeftTrigger = 6;
    // Add the original crouch input while L3 is held. Physical LT remains valid.
    if (port == 0 && pad.connected && s_l3_crouch && (pad.buttons & kLeftThumb)) {
        pad.analog[kLeftTrigger] = 0xFF;
    }
}
