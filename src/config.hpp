// SPDX-License-Identifier: GPL-3.0-or-later
// ScreenSense configuration: a small INI file under $XDG_CONFIG_HOME.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ss {

// Head direction features measured from one camera frame.
// Both are dimensionless ratios derived from face landmarks, not degrees.
struct Pose {
    double yaw = 0.0;    // left/right turn; sign depends on camera, calibration absorbs it
    double pitch = 0.0;  // up/down tilt
};

struct MonitorGeometry {
    int x = 0, y = 0, width = 0, height = 0;
    bool operator==(const MonitorGeometry&) const = default;
};

struct Calibration {
    // Layout the calibration was recorded against. If the user's monitor
    // layout changes, the calibration is stale and must be redone.
    std::vector<MonitorGeometry> layout;
    // Average head pose while looking at each monitor, same order as layout.
    std::vector<Pose> centers;
    bool valid() const { return !centers.empty() && centers.size() == layout.size(); }
};

struct Config {
    // What "focus" means. Both can be on; either can be off.
    bool activate_window = true;
    bool move_pointer = true;

    std::string camera = "auto";  // "auto", an index like "0", or a path like "/dev/video0"
    int capture_width = 320;
    int capture_height = 240;
    // Face detection input width. 240 finds a face at desk distance reliably
    // at about a quarter of the CPU cost of 320.
    int detect_width = 240;
    double fps = 10.0;         // processing rate while a face is visible
    double idle_fps = 2.0;     // processing rate after no face for idle_after_s
    double idle_after_s = 5.0;

    int dwell_ms = 400;         // how long a new direction must hold before switching
    int cooldown_ms = 600;      // minimum time between two switches
    double hysteresis = 0.25;   // a new monitor must be this much closer than the current one
    double smoothing = 0.5;     // exponential smoothing factor per frame, 0..1 (1 = no smoothing)
    double min_face_score = 0.75;

    std::string model_path;  // empty = search default locations

    Calibration calibration;
};

// $XDG_CONFIG_HOME/screensense/config.ini, falling back to ~/.config.
std::string default_config_path();

// Missing file is not an error: returns defaults. Malformed values are
// reported in `warnings` and the default is kept.
Config load_config(const std::string& path, std::vector<std::string>* warnings = nullptr);
Config parse_config(const std::string& text, std::vector<std::string>* warnings = nullptr);

std::string serialize_config(const Config& cfg);
// Creates parent directories. Writes atomically via a temporary file.
bool save_config(const Config& cfg, const std::string& path, std::string* error = nullptr);

}  // namespace ss
