#include "cpu/x86_ops.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <thread>
#include <csignal>

static void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
int main() {
    using namespace std::chrono;
    Check(apu::SamplesAtNanoseconds(0) == 0, "clock epoch");
    Check(apu::SamplesAtNanoseconds(1000000000) == 48000, "48 kHz rate");
    Check(apu::SamplesAtNanoseconds(20833) == 0 && apu::SamplesAtNanoseconds(20834) == 1, "sample rounding");
    const uint64_t wrapNs = ((1ull << 32) * 1000000000 + 47999) / 48000;
    Check(apu::SamplesAtNanoseconds(wrapNs - 1) == UINT32_MAX, "before 32-bit wrap");
    Check(apu::SamplesAtNanoseconds(wrapNs) == 0, "32-bit wrap");
    constexpr uint64_t size = 1ull << 32;
    auto* base = static_cast<uint8_t*>(mmap(nullptr, size, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    Check(base != MAP_FAILED, "reserve guest space");
    Check(mprotect(base + 0x10000, 4096, PROT_READ | PROT_WRITE) == 0, "commit RAM");
    const uint32_t marker = 0x78563412;
    std::memcpy(base + 0x10001, &marker, 4);
    Check(x86_read32(base, 0x10001) == marker, "unaligned RAM read unchanged");
    volatile uint32_t address = apu::kSampleCounter;  // exercise dynamic addressing too
    const auto before = steady_clock::now();
    const uint32_t first = x86_read32(base, address);
    std::this_thread::sleep_for(milliseconds(20));
    const uint32_t last = x86_read32(base, address);
    const auto after = steady_clock::now();
    const uint32_t delta = last - first;
    Check(delta >= 400, "clock advances without audio output");
    Check(delta <= duration_cast<nanoseconds>(after - before).count() * 48000 / 1000000000 + 2,
          "counter tracks monotonic elapsed time");
    // An unsupported adjacent register must still fault, not silently read zero.
    const pid_t child = fork();
    Check(child >= 0, "fork");
    if (child == 0) {
        const rlimit noCore{0, 0};
        setrlimit(RLIMIT_CORE, &noCore);
        volatile auto value = x86_read32(base, apu::kSampleCounter + 4);
        (void)value;
        _exit(0);
    }
    int status = 0;
    Check(waitpid(child, &status, 0) == child, "waitpid");
    Check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "unknown MMIO remains protected");
    munmap(base, size);
    std::cout << "PASS: 48 kHz, rounding, wrap, live MMIO, RAM and unknown-register protection\n";
}
