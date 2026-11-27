#undef NDEBUG
#include <cassert>
#include <toml++/toml.hpp>
#include "src/game/input_system.h"
#include "core/log.h"
#include "core/desktop_input.h"
#include <SDL3/SDL.h>

namespace logx { void Write(Level, const char*, ...) {} }

namespace {
int saves = 0;
}
namespace app {
void Save() { ++saves; }
}

int main() {
    assert(InputSystem_GetL3Crouch());
    for (const auto* legacy : {"modern", "classic", "Classic Xbox"}) {
        toml::table table{{"controller_profile", legacy}, {"invert_camera_y", true}};
        const bool enabled = std::string_view(legacy) == "modern";
        InputSystem_Load(&table);
        assert(InputSystem_GetL3Crouch() == enabled);
        InputSystem_Save(&table);
        assert(!table.contains("controller_profile"));
        assert(table["l3_crouch"].value_exact<bool>() == enabled);
        assert(table["invert_camera_y"].value_exact<bool>() == true);
    }

    toml::table table{{"controller_profile", 1}};
    InputSystem_Load(&table);
    assert(!InputSystem_GetL3Crouch());
    table.insert("l3_crouch", true);
    InputSystem_Load(&table);
    assert(InputSystem_GetL3Crouch());
    assert(saves == 0);

    assert(input::AssignDesktopBinding(input::DesktopAction::Forward, SDL_SCANCODE_I)
           == input::BindingResult::Applied);
    assert(input::AssignDesktopBinding(input::DesktopAction::Inventory, -SDL_BUTTON_X2)
           == input::BindingResult::Applied);
    InputSystem_Save(&table);
    input::ResetDesktopBindings();
    InputSystem_Load(&table);
    assert(input::GetDesktopBindings()[0] == SDL_SCANCODE_I);
    assert(input::GetDesktopBindings()[static_cast<unsigned>(input::DesktopAction::Inventory)] == -SDL_BUTTON_X2);
    const toml::table legacyBindings{{"keyboard_bindings", toml::table{{"move_forward", SDL_SCANCODE_I}}}};
    InputSystem_Load(&legacyBindings);
    assert(input::GetDesktopBindings()[0] == SDL_SCANCODE_I);
    assert(input::GetDesktopBindings()[static_cast<unsigned>(input::DesktopAction::Inventory)] == SDL_SCANCODE_TAB);

    input::Pad physical{};
    physical.connected = true;
    physical.buttons = 0x0032 | 0x0040 | 0x0080;
    physical.analog[1] = 31;
    physical.analog[6] = 90;
    auto pad = physical;
    InputSystem_ProcessPad(0, pad);
    assert(pad.buttons == physical.buttons);
    assert(pad.analog[1] == 31 && pad.analog[6] == 255);
    assert(saves == 0 && InputSystem_GetL3Crouch());

    InputSystem_SetL3Crouch(false);
    InputSystem_SetL3Crouch(false);
    assert(saves == 1);
    pad = physical;
    InputSystem_ProcessPad(0, pad);
    assert(pad.buttons == physical.buttons);
    assert(pad.analog[1] == 31 && pad.analog[6] == 90);
    InputSystem_Load(nullptr);
    assert(InputSystem_GetL3Crouch());
}
