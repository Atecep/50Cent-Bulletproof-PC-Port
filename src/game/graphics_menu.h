#pragma once
#include <cstdint>
#include <string>
#include "app/app.h"

// Modern Invert Camera Y option (persisted in settings.toml)
// default: false (Modern standard: Stick UP = Look UP, Stick DOWN = Look DOWN)
// true: Classic 2005 Xbox inverted (Stick UP = Look DOWN, Stick DOWN = Look UP)
extern bool s_invert_y;

// Native Audio/Video pages and secondary graphics debug overlay
void GraphicsMenu_Init();
void GraphicsMenu_RenderUi(double fps);
void GraphicsMenu_ProcessPad(int port, input::Pad& pad);
bool GraphicsMenu_IsOpen();
void GraphicsMenu_SetOpen(bool open);
bool GraphicsMenu_GetInvertY();
void GraphicsMenu_SetInvertY(bool invert);
// Runtime pages reuse the four original Audio/Video widgets and text nodes.
// role0..3 are labels, role4 the title, role5 the original A-button hint.
bool GraphicsMenu_NativeText(int role, bool value, std::string& text);
int GraphicsMenu_TakeNativeSelection();

// Native Controls submenu text handler (replaces labels, values, titles and hints)
bool ControlsMenu_NativeText(int role, bool is_value, std::string& text);

// Native Keyboard & Mouse submenu text handler
bool KeyboardMouseMenu_NativeText(int role, bool is_value, std::string& text);

// Native Options menu text handler (manages independent entries: Controls, Camera, Audio/Video, Customize, KBM, Credits/Codes/Autosave)
bool OptionsMenu_ShiftHighlight();

uint32_t GraphicsMenu_KeyboardDescriptor();
void GraphicsMenu_SetKeyboardDescriptor(uint32_t desc);
int GraphicsMenu_TakeKeyboardTransition();

bool MainMenu_ExitSelected();

// Scoped layout for the expanded display and input reference lists.
struct GraphicsMenuRows {
    int count = 0;
    int selected = 0;
    int top_margin_rows = 0; // Shift from the first native row, measured in native row strides.
};
GraphicsMenuRows GraphicsMenu_CompactRows();
std::string GraphicsMenu_CompactRowText(int row, bool value);
