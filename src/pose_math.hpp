// SPDX-License-Identifier: GPL-3.0-or-later
// Head direction from five facial landmarks. Header-only and dependency-free
// so it can be unit-tested without OpenCV.
#pragma once

#include <cmath>

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

}  // namespace ss
