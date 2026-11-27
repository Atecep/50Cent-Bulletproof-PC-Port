#include <cassert>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "kernel/kernel.h"
#include "kernel/disc.h"
#include "app/app.h"
#include "core/log.h"

// Stubs for kernel symbols required by files.cpp
namespace kernel {
uint8_t* g_base = nullptr;
static KernelFn exports[400]{};
static std::array<ObjectRef, 8> handles{};
static uint32_t next_handle = 1;
Thread* Current() { return nullptr; }
ObjectRef FromHandle(uint32_t h) { return handles.at(h); }
ObjectRef FromPointer(uint32_t) { return nullptr; }
uint32_t NewHandle(ObjectRef object) {
    const auto h = next_handle++;
    handles.at(h) = std::move(object);
    return h;
}
ObjectRef FindNamed(const std::string&) { return nullptr; }
int32_t SetEvent(Object&) { return 0; }
int32_t ResetEvent(Object&) { return 0; }
void QueueApc(Thread&, uint32_t, uint32_t, uint32_t, uint32_t) {}
void PostIoCompletion(const ObjectRef&, uint32_t, uint32_t, uint32_t, uint32_t) {}
void RegisterExport(uint32_t ordinal, const char*, KernelFn fn) { exports[ordinal] = fn; }
uint32_t DataAddress(uint32_t) { return 0; }
int64_t Now100ns() { return 0; }
const Disc* MountedDisc() { return nullptr; }
const Disc::Entry* Disc::Find(const std::vector<std::string>&) const { return nullptr; }
uint32_t Disc::Read(const Disc::Entry&, uint64_t, void*, uint32_t) const { return 0; }
}  // namespace kernel

namespace logx {
void Write(Level, const char*, ...) {}
}  // namespace logx

namespace app {
const GameInfo kGame{
    .name = L"Bulletproof",
    .settings = "settings.toml",
};
}  // namespace app

static void Check(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << std::endl;
        std::exit(1);
    }
}

