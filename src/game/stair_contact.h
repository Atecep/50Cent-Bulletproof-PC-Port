#pragma once

#include <cmath>

namespace stair_contact {

// Preserve the requested horizontal motion and supply the missing component
// along an already accepted ground plane. The guest still decides contact.
inline float DownhillVelocity(bool supported, float vx, float vy, float vz,
                              float nx, float ny, float nz, float slopeCosine) {
    if (!supported || vy != 0.0f || !(slopeCosine > 0.0f) ||
        !(ny >= slopeCosine && ny < 1.0f)) {
        return vy;
    }

    const float downhill = nx * vx + nz * vz;
    const float tangentY = -downhill / ny;
    return downhill > 0.0f && std::isfinite(tangentY) ? tangentY : vy;
}

}
