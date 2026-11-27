#pragma once
#include <algorithm>
#include <cmath>

namespace input {

// The guest camera interprets a stick value as a rate. Estimate that rate
// from SDL's displacement over elapsed time, smoothing packet arrival jitter.
struct MouseRate {
    float x = 0;
    float y = 0;

    void Update(float dx, float dy, float seconds) {
        if (seconds <= 0 || seconds > 0.1f) {
            *this = {};
            return;
        }
        constexpr float smoothingSeconds = 0.025f;
        const float weight = -std::expm1(-seconds / smoothingSeconds);
        x += (dx / seconds - x) * weight;
        y += (dy / seconds - y) * weight;
        if (dx == 0 && std::abs(x) < 1.f) x = 0;
        if (dy == 0 && std::abs(y) < 1.f) y = 0;
    }

    float Axis(int axis, unsigned sensitivity) const {
        constexpr float gainAt60Hz = 0.018f;
        const float gain = gainAt60Hz / 60 * std::clamp(sensitivity, 10u, 400u) / 100.f;
        return std::clamp((axis == 2 ? x : y) * gain, -1.f, 1.f);
    }
};
}
