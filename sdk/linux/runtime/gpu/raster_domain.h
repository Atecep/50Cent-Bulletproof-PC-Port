#pragma once

#include "gpu/state.h"

namespace gpu {

// XDK 5849: 0x24CDE0 and 0x24C740 select SQUARE_OFFSET_4 for mode 2
// on a non-backbuffer target whose surface format is pitched (0x254090).
constexpr RasterDomain RenderTargetRasterDomain(uint32_t mode, const Surface& color,
                                                uint32_t backbuffer) {
    if (mode != 2 || !color.header || color.header == backbuffer) return RasterDomain::Native;
    switch ((color.format >> 8) & 0xff) {
        case 0x11:  // LIN_R5G6B5
        case 0x12:  // LIN_A8R8G8B8
        case 0x13:  // LIN_L8
        case 0x17:  // LIN_G8B8
        case 0x1c:  // LIN_X1R5G5B5
        case 0x1e:  // LIN_X8R8G8B8
            return RasterDomain::SquareOffset4;
        default:
            return RasterDomain::Native;
    }
}

struct PositionExtent {
    float width;
    float height;
};

// Fixed and pre-transformed paths already use frame coordinates. A vertex
// program writes pixels in the active NV2A raster domain.
constexpr PositionExtent DrawPositionExtent(const State& s, bool programmable, bool hasColorTarget,
                                           uint32_t width, uint32_t height) {
    if (!programmable) return {float(width), float(height)};
    if (hasColorTarget && s.raster_domain == RasterDomain::SquareOffset4)
        return {float((width + 1) >> 1), float((height + 1) >> 1)};
    return {float(width) * s.super_x, float(height) * s.super_y};
}

}  // namespace gpu
