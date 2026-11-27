// Generated XDVDFS fixtures only; no game bytes or title-specific addresses.
#include "kernel/disc.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
static void Put32(char* p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = char(v >> (8 * i));
}
static void Fixture(const std::filesystem::path& path, uint64_t base) {
    std::ofstream f(path, std::ios::binary);
    std::array<char, 2048> volume{};
    constexpr char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(volume.data(), magic, sizeof(magic) - 1);
    std::memcpy(volume.data() + 0x7ec, magic, sizeof(magic) - 1);
    Put32(volume.data() + 0x14, 33);
    Put32(volume.data() + 0x18, 28);
    f.seekp(base + 32 * 2048);
    f.write(volume.data(), volume.size());
    std::array<char, 28> entry{};
    Put32(entry.data() + 4, 34);
    Put32(entry.data() + 8, 7);
    entry[13] = 9;
    std::memcpy(entry.data() + 14, "Probe.bin", 9);
    f.seekp(base + 33 * 2048);
    f.write(entry.data(), entry.size());
    f.seekp(base + 34 * 2048);
    f.write("fixture", 7);
    Check(bool(f), "writing fixture");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    try {
        for (uint64_t base : {uint64_t(0), uint64_t(0x18300000)}) {
            const auto path = dir / (base ? "whole-disc.iso" : "partition.iso");
            Fixture(path, base);
            kernel::Disc disc;
            std::string error;
            Check(disc.Open(path, error), "opening XDVDFS");
            const auto* entry = disc.Find("pRoBe.BIN");
            Check(entry && !entry->dir && entry->size == 7, "case-insensitive lookup");
            std::vector<uint8_t> data;
            Check(disc.Load("PROBE.BIN", data), "loading entry");
            Check(std::string(data.begin(), data.end()) == "fixture", "entry bytes");
            char bytes[16]{};
            Check(disc.Read(*entry, 4, bytes, sizeof(bytes)) == 3, "EOF short read");
            Check(std::string(bytes, 3) == "ure", "partial read bytes");
            Check(disc.Read(*entry, 7, bytes, 1) == 0, "EOF read");
            Check(!disc.Find("missing.bin"), "missing entry");
        }
        const auto bad = dir / "invalid.iso";
        { std::ofstream f(bad); f << "invalid"; }
        kernel::Disc disc;
        std::string error;
        Check(!disc.Open(bad, error) && !error.empty(), "reject invalid image");
        std::cout << "PASS: partition and whole-disc offsets, lookup, reads, invalid image\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
