// The executable owns the runtime and stays loaded
// for the entire lifetime of the guest, including every guest thread.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "app/portable_guest_abi.h"
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include "SDL_render_debug_font.h"

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/crypto.h"

const X86FuncMapping* X86FuncMappings = nullptr;
const uint32_t* X86Continuations = nullptr;
uint32_t X86CodeBase, X86CodeEnd, X86EntryPoint;

bool BindGuestFunctions(void* module);
extern const char kImageSha1[];
#ifdef _WIN32
const PortableGuestServices& GuestHostServices();
#else
extern char** environ;
#endif

namespace app {
namespace {
namespace fs = std::filesystem;

enum class Stage { Checking, Translating, Compiling, Linking, Starting };

struct Progress {
    std::atomic<Stage> stage{Stage::Checking};
    std::atomic<bool> cancel{false};
    std::atomic<unsigned> completed{0};
    std::atomic<unsigned> total{0};
    std::string error;

    void CheckCancellation() const {
        if (cancel) throw std::runtime_error("Preparation cancelled.");
    }
};

class Fingerprint {
public:
    Fingerprint() { crypto::ShaInit(context_); }

    void Add(std::string_view text) {
        crypto::ShaUpdate(context_, reinterpret_cast<const uint8_t*>(text.data()), text.size());
        static constexpr uint8_t separator = 0;
        crypto::ShaUpdate(context_, &separator, 1);
    }

    void File(const fs::path& path, const Progress& progress) {
        Add(std::to_string(fs::file_size(path)));
        std::ifstream f(path, std::ios::binary);
        std::array<uint8_t, 65536> bytes;
        while (f.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) || f.gcount()) {
            progress.CheckCancellation();
            crypto::ShaUpdate(context_, bytes.data(), f.gcount());
        }
        if (!f.eof() || f.bad()) throw std::runtime_error("Could not read " + path.filename().string());
    }

