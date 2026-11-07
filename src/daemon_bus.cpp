// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_bus.hpp"

namespace ss {
namespace {

constexpr const char* kName = "org.screensense.Daemon";
constexpr const char* kPath = "/org/screensense/Daemon";
constexpr const char* kIface = "org.screensense.Daemon1";

constexpr const char* kXml = R"(
<node>
  <interface name="org.screensense.Daemon1">
    <method name="Pause"/>
    <method name="Resume"/>
    <method name="Toggle"/>
    <method name="Reload"/>
    <property name="State" type="s" access="read"/>
    <property name="ActivateWindow" type="b" access="readwrite"/>
    <property name="MovePointer" type="b" access="readwrite"/>
    <property name="DwellMs" type="i" access="readwrite"/>
    <property name="InputHoldMs" type="i" access="readwrite"/>
  </interface>
</node>)";

}  // namespace

DaemonBus::~DaemonBus() {
    if (bus_ && reg_id_) g_dbus_connection_unregister_object(bus_, reg_id_);
    if (own_id_) g_bus_unown_name(own_id_);
    if (node_) g_dbus_node_info_unref(node_);
    if (bus_) g_object_unref(bus_);
}

bool DaemonBus::start(Handler handler, OptionHandler option_handler, TimeHandler time_handler, std::string* error) {
    time_handler_ = std::move(time_handler);
    handler_ = std::move(handler);
    option_handler_ = std::move(option_handler);
    GError* err = nullptr;
    bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!bus_) {
        if (error) *error = std::string("cannot reach the session bus: ") + err->message;
        g_error_free(err);
        return false;
    }
    node_ = g_dbus_node_info_new_for_xml(kXml, nullptr);
    static const GDBusInterfaceVTable vtable = {&DaemonBus::on_method, &DaemonBus::on_get_property,
                                                     &DaemonBus::on_set_property, {}};
    reg_id_ = g_dbus_connection_register_object(bus_, kPath, node_->interfaces[0], &vtable, this, nullptr, &err);
    if (!reg_id_) {
        if (error) *error = std::string("cannot export D-Bus object: ") + err->message;
        g_error_free(err);
        return false;
    }
    own_id_ = g_bus_own_name_on_connection(
        bus_, kName, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
        [](GDBusConnection*, const gchar*, gpointer self) { static_cast<DaemonBus*>(self)->name_acquired_ = true; },
        [](GDBusConnection*, const gchar*, gpointer self) { static_cast<DaemonBus*>(self)->name_lost_ = true; },
        this, nullptr);
    // Wait for the bus to answer the name request.
    while (!name_acquired_ && !name_lost_) g_main_context_iteration(nullptr, TRUE);
    if (name_lost_) {
        if (error) *error = "another ScreenSense daemon is already running";
        return false;
    }
    return true;
}

void DaemonBus::set_state(const std::string& state) {
    if (state == state_) return;
    state_ = state;
    emit_changed("State", g_variant_new_string(state_.c_str()));
}

void DaemonBus::set_focus_options(bool activate_window, bool move_pointer) {
    if (activate_window != activate_window_) {
        activate_window_ = activate_window;
        emit_changed("ActivateWindow", g_variant_new_boolean(activate_window_));
    }
    if (move_pointer != move_pointer_) {
        move_pointer_ = move_pointer;
        emit_changed("MovePointer", g_variant_new_boolean(move_pointer_));
    }
}

void DaemonBus::set_times(int dwell_ms, int input_hold_ms) {
    if (dwell_ms != dwell_ms_) {
        dwell_ms_ = dwell_ms;
        emit_changed("DwellMs", g_variant_new_int32(dwell_ms_));
    }
    if (input_hold_ms != input_hold_ms_) {
        input_hold_ms_ = input_hold_ms;
        emit_changed("InputHoldMs", g_variant_new_int32(input_hold_ms_));
    }
}

void DaemonBus::emit_changed(const char* property, GVariant* value) {
    g_variant_ref_sink(value);
    if (bus_) {
        GVariantBuilder changed;
        g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&changed, "{sv}", property, value);
        g_dbus_connection_emit_signal(bus_, nullptr, kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                      g_variant_new("(sa{sv}as)", kIface, &changed, nullptr), nullptr);
    }
    g_variant_unref(value);
}

void DaemonBus::on_method(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                          GVariant*, GDBusMethodInvocation* inv, gpointer self) {
    auto* d = static_cast<DaemonBus*>(self);
    std::string m = method;
    Command c;
    if (m == "Pause") c = Command::Pause;
    else if (m == "Resume") c = Command::Resume;
    else if (m == "Toggle") c = Command::Toggle;
    else if (m == "Reload") c = Command::Reload;
    else {
        g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "unknown method");
        return;
    }
    // Run the command before replying, so a caller that waits for the reply
    // (calibrate) knows the camera has already been released.
    if (d->handler_) d->handler_(c);
    g_dbus_method_invocation_return_value(inv, nullptr);
}

GVariant* DaemonBus::on_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                     const gchar* prop, GError**, gpointer self) {
    auto* d = static_cast<DaemonBus*>(self);
    const std::string name = prop;
    if (name == "State") return g_variant_new_string(d->state_.c_str());
    if (name == "ActivateWindow") return g_variant_new_boolean(d->activate_window_);
    if (name == "MovePointer") return g_variant_new_boolean(d->move_pointer_);
    if (name == "DwellMs") return g_variant_new_int32(d->dwell_ms_);
    if (name == "InputHoldMs") return g_variant_new_int32(d->input_hold_ms_);
    return nullptr;
}

gboolean DaemonBus::on_set_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                                    const gchar* prop, GVariant* value, GError** error, gpointer self) {
    auto* d = static_cast<DaemonBus*>(self);
    // GDBus has already checked the name and type against the interface above.
    const std::string name = prop;
    if (name == "DwellMs" || name == "InputHoldMs") {
        std::string why = "no handler";
        const TimeOption time = name == "DwellMs" ? TimeOption::Dwell : TimeOption::InputHold;
        if (!d->time_handler_ || !d->time_handler_(time, g_variant_get_int32(value), &why)) {
            g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, why.c_str());
            return FALSE;
        }
        return TRUE;
    }
    const FocusOption option =
        std::string(prop) == "ActivateWindow" ? FocusOption::ActivateWindow : FocusOption::MovePointer;
    std::string why = "no handler";
    if (!d->option_handler_ || !d->option_handler_(option, g_variant_get_boolean(value), &why)) {
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, why.c_str());
        return FALSE;
    }
    return TRUE;
}

namespace daemon_client {

static GDBusConnection* session() {
    static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    return bus;
}

std::string state() {
    if (!session()) return {};
    GVariant* r = g_dbus_connection_call_sync(session(), kName, kPath, "org.freedesktop.DBus.Properties", "Get",
                                              g_variant_new("(ss)", kIface, "State"), G_VARIANT_TYPE("(v)"),
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, nullptr);
    if (!r) return {};
    GVariant* v = nullptr;
    g_variant_get(r, "(v)", &v);
    std::string s = g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) ? g_variant_get_string(v, nullptr) : "";
    g_variant_unref(v);
    g_variant_unref(r);
    return s;
}

bool running() { return !state().empty(); }

bool call(const char* method) {
    if (!session()) return false;
    GVariant* r = g_dbus_connection_call_sync(session(), kName, kPath, kIface, method, nullptr, nullptr,
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, nullptr, nullptr);
    if (!r) return false;
    g_variant_unref(r);
    return true;
}

}  // namespace daemon_client
}  // namespace ss
