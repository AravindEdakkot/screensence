// SPDX-License-Identifier: GPL-3.0-or-later
// ScreenSense GNOME Shell extension.
//
// GNOME on Wayland does not let ordinary programs activate windows or move
// the pointer. This extension does those two things on behalf of the
// screensense service, through a small D-Bus API on the session bus:
//
//   org.screensense.Shell  /org/screensense/Shell  org.screensense.Shell1
//
// It also shows a top-bar indicator whenever the service is running, so it is
// always visible when the camera may be in use, with pause and calibrate.
//
// The extension never touches the camera or sees any video.

import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const API_VERSION = 1;

const SHELL_BUS_NAME = 'org.screensense.Shell';
const SHELL_PATH = '/org/screensense/Shell';
const SHELL_IFACE = `
<node>
  <interface name="org.screensense.Shell1">
    <method name="GetVersion">
      <arg type="u" direction="out" name="version"/>
    </method>
    <method name="GetMonitors">
      <arg type="a(iiiib)" direction="out" name="monitors"/>
    </method>
    <method name="GetCurrentMonitor">
      <arg type="b" direction="in" name="byWindow"/>
      <arg type="i" direction="out" name="index"/>
    </method>
    <method name="FocusMonitor">
      <arg type="i" direction="in" name="index"/>
      <arg type="b" direction="in" name="activateWindow"/>
      <arg type="b" direction="in" name="movePointer"/>
      <arg type="b" direction="out" name="changed"/>
    </method>
    <method name="ShowCalibrationTarget">
      <arg type="i" direction="in" name="index"/>
      <arg type="s" direction="in" name="text"/>
    </method>
    <method name="HideCalibrationTarget"/>
  </interface>
</node>`;

const DAEMON_BUS_NAME = 'org.screensense.Daemon';
const DAEMON_PATH = '/org/screensense/Daemon';
const DAEMON_IFACE = 'org.screensense.Daemon1';

const formatTime = ms => (ms === 0 ? 'Off' : `${ms / 1000} s`);
const DaemonProxy = Gio.DBusProxy.makeProxyWrapper(`
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
</node>`);

// What each daemon state means for the user, and whether the camera is on.
const STATES = {
    'starting': {text: 'Starting', camera: false},
    'tracking': {text: 'Following where you look', camera: true},
    'idle': {text: 'No face seen, checking occasionally', camera: true},
    'paused': {text: 'Paused, camera off', camera: false},
    'locked': {text: 'Screen locked, camera off', camera: false},
    'uncalibrated': {text: 'Needs calibration, camera off', camera: false},
    'no-desktop': {text: 'Waiting for the desktop', camera: false},
    'no-camera': {text: 'Camera unavailable', camera: false},
};

function monitorAt(x, y) {
    const display = global.display;
    for (let i = 0; i < display.get_n_monitors(); i++) {
        const g = display.get_monitor_geometry(i);
        if (x >= g.x && x < g.x + g.width && y >= g.y && y < g.y + g.height)
            return i;
    }
    return -1;
}

