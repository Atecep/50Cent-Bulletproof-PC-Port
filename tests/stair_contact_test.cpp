#include "game/stair_contact.h"

#include <cmath>
#include <cstdlib>
#include <limits>

int main() {
    constexpr float slopeCosine = 0.6427876f;
    constexpr float ny = 0.748697f;
    constexpr float nz = 0.662912f;
    using stair_contact::DownhillVelocity;
    const auto check = [](bool condition) { if (!condition) std::abort(); };

    const float down = DownhillVelocity(true, 0, 0, 5, 0, ny, nz, slopeCosine);
    check(down < 0 && std::abs(ny * down + nz * 5) < 0.00001f);
    check(DownhillVelocity(true, 0, 0, -5, 0, ny, nz, slopeCosine) == 0);
    check(DownhillVelocity(true, 5, 0, 0, 0, ny, nz, slopeCosine) == 0);
    check(DownhillVelocity(true, 0, 0, 5, 0, 1, 0, slopeCosine) == 0);
    check(DownhillVelocity(true, 0, 0, 5, 0, 0.6f, 0.8f, slopeCosine) == 0);
    check(DownhillVelocity(false, 0, 0, 5, 0, ny, nz, slopeCosine) == 0);
    check(DownhillVelocity(true, 0, 6, 5, 0, ny, nz, slopeCosine) == 6);
    check(DownhillVelocity(true, 0, -2, 5, 0, ny, nz, slopeCosine) == -2);
    check(DownhillVelocity(true, 0, 0, 5, 0, ny,
                           std::numeric_limits<float>::infinity(), slopeCosine) == 0);
}
