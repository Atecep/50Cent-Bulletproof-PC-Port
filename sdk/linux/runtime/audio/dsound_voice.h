#pragma once
#include <cstdint>

// HLE operations used by verified title bindings; obj is the host voice handle,
// never a pointer to the console's private CDirectSoundBuffer object.
namespace dsound {
uint32_t Use3DVoiceData(uint32_t obj, bool enabled);
uint32_t Set3DVoiceData(uint32_t obj, uint32_t data);
uint32_t GetVoiceProperties(uint32_t obj, uint32_t out);
uint32_t SetPitch(uint32_t obj, int32_t pitch);
uint32_t SetLFO(uint32_t obj, uint32_t desc);
}
