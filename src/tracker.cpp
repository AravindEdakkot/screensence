#include "tracker.hpp"

#include <cmath>
#include <limits>

namespace ss {

TrackerParams TrackerParams::from_config(const Config& c) {
    TrackerParams p;
    p.dwell_ms = c.dwell_ms;
    p.cooldown_ms = c.cooldown_ms;
    p.hysteresis = c.hysteresis;
    p.smoothing = c.smoothing;
    return p;
}

static double dist(const Pose& a, const Pose& b) {
    return std::hypot(a.yaw - b.yaw, a.pitch - b.pitch);
}

Tracker::Tracker(TrackerParams params, std::vector<Pose> centers)
    : params_(params), centers_(std::move(centers)) {
    double spacing = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < centers_.size(); ++i)
        for (size_t j = i + 1; j < centers_.size(); ++j)
            spacing = std::min(spacing, dist(centers_[i], centers_[j]));
    reject_distance_ = std::isfinite(spacing) ? spacing * params_.reject_factor
                                              : std::numeric_limits<double>::infinity();
    state_.distances.assign(centers_.size(), 0.0);
}

void Tracker::set_current(int monitor) {
    state_.current = monitor;
    state_.pending = -1;
}

std::optional<int> Tracker::update(std::optional<Pose> pose, int64_t now_ms) {
    state_.has_face = pose.has_value();
    if (!pose) {
        // Hold: losing the face must never move focus.
        state_.pending = -1;
        if (now_ms - last_face_ms_ > params_.face_lost_reset_ms) have_smoothed_ = false;
        return std::nullopt;
    }
    last_face_ms_ = now_ms;

    if (!have_smoothed_) {
        state_.smoothed = *pose;
        have_smoothed_ = true;
    } else {
        const double a = params_.smoothing;
        state_.smoothed.yaw += a * (pose->yaw - state_.smoothed.yaw);
        state_.smoothed.pitch += a * (pose->pitch - state_.smoothed.pitch);
    }

    if (centers_.size() < 2) return std::nullopt;  // nothing to switch between

    int nearest = 0;
    for (size_t i = 0; i < centers_.size(); ++i) {
        state_.distances[i] = dist(state_.smoothed, centers_[i]);
        if (state_.distances[i] < state_.distances[nearest]) nearest = static_cast<int>(i);
    }
    state_.nearest = nearest;
    state_.rejected = state_.distances[nearest] > reject_distance_;

    int candidate;
    if (state_.rejected) {
        candidate = state_.current;  // looking elsewhere: hold
    } else if (state_.current < 0) {
        candidate = nearest;
    } else if (nearest != state_.current &&
               state_.distances[nearest] < state_.distances[state_.current] * (1.0 - params_.hysteresis)) {
        candidate = nearest;
    } else {
        candidate = state_.current;
    }

    if (candidate == state_.current || candidate < 0) {
        state_.pending = -1;
        return std::nullopt;
    }
    if (candidate != state_.pending) {
        state_.pending = candidate;
        state_.pending_since_ms = now_ms;
    }
    if (now_ms - state_.pending_since_ms >= params_.dwell_ms &&
        now_ms - last_switch_ms_ >= params_.cooldown_ms) {
        state_.current = candidate;
        state_.pending = -1;
        last_switch_ms_ = now_ms;
        return candidate;
    }
    return std::nullopt;
}

}  // namespace ss