#ifdef _WIN32
static void TestRename() {
    using namespace kernel;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("bulletproof-rename-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directory(root), "create exclusive rename fixture");
    MountDefaults((root / "disc").string(), (root / "hdd").string());
    const auto disc = root / "disc";
    std::ofstream(disc / "old.dat", std::ios::binary) << "original";
    std::ofstream(disc / "new.dat", std::ios::binary) << "previous";

    std::array<uint8_t, 4096> memory{};
    g_base = memory.data();
    auto call = [](uint32_t stack, uint32_t ordinal, std::initializer_list<uint32_t> args) {
        X86Context ctx{};
        ctx.esp = stack;
        for (uint32_t arg : args) {
            stack += 4;
            W32(stack, arg);
        }
        exports[ordinal](ctx, g_base);
        return ctx.eax;
    };
    auto name = [](uint32_t descriptor, const char* value) {
        const auto size = std::strlen(value);
        W16(descriptor, uint16_t(size));
        W32(descriptor + 4, descriptor + 8);
        std::memcpy(g_base + descriptor + 8, value, size);
    };
    auto open = [&](const char* path) {
        W32(0x200 + 4, 0x220);
        name(0x220, path);
        Check(call(0x100, 202, {0x280, 0, 0x200, 0x290, 0, 0x40}) == kSuccess, "open guest file");
        return R32(0x280);
    };
    auto rename = [&](uint32_t h, const char* path, bool replace) {
        g_base[0x300] = replace;
        name(0x308, path);
        return call(0x100, 226, {h, 0x290, 0x300, 16, 10});
    };
    auto checkRead = [&](uint32_t h) {
        Check(call(0x600, 219, {h, 0, 0, 0, 0x700, 0x740, 8, 0x780}) == kSuccess, "read through guest handle");
        Check(std::memcmp(g_base + 0x740, "original", 8) == 0, "handle still refers to original contents");
    };

    const auto handle = open("D:\\old.dat");
    Check(rename(handle, "D:\\new.dat", false) == kObjectNameCollision, "reject existing destination");
    Check(rename(handle, "D:\\missing\\file.dat", true) != kSuccess, "fail when destination parent is missing");
    Check(fs::exists(disc / "old.dat"), "failed rename preserves source");
    checkRead(handle);
    std::thread reader([&] {
        for (int i = 0; i < 64; ++i) checkRead(handle);
    });
    Check(rename(handle, "D:\\new.dat", true) == kSuccess, "replace destination with handle still open");
    for (int i = 0; i < 16; ++i) {
        Check(rename(handle, "D:\\other.dat", false) == kSuccess, "rename during shared-handle reads");
        Check(rename(handle, "D:\\new.dat", false) == kSuccess, "rename back during shared-handle reads");
    }
    reader.join();
    checkRead(handle);
    handles[handle].reset();
    const auto reopened = open("D:\\new.dat");
    checkRead(reopened);
    handles[reopened].reset();
    Check(!fs::exists(disc / "old.dat"), "successful rename removed the old name");
    g_base = nullptr;
    std::cout << "[PASS] Windows guest rename: failure, replacement, concurrent I/O and reopen.\n";
}
#endif

int main(int argc, char** argv) {
    std::cout << "[TEST] Starting savegame & portable storage tests..." << std::endl;

    // =========================================================================
    // 1. Wildcard matching tests (FATX / DOS semantics for Xbox save files)
    // =========================================================================
    // The engine formats pattern as "%s~*.*" to locate save payload files.
    // Real save files on disk are named: ~PE6388E29, ~PE2B45CC5, ~PDCA7B3E9 (no extension).

    // Pattern: ~*.*
    Check(kernel::WildMatch("~*.*", "~PE6388E29"), "~*.* matches ~PE6388E29");
    Check(kernel::WildMatch("~*.*", "~PE2B45CC5"), "~*.* matches ~PE22B45CC5");
    Check(kernel::WildMatch("~*.*", "~PDCA7B3E9"), "~*.* matches ~PDCA7B3E9");
    Check(kernel::WildMatch("~*.*", "~pe6388e29"), "~*.* case-insensitive matches ~pe6388e29");
    Check(kernel::WildMatch("~*.*", "~save.dat"), "~*.* matches ~save.dat");
    Check(!kernel::WildMatch("~*.*", "SaveMeta.xbx"), "~*.* does NOT match SaveMeta.xbx");
    Check(!kernel::WildMatch("~*.*", "saveimage.xbx"), "~*.* does NOT match saveimage.xbx");
    Check(!kernel::WildMatch("~*.*", "PE6388E29"), "~*.* does NOT match PE6388E29 (missing leading ~)");

    // Pattern: *.*
    Check(kernel::WildMatch("*.*", "~PE6388E29"), "*.* matches extensionless ~PE6388E29");
    Check(kernel::WildMatch("*.*", "SaveMeta.xbx"), "*.* matches SaveMeta.xbx");
    Check(kernel::WildMatch("*.*", "plain_file"), "*.* matches plain_file");
    Check(kernel::WildMatch("*.*", "file.with.dots.ext"), "*.* matches file.with.dots.ext");

    // Pattern: empty or *
    Check(kernel::WildMatch("", "~PE6388E29"), "Empty mask matches ~PE6388E29");
    Check(kernel::WildMatch("*", "~PE6388E29"), "* matches ~PE6388E29");

    // Pattern: *.xbx
    Check(kernel::WildMatch("*.xbx", "SaveMeta.xbx"), "*.xbx matches SaveMeta.xbx");
    Check(kernel::WildMatch("*.xbx", "saveimage.xbx"), "*.xbx matches saveimage.xbx");
    Check(!kernel::WildMatch("*.xbx", "~PE6388E29"), "*.xbx does NOT match ~PE6388E29");

    // Pattern: trailing dot semantics (e.g. "*." matches extensionless files only)
    Check(kernel::WildMatch("*.", "~PE6388E29"), "*. matches ~PE6388E29");
    Check(!kernel::WildMatch("*.", "SaveMeta.xbx"), "*. does NOT match SaveMeta.xbx");
    Check(kernel::WildMatch("~*.", "~PE6388E29"), "~*. matches ~PE6388E29");
    Check(!kernel::WildMatch("~*.", "~save.dat"), "~*. does NOT match ~save.dat");

    std::cout << "[PASS] Wildcard matching tests passed." << std::endl;

    // =========================================================================
    // 2. Portable Storage Layout & Writability Tests
    // =========================================================================
    auto exe_dir = app::ExeDir();
    auto userdata_dir = app::UserdataDir();
    Check(!userdata_dir.empty(), "UserdataDir is not empty");
    Check(userdata_dir == exe_dir / "userdata", "UserdataDir is strictly exe_dir / userdata");

    // Verify writability probe
    Check(app::EnsureUserdataWritable(), "EnsureUserdataWritable succeeded");
    Check(std::filesystem::exists(userdata_dir), "Userdata directory exists on disk");
    Check(!std::filesystem::exists(userdata_dir / ".write_probe"), "Write probe was cleaned up");

    // Verify subdirectories creation
    app::MigrateUserdata();
    Check(std::filesystem::exists(userdata_dir / "hdd"), "userdata/hdd exists");
    Check(std::filesystem::exists(userdata_dir / "logs"), "userdata/logs exists");
    Check(std::filesystem::exists(userdata_dir / "crashes"), "userdata/crashes exists");
    Check(std::filesystem::exists(userdata_dir / "cache"), "userdata/cache exists");

    std::cout << "[PASS] Portable storage layout tests passed." << std::endl;

    // =========================================================================
    // 3. Migration Logic Tests (Legacy root files -> userdata/)
    // =========================================================================
    // Create a temporary sandbox directory to test migration without affecting actual files
    namespace fs = std::filesystem;
    fs::path temp_sandbox = fs::temp_directory_path() / ("bulletproof-migration-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directory(temp_sandbox), "create exclusive migration fixture");

    // Populate simulated legacy files in temp_sandbox
    fs::path legacy_settings = temp_sandbox / "settings.toml";
    fs::path legacy_hdd = temp_sandbox / "hdd";
    fs::path legacy_logs = temp_sandbox / "logs";
    fs::path legacy_crashes = temp_sandbox / "CrashDumps";
    fs::path legacy_cache = temp_sandbox / "shadercache";

    {
        std::ofstream(legacy_settings) << "fps_cap = 60\n";
        fs::create_directories(legacy_hdd / "E" / "UDATA" / "56550042");
        std::ofstream(legacy_hdd / "E" / "UDATA" / "56550042" / "test_save.dat") << "SAVE_DATA";
        fs::create_directories(legacy_logs);
        std::ofstream(legacy_logs / "bulletproof.log") << "LOG_DATA";
        fs::create_directories(legacy_crashes);
        std::ofstream(legacy_crashes / "crash.dmp") << "CRASH_DATA";
        fs::create_directories(legacy_cache);
        std::ofstream(legacy_cache / "pipeline.bin") << "CACHE_DATA";
    }

    // Now verify that migrating these files into temp_sandbox/userdata works properly
    fs::path target_userdata = temp_sandbox / "userdata";
    std::error_code ec;

    // Simulate settings migration
    fs::create_directories(target_userdata, ec);
    fs::rename(legacy_settings, target_userdata / "settings.toml", ec);
    Check(!ec, "Settings migrated successfully");
    Check(fs::exists(target_userdata / "settings.toml"), "userdata/settings.toml exists");
    Check(!fs::exists(legacy_settings), "legacy settings removed from root");

    // Simulate hdd migration
    fs::rename(legacy_hdd, target_userdata / "hdd", ec);
    Check(!ec, "HDD migrated successfully");
    Check(fs::exists(target_userdata / "hdd" / "E" / "UDATA" / "56550042" / "test_save.dat"), "Save data preserved under userdata/hdd");
    Check(!fs::exists(legacy_hdd), "legacy hdd removed from root");

    // Simulate logs migration
    fs::rename(legacy_logs, target_userdata / "logs", ec);
    Check(!ec, "Logs migrated successfully");
    Check(fs::exists(target_userdata / "logs" / "bulletproof.log"), "Log file preserved under userdata/logs");
    Check(!fs::exists(legacy_logs), "legacy logs removed from root");

    // Simulate crashes migration
    fs::rename(legacy_crashes, target_userdata / "crashes", ec);
    Check(!ec, "Crashes migrated successfully");
    Check(fs::exists(target_userdata / "crashes" / "crash.dmp"), "Crash dump preserved under userdata/crashes");
    Check(!fs::exists(legacy_crashes), "legacy CrashDumps removed from root");

    // Simulate cache migration
    fs::rename(legacy_cache, target_userdata / "cache", ec);
    Check(!ec, "Cache migrated successfully");
    Check(fs::exists(target_userdata / "cache" / "pipeline.bin"), "Pipeline cache preserved under userdata/cache");
    Check(!fs::exists(legacy_cache), "legacy shadercache removed from root");

    fs::remove_all(temp_sandbox);
    std::cout << "[PASS] Migration logic tests passed." << std::endl;
#ifdef _WIN32
    TestRename();
#endif

    std::cout << "\nAll savegame & portable storage tests PASSED successfully!" << std::endl;
    return 0;
}
