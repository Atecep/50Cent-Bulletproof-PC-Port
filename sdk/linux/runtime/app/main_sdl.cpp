// The host on Linux and Windows Vulkan: SDL3 window, event loop and boot.
#if !defined(_WIN32) || defined(XBE_VK_TARGET)

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <SDL3/SDL_main.h>
#endif
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "app/app.h"
#include "app/platform.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"  // gpu::g_capture (F3)
#include "gpu/vulkan.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

namespace app {

namespace {

SDL_Window* g_window = nullptr;
char** g_argv = nullptr;
int g_argc = 0;
// Written on the window thread (creation, resize events, ApplyNow), read on
// any thread: SDL video calls belong to the main thread, guest threads do not.
std::atomic<uint32_t> g_client_w{0}, g_client_h{0};
std::atomic<bool> g_focus{false};
std::atomic<bool> g_minimized{false};
std::atomic<bool> g_imgui_vulkan{false};
// Cross-thread requests to the window thread, mirroring main.cpp's
// kApplyDisplay / kOpenWindow window messages.
Uint32 g_apply_event = 0, g_open_event = 0;
std::atomic<DisplayApplyResult> g_display_result{DisplayApplyResult::Applied};
std::atomic<uint64_t> g_desktop_size{(uint64_t(1280) << 32) | 720};

SDL_DisplayID WindowDisplay() {
    if (g_window)
        if (const auto id = SDL_GetDisplayForWindow(g_window)) return id;
    return SDL_GetPrimaryDisplay();
}

Mode ModePixels(const SDL_DisplayMode& mode) {
    const float density = mode.pixel_density > 0 ? mode.pixel_density : 1.0f;
    return {uint32_t(std::lround(mode.w * density)), uint32_t(std::lround(mode.h * density))};
}

void CacheDesktopSize() {
    if (const auto* mode = SDL_GetDesktopDisplayMode(WindowDisplay())) {
        const auto size = ModePixels(*mode);
        g_desktop_size.store(uint64_t(size.width) << 32 | size.height, std::memory_order_relaxed);
    }
}

std::string Narrow(const wchar_t* wide) {
    std::string out;
    if (!wide) return out;
#if defined(_WIN32)
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len > 1) {
        out.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    }
#else
    for (const wchar_t* p = wide; *p; p++) {
        char32_t c = char32_t(*p);  // wchar_t is one UTF-32 code unit on Linux
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
#endif
    return out;
}

bool Relaunched() {
    for (int i = 1; i < g_argc; i++)
        if (g_argv[i] && std::strstr(g_argv[i], "--relaunched")) return true;
    return false;
}

void CacheClientSize() {
    if (!g_window) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    g_client_w = uint32_t(std::max(w, 0));
    g_client_h = uint32_t(std::max(h, 0));
}

// F3, as on Windows: dump every guest thread's stack and capture the frame.
void DumpThreads() {
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGI("thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
}

#if defined(_WIN32)
LONG WINAPI WindowsCrashFilter(EXCEPTION_POINTERS* e) {
    if (kernel::Thread* t = kernel::Current(); e && t && t->ctx) {
        auto* r = e->ExceptionRecord;
        LOGE("exception %08lX at %p on guest thread %X", r->ExceptionCode, r->ExceptionAddress, t->id);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
            uint64_t va = r->ExceptionInformation[1], g = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
            LOGE("  %s %llX, guest %08llX", r->ExceptionInformation[0] ? "write" : "read", (unsigned long long)va,
                 (unsigned long long)(va - g));
        }
        kernel::LogGuestStack(*t->ctx, 256);
    } else if (e && e->ExceptionRecord) {
        LOGE("host exception %08lX at %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    }
    logx::Flush();
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// No minidump on Linux: the guest registers and return addresses go to the
// log, then the process dies with the signal, as the Windows filter leaves
// it to the system after writing its dump.
void CrashHandler(int sig) {
    if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
        LOGE("signal %d on guest thread %X", sig, t->id);
        kernel::LogGuestStack(*t->ctx, 256);
    } else {
        LOGE("signal %d", sig);
    }
    logx::Flush();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
    _exit(128 + sig);
}

// Window thread only: put the settings' display mode on the SDL window.
// Visibility is preserved: a window held back for a splash or handover
// stays hidden until OpenGameWindow shows it.
bool SetPresentation(DisplayMode kind, Mode output, bool automatic) {
    const auto display = WindowDisplay();
    const auto* desktop = SDL_GetDesktopDisplayMode(display);
    if (!desktop) return false;

    if (kind != DisplayMode::Windowed) {
        // Borderless is the compositor's desktop fullscreen. Auto fullscreen
        // also explicitly requests the current desktop mode.
        const SDL_DisplayMode* selected = nullptr;
        SDL_DisplayMode** modes = nullptr;
        if (kind == DisplayMode::Fullscreen && !automatic) {
            if (std::strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
                // Wayland fullscreen cannot switch the physical output. SDL's
                // emulated modes change the drawable via a viewport instead.
                const auto pixels = ModePixels(*desktop);
                if (pixels.width != output.width || pixels.height != output.height)
                    return SDL_SetError("Wayland cannot switch the monitor mode. Use Borderless for this resolution.");
            } else {
                int count = 0;
                modes = SDL_GetFullscreenDisplayModes(display, &count);
                const auto* current = SDL_GetCurrentDisplayMode(display);
                const float refresh = current ? current->refresh_rate : desktop->refresh_rate;
                for (int i = 0; i < count; ++i) {
                    const auto& candidate = *modes[i];
                    const auto pixels = ModePixels(candidate);
                    if (pixels.width != output.width || pixels.height != output.height) continue;
                    if (!selected || std::abs(candidate.refresh_rate - refresh) <
                                     std::abs(selected->refresh_rate - refresh))
                        selected = &candidate;
                }
                if (!selected) {
                    SDL_free(modes);
                    return SDL_SetError("No enumerated fullscreen mode matches %ux%u on this display.",
                                        output.width, output.height);
                }
            }
        }
        const bool registered = SDL_SetWindowFullscreenMode(g_window, selected);
        SDL_free(modes);
        return registered && SDL_SetWindowFullscreen(g_window, true) && SDL_SyncWindow(g_window);
    }

    if (!SDL_SetWindowFullscreen(g_window, false) || !SDL_SyncWindow(g_window) ||
        !SDL_SetWindowFullscreenMode(g_window, nullptr) || !SDL_SetWindowBordered(g_window, true) ||
        !SDL_SetWindowResizable(g_window, true))
        return false;

    const float density = std::max(SDL_GetWindowPixelDensity(g_window), 0.1f);
    float width = std::max(1.0f, output.width / density);
    float height = std::max(1.0f, output.height / density);
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(display, &usable)) {
        const float fit = std::min({1.0f, usable.w / width, usable.h / height});
        width = std::max(1.0f, width * fit);
        height = std::max(1.0f, height * fit);
    }
    if (!SDL_SetWindowSize(g_window, int(std::lround(width)), int(std::lround(height)))) return false;
    // Wayland does not let clients position top-level windows.
    if (std::strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0)
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    return SDL_SyncWindow(g_window);
}

void ApplyNow([[maybe_unused]] bool startup = false) {
    if (!g_window) return;
    const Settings& s = Get();
    CacheDesktopSize();
    const Mode output = Output();
    const bool hidden = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN) != 0;
    const bool applied = SetPresentation(s.display_mode, output, !s.width || !s.height);
    if (!applied)
        LOGE("display: could not apply mode %u at %ux%u: %s", unsigned(s.display_mode),
             output.width, output.height, SDL_GetError());
#ifdef _WIN32
    // A failed exclusive-mode selection at startup leaves the initial window
    // at its requested size. Fit it to the desktop without changing the saved
    // preference or masking the failure from the settings UI.
    if (!applied && startup && s.display_mode == DisplayMode::Fullscreen) {
        if (SetPresentation(DisplayMode::Windowed, output, false))
            LOGR("display: unavailable startup fullscreen mode, using a fitted window");
        else
            LOGE("display: could not fit the startup window: %s", SDL_GetError());
    }
#endif
    if (hidden) SDL_HideWindow(g_window);
    CacheClientSize();
    g_display_result.store(applied ? DisplayApplyResult::Applied : DisplayApplyResult::Failed);
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    const auto* mode = SDL_GetCurrentDisplayMode(WindowDisplay());
    LOGR("display: mode %u, selected %ux%u, logical %dx%d, drawable %ux%u, monitor mode %ux%u at %.2f Hz, applied %d",
         unsigned(s.display_mode), output.width, output.height, w, h, g_client_w.load(), g_client_h.load(),
         mode ? ModePixels(*mode).width : 0, mode ? ModePixels(*mode).height : 0,
         mode ? double(mode->refresh_rate) : 0.0, int(applied));
}

struct OpenRequest {
    bool apply;
    SDL_Semaphore* done;
};

void DoOpenGameWindow(bool apply) {
    CloseSplash();
    if (apply) ApplyNow(true);
    SDL_ShowWindow(g_window);
    SDL_RaiseWindow(g_window);
}

// The Vulkan backend owns the device; once its Init has run (on a guest
// thread), finish the ImGui Vulkan backend on this thread under UiLock.
void TryInitImGuiVulkan() {
    if (g_imgui_vulkan.load(std::memory_order_relaxed) || !ImGui::GetCurrentContext()) return;
    const gpu::VulkanHandles& h = gpu::GetVulkanHandles();
    if (h.device == VK_NULL_HANDLE || h.renderPass == VK_NULL_HANDLE) return;
    std::lock_guard lock(UiLock());
    if (g_imgui_vulkan.load(std::memory_order_relaxed)) return;
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = h.instance;
    info.PhysicalDevice = h.physicalDevice;
    info.Device = h.device;
    info.QueueFamily = h.graphicsQueueFamily;
    info.Queue = h.graphicsQueue;
    info.PipelineCache = h.pipelineCache;
    info.DescriptorPool = h.descriptorPool;
    info.RenderPass = h.renderPass;
    info.Subpass = 0;
    info.MinImageCount = h.minImageCount;
    info.ImageCount = h.imageCount;
    info.MSAASamples = h.msaaSamples;
    info.UseDynamicRendering = false;
    ImGui_ImplVulkan_Init(&info);
    g_imgui_vulkan.store(true, std::memory_order_relaxed);
    LOGI("imgui: Vulkan backend ready (%u images)", h.imageCount);
}

bool HandleEvent(const SDL_Event& e) {  // window thread; true: quit
    input::DesktopEvent(e);
    if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED)
        input::DeviceChanged(e.gdevice.which, e.type == SDL_EVENT_GAMEPAD_ADDED);
    // ImGui first, under the same lock the renderer builds its frame with,
    // as ImGui_ImplWin32_WndProcHandler is in main.cpp's WndProc.
    if (ImGui::GetCurrentContext()) {
        std::lock_guard lock(UiLock());
        ImGui_ImplSDL3_ProcessEvent(&e);
    }
    // GameInfo::message is a Win32-message hook; there are no window
    // messages to offer it here, and games leave it null off Windows.
    if (e.type == g_apply_event) {
        ApplyNow();
        return false;
    }
    if (e.type == g_open_event) {
        auto* req = static_cast<OpenRequest*>(e.user.data1);
        DoOpenGameWindow(req->apply);
        SDL_SignalSemaphore(req->done);
        return false;
    }
    if (e.type == SDL_EVENT_QUIT) return true;
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(g_window)) return true;
    // Pixel size / display scale changes refresh the cached drawable size; the
    // Vulkan backend polls it (platform::ClientSize) in ResizeSwapIfNeeded and
    // recreates the swapchain at the new size - on Wayland the swapchain
    // extent is the window size, so this event is what drives fullscreen.
    if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED ||
        e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED || e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
        uint32_t was_w = g_client_w.load(std::memory_order_relaxed), was_h = g_client_h.load(std::memory_order_relaxed);
        CacheDesktopSize();
        CacheClientSize();
        if (g_client_w.load(std::memory_order_relaxed) != was_w || g_client_h.load(std::memory_order_relaxed) != was_h)
            LOGI("display: window drawable now %ux%u", g_client_w.load(std::memory_order_relaxed),
                 g_client_h.load(std::memory_order_relaxed));
    }
    if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) g_focus = true;
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) g_focus = false;
    if (e.type == SDL_EVENT_WINDOW_MINIMIZED) g_minimized = true;
    if (e.type == SDL_EVENT_WINDOW_RESTORED || e.type == SDL_EVENT_WINDOW_MAXIMIZED) g_minimized = false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.scancode == SDL_SCANCODE_F3) {
            DumpThreads();
        } else if (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
            // Alt+Enter: desktop fullscreen and back, without rewriting the
            // saved display mode (the F1 panel owns that setting).
            SDL_SetWindowFullscreen(g_window, (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) == 0);
            CacheClientSize();
        }
        // F1/F2 need no host handling: ImGui has the key from ProcessEvent
        // above and the overlay toggles itself in GameInfo::ui.
    }
    return false;
}

}  // namespace

