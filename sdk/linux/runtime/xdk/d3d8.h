#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

// read-only: the XDK entry points write it and mark what they wrote for the render thread (gpu::State::Mark)
const gpu::State& State();
std::recursive_mutex& Lock();

// Instruction count of an HLE program, independent of its opaque guest handle.
bool VertexProgramSize(uint32_t handle, uint32_t& instructions);
void SetPalette(uint32_t stage, uint32_t palette);

}  // namespace d3d
