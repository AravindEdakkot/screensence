// SPDX-License-Identifier: GPL-3.0-or-later
// The daemon's own D-Bus service, org.screensense.Daemon.
//
// Lets the GNOME extension (or any other front end) show whether the camera
// is in use and pause/resume tracking, and lets `screensense calibrate`
// borrow the camera from a running daemon.
#pragma once

#include <functional>
#include <string>

#include <gio/gio.h>

namespace ss {

// Published states. The camera is on only in "tracking" and "idle".
//   tracking      camera on, face visible
//   idle          camera on at low rate, no face seen for a while
//   paused        camera off, paused by the user or by calibration
//   locked        camera off, screen is locked
//   uncalibrated  camera off, run `screensense calibrate`
//   no-desktop    camera off, desktop backend unreachable
//   no-camera     camera could not be opened

class DaemonBus {
public:
    enum class Command { Pause, Resume, Toggle, Reload };
    using Handler = std::function<void(Command)>;

    // What "focus" means, as read-write properties for the extension's
    // switches. The handler applies and saves a change. Returning false
    // refuses it, and the text in `error` goes back to the caller.
    enum class FocusOption { ActivateWindow, MovePointer };
    using OptionHandler = std::function<bool(FocusOption, bool value, std::string* error)>;

    // The two delays, in milliseconds, also read-write for the menu.
    enum class TimeOption { Dwell, InputHold };
    using TimeHandler = std::function<bool(TimeOption, int ms, std::string* error)>;

    ~DaemonBus();
    // Requests the bus name. Returns false if another daemon already owns it
    // or the session bus is unreachable.
    bool start(Handler handler, OptionHandler option_handler, TimeHandler time_handler, std::string* error);
    void set_state(const std::string& state);
    // Publishes the current values, announcing any that changed.
    void set_focus_options(bool activate_window, bool move_pointer);
    void set_times(int dwell_ms, int input_hold_ms);
    const std::string& state() const { return state_; }

private:
    static void on_method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                          GVariant*, GDBusMethodInvocation* inv, gpointer self);
    static GVariant* on_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                     const gchar* prop, GError**, gpointer self);
    static gboolean on_set_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar* prop, GVariant* value, GError** error, gpointer self);
    void emit_changed(const char* property, GVariant* value);

    Handler handler_;
    OptionHandler option_handler_;
    TimeHandler time_handler_;
    GDBusConnection* bus_ = nullptr;
    GDBusNodeInfo* node_ = nullptr;
    guint reg_id_ = 0;
    guint own_id_ = 0;
    bool name_acquired_ = false;
    bool name_lost_ = false;
    std::string state_ = "starting";
    bool activate_window_ = true;
    bool move_pointer_ = true;
    int dwell_ms_ = 0;
    int input_hold_ms_ = 0;
};

// Client side, used by `screensense calibrate` and `screensense pause|resume`.
namespace daemon_client {
bool running();
std::string state();  // empty if not running
bool call(const char* method);
}  // namespace daemon_client

}  // namespace ss
