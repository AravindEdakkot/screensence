// SPDX-License-Identifier: GPL-3.0-or-later
// Head direction from five facial landmarks. Header-only and dependency-free
// so it can be unit-tested without OpenCV.
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "config.hpp"

namespace ss {

struct Pt {
    double x = 0, y = 0;
};

// Landmarks in image coordinates, as YuNet reports them.
struct Landmarks {
    Pt right_eye, left_eye, nose, right_mouth, left_mouth;
};

// Returns dimensionless yaw and pitch ratios:
//  - yaw:   horizontal offset of the nose tip from the eye midpoint, divided
//           by the distance between the eyes. About 0 when facing the camera.
//  - pitch: vertical position of the nose tip between the eye line (0) and
//           the mouth line (1). About 0.5 when facing the camera.
// Head roll is removed first by rotating so the eyes are level. Both values
// are independent of face size, so distance to the camera does not matter.
inline Pose pose_from_landmarks(const Landmarks& l) {
    const Pt eye_mid{(l.right_eye.x + l.left_eye.x) / 2, (l.right_eye.y + l.left_eye.y) / 2};
    const Pt mouth_mid{(l.right_mouth.x + l.left_mouth.x) / 2, (l.right_mouth.y + l.left_mouth.y) / 2};
    const double ex = l.left_eye.x - l.right_eye.x;
    const double ey = l.left_eye.y - l.right_eye.y;
    const double eye_dist = std::hypot(ex, ey);
    if (eye_dist < 1e-6) return {};

    const double angle = std::atan2(ey, ex);
    const double c = std::cos(-angle), s = std::sin(-angle);
    auto rotate = [&](Pt p) { return Pt{p.x * c - p.y * s, p.x * s + p.y * c}; };

    const Pt nose = rotate({l.nose.x - eye_mid.x, l.nose.y - eye_mid.y});
    const Pt mouth = rotate({mouth_mid.x - eye_mid.x, mouth_mid.y - eye_mid.y});

    if (std::abs(mouth.y) < 1e-6) return {};

    // How far down the face the nose sits, eye line = 0, mouth line = 1.
    const double t = nose.y / mouth.y;
    // Where the face midline (eye midpoint to mouth midpoint) is at that height.
    const double midline_x = mouth.x * t;

    Pose p;
    p.yaw = (nose.x - midline_x) / eye_dist;
    p.pitch = t;
    return p;
}

// One eye, as read from the iris landmark model.
struct EyeReading {
    double offset = 0;  // iris from the middle of the eye, in eye widths, positive towards image right
    double width = 0;   // corner to corner, in the model's input pixels
};

// Corners, lids and iris are in the model's crop, which is level with the eyes
// and mirrored for the right eye. Empty if the eye is closed, or if the points
// are not plausibly an eye.
inline std::optional<EyeReading> eye_reading(Pt corner_a, Pt corner_b, Pt lid_upper, Pt lid_lower, Pt iris,
                                             bool mirrored) {
    constexpr double kMinWidth = 8;       // of 64: too foreshortened to read
    constexpr double kMinOpening = 0.12;  // lid gap / width: below this the eye is shut
    constexpr double kMaxOffset = 0.5;    // an iris cannot leave the eye

    const double ux = corner_b.x - corner_a.x, uy = corner_b.y - corner_a.y;
    const double width = std::hypot(ux, uy);
    if (width < kMinWidth) return std::nullopt;
    if (std::hypot(lid_upper.x - lid_lower.x, lid_upper.y - lid_lower.y) / width < kMinOpening) return std::nullopt;

    const double mid_x = (corner_a.x + corner_b.x) / 2, mid_y = (corner_a.y + corner_b.y) / 2;
    double offset = ((iris.x - mid_x) * ux + (iris.y - mid_y) * uy) / (width * width);
    if (std::abs(offset) > kMaxOffset) return std::nullopt;
    if (mirrored) offset = -offset;
    return EyeReading{offset, width};
}

// Average of the eyes that could be read. A head turned away foreshortens the
// far eye, so the wider, better-seen eye counts for more.
inline std::optional<double> combine_eyes(const std::optional<EyeReading>& a, const std::optional<EyeReading>& b) {
    if (a && b) return (a->offset * a->width + b->offset * b->width) / (a->width + b->width);
    if (a) return a->offset;
    if (b) return b->offset;
    return std::nullopt;
}

// Where the person is looking: the head direction plus the eyes' horizontal
// turn within the head. Vertical eye movement is left out because the eyelids
// make it far less reliable to read.
inline Pose with_eyes(Pose head, double eye_offset, double gain) {
    head.yaw += gain * eye_offset;
    return head;
}

// Whether the eyes need reading at all. While the head stays close to the
// monitor that has focus there is nothing to overrule, and the model costs as
// much CPU as everything else combined.
inline bool head_near_monitor(const Pose& head, const Calibration& c, int monitor) {
    constexpr double kNearShare = 0.25;  // of the gap between the two closest monitors
    if (monitor < 0 || monitor >= static_cast<int>(c.centers.size())) return false;
    double gap = INFINITY;
    for (size_t i = 0; i < c.centers.size(); ++i)
        for (size_t j = i + 1; j < c.centers.size(); ++j)
            gap = std::min(gap, std::hypot(c.centers[i].yaw - c.centers[j].yaw, c.centers[i].pitch - c.centers[j].pitch));
    const Pose& m = c.centers[monitor];
    return std::hypot(head.yaw - m.yaw, head.pitch - m.pitch) < kNearShare * gap;
}

// Result of watching someone keep their eyes still while turning their head.
struct EyeFit {
    bool head_moved = false;   // the head turned enough to measure anything
    bool usable = false;       // the eyes visibly moved against the head
    double gain = 0;           // eye movement * gain cancels the head movement
    double correlation = 0;    // -1 is a perfect match
};

// With the eyes fixed on one point, every head turn should be matched by an
// opposite eye turn. If it is, that gives the weight for this person and camera.
// If it is not, the eye readings are noise here and must not steer anything.
inline EyeFit fit_eye_gain(const std::vector<double>& yaw, const std::vector<double>& eye) {
    constexpr size_t kMinSamples = 20;
    constexpr double kMinHeadSpread = 0.05;
    constexpr double kMaxCorrelation = -0.5;
    constexpr double kMinGain = 0.3, kMaxGain = 6.0;

    EyeFit fit;
    const size_t n = std::min(yaw.size(), eye.size());
    if (n < kMinSamples) return fit;
    double mean_y = 0, mean_e = 0;
    for (size_t i = 0; i < n; ++i) { mean_y += yaw[i]; mean_e += eye[i]; }
    mean_y /= n;
    mean_e /= n;
    double var_y = 0, var_e = 0, cov = 0;
    for (size_t i = 0; i < n; ++i) {
        var_y += (yaw[i] - mean_y) * (yaw[i] - mean_y);
        var_e += (eye[i] - mean_e) * (eye[i] - mean_e);
        cov += (yaw[i] - mean_y) * (eye[i] - mean_e);
    }
    fit.head_moved = std::sqrt(var_y / n) >= kMinHeadSpread;
    if (!fit.head_moved || var_e <= 0) return fit;
    fit.correlation = cov / std::sqrt(var_y * var_e);
    // Fit eye against head, not the other way round: the eye reading is the
    // noisier one, and noise in the input of a fit would shrink the slope.
    const double slope = cov / var_y;
    if (slope >= 0) return fit;
    fit.gain = -1.0 / slope;
    fit.usable = fit.correlation <= kMaxCorrelation && fit.gain >= kMinGain && fit.gain <= kMaxGain;
    return fit;
}

// The monitor centres the tracker should compare against, for these settings.
inline std::vector<Pose> gaze_centers(const Config& c) {
    std::vector<Pose> out = c.calibration.centers;
    if (c.use_eyes && c.calibration.has_eyes())
        for (size_t i = 0; i < out.size(); ++i) out[i] = with_eyes(out[i], c.calibration.eyes[i], c.eye_gain);
    return out;
}

}  // namespace ss