    std::string Finish() {
        uint8_t digest[20];
        crypto::ShaFinal(context_, digest);
        std::string result;
        for (auto b : digest) {
            result += "0123456789abcdef"[b >> 4];
            result += "0123456789abcdef"[b & 15];
        }
        return result;
    }

private:
    alignas(8) uint8_t context_[128];
};

std::string CacheKey(const fs::path& compiler, const Progress& progress) {
    Fingerprint fingerprint;
#ifdef _WIN32
    fingerprint.Add("bulletproof-guest-pe-msvc-x64-v" + std::to_string(kPortableGuestAbi));
    std::wstring exe(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (!length || length >= exe.size()) throw std::runtime_error("Could not locate the runtime executable.");
    exe.resize(length);
#else
    fingerprint.Add("bulletproof-guest-elf-v1");
    const fs::path exe = "/proc/self/exe";
#endif
    fingerprint.Add(kImageSha1);
    fingerprint.File(exe, progress);
    std::vector<fs::path> inputs;
    for (const auto& file : fs::recursive_directory_iterator(compiler))
        if (file.is_regular_file()) inputs.push_back(file.path().lexically_relative(compiler));
    std::sort(inputs.begin(), inputs.end());
    for (const auto& input : inputs) {
        fingerprint.Add(input.generic_string());
        fingerprint.File(compiler / input, progress);
    }
    return fingerprint.Finish();
}

std::string ModuleHash(const fs::path& module, const Progress& progress) {
    Fingerprint fingerprint;
    fingerprint.File(module, progress);
    return fingerprint.Finish();
}

#ifdef _WIN32
class WinHandle {
public:
    explicit WinHandle(HANDLE handle) : handle_(handle) {}
    ~WinHandle() { if (*this) CloseHandle(handle_); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return handle_; }
private:
    HANDLE handle_;
};

class BuildLock {
public:
    BuildLock(const fs::path& path, const Progress& progress)
        : file_(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (!file_) throw std::runtime_error("cannot open guest build lock");
        OVERLAPPED range{};
        while (!LockFileEx(file_.Get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) {
            if (GetLastError() != ERROR_LOCK_VIOLATION) throw std::runtime_error("cannot lock guest build directory");
            progress.CheckCancellation();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
private:
    WinHandle file_;
};

std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        quoted.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"') quoted += L'\\';
        quoted += c;
        slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L'"';
}

bool Run(const fs::path&, const Progress& progress, const std::vector<std::string>& args) {
    if (progress.cancel) return false;
    const auto cwd = ExeDir();
    const auto exe = cwd / args.front();
    std::wstring command;
    for (const auto& arg : args) {
        if (!command.empty()) command += L' ';
        command += QuoteArgument(fs::path(arg).wstring());
    }
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    const auto log_path = UserdataDir() / "logs" / "guest-build.log";
    WinHandle log(CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    WinHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log || !input) throw std::runtime_error("cannot redirect the guest compiler output");
    WinHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("cannot create the guest compiler job");

    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    STARTUPINFOEXW startup{};
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
        throw std::runtime_error("cannot initialize guest compiler process attributes");
    HANDLE handles[]{input.Get(), log.Get()};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.Get();
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log.Get();
    PROCESS_INFORMATION process{};
    const bool started =
        UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                  sizeof(handles), nullptr, nullptr) &&
        CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                       cwd.c_str(), &startup.StartupInfo, &process);
    const auto error = started ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!started) {
        LOGE("guest: cannot spawn %s (error %lu)", args.front().c_str(), error);
        return false;
    }
    WinHandle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.Get(), child.Get()) || ResumeThread(thread.Get()) == DWORD(-1)) {
        LOGE("guest: cannot start job for %s (error %lu)", args.front().c_str(), GetLastError());
        TerminateProcess(child.Get(), 1);
        WaitForSingleObject(child.Get(), INFINITE);
        return false;
    }
    DWORD status;
    while ((status = WaitForSingleObject(child.Get(), 25)) == WAIT_TIMEOUT) {
        if (!progress.cancel) continue;
        TerminateJobObject(job.Get(), 1);
        WaitForSingleObject(child.Get(), INFINITE);
        return false;
    }
    DWORD code = 1;
    if (status == WAIT_OBJECT_0 && GetExitCodeProcess(child.Get(), &code) && code == 0) return true;
    LOGE("guest: %s failed (exit %lu)", args.front().c_str(), code);
    return false;
}

std::string ToolPath(const fs::path& path) {
    return path.lexically_relative(ExeDir()).generic_string();
}
constexpr auto kGuestName = "guest.dll";
constexpr auto kRecompiler = "xbe_recomp.exe";
#else
class BuildLock {
public:
    BuildLock(const fs::path& path, const Progress& progress) : fd_(open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600)) {
        if (fd_ < 0) throw std::runtime_error("cannot open guest build lock");
        int result;
        do {
            result = flock(fd_, LOCK_EX | LOCK_NB);
            if (result == 0 || (errno != EWOULDBLOCK && errno != EINTR) || progress.cancel) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (true);
        if (result < 0) {
            close(fd_);
            throw std::runtime_error("cannot lock guest build directory");
        }
    }
    ~BuildLock() { close(fd_); }
    BuildLock(const BuildLock&) = delete;
    BuildLock& operator=(const BuildLock&) = delete;
private:
    int fd_;
};

