#!/bin/bash
# End-to-end test of the GNOME Shell extension in a throwaway headless
# GNOME Shell with two virtual monitors. Uses private config, data and
# D-Bus, so the real desktop session is not touched.
#
#   scripts/test-gnome-headless.sh [--with-daemon]
#
# --with-daemon also runs the screensense daemon (built in Docker) against
# the test shell, to check the service <-> extension handshake.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/build-gnome-test
EXT=screensense@aravindedakkot.gmail.com
WITH_DAEMON=${1:-}

rm -rf "$T"
mkdir -p "$T"/{config,data/gnome-shell/extensions,cache,runtime,bus}
chmod 700 "$T/runtime"
cp -r "$ROOT/gnome-extension/$EXT" "$T/data/gnome-shell/extensions/"
# Private bus in the project tree (so the Docker-built daemon can reach it).
# AppArmor mediation is off on this test bus only: the daemon runs inside a
# Docker-confined process here, unlike a real install.
sed -e "s|<listen>.*</listen>|<listen>unix:dir=$T/bus</listen>|" \
    -e "s|</busconfig>|  <apparmor mode=\"disabled\"/>\n</busconfig>|" \
    /usr/share/dbus-1/session.conf > "$T/session.conf"

# Test-only helper: reports window placement and pointer position, and gives
# the headless seat a virtual pointer (a headless session has no mouse).
H="$T/data/gnome-shell/extensions/ss-test-helper@localhost"
mkdir -p "$H"
cat > "$H/metadata.json" <<'JSON'
{"uuid": "ss-test-helper@localhost", "name": "ss test helper", "description": "test only", "shell-version": ["50"]}
JSON
cat > "$H/extension.js" <<'JS'
import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
const XML = '<node><interface name="ss.TestHelper"><method name="Inspect"><arg type="s" direction="out"/></method><method name="Warp"><arg type="i" direction="in"/><arg type="i" direction="in"/></method></interface></node>';
export default class Helper extends Extension {
    enable() {
        const seat = Clutter.get_default_backend().get_default_seat();
        this._pointer = seat.create_virtual_device(Clutter.InputDeviceType.POINTER_DEVICE);
        this._obj = Gio.DBusExportedObject.wrapJSObject(XML, {
            Warp(x, y) { Clutter.get_default_backend().get_default_seat().warp_pointer(x, y); },
            Inspect() {
                const focus = global.display.focus_window;
                const windows = global.get_window_actors().map(a => a.meta_window).map(w => ({
                    title: w.get_title(), monitor: w.get_monitor(), focused: w === focus}));
                const [x, y] = global.get_pointer();
                const ind = Main.panel.statusArea['screensense@aravindedakkot.gmail.com'];
                const indicator = ind ? {icon: ind._icon.icon_name, status: ind._status.label.text} : null;
                return JSON.stringify({windows, pointer: [x, y], indicator});
            },
        });
        this._obj.export(Gio.DBus.session, '/ss/TestHelper');
    }
    disable() { this._obj.unexport(); this._pointer = null; }
}
JS

cat > "$T/client.js" <<'JS'
imports.gi.versions.Gtk = '4.0';
const {Gtk, Gdk, GLib} = imports.gi;
const [title, monitor] = ARGV;
Gtk.init();
const win = new Gtk.Window({title, default_width: 300, default_height: 200});
win.fullscreen_on_monitor(Gdk.Display.get_default().get_monitors().get_item(Number(monitor)));
win.present();
GLib.MainLoop.new(null, false).run();
JS

cat > "$T/inner.sh" <<'INNER'
#!/bin/bash
set -u
pass=0; fail=0
check() {  # check NAME EXPECTED ACTUAL
    if [[ "$3" == *"$2"* ]]; then echo "PASS  $1: $3"; pass=$((pass+1));
    else echo "FAIL  $1: expected '$2', got '$3'"; fail=$((fail+1)); fi
}
inspect() { gdbus call --session --dest org.gnome.Shell --object-path /ss/TestHelper --method ss.TestHelper.Inspect 2>&1; }
call() { gdbus call --session --dest org.screensense.Shell --object-path /org/screensense/Shell \
                    --method org.screensense.Shell1.$1 "${@:2}" 2>&1; }

gsettings set org.gnome.shell enabled-extensions "['screensense@aravindedakkot.gmail.com', 'ss-test-helper@localhost']"
gnome-shell --headless --wayland --no-x11 --wayland-display=ss-test \
    --virtual-monitor 1280x720 --virtual-monitor 1280x720 > "$T/shell.log" 2>&1 &
SHELL_PID=$!

for i in $(seq 40); do call GetVersion | grep -q uint32 && break; sleep 0.5; done
check "extension exports API" "uint32 1" "$(call GetVersion)"
check "two monitors" "[(0, 0, 1280, 720, true), (1280, 0, 1280, 720, false)]" "$(call GetMonitors)"

export WAYLAND_DISPLAY=ss-test GDK_BACKEND=wayland GSK_RENDERER=cairo
gjs "$T/client.js" Left 0 > "$T/client-left.log" 2>&1 &
sleep 2
gjs "$T/client.js" Right 1 > "$T/client-right.log" 2>&1 &
sleep 3

