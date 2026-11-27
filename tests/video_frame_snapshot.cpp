#include "src/game/video_frame_snapshot.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>
static void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    using sofdec::PlanarBytes;
    Check(PlanarBytes(3, 640, 480) == 460800, "original 640x480 planes");
    Check(PlanarBytes(3, 33, 17) == 3072, "padded Y and chroma strides");
    Check(!PlanarBytes(2,640,480) && !PlanarBytes(3,0,480) &&
          !PlanarBytes(3,0xFFFFFFFF,480), "unsupported and invalid layouts");
    std::vector<uint8_t> ram(2 * 1024 * 1024);
    sofdec::FrameSnapshots frames;
    unsigned allocations = 0, frees = 0;
    auto allocate = [&](uint32_t) { ++allocations; return 1024u * 1024; };
    auto free = [&](uint32_t) { ++frees; };
    auto readable = [&](uint32_t va, uint32_t size) { return uint64_t(va)+size <= ram.size(); };
    const auto bytes = PlanarBytes(3,640,480);
    std::fill_n(ram.data()+4096, bytes, 32); // acquired frame 32
    const auto stable = frames.Capture(1, ram.data(), 4096, 3,640,480, allocate,free,readable);
    Check(stable, "snapshot acquired");
    // Original release still occurs immediately. A future MPEG B/P frame may
    // overwrite the freed pool allocation before either late RGB conversion.
    std::fill_n(ram.data()+4096, bytes, 36);
    for (int conversion=0; conversion<2; ++conversion)
        Check(std::all_of(ram.begin()+stable,ram.begin()+stable+bytes,
                         [](uint8_t b){return b==32;}), "late conversion must retain frame 32");
    Check(frames.Capture(1,ram.data(),4096,3,640,480,allocate,free,readable)==stable,
          "next successful acquisition reuses owned storage");
    Check(ram[stable]==36 && allocations==1, "new frame captured without per-frame allocation");
    Check(!frames.Capture(1,ram.data(),0xFFFFFFF0,3,640,480,allocate,free,readable), "guest range overflow");
    Check(!frames.Capture(1,ram.data(),ram.size()-16,3,640,480,allocate,free,readable), "unreadable source");
    frames.Close(1,free); frames.Close(1,free);
    Check(frees==1, "close invalidates player and frees exactly once");
    std::cout << "PASS: planar layout, released pool reuse, late conversion and lifetime\n";
}