bool Run(const fs::path& compiler, const Progress& progress, std::vector<std::string> args) {
    if (progress.cancel) return false;
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    std::string library_path = "LD_LIBRARY_PATH=" + (compiler / "lib").string();
    std::vector<char*> env{library_path.data()};
    for (char** e = environ; *e; ++e)
        if (!std::string_view(*e).starts_with("LD_LIBRARY_PATH=")) env.push_back(*e);
    env.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions)) return false;
    if (posix_spawnattr_init(&attributes)) {
        posix_spawn_file_actions_destroy(&actions);
        return false;
    }
    const auto log = UserdataDir() / "logs" / "guest-build.log";
    pid_t pid;
    int error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (!error) error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    if (!error) error = posix_spawnattr_setpgroup(&attributes, 0);
    if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (!error) error = posix_spawn(&pid, argv.front(), &actions, &attributes, argv.data(), env.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        LOGE("guest: cannot spawn %s (error %d)", argv.front(), error);
        return false;
    }
    int status;
    pid_t result;
    do {
        if (progress.cancel) kill(-pid, SIGKILL);
        result = waitpid(pid, &status, WNOHANG);
        if (result == 0) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (result == 0 || (result < 0 && errno == EINTR));
    if (result == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
    LOGE("guest: %s failed", argv.front());
    return false;
}

std::string ToolPath(const fs::path& path) { return path.string(); }
constexpr auto kGuestName = "guest.so";
constexpr auto kRecompiler = "xbe_recomp";
#endif

void Build(const GameFiles& game, const fs::path& compiler, const fs::path& work, Progress& progress) {
    progress.CheckCancellation();
    progress.stage = Stage::Translating;
    LOGR("guest: translating game code");
    fs::create_directories(work);
    const auto xbe = work / "default.xbe";
    {
        std::ofstream f(xbe, std::ios::binary | std::ios::trunc);
        if (!f.write(reinterpret_cast<const char*>(game.xbe.data()), game.xbe.size()))
            throw std::runtime_error("cannot write the local XBE for the recompiler");
        f.close();
        if (!f) throw std::runtime_error("cannot close the local XBE");
    }
    const bool translated = Run(compiler, progress, {ToolPath(compiler / kRecompiler), ToolPath(compiler / "config/game.toml"),
                                          ToolPath(xbe), ToolPath(work), "--xdk", ToolPath(compiler / "xdk")});
    fs::remove(xbe);
    if (!translated) throw std::runtime_error("XBE translation failed");

    std::vector<fs::path> sources{work / "x86_func_mapping.cpp"};
    for (const auto& file : fs::directory_iterator(work))
        if (file.path().extension() == ".cpp" && file.path().filename().string().starts_with("x86_recomp."))
            sources.push_back(file.path());
    if (sources.size() == 1) throw std::runtime_error("no guest partitions generated");

#ifdef _WIN32
    constexpr std::array project_libraries{"xbe-vcruntime.lib", "xbe-ucrt-heap.lib", "xbe-ucrt-math.lib"};
    constexpr std::array reference_libraries{"msvcrt.lib", "vcruntime.lib", "ucrt.lib"};
    const auto lib_dir = compiler / "lib";
    const bool project_crt = std::any_of(project_libraries.begin(), project_libraries.end(),
        [&](const char* name) { return fs::exists(lib_dir / name); });
    const auto crt_source = compiler / "portable_guest_crt.cc";
    sources.push_back(compiler / "portable_guest_module.cc");
    if (project_crt) sources.push_back(crt_source);
#endif
    progress.total = sources.size();
    progress.stage = Stage::Compiling;
    LOGR("guest: compiling %u translation units", progress.total.load());
    const auto temporary = work / kGuestName;
#ifdef _WIN32
    std::vector<std::string> link{ToolPath(compiler / "lld-link.exe"), "/dll", "/noentry", "/nodefaultlib",
        "/machine:x64", "/noimplib", "/out:" + ToolPath(temporary), "/def:" + ToolPath(compiler / "guest.def")};
    for (const auto* library : project_crt ? project_libraries : reference_libraries) {
        const auto path = lib_dir / library;
        if (!fs::is_regular_file(path)) throw std::runtime_error("Missing guest link input: " + ToolPath(path));
        link.push_back(ToolPath(path));
    }
#else
    std::vector<std::string> link{(compiler / "ld.lld").string(), "-shared", "--eh-frame-hdr", "-z", "now", "-o", temporary.string()};
#endif
    std::vector<std::future<bool>> jobs;
    const size_t parallel = std::clamp(std::thread::hardware_concurrency(), 1u, 4u);
    auto wait = [&] {
        bool ok = true;
        for (auto& job : jobs) ok = job.get() && ok;
        jobs.clear();
        if (!ok) throw std::runtime_error("guest compilation failed");
    };
    for (const auto& source : sources) {
        auto object = work / source.filename().replace_extension(".o");
        link.push_back(ToolPath(object));
        jobs.push_back(std::async(std::launch::async, [&, source, object] {
#ifdef _WIN32
            bool ok = Run(compiler, progress, {ToolPath(compiler / "clang.exe"), "--target=x86_64-pc-windows-msvc",
                "-std=c++20", "-O2", "-march=x86-64-v2", "-fexceptions", "-fcxx-exceptions", "-fno-strict-aliasing",
                // Avoid recursive RTTI emission for the CRT's own type_info class.
                source == crt_source ? "-fno-rtti" : "-frtti",
                "--no-default-config", "-fno-crash-diagnostics", "-nostdinc", "-w",
                "-I", ToolPath(compiler / "include"), "-I", ToolPath(compiler),
                "-isystem", ToolPath(compiler / "freestanding"), "-include", ToolPath(compiler / "guest_hook_defines.h"),
                "-c", ToolPath(source), "-o", ToolPath(object)});
#else
            bool ok = Run(compiler, progress, {(compiler / "clang").string(), "--target=x86_64-linux-gnu", "-std=c++20", "-O2",
                                  "-march=x86-64-v2", "-fPIC", "-fexceptions", "-fno-strict-aliasing", "-nostdinc",
                                  "-nostdinc++", "-w", "-I", (compiler / "include").string(), "-isystem",
                                  (compiler / "freestanding").string(), "-c", source.string(), "-o", object.string()});
#endif
            if (ok) ++progress.completed;
            return ok;
        }));
        if (jobs.size() == parallel) wait();
    }
    wait();
    progress.CheckCancellation();
    progress.stage = Stage::Linking;
    LOGR("guest: linking game code");
    if (!Run(compiler, progress, std::move(link))) throw std::runtime_error("Guest module link failed.");
}

#ifdef _WIN32
bool Load(const fs::path& path, bool retain = true) {
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        LOGE("guest: cannot load %ls (error %lu)", path.c_str(), GetLastError());
        return false;
    }
    const auto bind = reinterpret_cast<PortableGuestBind*>(GetProcAddress(module, "XbeBindGuest"));
    const auto probe = reinterpret_cast<PortableGuestProbe*>(GetProcAddress(module, "XbeProbeGuest"));
    auto check_unwind = [&] {
        if (!probe) return false;
        volatile bool unwound = false;
        try {
            struct Guard {
                volatile bool& unwound;
                ~Guard() { unwound = true; }
            } guard{unwound};
            probe([] { throw X86Resume{kPortableGuestAbi, 0}; });
        } catch (const X86Resume& r) {
            return unwound && r.target == kPortableGuestAbi && r.esp == 1;
        } catch (...) {
            return false;
        }
        return false;
    };
    PortableGuestImage image{};
    if (!bind || !bind(kPortableGuestAbi, &GuestHostServices(), &image) ||
        !image.mappings || !image.continuations || image.code_end <= image.code_base ||
        image.entry_point < image.code_base || image.entry_point >= image.code_end ||
        !BindGuestFunctions(module) || !check_unwind()) {
        LOGE("guest: incompatible dispatch, hooks or unwind ABI in %ls", path.c_str());
        FreeLibrary(module);
        return false;
    }
    if (retain) {
        X86FuncMappings = image.mappings;
        X86Continuations = image.continuations;
        X86CodeBase = image.code_base;
        X86CodeEnd = image.code_end;
        X86EntryPoint = image.entry_point;
        LOGR("guest: loaded %ls (PE x64, unwind verified)", path.c_str());
    } else {
        FreeLibrary(module);
    }
    return true;
}