function rectContains(r, x, y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

// Implements org.screensense.Shell1. Method names match the D-Bus API.
class ShellService {
    constructor() {
        // Last pointer position on each monitor, so returning to a monitor
        // puts the pointer back where you left it.
        this._pointerMemory = new Map();
        this._target = null;
        this._label = null;
        this._monitorsChangedId = Main.layoutManager.connect('monitors-changed', () => {
            this._pointerMemory.clear();
        });
    }

    destroy() {
        this.HideCalibrationTarget();
        Main.layoutManager.disconnect(this._monitorsChangedId);
    }

    GetVersion() {
        return API_VERSION;
    }

    GetMonitors() {
        const display = global.display;
        const primary = display.get_primary_monitor();
        const out = [];
        for (let i = 0; i < display.get_n_monitors(); i++) {
            const g = display.get_monitor_geometry(i);
            out.push([g.x, g.y, g.width, g.height, i === primary]);
        }
        return out;
    }

    GetCurrentMonitor(byWindow) {
        if (byWindow) {
            const focused = global.display.focus_window;
            if (focused)
                return focused.get_monitor();
        }
        const [x, y] = global.get_pointer();
        return monitorAt(x, y);
    }

    FocusMonitor(index, activateWindow, movePointer) {
        const display = global.display;
        if (index < 0 || index >= display.get_n_monitors())
            return false;

        let changed = false;
        let window = null;
        if (activateWindow) {
            const focused = display.focus_window;
            if (focused && focused.get_monitor() === index) {
                window = focused;
            } else {
                window = this._mostRecentWindowOn(index);
                if (window) {
                    Main.activateWindow(window);
                    changed = true;
                }
            }
        }

        if (movePointer) {
            const [px, py] = global.get_pointer();
            const from = monitorAt(px, py);
            if (from !== index) {
                if (from >= 0)
                    this._pointerMemory.set(from, [px, py]);
                const [x, y] = this._pointerTarget(index, window);
                Clutter.get_default_backend().get_default_seat().warp_pointer(x, y);
                changed = true;
            }
        }
        return changed;
    }

    ShowCalibrationTarget(index, text) {
        const display = global.display;
        if (index < 0 || index >= display.get_n_monitors())
            return;
        if (!this._target) {
            this._label = new St.Label({
                style: 'font-size: 36px; font-weight: bold; color: white; text-align: center;',
                x_align: Clutter.ActorAlign.CENTER,
                y_align: Clutter.ActorAlign.CENTER,
                x_expand: true,
                y_expand: true,
            });
            this._target = new St.Widget({
                style: 'background-color: rgba(0, 0, 0, 0.65);',
                reactive: false,
                layout_manager: new Clutter.BinLayout(),
            });
            this._target.add_child(this._label);
            Main.uiGroup.add_child(this._target);
        }
        const g = display.get_monitor_geometry(index);
        this._label.text = `●\n\n${text}\n\nScreenSense calibration`;
        this._target.set_position(g.x, g.y);
        this._target.set_size(g.width, g.height);
        Main.uiGroup.set_child_above_sibling(this._target, null);
    }

    HideCalibrationTarget() {
        this._target?.destroy();
        this._target = null;
        this._label = null;
    }

    // Most recently used normal window on that monitor and the current
    // workspace. Windows shown on all workspaces count too.
    _mostRecentWindowOn(index) {
        const workspace = global.workspace_manager.get_active_workspace();
        const windows = global.display.get_tab_list(Meta.TabList.NORMAL, workspace);
        return windows.find(w => w.get_monitor() === index && !w.minimized) ?? null;
    }

    _pointerTarget(index, window) {
        const g = global.display.get_monitor_geometry(index);
        const remembered = this._pointerMemory.get(index);
        if (window) {
            // Land inside the window we activated, so focus-follows-mouse
            // setups do not focus a different window under the pointer.
            const r = window.get_frame_rect();
            if (remembered && rectContains(r, ...remembered))
                return remembered;
            const cx = Math.round(r.x + r.width / 2);
            const cy = Math.round(r.y + r.height / 2);
            if (rectContains(g, cx, cy))
                return [cx, cy];
        } else if (remembered && rectContains(g, ...remembered)) {
            return remembered;
        }
        return [g.x + Math.floor(g.width / 2), g.y + Math.floor(g.height / 2)];
    }
}

const Indicator = GObject.registerClass(
class ScreenSenseIndicator extends PanelMenu.Button {
    _init(proxy) {
        super._init(0.5, 'ScreenSense');
        this._proxy = proxy;

        this._icon = new St.Icon({style_class: 'system-status-icon'});
        this.add_child(this._icon);

        this._status = new PopupMenu.PopupMenuItem('', {reactive: false});
        this.menu.addMenuItem(this._status);
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        this._activateSwitch = this._addOption('Activate window', 'ActivateWindow');
        this._pointerSwitch = this._addOption('Move pointer', 'MovePointer');
        this._timeMenus = [
            this._addTimeMenu('Look time', 'DwellMs', [300, 600, 1000, 1500, 2000]),
            this._addTimeMenu('Wait after typing', 'InputHoldMs', [0, 1000, 1500, 2000, 3000, 5000]),
        ];
        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        this._pauseItem = new PopupMenu.PopupMenuItem('Pause');
        this._pauseItem.connect('activate', () => this._proxy.ToggleRemote(() => {}));
        this.menu.addMenuItem(this._pauseItem);

        const calibrate = new PopupMenu.PopupMenuItem('Calibrate…');
        calibrate.connect('activate', () => this._calibrate());
        this.menu.addMenuItem(calibrate);

        this._propsId = proxy.connect('g-properties-changed', () => this._sync());
        this._sync();
    }

    _addOption(label, property) {
        const item = new PopupMenu.PopupSwitchMenuItem(label, true);
        // Newer GNOME Shell versions also emit this when _sync sets the switch.
        // Only a state that differs from the service's is a real user change.
        item.connect('toggled', (_item, state) => {
            if (state !== this._proxy[property])
                this._setProperty(property, new GLib.Variant('b', state));
        });
        this.menu.addMenuItem(item);
        return item;
    }

    _addTimeMenu(label, property, choices) {
        const menu = new PopupMenu.PopupSubMenuMenuItem(label);
        const items = choices.map(ms => {
            const item = new PopupMenu.PopupMenuItem(formatTime(ms));
            item.connect('activate', () => this._setProperty(property, new GLib.Variant('i', ms)));
            menu.menu.addMenuItem(item);
            return {ms, item};
        });
        this.menu.addMenuItem(menu);
        return {label, property, menu, items};
    }

    // The service owns these settings. It saves the change and announces it,
    // which is what confirms the switch. If it refuses, say why and put the
    // switch back, so the menu never shows a setting that is not in effect.
    _setProperty(property, value) {
        this._proxy.call('org.freedesktop.DBus.Properties.Set',
            new GLib.Variant('(ssv)', [DAEMON_IFACE, property, value]),
            Gio.DBusCallFlags.NONE, -1, null, (proxy, res) => {
                try {
                    proxy.call_finish(res);
                } catch (e) {
                    const why = e.message.replace(/^GDBus\.Error:\S+ /, '');
                    Main.notifyError('ScreenSense', `Could not change the setting: ${why}`);
                    if (this._proxy)
                        this._sync();
                }
            });
    }

    // A service too old to have the property leaves it null: hide the switch.
    _syncOption(item, value) {
        item.visible = value !== null;
        if (value !== null)
            item.setToggleState(value);
    }

    _sync() {
        const state = this._proxy.State ?? 'starting';
        const info = STATES[state] ?? {text: state, camera: false};
        this._icon.icon_name = info.camera ? 'camera-web-symbolic' : 'camera-disabled-symbolic';
        this._status.label.text = `ScreenSense: ${info.text}`;
        this._pauseItem.label.text = state === 'paused' ? 'Resume' : 'Pause';
        this._syncOption(this._activateSwitch, this._proxy.ActivateWindow);
        this._syncOption(this._pointerSwitch, this._proxy.MovePointer);
        for (const {label, property, menu, items} of this._timeMenus) {
            const value = this._proxy[property];
            menu.visible = value !== null;
            if (value === null)
                continue;
            menu.label.text = `${label}: ${formatTime(value)}`;
            for (const {ms, item} of items)
                item.setOrnament(ms === value ? PopupMenu.Ornament.CHECK : PopupMenu.Ornament.NONE);
        }
    }

    _calibrate() {
        try {
            const proc = Gio.Subprocess.new(['screensense', 'calibrate'], Gio.SubprocessFlags.NONE);
            proc.wait_check_async(null, (p, res) => {
                try {
                    p.wait_check_finish(res);
                    Main.notify('ScreenSense', 'Calibration saved.');
                } catch (e) {
                    Main.notifyError('ScreenSense',
                        'Calibration did not finish. Run “screensense calibrate” in a terminal for details.');
                }
            });
        } catch (e) {
            Main.notifyError('ScreenSense', `Could not start calibration: ${e.message}`);
        }
    }

    destroy() {
        this._proxy.disconnect(this._propsId);
        this._proxy = null;
        super.destroy();
    }
});

export default class ScreenSenseExtension extends Extension {
    enable() {
        this._service = new ShellService();
        this._exported = Gio.DBusExportedObject.wrapJSObject(SHELL_IFACE, this._service);
        this._exported.export(Gio.DBus.session, SHELL_PATH);
        this._ownId = Gio.bus_own_name(Gio.BusType.SESSION, SHELL_BUS_NAME,
            Gio.BusNameOwnerFlags.NONE, null, null, null);

        // The indicator exists exactly while the service is running.
        this._watchId = Gio.bus_watch_name(Gio.BusType.SESSION, DAEMON_BUS_NAME,
            Gio.BusNameWatcherFlags.NONE,
            () => this._daemonAppeared(),
            () => this._daemonVanished());
    }

    disable() {
        Gio.bus_unwatch_name(this._watchId);
        this._daemonVanished();
        Gio.bus_unown_name(this._ownId);
        this._exported.unexport();
        this._exported = null;
        this._service.destroy();
        this._service = null;
    }

    _daemonAppeared() {
        this._daemonVanished();
        this._proxyCancellable = new Gio.Cancellable();
        const cancellable = this._proxyCancellable;
        new DaemonProxy(Gio.DBus.session, DAEMON_BUS_NAME, DAEMON_PATH, (proxy, error) => {
            if (error || cancellable.is_cancelled())
                return;
            this._indicator = new Indicator(proxy);
            Main.panel.addToStatusArea(this.uuid, this._indicator);
        }, cancellable, Gio.DBusProxyFlags.DO_NOT_AUTO_START);
    }

    _daemonVanished() {
        this._proxyCancellable?.cancel();
        this._proxyCancellable = null;
        this._indicator?.destroy();
        this._indicator = null;
    }
}
