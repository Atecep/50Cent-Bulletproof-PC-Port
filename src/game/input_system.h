#pragma once
#include "core/input.h"

bool InputSystem_GetL3Crouch();
void InputSystem_SetL3Crouch(bool enabled);

void InputSystem_Load(const void* toml_table);
void InputSystem_Save(void* toml_table);
void InputSystem_ProcessPad(int port, input::Pad& pad);
