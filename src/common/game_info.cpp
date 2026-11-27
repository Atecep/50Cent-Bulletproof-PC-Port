#include <SDL3/SDL_init.h>
#include "app/app.h"
#include "bulletproof_metadata.h"
#if defined(__linux__) || defined(XBE_VK_TARGET)
void BulletproofLinuxLoaded();
namespace input {
extern uint32_t mouseSensitivity;
extern bool invertMouseY;
}
#endif

void GraphicsMenu_RenderUi(double fps);
void GraphicsMenu_ProcessPad(int port, input::Pad& pad);
void InputSystem_Load(const void* table);
void InputSystem_Save(void* table);
extern bool s_invert_y;

namespace {
void SetAppMetadata() {
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, bulletproof_metadata::name);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, bulletproof_metadata::version);
}
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = bulletproof_metadata::wide_name,     // window and splash
    .short_title = bulletproof_metadata::wide_name, // message boxes
    .name = L"Bulletproof",                      // window classes, crash dumps, handover event
    .log = "bulletproof",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "Bulletproof"}},  // each recompiled xbe and its exe, for title launches
    .options = {
        {"invert_camera_y", &s_invert_y, nullptr},
#if defined(__linux__) || defined(XBE_VK_TARGET)
        {"mouse_sensitivity", nullptr, &input::mouseSensitivity},
        {"invert_mouse_y", &input::invertMouseY, nullptr},
#endif
    },                        // game settings kept in the settings file
    .load = InputSystem_Load,                      // reads more from the settings file's toml::table
    .save = InputSystem_Save,                      // writes more into it
    .init = SetAppMetadata,
#if defined(__linux__) || defined(XBE_VK_TARGET)
    .loaded = BulletproofLinuxLoaded,     // restore DSOUND's pure guest table initializers
#else
    .loaded = nullptr,
#endif
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = GraphicsMenu_RenderUi,          // ImGui drawing each frame
    .pad = GraphicsMenu_ProcessPad,       // changes a polled pad
    .rumble = nullptr,                    // changes a rumble before it is set
};