int Instance() {
    static const int n = [] {
        auto clamp = [](int v) { return std::max(0, std::min(8, v)); };
        for (int i = 1; i < g_argc; i++) {
            if (!g_argv[i]) continue;
            // D10: the documented forms are "--instance 2" (two argv entries) and
            // "--instance=2"; the old strstr(p + 10) read the empty string / "=2"
            // for both and silently yielded instance 0. The glued "--instance2"
            // that used to be the only working form is still accepted.
            if (std::strcmp(g_argv[i], "--instance") == 0) {
                if (i + 1 < g_argc && g_argv[i + 1]) return clamp(std::atoi(g_argv[i + 1]));
                return 0;
            }
            if (std::strncmp(g_argv[i], "--instance=", 11) == 0) return clamp(std::atoi(g_argv[i] + 11));
            if (std::strncmp(g_argv[i], "--instance", 10) == 0 && g_argv[i][10] >= '0' && g_argv[i][10] <= '9')
                return clamp(std::atoi(g_argv[i] + 10));
        }
        return 0;
    }();
    return n;
}

void* Window() { return g_window; }

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void RequestQuit() {
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    if (!SDL_PushEvent(&event)) LOGE("Could not queue quit event: %s", SDL_GetError());
}

DisplayApplyResult LastDisplayApply() { return g_display_result.load(); }