warp() { gdbus call --session --dest org.gnome.Shell --object-path /ss/TestHelper --method ss.TestHelper.Warp "$@" >/dev/null; sleep 0.2; }
pointer() { inspect | grep -o '"pointer":\[[0-9,]*\]'; }
# A headless session has no real mouse. The virtual one ignores the x axis
# for a moment after start-up, so wait until it accepts a position.
for i in $(seq 25); do warp 10 10; [[ "$(pointer)" == *"[10,10]"* ]] && break; done
check "virtual pointer ready" '"pointer":[10,10]' "$(pointer)"
check "windows placed on their monitors" '"title":"Left","monitor":0' "$(inspect)"
check "windows placed on their monitors" '"title":"Right","monitor":1' "$(inspect)"
check "current monitor reported" "(0,)" "$(call GetCurrentMonitor true)"
call FocusMonitor 0 true false >/dev/null   # whatever the start state, focus the left window
check "focus monitor 1 activates its window" "(true,)" "$(call FocusMonitor 1 true false)"
check "  focused window now on monitor 1" "(1,)" "$(call GetCurrentMonitor true)"
check "  it is the right-hand window" '"title":"Right","monitor":1,"focused":true' "$(inspect)"
check "focusing the same monitor again is a no-op" "(false,)" "$(call FocusMonitor 1 true false)"
check "focus monitor 0 activates its window" "(true,)" "$(call FocusMonitor 0 true false)"
check "  focused window now on monitor 0" "(0,)" "$(call GetCurrentMonitor true)"
check "out-of-range monitor rejected" "(false,)" "$(call FocusMonitor 7 true true)"
check "pointer moves to monitor 1" "(true,)" "$(call FocusMonitor 1 false true)"
sleep 0.2
check "pointer lands at centre of monitor 1" '"pointer":[1920,360]' "$(pointer)"
check "pointer moves back to monitor 0" "(true,)" "$(call FocusMonitor 0 false true)"
sleep 0.2
check "pointer returns to where it was on monitor 0" '"pointer":[10,10]' "$(pointer)"
check "pointer already there is a no-op" "(false,)" "$(call FocusMonitor 0 false true)"
check "activate and move pointer together" "(true,)" "$(call FocusMonitor 1 true true)"
sleep 0.2
check "  window focused on monitor 1" "(1,)" "$(call GetCurrentMonitor true)"
check "  pointer on monitor 1" "(1,)" "$(call GetCurrentMonitor false)"
check "calibration overlay shows" "()" "$(call ShowCalibrationTarget 1 'Look at this screen')"
check "calibration overlay hides" "()" "$(call HideCalibrationTarget)"

if [ "$WITH_DAEMON" = "--with-daemon" ]; then
    mkdir -p "$T/config/screensense"
    docker run -d --name ss-daemon-test -u "$(id -u):$(id -g)" --network none \
        -v "$ROOT":"$ROOT" -w "$ROOT" \
        -e DBUS_SESSION_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" -e XDG_CURRENT_DESKTOP=GNOME \
        -e XDG_SESSION_TYPE=wayland -e XDG_CONFIG_HOME="$T/config" \
        screensense-dev ./build-docker/screensense run > /dev/null
    for i in $(seq 20); do s=$(gdbus call --session --dest org.screensense.Daemon --object-path /org/screensense/Daemon \
        --method org.freedesktop.DBus.Properties.Get org.screensense.Daemon1 State 2>/dev/null); [ -n "$s" ] && \
        [[ "$s" != *starting* ]] && break; sleep 0.5; done
    check "daemon reaches extension, waits for calibration" "uncalibrated" "$s"
    sleep 0.5
    check "indicator shown, camera-off icon" '"icon":"camera-disabled-symbolic","status":"ScreenSense: Needs calibration, camera off"' "$(inspect)"
    gdbus call --session --dest org.screensense.Daemon --object-path /org/screensense/Daemon \
        --method org.screensense.Daemon1.Pause > /dev/null
    s=$(gdbus call --session --dest org.screensense.Daemon --object-path /org/screensense/Daemon \
        --method org.freedesktop.DBus.Properties.Get org.screensense.Daemon1 State)
    check "daemon pauses on request" "paused" "$s"
    sleep 0.5
    check "indicator follows state" '"status":"ScreenSense: Paused, camera off"' "$(inspect)"
    docker stop -t 3 ss-daemon-test > /dev/null
    sleep 1
    check "indicator removed when daemon stops" '"indicator":null' "$(inspect)"
    docker logs ss-daemon-test 2>&1; docker rm ss-daemon-test >/dev/null
fi

sleep 1
errors=$(grep -iE "screensense|JS ERROR|TypeError|ReferenceError" "$T/shell.log" | grep -v "^$" || true)
if [ -n "$errors" ]; then echo "--- shell log mentions:"; echo "$errors" | head -20; fi
check "no JS errors in shell log" "" "$(grep -cE 'JS ERROR|TypeError|ReferenceError' "$T/shell.log" | sed 's/^0$//')"
kill $SHELL_PID 2>/dev/null; wait $SHELL_PID 2>/dev/null
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
INNER
chmod +x "$T/inner.sh"

env -i HOME="$HOME" PATH="$PATH" USER="$USER" LANG="${LANG:-C.UTF-8}" \
    XDG_CONFIG_HOME="$T/config" XDG_DATA_HOME="$T/data" XDG_CACHE_HOME="$T/cache" \
    XDG_RUNTIME_DIR="$T/runtime" XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_TYPE=wayland \
    T="$T" ROOT="$ROOT" WITH_DAEMON="$WITH_DAEMON" \
    dbus-run-session --config-file="$T/session.conf" -- "$T/inner.sh"
