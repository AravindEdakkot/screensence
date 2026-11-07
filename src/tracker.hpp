// SPDX-License-Identifier: GPL-3.0-or-later
// Decides which monitor the user is looking at, and when to switch.
//
// Pure logic: no camera, no desktop. Feed it head poses with timestamps and
// it returns a monitor index at the moment a switch should happen.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "config.hpp"

namespace ss {

struct TrackerParams {
    int dwell_ms = 600;
    int cooldown_ms = 600;
    double hysteresis = 0.6;
    double smoothing = 0.5;
    // A pose farther than reject_factor * (smallest distance between two
    // calibrated monitors) from every monitor is treated as "looking
    // elsewhere" (keyboard, phone, window) and never causes a switch.
    double reject_factor = 1.0;
    // After this long without a face, smoothing restarts from scratch.
    int face_lost_reset_ms = 1000;

    static TrackerParams from_config(const Config& c);
};

// Should a due switch wait because the person is using the keyboard or mouse?
// Desktops count our own pointer move as input, so input no newer than our
// last focus change says nothing about the person.
inline bool user_input_recent(int64_t idle_ms, int64_t since_own_focus_change_ms, int hold_ms) {
    constexpr int64_t kOwnMoveMarginMs = 150;  // our move and the desktop's clock are not simultaneous
    return idle_ms < hold_ms && idle_ms + kOwnMoveMarginMs < since_own_focus_change_ms;
}

struct TrackerState {
    bool has_face = false;
    Pose smoothed;
    std::vector<double> distances;  // to each calibrated monitor
    int nearest = -1;
    bool rejected = false;          // nearest monitor too far away
    int current = -1;               // monitor focus is on, as far as we know
    int pending = -1;               // monitor we may switch to after dwell
    int64_t pending_since_ms = 0;
    bool input_hold = false;        // a switch is due but is waiting for input to stop
};

class Tracker {
public:
    Tracker(TrackerParams params, std::vector<Pose> centers);

    // `pose` is empty when no face was detected in this frame.
    // Returns the monitor to focus if a switch should happen now.
    // `input_active` is asked only when a switch is otherwise due, so it may
    // be slow. While it returns true the switch waits, and is dropped if the
    // person looks back in the meantime.
    std::optional<int> update(std::optional<Pose> pose, int64_t now_ms,
                              const std::function<bool()>& input_active = {});

    // Tell the tracker where focus is, e.g. at startup or after the user
    // moved focus by hand. Clears any pending switch.
    void set_current(int monitor);
    void set_dwell_ms(int ms) { params_.dwell_ms = ms; }

    const TrackerState& state() const { return state_; }
    size_t monitor_count() const { return centers_.size(); }

private:
    TrackerParams params_;
    std::vector<Pose> centers_;
    double reject_distance_;
    TrackerState state_;
    bool have_smoothed_ = false;
    int64_t last_face_ms_ = INT64_MIN / 2;
    int64_t last_switch_ms_ = INT64_MIN / 2;
};

}  // namespace ss
