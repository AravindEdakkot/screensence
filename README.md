# ScreenSense

Look at a monitor, and ScreenSense moves focus there. No click needed.

ScreenSense uses your webcam to tell which monitor you are facing. When you
turn to another screen and hold it for a moment, it activates the window you
last used on that screen, moves the mouse pointer there, or both. You choose
which.

It is a small native service written in C++ with a lightweight face model,
designed to run all day in the background.

## Privacy

ScreenSense watches you through your camera, so it is built to be trustworthy:

- **Video never leaves your computer.** There is no network code at all.
- **Nothing is recorded.** Frames are held in memory only long enough to
  find your face, then discarded. Nothing is written to disk except your
  settings and calibration, which are a handful of numbers.
- **You can always see when the camera is on.** A top-bar icon is shown
  whenever the service is running, with its state and a pause item.
- **The camera is switched off** while paused, while the screen is locked,
  and until you have calibrated.

## Status

Version 0.2, early development.

| Desktop | Session | Status |
|---|---|---|
| GNOME 45 to 50 (Ubuntu, Fedora, Pop!_OS, ...) | Wayland and X11 | Works (tested on GNOME 50) |
| Any desktop | X11 | Planned next |
| KDE Plasma | Wayland | Planned |
| Sway, Hyprland, river, labwc | Wayland | Planned |

Known limitation: most webcams serve only one program at a time. While
ScreenSense is running, a video call app may not get the camera. Pause
ScreenSense from the top bar before a call. Sharing the camera through
PipeWire, which avoids this, is on the roadmap.

## Install

