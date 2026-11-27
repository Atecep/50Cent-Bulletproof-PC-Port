#pragma once

#include <cstdint>

// Phase 6D.5: Reusable Timing & Frame Rate Telemetry Diagnostic System
// Diagnostic facility to record real timestamp, update number, delta times,
// object/emitter identifiers, 3D position/velocity, age, scale, opacity (alpha),
// texture index, and RNG state.
//
// Zero runtime cost when disabled: controlled by environment variables
// (BP_TELEMETRY_ENABLED, BP_TELEMETRY_SLOT, BP_TELEMETRY_LOG, BP_TELEMETRY_MAX).

namespace timing {

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
__attribute__((weak)) bool IsTelemetryEnabled();
__attribute__((weak)) void OnParticleIntegrate(uint32_t buf, float dt_in, float dt_used, uint8_t* base);
__attribute__((weak)) void OnParticleIntegratePost(uint32_t buf, float dt_used, uint8_t* base);
__attribute__((weak)) void OnParticleDraw(uint32_t buf, uint8_t* base);
__attribute__((weak)) void OnParticleSpawn(uint32_t buf, uint16_t idx, uint8_t* base);
__attribute__((weak)) void OnEmitterTick(uint32_t emitter_ptr, float dt_in, uint8_t* base);
__attribute__((weak)) void OnEmitterForceFieldBefore(uint32_t buf, uint16_t first_p, float dt, uint8_t* base);
__attribute__((weak)) void OnEmitterForceFieldAfter(uint32_t buf, uint16_t first_p, float dt, uint8_t* base);
#else
bool IsTelemetryEnabled();
void OnParticleIntegrate(uint32_t buf, float dt_in, float dt_used, uint8_t* base);
void OnParticleIntegratePost(uint32_t buf, float dt_used, uint8_t* base);
void OnParticleDraw(uint32_t buf, uint8_t* base);
void OnParticleSpawn(uint32_t buf, uint16_t idx, uint8_t* base);
void OnEmitterTick(uint32_t emitter_ptr, float dt_in, uint8_t* base);
void OnEmitterForceFieldBefore(uint32_t buf, uint16_t first_p, float dt, uint8_t* base);
void OnEmitterForceFieldAfter(uint32_t buf, uint16_t first_p, float dt, uint8_t* base);
#endif

} // namespace timing
