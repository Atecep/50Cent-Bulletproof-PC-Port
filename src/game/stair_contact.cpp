#if defined(__linux__) || defined(XBE_VK_TARGET)
#include "stair_contact.h"

#include <bit>

#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_000F41D0);
X86_EXTERN_FUNC(__imp__sub_000F4A00);

namespace {

struct Floor {
    float y, nx, ny, nz;
};

float readFloat(uint32_t address) {
    return std::bit_cast<float>(kernel::R32(address));
}

void writeFloat(uint32_t address, float value) {
    kernel::W32(address, std::bit_cast<uint32_t>(value));
}

bool floorAt(X86Context& original, uint8_t* base, uint32_t world, float x, float y,
             float z, float bottom, Floor& floor) {
    auto shadow = original;
    auto& ctx = shadow;
    const auto output = (ctx.esp - 0x1e0u) & ~0xfu;
    const auto from = output + 0x1a0;
    const auto to = from + 0x10;
    ctx.esp = output;
    ctx.ecx = world;
    writeFloat(from, x);
    writeFloat(from + 4, y);
    writeFloat(from + 8, z);
    kernel::W32(from + 12, 0);
    writeFloat(to, x);
    writeFloat(to + 4, bottom);
    writeFloat(to + 8, z);
    kernel::W32(to + 12, 0);
    X86_PUSH32(0);
    X86_PUSH32(output);
    X86_PUSH32(to);
    X86_PUSH32(from);
    X86_PUSH32(0x1A760);
    __imp__sub_000F4A00(ctx, base);
    if (!kernel::R32(output)) return false;
    floor = {readFloat(output + 0x44), readFloat(output + 0xc0), readFloat(output + 0xc4), readFloat(output + 0xc8)};
    return true;
}

} // namespace

X86_FUNC(sub_000F41D0) {
    // Only the local player's locomotion update calls the setter from
    // 0x0007D0E9. Other setters and scripted movement keep the guest behavior.
    const auto actor = kernel::R32(ctx.esp) == 0x0007D0EE
        ? kernel::R32(0x0032DCFC) : 0;
    const auto wrapper = ctx.ecx;
    const auto proxy = actor && kernel::R32(actor + 0x32C4) == wrapper &&
                       base[actor + 0x18] == 1 ? kernel::R32(wrapper) : 0;

    auto rayContext = ctx;
    __imp__sub_000F41D0(ctx, base);
    if (!proxy) return;

    const auto phantom = kernel::R32(proxy + 0x30);
    const auto pose = phantom ? kernel::R32(phantom + 0x1c) + 0x30 : 0;
    const float x = pose ? readFloat(pose) : 0.0f;
    const float y = pose ? readFloat(pose + 4) : 0.0f;
    const float z = pose ? readFloat(pose + 8) : 0.0f;
    const float vx = readFloat(proxy + 0x10);
    const float vy = readFloat(proxy + 0x14);
    const float vz = readFloat(proxy + 0x18);
    const float limit = readFloat(proxy + 0xa4);

    const auto level = kernel::R32(0x00322FF8);
    const auto world = level ? kernel::R32(level + 0x6c) : 0;
    Floor floor{};
    const bool hit = pose && world && floorAt(rayContext, base, world, x, y + 0.10f, z, y - 0.50f, floor);

    float normX = readFloat(actor + 0x4170);
    float normY = readFloat(actor + 0x4174);
    float normZ = readFloat(actor + 0x4178);

    // If floor directly below is a downward walkable slope and player is moving downhill:
    if (hit && floor.ny >= limit && floor.ny < 0.999f) {
        const float downhill = floor.nx * vx + floor.nz * vz;
        if (downhill > 0.0f) {
            // When proxy normal is flatter than actual floor (e.g. at convex landing edge)
            if (normY > floor.ny + 0.05f || normY >= 0.999f) {
                normX = floor.nx;
                normY = floor.ny;
                normZ = floor.nz;
            }
        }
    }

    // Set Havok character proxy keep contact tolerance to 0.15f (Havok SDK recommended for stairs/slopes)
    writeFloat(proxy + 0x5C, 0.15f);

    const float newY = stair_contact::DownhillVelocity(
        true, vx, vy, vz, normX, normY, normZ, limit);
    if (newY != vy) writeFloat(proxy + 0x14, newY);
}

#endif