void ApplyDisplay() {
    if (!g_window || !g_apply_event) return;
    g_display_result = DisplayApplyResult::Pending;
    SDL_Event e{};
    e.type = g_apply_event;
    if (!SDL_PushEvent(&e)) g_display_result = DisplayApplyResult::Failed;
}

void OpenGameWindow(bool apply) {
    if (!g_window) return;
    if (!g_open_event) {
        DoOpenGameWindow(apply);
        return;
    }
    // SendMessage on Windows: returns once the window is up.
    SDL_Semaphore* done = SDL_CreateSemaphore(0);
    OpenRequest req{apply, done};
    SDL_Event e{};
    e.type = g_open_event;
    e.user.data1 = &req;
    if (!SDL_PushEvent(&e)) {
        DoOpenGameWindow(apply);
        SDL_DestroySemaphore(done);
        return;
    }
    SDL_WaitSemaphore(done);
    SDL_DestroySemaphore(done);
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

namespace platform {

NativeWindow NativeHandle() { return g_window; }

SDL_Window* SdlWindow() { return g_window; }

Size ClientSize() { return {g_client_w.load(std::memory_order_relaxed), g_client_h.load(std::memory_order_relaxed)}; }

Size DesktopSize() {
    const auto size = g_desktop_size.load(std::memory_order_relaxed);
    return {uint32_t(size >> 32), uint32_t(size)};
}

bool HasFocus() { return g_focus.load(std::memory_order_relaxed); }

bool IsVisible() { return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN) == 0; }

bool IsMinimized() { return g_minimized.load(std::memory_order_relaxed); }

void SetVisible(bool visible) {
    if (!g_window) return;
    if (visible) SDL_ShowWindow(g_window);
    else SDL_HideWindow(g_window);
}

void Raise() {
    if (g_window) SDL_RaiseWindow(g_window);
}

void SetCursorVisible(bool visible) {
    if (visible) SDL_ShowCursor();
    else SDL_HideCursor();
}

bool CursorVisible() { return SDL_CursorVisible(); }

HdrInfo QueryHdr() {
    // No HDR swap chain exists in the Vulkan build; say so, never pretend.
    return {false, false, 0, "HDR is not available on Linux"};
}

}  // namespace platform

}  // namespace app