fs::path CachedModule(const fs::path& root, const std::string& hash) {
    return root / ("guest-" + hash + ".dll");
}
#else
bool Load(const fs::path& path) {
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        LOGE("guest: dlopen %s: %s", path.c_str(), dlerror());
        return false;
    }
    auto scalar = [&](const char* name, uint32_t& value) {
        auto* address = static_cast<const uint32_t*>(dlsym(module, name));
        if (!address) return false;
        value = *address;
        return true;
    };
    X86FuncMappings = static_cast<const X86FuncMapping*>(dlsym(module, "X86FuncMappings"));
    X86Continuations = static_cast<const uint32_t*>(dlsym(module, "X86Continuations"));
    if (!X86FuncMappings || !X86Continuations || !scalar("X86CodeBase", X86CodeBase) ||
        !scalar("X86CodeEnd", X86CodeEnd) || !scalar("X86EntryPoint", X86EntryPoint) ||
        X86CodeEnd <= X86CodeBase || !BindGuestFunctions(module)) {
        LOGE("guest: incompatible dispatch or hook symbols in %s", path.c_str());
        dlclose(module);
        return false;
    }
    LOGR("guest: loaded %s", path.c_str());
    return true;
}
fs::path CachedModule(const fs::path& root, const std::string&) { return root / kGuestName; }
#endif

