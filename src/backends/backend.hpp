// SPDX-License-Identifier: GPL-3.0-or-later
// A desktop backend performs the one non-portable step: changing focus.
//
// Every compositor exposes window activation and pointer warping
// differently (or not at all) to outside programs, so each desktop family
// gets its own implementation of this interface. Everything else in
// ScreenSense is shared.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"

namespace ss {

struct Monitor {
    int index = 0;  // backend's own index, used in focus_monitor()
    MonitorGeometry geometry;
    bool primary = false;
};

class Backend {
public:
    virtual ~Backend() = default;

    virtual std::string name() const = 0;
    // Checks that the desktop side is reachable. On failure, `error` says
    // what the user should do (e.g. enable the GNOME extension).
    virtual bool connect(std::string* error) = 0;

    virtual bool can_activate_window() const = 0;
    virtual bool can_move_pointer() const = 0;

    virtual std::vector<Monitor> monitors() = 0;
    // Activates the most recently used window on that monitor and/or moves
    // the pointer there. Returns false if the call failed.
    virtual bool focus_monitor(int index, bool activate_window, bool move_pointer) = 0;
    // Monitor holding the focused window (by_window) or the pointer.
    virtual std::optional<int> current_monitor(bool by_window) = 0;

    // True while the screen is locked; the camera is turned off then.
    virtual bool session_locked() { return false; }

    // Milliseconds since the last keyboard or mouse input, or empty if this
    // desktop cannot say. Used to avoid switching while someone is typing.
    virtual std::optional<int64_t> input_idle_ms() { return std::nullopt; }

    // Calibration prompts. The default prints to the terminal.
    virtual void show_calibration_target(int index, const std::string& text);
    virtual void hide_calibration_target() {}
};

// Picks a backend for the running desktop, or the one named in `forced`
// ("gnome", "null"). On failure, `error` explains why.
std::unique_ptr<Backend> create_backend(const std::string& forced, std::string* error);

std::unique_ptr<Backend> make_gnome_backend();
std::unique_ptr<Backend> make_null_backend();

}  // namespace ss
