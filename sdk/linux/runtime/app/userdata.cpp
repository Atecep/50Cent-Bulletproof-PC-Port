#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "app/app.h"

namespace app {

std::filesystem::path ExeDir() {
#if defined(_WIN32)
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path();
#else
    char self[4096] = {};
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) return std::filesystem::current_path();
    return std::filesystem::path(std::string(self, size_t(n))).parent_path();
#endif
}

std::filesystem::path UserdataDir() {
    return ExeDir() / "userdata";
}

bool EnsureUserdataWritable() {
    auto udir = UserdataDir();
    std::error_code ec;
    if (!std::filesystem::exists(udir, ec)) {
        std::filesystem::create_directories(udir, ec);
        if (ec) return false;
    }
    // Test write permission by writing and removing a temporary probe file
    auto probe = udir / ".write_probe";
    {
        std::ofstream f(probe);
        if (!f.is_open()) return false;
        f << "probe\n";
    }
    std::filesystem::remove(probe, ec);
    return true;
}

void MigrateUserdata() {
    auto root = ExeDir();
    auto userdata = UserdataDir();
    std::error_code ec;

    // 1. Settings migration: root / kGame.settings -> userdata / kGame.settings
    if (kGame.settings) {
        auto old_settings = root / kGame.settings;
        auto new_settings = userdata / kGame.settings;
        if (std::filesystem::exists(old_settings, ec) && !std::filesystem::exists(new_settings, ec)) {
            std::filesystem::create_directories(new_settings.parent_path(), ec);
            std::filesystem::rename(old_settings, new_settings, ec);
            if (ec) {
                std::filesystem::copy_file(old_settings, new_settings, std::filesystem::copy_options::overwrite_existing, ec);
                std::filesystem::remove(old_settings, ec);
            }
        }
    }

    // 2. HDD / Save data migration: root / "hdd" -> userdata / "hdd"
    // Also handle instance hdd if present ("hdd1", "hdd2", etc.)
    for (int inst = 0; inst <= 8; inst++) {
        std::string hdd_name = inst > 1 ? ("hdd" + std::to_string(inst)) : "hdd";
        auto old_hdd = root / hdd_name;
        auto new_hdd = userdata / hdd_name;
        if (std::filesystem::exists(old_hdd, ec) && !std::filesystem::exists(new_hdd, ec)) {
            std::filesystem::create_directories(userdata, ec);
            std::filesystem::rename(old_hdd, new_hdd, ec);
            if (ec) {
                std::filesystem::copy(old_hdd, new_hdd, std::filesystem::copy_options::recursive, ec);
                std::filesystem::remove_all(old_hdd, ec);
            }
        }
    }

    // 3. Logs migration: root / "logs" -> userdata / "logs"
    auto old_logs = root / "logs";
    auto new_logs = userdata / "logs";
    if (std::filesystem::exists(old_logs, ec) && !std::filesystem::exists(new_logs, ec)) {
        std::filesystem::create_directories(userdata, ec);
        std::filesystem::rename(old_logs, new_logs, ec);
        if (ec) {
            std::filesystem::copy(old_logs, new_logs, std::filesystem::copy_options::recursive, ec);
            std::filesystem::remove_all(old_logs, ec);
        }
    }

    // 4. Crash dumps migration: root / "CrashDumps" (or "crashes") -> userdata / "crashes"
    auto old_crashes = root / "CrashDumps";
    auto new_crashes = userdata / "crashes";
    if (std::filesystem::exists(old_crashes, ec) && !std::filesystem::exists(new_crashes, ec)) {
        std::filesystem::create_directories(userdata, ec);
        std::filesystem::rename(old_crashes, new_crashes, ec);
        if (ec) {
            std::filesystem::copy(old_crashes, new_crashes, std::filesystem::copy_options::recursive, ec);
            std::filesystem::remove_all(old_crashes, ec);
        }
    }

    // 5. Shader cache migration: root / "shadercache" -> userdata / "cache"
    auto old_cache = root / "shadercache";
    auto new_cache = userdata / "cache";
    if (std::filesystem::exists(old_cache, ec) && !std::filesystem::exists(new_cache, ec)) {
        std::filesystem::create_directories(userdata, ec);
        std::filesystem::rename(old_cache, new_cache, ec);
        if (ec) {
            std::filesystem::copy(old_cache, new_cache, std::filesystem::copy_options::recursive, ec);
            std::filesystem::remove_all(old_cache, ec);
        }
    }

    // Check if CWD is different from root, and migrate if files were created in CWD
    auto cwd = std::filesystem::current_path(ec);
    if (!ec && cwd != root && cwd != userdata) {
        if (kGame.settings) {
            auto cwd_settings = cwd / kGame.settings;
            auto new_settings = userdata / kGame.settings;
            if (std::filesystem::exists(cwd_settings, ec) && !std::filesystem::exists(new_settings, ec)) {
                std::filesystem::create_directories(new_settings.parent_path(), ec);
                std::filesystem::rename(cwd_settings, new_settings, ec);
            }
        }
        auto cwd_hdd = cwd / "hdd";
        auto new_hdd = userdata / "hdd";
        if (std::filesystem::exists(cwd_hdd, ec) && !std::filesystem::exists(new_hdd, ec)) {
            std::filesystem::create_directories(userdata, ec);
            std::filesystem::rename(cwd_hdd, new_hdd, ec);
        }
        auto cwd_logs = cwd / "logs";
        auto new_logs_dir = userdata / "logs";
        if (std::filesystem::exists(cwd_logs, ec) && !std::filesystem::exists(new_logs_dir, ec)) {
            std::filesystem::create_directories(userdata, ec);
            std::filesystem::rename(cwd_logs, new_logs_dir, ec);
        }
    }

    // Create standard subdirectories so they always exist
    std::filesystem::create_directories(userdata / "hdd", ec);
    std::filesystem::create_directories(userdata / "logs", ec);
    std::filesystem::create_directories(userdata / "crashes", ec);
    std::filesystem::create_directories(userdata / "cache", ec);
}

}  // namespace app
