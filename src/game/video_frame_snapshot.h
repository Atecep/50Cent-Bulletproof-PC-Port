#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace sofdec {
// Original 14E330: planar mode 3, macroblock-aligned planes, 32-byte strides.
inline uint32_t PlanarBytes(uint32_t mode, uint32_t width, uint32_t height) {
    if (mode != 3 || !width || !height || width > 32752 || height > 32752) return 0;
    const uint64_t w = (uint64_t(width) + 15) & ~15ull;
    const uint64_t h = (uint64_t(height) + 15) & ~15ull;
    const uint64_t y = (w + 31) & ~31ull;
    const uint64_t c = (w / 2 + 31) & ~31ull;
    if (y > 32767 || c > 32767) return 0; // original signed 16-bit strides
    const uint64_t size = y * h + c * h;
    return size <= UINT32_MAX ? uint32_t(size) : 0;
}

// The public frame may be converted after the raw MPEG pool frame is released.
// Retain pixels until the next successful acquisition (or player close), without
// retaining any MPEG reference, changing the pool, or changing timestamps.
class FrameSnapshots {
    struct Slot { uint32_t address = 0, capacity = 0; };
    std::unordered_map<uint32_t, Slot> slots_;
    std::mutex mutex_;
public:
    template<class Allocate, class Free, class Readable>
    uint32_t Capture(uint32_t player, uint8_t* base, uint32_t source,
                     uint32_t mode, uint32_t width, uint32_t height,
                     Allocate allocate, Free free, Readable readable) {
        const auto bytes = PlanarBytes(mode, width, height);
        if (!source || !bytes || uint64_t(source) + bytes > (1ull << 32) ||
            !readable(source, bytes)) return 0;
        std::lock_guard lock(mutex_);
        auto& slot = slots_[player];
        if (slot.capacity < bytes) {
            const auto replacement = allocate(bytes);
            if (!replacement) return 0;
            if (slot.address) free(slot.address);
            slot = {replacement, bytes};
        }
        std::memcpy(base + slot.address, base + source, bytes);
        return slot.address;
    }
    template<class Free> void Close(uint32_t player, Free free) {
        std::lock_guard lock(mutex_);
        auto it = slots_.find(player);
        if (it != slots_.end()) {
            if (it->second.address) free(it->second.address);
            slots_.erase(it);
        }
    }
};
} // namespace sofdec
