#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "kernel/kernel.h"
#include "core/log.h"
#include "core/vblank.h"

using namespace std::chrono_literals;

static void Check(bool ok, const char* why) {
    if (!ok) {
        std::cerr << "FAIL: " << why << '\n';
        std::exit(1);
    }
}

namespace mem {
static uint8_t s_mem_pool[1024 * 1024];
static size_t s_mem_off = 4096;
uint32_t AllocSystem(uint32_t size, uint32_t align) {
    s_mem_off = (s_mem_off + align - 1) & ~(align - 1);
    uint32_t res = (uint32_t)s_mem_off;
    s_mem_off += size;
    return res;
}
uint32_t AllocStack(uint32_t size) {
    uint32_t bottom = AllocSystem(size, 4096);
    return bottom + size;
}
}

namespace kernel {
uint8_t* g_base = mem::s_mem_pool;
static std::mutex s_dispatch_lock;
std::mutex& DispatchLock() { return s_dispatch_lock; }
void SignalChanged() {}
void Bind(ObjectRef, uint32_t) {}
void SetSignal(Object&, int32_t) {}

static std::unordered_map<uint32_t, std::function<uint32_t(X86Context&)>> s_guest_routines;
uint32_t CallGuest(X86Context& ctx, uint32_t routine, const uint32_t* args, size_t count) {
    auto it = s_guest_routines.find(routine);
    if (it != s_guest_routines.end()) {
        return it->second(ctx);
    }
    return 0;
}
}

namespace logx {
void Write(Level, const char*, ...) {}
}

int main() {
    // Test 1: Created suspended thread starts in parked state
    auto t1 = kernel::CreateThread(0x4000, 0x100, 0, 0x1000, 0, true);
    Check(t1->parked.load() == true, "suspended thread must be initialized as parked");
    Check(t1->suspend.load() == 1, "suspended thread must have suspend count 1");

    std::atomic<bool> t1_ran{false};
    kernel::s_guest_routines[0x1000] = [&](X86Context&) -> uint32_t {
        t1_ran.store(true);
        // Self-suspend
        kernel::SuspendPoint();
        return 0;
    };

    kernel::StartThread(t1);
    std::this_thread::sleep_for(15ms);
    Check(t1->parked.load() == true, "worker thread remains parked before resume");
    Check(!t1_ran.load(), "worker thread does not execute while suspended");

    // Test 2: Resume wakes worker and clears parked
    kernel::Resume(*t1);
    {
        std::unique_lock<std::mutex> lock(t1->apc_lock);
        bool parked_again = t1->park_cv.wait_for(lock, 500ms, [&] {
            return t1_ran.load() && t1->parked.load();
        });
        Check(parked_again, "worker ran and parked itself in SuspendPoint");
    }

    // Wake it to exit
    kernel::Resume(*t1);
    std::this_thread::sleep_for(20ms);

    // Test 3: High-frequency handshake loop replicating adxm_goto_mwidle_border / sub_0012F020
    std::atomic<uint32_t> guest_flag{0};
    std::atomic<int> completed_cycles{0};
    std::atomic<bool> worker2_keep_running{true};

    kernel::ThreadRef t2 = kernel::CreateThread(0x4000, 0x100, 0, 0x2000, 0, true);

    kernel::s_guest_routines[0x2000] = [&](X86Context&) -> uint32_t {
        while (worker2_keep_running.load()) {
            if (guest_flag.load() == 1) {
                completed_cycles++;
                guest_flag.store(0);
                kernel::Suspend(*t2);
            } else {
                std::this_thread::yield();
            }
        }
        return 0;
    };

    kernel::StartThread(t2);
    std::this_thread::sleep_for(15ms);
    Check(t2->parked.load(), "t2 starts parked");

    for (int cycle = 0; cycle < 100; cycle++) {
        // Coordinator signals border processing request
        guest_flag.store(1);

        {
            std::unique_lock<std::mutex> lock(t2->apc_lock);
            if (t2->parked.load() || t2->suspend.load() > 0) {
                if (t2->suspend.load() > 0) {
                    t2->suspend--;
                }
                t2->resume_cv.notify_all();
            }

            bool ok = t2->park_cv.wait_for(lock, 200ms, [&] {
                return guest_flag.load() == 0 && t2->parked.load();
            });
            Check(ok, "worker completed cycle and parked within deadline");
        }

        Check(completed_cycles.load() == cycle + 1, "completed cycle count matches iteration");
        Check(guest_flag.load() == 0, "guest flag was cleared by worker");
        Check(t2->parked.load() == true, "worker is firmly parked at end of cycle");
    }

    // Clean up worker 2
    worker2_keep_running.store(false);
    kernel::Resume(*t2);
    std::this_thread::sleep_for(20ms);

    std::cout << "PASS: Thread parked state tracking and 100-cycle border handshake synchronization\n";
    return 0;
}
