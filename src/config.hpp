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
    // Iris offset while looking at each monitor, same order. Empty if this
    // calibration was recorded without eye tracking.
    std::vector<double> eyes;
    bool valid() const { return !centers.empty() && centers.size() == layout.size(); }
    bool has_eyes() const { return valid() && eyes.size() == centers.size(); }
};

// Set by the build to suit the bundled face model. The model used with OpenCV
// before 4.8 scores real faces lower: on one clear photo it gave 0.59 where
// the newer model gave 0.85.
#ifndef SCREENSENSE_DEFAULT_MIN_FACE_SCORE
#define SCREENSENSE_DEFAULT_MIN_FACE_SCORE 0.75
#endif

struct Config {
    // What "focus" means. Both can be on; either can be off.
    bool activate_window = true;
    bool move_pointer = true;

    std::string camera = "auto";  // "auto", an index like "0", or a path like "/dev/video0"
    // Reading an iris needs more pixels than finding a face: at 320 x 240 an
    // eye is only about 10 pixels wide.
    int capture_width = 640;
    int capture_height = 480;
    // Face detection input width. Below 320 the landmarks get coarse: measured
    // at desk distance, 240 read the head as 60% more turned than 320 or 480
    // did, with 2.5 times the jitter. 480 was no better than 320.
    int detect_width = 320;
    double fps = 10.0;         // processing rate while a face is visible
    double idle_fps = 2.0;     // processing rate after no face for idle_after_s
    double idle_after_s = 5.0;

    int dwell_ms = 600;         // how long a new direction must hold before switching
    int cooldown_ms = 600;      // minimum time between two switches
    // A new monitor must be this much closer than the current one. 0.6 means
    // the head has to travel about 70% of the way there, so shifting in your
    // seat does not count but really turning to the other monitor does.
    double hysteresis = 0.6;
    double smoothing = 0.5;     // exponential smoothing factor per frame, 0..1 (1 = no smoothing)
    double min_face_score = SCREENSENSE_DEFAULT_MIN_FACE_SCORE;
    int input_hold_ms = 1500;   // no switch until this long after the last key press or mouse movement
    bool use_eyes = true;       // use eye direction as well as head direction, once calibrated for it
    double eye_gain = 1.0;      // how much an eye movement counts relative to a head movement

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

// Returns `text` with `key = value` set in [section]. Every other line stays
// exactly as it was, so comments and hand edits survive. Adds the key or the
// section if it is missing.
std::string set_config_value(const std::string& text, const std::string& section, const std::string& key,
                             const std::string& value);
// Applies set_config_value to the file at `path`, creating it if needed.
// Writes atomically via a temporary file.
bool update_config_value(const std::string& path, const std::string& section, const std::string& key,
                         const std::string& value, std::string* error = nullptr);

}  // namespace ss