On Ubuntu 24.04 or newer, or Debian 12 or newer, download the `.deb` for your
release from the [Releases](https://github.com/AravindEdakkot/screensence/releases)
page and install it:

```sh
sudo apt install ./screensense_0.2.0_ubuntu24.04_amd64.deb
```

Use the file that matches your release. Then follow [Set up](#set-up). The
package starts the service at every login for every user of the computer, but
the camera stays off until that user calibrates. To remove ScreenSense, run
`sudo apt remove screensense`.

On other distributions, build from source as below.

## Build and install

Dependencies are a C++20 compiler, CMake, OpenCV 4.5.4 or newer (with the
`dnn` and `objdetect` modules) and GLib.

```sh
# Ubuntu / Debian
sudo apt install build-essential cmake pkg-config libopencv-dev libglib2.0-dev
# Fedora
sudo dnf install gcc-c++ cmake opencv-devel glib2-devel
# Arch
sudo pacman -S base-devel cmake opencv glib2
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

This installs the `screensense` program, the face model, a systemd user
service and the GNOME Shell extension under `/usr/local`.

To install for your user only, without `sudo`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local \
      -DSCREENSENSE_SYSTEMD_USER_UNIT_DIR=$HOME/.local/share/systemd/user
cmake --build build && cmake --install build
```

## Set up

1. **Enable the GNOME extension.** On Wayland, GNOME only notices a newly
   installed extension after you log out and back in. Then run:
   ```sh
   gnome-extensions enable screensense@aravindedakkot.gmail.com
   ```
2. **Calibrate.** Sit as you normally do and run:
   ```sh
   screensense calibrate
   ```
   Each monitor in turn darkens and asks you to look at it for a few
   seconds. ScreenSense records how your head is turned for each one. Then it
   asks you to keep your eyes on some text while you slowly turn your head,
   which shows whether your eyes can be followed reliably with your camera. It
   tells you the result, and uses head direction alone if they cannot. Run
   it again whenever you move the camera or rearrange your monitors. It will
   tell you if your layout changed.
3. **Start the service**, now and at every login. The `.deb` package does
   this at your next login, so skip this step if you installed it.
   ```sh
   systemctl --user enable --now screensense.service
   ```

To see what ScreenSense sees, run `screensense debug`. It prints your head
direction, the distance to each calibrated monitor and every decision live,
without changing focus. Add `--act` to let it change focus too.

Other commands: `pause`, `resume`, `toggle`, `status` and `cameras`. Run
`screensense --help` for details.

## Configuration

The top-bar menu has two switches, **Activate window** and **Move pointer**,
which choose what happens when you look at another monitor. A change applies
straight away and is saved. Either can be off. With both off, ScreenSense keeps
watching but changes nothing.

The same menu sets **Look time**, how long you must look at a monitor before it
switches, and **Wait after typing**, how long after your last input a switch may
happen. These also apply straight away and are saved.

All settings live in `~/.config/screensense/config.ini`. Every key is optional.
After editing, run `systemctl --user reload-or-restart screensense`, or send
the service `SIGHUP`.

```ini
[focus]
activate_window = true   # activate the last-used window on the monitor you look at
move_pointer = true      # move the mouse pointer to that monitor

[camera]
device = auto            # auto, an index such as 0, or a path such as /dev/video0
width = 640              # eye tracking needs at least 640 x 480
height = 480
detect_width = 320       # face detection input width; below 320 the head reading gets unreliable
fps = 10                 # frames per second while your face is visible
idle_fps = 2             # frames per second after no face for idle_after_s
idle_after_s = 5

[tracking]
dwell_ms = 600           # how long you must look at a monitor before it switches
cooldown_ms = 600        # minimum time between two switches
hysteresis = 0.6         # how much closer a new monitor must be; higher is steadier
smoothing = 0.5          # 1 = no smoothing, lower = smoother but slower
min_face_score = 0.75     # how sure face detection must be; the default is 0.5 with OpenCV older than 4.8
input_hold_ms = 1500     # no switch until this long after the last key press or mouse movement; 0 = off
use_eyes = true          # use eye direction as well as head direction, once calibrated for it
eye_gain = 1.0           # how much an eye movement counts; calibration measures this for you
```

ScreenSense does not switch while you are using the keyboard or mouse. If you
are still looking at the other monitor once you stop, it switches then. This
currently works on GNOME, where the desktop can report the time since your last
input.

`auto` camera selection skips infrared face-unlock cameras. The pointer
returns to where you last left it on each monitor.

## How it works

```
webcam ──> face + 5 landmarks ──> head direction ──> which monitor? ──> desktop backend
           (YuNet, OpenCV)        (yaw, pitch)        (calibration,      (GNOME extension,
                                                       dwell, hysteresis)  X11, KWin, ...)
```

1. **Face landmarks.** The bundled YuNet model finds your face and five
   points: eyes, nose tip and mouth corners.
2. **Head direction.** How far the nose tip sits from the middle of your
   face gives the left-right turn, and its height between eyes and mouth
   gives the up-down tilt. Both are ratios, so they do not change with your
   distance from the camera or with head tilt to the side.
3. **Eye direction.** When your head moves away from the current monitor, the
   bundled MediaPipe iris model reads where your irises sit within your eyes.
   Adding that to the head direction cancels out a head that moved while your
   eyes stayed on the screen. It is used only if calibration showed that your
   eyes can be followed reliably with your camera.
4. **Which monitor.** The nearest calibrated monitor wins, but only if it
   is clearly nearer than the current one and you keep looking for
   `dwell_ms`. Looking far away from every monitor, at the keyboard or a
   phone, changes nothing. Neither does losing sight of your face. A switch
   also waits until `input_hold_ms` after your last key press or mouse movement.
5. **Desktop backend.** Changing focus is the only part that differs
   between desktops. Wayland compositors do not let ordinary programs focus
   windows or move the pointer, so each desktop family gets a small backend.
   On GNOME it is a Shell extension that the service talks to over D-Bus.

Measured on an Intel Core i7-1265U laptop, with the defaults:

| Frames per second | CPU, share of one core | Memory |
|---|---|---|
| 10 | about 8 percent | about 80 MB |
| 5 | about 4 percent | about 60 MB |

These figures were measured at 320 x 240 with `detect_width = 240`. The current
defaults, 640 x 480 with `detect_width = 320`, read your head far more reliably
and use roughly twice as much CPU, about 20 percent of one core. Eye tracking
adds about 10 percent of a core while its model runs, which is only when your
head has moved away from the current monitor. Lowering `fps` reduces all of this
in proportion.

While no face is visible, it drops to 2 frames per second. While paused,
locked or uncalibrated, the camera is off and the service uses no CPU.

### Repository layout

| Path | Contents |
|---|---|
| `src/` | The service: camera, head pose, tracker, engine, CLI |
| `src/backends/` | Desktop backends behind one interface |
| `gnome-extension/` | The GNOME Shell half |
| `tests/` | Unit tests for the tracker, pose maths and config |
| `scripts/test-gnome-headless.sh` | End-to-end test in a throwaway headless GNOME Shell |
| `debian/` | Debian and Ubuntu packaging |
| `scripts/build-deb.sh`, `scripts/test-deb.sh` | Build a `.deb` in a clean container, then install and check it |
| `docker/Dockerfile.dev` | Reproducible build environment |
| `RELEASING.md` | How to publish a new version |

## Development

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Or build without installing anything on the host:

```sh
docker build -t screensense-dev -f docker/Dockerfile.dev docker
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src screensense-dev \
    sh -c 'cmake -S . -B build-docker -G Ninja && cmake --build build-docker && ctest --test-dir build-docker'
```

`scripts/test-gnome-headless.sh` starts a private headless GNOME Shell with
two virtual monitors and test windows, then checks every extension call:
window activation, pointer moves and pointer memory, the calibration
overlay, and the top-bar indicator. With `--with-daemon` it also runs the
service from `build/` against it. Your real session is not touched.

To build and check the package for a release, run
`scripts/build-deb.sh ubuntu:24.04` and then `scripts/test-deb.sh ubuntu:24.04`.

### Adding a desktop backend

Implement the `Backend` interface in `src/backends/backend.hpp`. It needs
monitor geometry, "focus this monitor", and "which monitor has focus". Then
register it in `src/backends/registry.cpp`. The camera, tracking,
calibration and control interface are shared.

## Roadmap

1. X11 backend, covering every X11 desktop.
2. Settings window for the remaining options, and a global shortcut to pause.
3. KDE Plasma Wayland backend, through a KWin script.
4. wlroots and Hyprland backend.
5. Camera capture through PipeWire, so video call apps can share the camera.
6. Packages: a PPA for apt updates, .rpm, the Arch User Repository, and the
   extension on extensions.gnome.org. A .deb for Ubuntu and Debian exists.

## Contact

Maintainer: <aravindedakkot@gmail.com>. Bug reports and ideas are welcome.

## License

Copyright (C) 2026 Aravind Edakkot.

GPL-3.0-or-later. See [LICENSE](LICENSE). The bundled YuNet face model is
MIT-licensed. See [models/README.md](models/README.md).