bool Prepare(GameFiles& game, Progress& progress) {
    if (!FindGame(game)) throw std::runtime_error(game.disc_error);
    progress.CheckCancellation();
    const auto root = UserdataDir() / "guest";
    const auto compiler = ExeDir() / "compiler";
    fs::create_directories(root);
    BuildLock lock(root / "build.lock", progress);
    const auto work = root / "build";
    const auto stamp = root / "guest.key";
    const auto key = CacheKey(compiler, progress);
    std::string saved_key, saved_hash;
    if (std::ifstream f(stamp); f >> saved_key >> saved_hash && saved_key == key &&
        saved_hash.size() == 40 && saved_hash.find_first_not_of("0123456789abcdef") == std::string::npos) {
        const auto module = CachedModule(root, saved_hash);
        if (fs::is_regular_file(module) && saved_hash == ModuleHash(module, progress) && Load(module)) return true;
    }

    fs::remove_all(work);
    std::ofstream(UserdataDir() / "logs" / "guest-build.log", std::ios::trunc);
    LOGR("guest: preparing local game code");
    try {
        Build(game, compiler, work, progress);
        const auto temporary = work / kGuestName;
        const auto hash = ModuleHash(temporary, progress);
        const auto module = CachedModule(root, hash);
        {
            std::ofstream f(work / "guest.key");
            f << key << '\n' << hash << '\n';
            f.close();
            if (!f) throw std::runtime_error("Could not write the guest cache manifest.");
        }
        progress.CheckCancellation();
#ifdef _WIN32
        // A mapped PE cannot be replaced. Verify the candidate, unload it, then
        // publish by digest so another running instance can keep its old image.
        if (!Load(temporary, false)) throw std::runtime_error("Could not load the prepared game code.");
        if (!fs::is_regular_file(module) || ModuleHash(module, progress) != hash)
            fs::rename(temporary, module);
        if (!Load(module)) throw std::runtime_error("Could not load the published game code.");
#else
        if (!Load(temporary)) throw std::runtime_error("Could not load the prepared game code.");
        fs::rename(temporary, module);
#endif
        fs::rename(work / "guest.key", stamp);
        fs::remove_all(work);
        LOGR("guest: published %s", module.filename().string().c_str());
        return true;
    } catch (...) {
        fs::remove_all(work);
        throw;
    }
}

void PrepText(SDL_Surface* surface, int x, int y, std::string_view text, int scale, Uint32 color) {
    for (unsigned char c : text) {
        if (c >= 33 && c <= 126) {
            const auto* glyph = SDL_RenderDebugTextFontData + (c - 33) * 8;
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    if (!(glyph[row] & (1 << col))) continue;
                    SDL_Rect pixel{x + col * scale, y + row * scale, scale, scale};
                    SDL_FillSurfaceRect(surface, &pixel, color);
                }
            }
        }
        x += 8 * scale;
    }
}

