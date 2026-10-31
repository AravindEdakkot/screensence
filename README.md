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
  whenever the service is running, with its state and a pause switch.
- **The camera is switched off** while paused, while the screen is locked,
  and until you have calibrated.

## Status

Version 0.1, early development.

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

## Build and install

Dependencies are a C++20 compiler, CMake, OpenCV 4.8 or newer (with the
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
   seconds. ScreenSense records how your head is turned for each one. Run
   it again whenever you move the camera or rearrange your monitors. It will
   tell you if your layout changed.
3. **Start the service**, now and at every login:
   ```sh
   systemctl --user enable --now screensense.service
   ```

To see what ScreenSense sees, run `screensense debug`. It prints your head
direction, the distance to each calibrated monitor and every decision live,
without changing focus. Add `--act` to let it change focus too.

Other commands: `pause`, `resume`, `toggle`, `status` and `cameras`. Run
`screensense --help` for details.

## Configuration

Settings live in `~/.config/screensense/config.ini`. Every key is optional.
After editing, run `systemctl --user reload-or-restart screensense`, or send
the service `SIGHUP`.

```ini
[focus]
activate_window = true   # activate the last-used window on the monitor you look at
move_pointer = true      # move the mouse pointer to that monitor

[camera]
device = auto            # auto, an index such as 0, or a path such as /dev/video0
width = 320
height = 240
detect_width = 240       # face detection input width; larger is more precise, costs more CPU
fps = 10                 # frames per second while your face is visible
idle_fps = 2             # frames per second after no face for idle_after_s
idle_after_s = 5

[tracking]
dwell_ms = 400           # how long you must look at a monitor before it switches
cooldown_ms = 600        # minimum time between two switches
hysteresis = 0.25        # how much closer a new monitor must be; higher is steadier
smoothing = 0.5          # 1 = no smoothing, lower = smoother but slower
min_face_score = 0.75
```

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
3. **Which monitor.** The nearest calibrated monitor wins, but only if it
   is clearly nearer than the current one and you keep looking for
   `dwell_ms`. Looking far away from every monitor, at the keyboard or a
   phone, changes nothing. Neither does losing sight of your face.
4. **Desktop backend.** Changing focus is the only part that differs
   between desktops. Wayland compositors do not let ordinary programs focus
   windows or move the pointer, so each desktop family gets a small backend.
   On GNOME it is a Shell extension that the service talks to over D-Bus.

Measured on an Intel Core i7-1265U laptop, with the defaults:

| Frames per second | CPU, share of one core | Memory |
|---|---|---|
| 10 | about 8 percent | about 80 MB |
| 5 | about 4 percent | about 60 MB |

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
| `docker/Dockerfile.dev` | Reproducible build environment |

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
Docker-built service against it. Your real session is not touched.

### Adding a desktop backend

Implement the `Backend` interface in `src/backends/backend.hpp`. It needs
monitor geometry, "focus this monitor", and "which monitor has focus". Then
register it in `src/backends/registry.cpp`. The camera, tracking,
calibration and control interface are shared.

## Roadmap

1. X11 backend, covering every X11 desktop.
2. Settings window, and a global shortcut to pause.
3. KDE Plasma Wayland backend, through a KWin script.
4. wlroots and Hyprland backend.
5. Camera capture through PipeWire, so video call apps can share the camera.
6. Packages: .deb through a PPA, .rpm, the Arch User Repository, and the
   extension on extensions.gnome.org.

## Contact

Maintainer: <aravindedakkot@gmail.com>. Bug reports and ideas are welcome.

## License

Copyright (C) 2026 Aravind Edakkot.

GPL-3.0-or-later. See [LICENSE](LICENSE). The bundled YuNet face model is
MIT-licensed. See [models/README.md](models/README.md).
