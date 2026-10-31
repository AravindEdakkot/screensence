#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "backends/backend.hpp"

namespace ss {

void Backend::show_calibration_target(int index, const std::string& text) {
    std::printf("[monitor %d] %s\n", index + 1, text.c_str());
    std::fflush(stdout);
}

namespace {

std::string lower_env(const char* name) {
    const char* v = std::getenv(name);
    std::string s = v ? v : "";
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool has_token(const std::string& desktops, const std::string& token) {
    // XDG_CURRENT_DESKTOP is a colon-separated list, e.g. "ubuntu:GNOME".
    size_t start = 0;
    while (start <= desktops.size()) {
        size_t end = desktops.find(':', start);
        if (end == std::string::npos) end = desktops.size();
        if (desktops.compare(start, end - start, token) == 0) return true;
        start = end + 1;
    }
    return false;
}

}  // namespace

std::unique_ptr<Backend> create_backend(const std::string& forced, std::string* error) {
    if (forced == "gnome") return make_gnome_backend();
    if (forced == "null") return make_null_backend();
    if (!forced.empty() && forced != "auto") {
        if (error) *error = "unknown backend '" + forced + "' (available: gnome, null)";
        return nullptr;
    }

    const std::string desktop = lower_env("XDG_CURRENT_DESKTOP");
    const std::string session = lower_env("XDG_SESSION_TYPE");

    // GNOME derivatives (Ubuntu, Pop!_OS, Zorin, ...) all report GNOME. The
    // Shell extension works on both Wayland and X11 sessions.
    if (has_token(desktop, "gnome")) return make_gnome_backend();

    std::string what = desktop.empty() ? "an unknown desktop" : "'" + desktop + "'";
    if (has_token(desktop, "kde"))
        what = "KDE Plasma";
    else if (has_token(desktop, "sway") || has_token(desktop, "hyprland") || has_token(desktop, "river") ||
             has_token(desktop, "labwc") || has_token(desktop, "wlroots"))
        what = "a wlroots-based compositor (" + desktop + ")";
    else if (session == "x11")
        what = "X11 (" + (desktop.empty() ? std::string("unknown desktop") : desktop) + ")";
    if (error)
        *error = "no backend for " + what +
                 " yet. Planned: X11 (any desktop), KDE Plasma Wayland, wlroots/Hyprland. "
                 "Use --backend null to test tracking without changing focus.";
    return nullptr;
}

}  // namespace ss
