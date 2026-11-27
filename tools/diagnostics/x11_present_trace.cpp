// Observe PresentCompleteNotify on the game's X11 window; never submit images.
// Build: c++ -std=c++20 x11_present_trace.cpp -o observer -lxcb-present -lxcb
#include <xcb/present.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <poll.h>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const auto window = uint32_t(std::strtoul(argv[1], nullptr, 0));
    const int seconds = std::atoi(argv[2]);
    auto* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection)) return 1;
    auto* version = xcb_present_query_version_reply(connection, xcb_present_query_version(connection, 1, 2), nullptr);
    if (!version) return 1;
    std::free(version);
    auto* error = xcb_request_check(connection, xcb_present_select_input_checked(connection,
        xcb_generate_id(connection), window, XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY));
    if (error) { std::fprintf(stderr, "PresentSelectInput error %u\n", error->error_code); return 1; }
    xcb_flush(connection);
    std::puts("ns,serial,ust_us,msc,kind,mode");
    using Clock = std::chrono::steady_clock;
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    pollfd fd{xcb_get_file_descriptor(connection), POLLIN, 0};
    while (Clock::now() < end && !xcb_connection_has_error(connection)) {
        while (auto* event = xcb_poll_for_event(connection)) {
            if ((event->response_type & 127) == XCB_GE_GENERIC) {
                auto* e = reinterpret_cast<xcb_present_complete_notify_event_t*>(event);
                if (e->event_type == XCB_PRESENT_COMPLETE_NOTIFY && e->window == window) {
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
                    std::printf("%llu,%u,%llu,%llu,%u,%u\n", (unsigned long long)ns, e->serial,
                        (unsigned long long)e->ust, (unsigned long long)e->msc, e->kind, e->mode);
                    std::fflush(stdout);
                }
            }
            std::free(event);
        }
        poll(&fd, 1, 100);
    }
    xcb_disconnect(connection);
}
