#pragma once

#include <cstdint>

// Phase 6D: Frame rate independence for animations, clock overrides (takedowns),
// scrolling textures (steam/blood/fluids), and environmental particle effects.
// Preserves original 30 FPS reference timing across 30, 60, 90, and 120 FPS.

namespace timing {

// Query current override status and scaled timing factor
bool IsClockOverrideActive();
float GetCurrentTimeScale();
float GetEffectiveDeltaTime();

// Phase 6D.4: Headshot bullet follow camera status
bool IsHeadshotCameraActive();
float GetHeadshotPreImpactTime();

} // namespace timing