bool RenderProgress(SDL_Window* window, const Progress& progress) {
    auto* surface = SDL_GetWindowSurface(window);
    if (!surface) return false;
    auto rgb = [&](Uint8 r, Uint8 g, Uint8 b) { return SDL_MapSurfaceRGB(surface, r, g, b); };
    SDL_FillSurfaceRect(surface, nullptr, rgb(17, 22, 30));
    SDL_Rect panel{16, 16, 528, 152};
    SDL_FillSurfaceRect(surface, &panel, rgb(36, 48, 64));
    PrepText(surface, 32, 32, "50 CENT: BULLETPROOF - PC PORT", 2, rgb(240, 222, 172));
    constexpr std::array labels{"Checking game disc...", "Translating game code...", "Compiling game code...",
                                 "Linking game code...", "Starting game..."};
    const auto stage = progress.stage.load();
    const auto complete = progress.completed.load();
    const auto total = progress.total.load();
    PrepText(surface, 32, 67, progress.cancel ? "Cancelling preparation..." : labels[static_cast<int>(stage)],
             2, rgb(243, 245, 250));
    if (stage == Stage::Compiling && total) {
        SDL_Rect bar{32, 103, 496, 8};
        SDL_FillSurfaceRect(surface, &bar, rgb(13, 20, 29));
        bar.w = bar.w * complete / total;
        SDL_FillSurfaceRect(surface, &bar, rgb(140, 169, 199));
        PrepText(surface, 32, 120, std::to_string(complete) + " / " + std::to_string(total) + " files",
                 1, rgb(213, 224, 235));
    }
    PrepText(surface, 32, 145, "Esc or close window to cancel", 1, rgb(213, 224, 235));
    return SDL_UpdateWindowSurface(window);
}
}  // namespace

bool PreparePortableGuest(GameFiles& game) {
    const char* app_name = SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING);
    const std::string title = app_name ? app_name : "Bulletproof";
    Progress progress;
    const bool first = !fs::exists(UserdataDir() / "guest" / "guest.key");
    const bool video = SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window* window = nullptr;
    bool window_attempted = false;
    bool presented = false;
    bool present_error_logged = false;
    auto present = [&] {
        if (RenderProgress(window, progress)) {
            if (!presented) LOGR("guest: preparation frame presented via SDL %s", SDL_GetCurrentVideoDriver());
            presented = true;
        } else if (!present_error_logged) {
            LOGE("guest: cannot present preparation window: %s", SDL_GetError());
            present_error_logged = true;
        }
    };
    auto open = [&] {
        if (video && !window_attempted) {
            window_attempted = true;
            window = SDL_CreateWindow((title + " – Game Preparation").c_str(), 560, 184, 0);
            if (!window) LOGE("guest: preparation window: %s", SDL_GetError());
        }
    };
    if (first) {
        open();
        if (window) present();
    }
    auto worker = std::async(std::launch::async, [&] {
        try {
            return Prepare(game, progress);
        } catch (const std::exception& e) {
            progress.error = e.what();
            LOGE("guest: %s", e.what());
            return false;
        }
    });
    while (worker.wait_for(std::chrono::milliseconds(window ? 0 : 20)) != std::future_status::ready) {
        if (!window && progress.stage != Stage::Checking && !progress.cancel) open();
        if (!window) continue;
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 20)) {
            do {
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(window) &&
                     event.key.key == SDLK_ESCAPE))
                    progress.cancel = true;
            } while (SDL_PollEvent(&event));
        }
        present();
    }
    bool ok = worker.get() && !progress.cancel;
    if (ok && window) {
        progress.stage = Stage::Starting;
        present();
    } else if (!ok && !progress.cancel && video) {
        auto message = progress.error + "\n\nSee userdata/logs/ for details.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, (title + " – Preparation Failed").c_str(), message.c_str(), window);
    }
    if (window) SDL_DestroyWindow(window);
    if (video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ok;
}
}  // namespace app
