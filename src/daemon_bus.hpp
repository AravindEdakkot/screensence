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

    ~DaemonBus();
    // Requests the bus name. Returns false if another daemon already owns it
    // or the session bus is unreachable.
    bool start(Handler handler, std::string* error);
    void set_state(const std::string& state);
    const std::string& state() const { return state_; }

private:
    static void on_method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                          GVariant*, GDBusMethodInvocation* inv, gpointer self);
    static GVariant* on_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                     const gchar* prop, GError**, gpointer self);

    Handler handler_;
    GDBusConnection* bus_ = nullptr;
    GDBusNodeInfo* node_ = nullptr;
    guint reg_id_ = 0;
    guint own_id_ = 0;
    bool name_acquired_ = false;
    bool name_lost_ = false;
    std::string state_ = "starting";
};

// Client side, used by `screensense calibrate` and `screensense pause|resume`.
namespace daemon_client {
bool running();
std::string state();  // empty if not running
bool call(const char* method);
}  // namespace daemon_client

}  // namespace ss
