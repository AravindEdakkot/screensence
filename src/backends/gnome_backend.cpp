// SPDX-License-Identifier: GPL-3.0-or-later
// GNOME Shell backend (Wayland and X11).
//
// Mutter does not let outside programs activate windows or move the pointer
// on Wayland, so a companion Shell extension does it and exports a small
// D-Bus API on the session bus. This backend is a thin client for it.
#include <cstdio>

#include <gio/gio.h>

#include "backends/backend.hpp"

namespace ss {
namespace {

constexpr const char* kName = "org.screensense.Shell";
constexpr const char* kPath = "/org/screensense/Shell";
constexpr const char* kIface = "org.screensense.Shell1";
constexpr const char* kExtensionUuid = "screensense@aravindedakkot.gmail.com";
constexpr unsigned kMinVersion = 1;
constexpr int kTimeoutMs = 1000;

class GnomeBackend final : public Backend {
public:
    ~GnomeBackend() override {
        if (bus_ && lock_sub_) g_dbus_connection_signal_unsubscribe(bus_, lock_sub_);
        if (bus_) g_object_unref(bus_);
    }

    std::string name() const override { return "gnome"; }
    bool can_activate_window() const override { return true; }
    bool can_move_pointer() const override { return true; }

    bool connect(std::string* error) override {
        if (!bus_) {
            GError* err = nullptr;
            bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
            if (!bus_) {
                if (error) *error = std::string("cannot reach the session bus: ") + err->message;
                g_error_free(err);
                return false;
            }
            subscribe_lock();
        }
        GVariant* r = call("GetVersion", nullptr, G_VARIANT_TYPE("(u)"), error);
        if (!r) {
            if (error)
                *error = std::string("the ScreenSense GNOME Shell extension is not running. Enable it with:\n"
                                     "    gnome-extensions enable ") +
                         kExtensionUuid + "\n(on Wayland, log out and back in after first installing it)";
            return false;
        }
        guint32 version = 0;
        g_variant_get(r, "(u)", &version);
        g_variant_unref(r);
        if (version < kMinVersion) {
            if (error) *error = "the ScreenSense GNOME extension is too old; please update it";
            return false;
        }
        return true;
    }

    std::vector<Monitor> monitors() override {
        std::vector<Monitor> out;
        GVariant* r = call("GetMonitors", nullptr, G_VARIANT_TYPE("(a(iiiib))"), nullptr);
        if (!r) return out;
        GVariantIter* it = nullptr;
        g_variant_get(r, "(a(iiiib))", &it);
        gint32 x, y, w, h;
        gboolean primary;
        while (g_variant_iter_next(it, "(iiiib)", &x, &y, &w, &h, &primary)) {
            Monitor m;
            m.index = static_cast<int>(out.size());
            m.geometry = {x, y, w, h};
            m.primary = primary;
            out.push_back(m);
        }
        g_variant_iter_free(it);
        g_variant_unref(r);
        return out;
    }

    bool focus_monitor(int index, bool activate, bool pointer) override {
        GVariant* r = call("FocusMonitor", g_variant_new("(ibb)", index, activate, pointer),
                           G_VARIANT_TYPE("(b)"), nullptr);
        if (!r) return false;
        g_variant_unref(r);
        return true;
    }

    std::optional<int> current_monitor(bool by_window) override {
        GVariant* r = call("GetCurrentMonitor", g_variant_new("(b)", by_window), G_VARIANT_TYPE("(i)"), nullptr);
        if (!r) return std::nullopt;
        gint32 idx = -1;
        g_variant_get(r, "(i)", &idx);
        g_variant_unref(r);
        if (idx < 0) return std::nullopt;
        return idx;
    }

    bool session_locked() override { return locked_; }

    void show_calibration_target(int index, const std::string& text) override {
        Backend::show_calibration_target(index, text);
        GVariant* r = call("ShowCalibrationTarget", g_variant_new("(is)", index, text.c_str()), nullptr, nullptr);
        if (r) g_variant_unref(r);
    }

    void hide_calibration_target() override {
        GVariant* r = call("HideCalibrationTarget", nullptr, nullptr, nullptr);
        if (r) g_variant_unref(r);
    }

private:
    GVariant* call(const char* method, GVariant* args, const GVariantType* reply, std::string* error) {
        if (!bus_) {
            if (args) g_variant_unref(g_variant_ref_sink(args));
            return nullptr;
        }
        GError* err = nullptr;
        GVariant* r = g_dbus_connection_call_sync(bus_, kName, kPath, kIface, method, args, reply,
                                                  G_DBUS_CALL_FLAGS_NO_AUTO_START, kTimeoutMs, nullptr, &err);
        if (!r) {
            if (error) *error = err->message;
            g_error_free(err);
        }
        return r;
    }

    // GNOME's screen shield announces lock state on org.gnome.ScreenSaver.
    // Signals are dispatched from the default main context, which the
    // engine iterates every frame.
    void subscribe_lock() {
        lock_sub_ = g_dbus_connection_signal_subscribe(
            bus_, "org.gnome.ScreenSaver", "org.gnome.ScreenSaver", "ActiveChanged", "/org/gnome/ScreenSaver",
            nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params,
               gpointer self) {
                gboolean active = FALSE;
                g_variant_get(params, "(b)", &active);
                static_cast<GnomeBackend*>(self)->locked_ = active;
            },
            this, nullptr);
        GVariant* r = g_dbus_connection_call_sync(bus_, "org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
                                                  "org.gnome.ScreenSaver", "GetActive", nullptr,
                                                  G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                  kTimeoutMs, nullptr, nullptr);
        if (r) {
            gboolean active = FALSE;
            g_variant_get(r, "(b)", &active);
            locked_ = active;
            g_variant_unref(r);
        }
    }

    GDBusConnection* bus_ = nullptr;
    guint lock_sub_ = 0;
    bool locked_ = false;
};

}  // namespace

std::unique_ptr<Backend> make_gnome_backend() { return std::make_unique<GnomeBackend>(); }

}  // namespace ss