#if defined(XBE_WINDOWS_GUI)
static int RunGame(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    app::g_argc = argc;
    app::g_argv = argv;
#if defined(_WIN32)
    SDL_SetMainReady();
    SetUnhandledExceptionFilter(app::WindowsCrashFilter);
    for (int sig : {SIGABRT, SIGSEGV, SIGILL, SIGFPE, SIGTERM}) std::signal(sig, app::CrashHandler);
#else
    for (int sig : {SIGABRT, SIGSEGV, SIGBUS, SIGILL, SIGFPE}) std::signal(sig, app::CrashHandler);
#endif

    // Verify write permissions for userdata directory
    if (!app::EnsureUserdataWritable()) {
        std::fprintf(stderr, "FATAL: Cannot write to userdata directory: %s\nPlease ensure the game directory has write permissions.\n", app::UserdataDir().string().c_str());
        return 1;
    }

    // Auto-migrate legacy files from game root or CWD into userdata/
    app::MigrateUserdata();

    // One log a session: a relaunch into another image carries on its predecessor's.
    bool relaunched = app::Relaunched();
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    logx::Init((app::UserdataDir() / "logs").string(), log, relaunched);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();

    app::GameFiles game;
#ifdef XBE_PORTABLE_GUEST
    if (!app::PreparePortableGuest(game)) return 1;
#else
    if (!app::FindGame(game)) return 1;
#endif

    bool splash = app::OpenSplash(), handover = app::StartHandover();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOGE("sdl: video failed to start: %s", SDL_GetError());
        return 1;
    }
    app::g_apply_event = SDL_RegisterEvents(2);
    app::g_open_event = app::g_apply_event + 1;

    app::CacheDesktopSize();
    const app::Mode out = app::Output();
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (splash || handover) flags |= SDL_WINDOW_HIDDEN;
    std::string title = app::Narrow(app::kGame.title);
    app::g_window = SDL_CreateWindow(title.c_str(), int(std::max(out.width, 1u)), int(std::max(out.height, 1u)), flags);
    if (!app::g_window) {
        LOGE("sdl: window failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetWindowPosition(app::g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    app::CacheClientSize();
    app::g_focus = SDL_GetKeyboardFocus() == app::g_window;
    // HDR is honestly off on this platform, from the start.
    app::SetHdr({false, false, 0, "HDR is not available on Linux"});

    // Vulkan handshake: the backend owns the instance; SDL names the
    // extensions it needs, the backend creates it, SDL makes the surface.
    uint32_t ext_count = 0;
    char const* const* exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!exts) {
        LOGE("sdl: no Vulkan instance extensions: %s", SDL_GetError());
        return 1;
    }
    gpu::SetVulkanInstanceExtensions(exts, ext_count);
    VkInstance instance = gpu::VulkanInstance();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (instance == VK_NULL_HANDLE || !SDL_Vulkan_CreateSurface(app::g_window, instance, nullptr, &surface)) {
        LOGE("vulkan: surface failed: %s", SDL_GetError());
        return 1;
    }
    gpu::SetVulkanSurface(instance, surface);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForVulkan(app::g_window);
    // ImGui_ImplVulkan_Init waits for the backend's device: TryInitImGuiVulkan
    // in the loop below, once gpu::GetVulkanHandles() is valid.

    input::Init();
    if (app::kGame.window) app::kGame.window(app::g_window);
    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    if (app::Instance()) SDL_SetWindowTitle(app::g_window, (title + " [" + std::to_string(app::Instance()) + "]").c_str());
    // Behind a splash the window stays hidden until the game's first frame;
    // handed over, it is set up hidden.
    if (handover) {
        app::ApplyNow(true);
    } else if (!splash) {
        app::ApplyNow(true);
        SDL_ShowWindow(app::g_window);
    }
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::UserdataDir() / hdd).string())) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app::Narrow(app::kGame.short_title).c_str(),
                                 "The game failed to start, see the log in the userdata/logs folder.", app::g_window);
        return 1;
    }

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 16)) {
            if (app::HandleEvent(e)) quit = true;
            while (!quit && SDL_PollEvent(&e))
                if (app::HandleEvent(e)) quit = true;
        }
        input::DesktopTick(app::g_window);
        app::TryInitImGuiVulkan();
    }
    // Vulkan teardown handshake (the one main_sdl change in the present fix): the
    // render thread records ImGui draw data into the backend's command buffers, so
    // ImGui's pipeline can still be in flight here. Wait for the device to go idle
    // before ImGui destroys it - validation otherwise reports vkDestroyPipeline
    // on a pipeline still in use by a command buffer at shutdown.
    if (app::g_imgui_vulkan.load(std::memory_order_relaxed)) {
        const gpu::VulkanHandles& vh = gpu::GetVulkanHandles();
        if (vh.device != VK_NULL_HANDLE) vkDeviceWaitIdle(vh.device);
        ImGui_ImplVulkan_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    // The instance and surface belong to the Vulkan backend and die with it
    // in kernel::Shutdown; the window is the process's, exiting with it.
    logx::Flush();
    kernel::Shutdown(0);
}

#if defined(XBE_WINDOWS_GUI)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return RunGame(__argc, __argv);
}
#endif

#endif  // !defined(_WIN32) || defined(XBE_VK_TARGET)
