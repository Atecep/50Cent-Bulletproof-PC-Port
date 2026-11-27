#pragma once
#include <cstdint>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

void Init();  // window thread, before boot
void DeviceChanged(uint32_t id, bool added);  // window thread, SDL gamepad hotplug events
// the controller on that port, then GameInfo::pad
Pad Poll(int port);
bool Connected(int port);
void Rumble(int port, uint16_t left, uint16_t right);

}  // namespace input

union SDL_Event;
struct SDL_Window;
namespace input {
void DesktopEvent(const SDL_Event& event);
void DesktopTick(SDL_Window* window);
void DesktopCamera(); // Called by the game's camera input reader.
bool DesktopLook(int axis, float& value);
}
