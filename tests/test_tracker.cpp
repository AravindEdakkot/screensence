// SPDX-License-Identifier: GPL-3.0-or-later
#include <optional>
#include <vector>

#include "check.hpp"
#include "pose_math.hpp"
#include "tracker.hpp"

using namespace ss;

namespace {

const std::vector<Pose> kTwo = {{-0.20, 0.50}, {0.20, 0.50}};  // left, right

TrackerParams params() {
    TrackerParams p;
    p.dwell_ms = 400;
    p.cooldown_ms = 600;
    p.hysteresis = 0.25;
    p.smoothing = 1.0;  // no smoothing: makes timings exact
    return p;
}

// Feeds `pose` every 100 ms from `t` for `ms`; returns the first switch.
std::optional<int> feed(Tracker& tr, int64_t& t, std::optional<Pose> pose, int ms) {
    std::optional<int> first;
    for (int elapsed = 0; elapsed < ms; elapsed += 100, t += 100) {
        auto r = tr.update(pose, t);
        if (r && !first) first = r;
    }
    return first;
}

void switches_after_dwell() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    CHECK(!feed(tr, t, Pose{0.21, 0.5}, 300));  // 0..200 ms: still dwelling
    auto r = feed(tr, t, Pose{0.21, 0.5}, 300);
    CHECK(r && *r == 1);
    CHECK(tr.state().current == 1);
}

void brief_glance_does_not_switch() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    CHECK(!feed(tr, t, Pose{0.2, 0.5}, 300));
    CHECK(!feed(tr, t, Pose{-0.2, 0.5}, 1000));
    CHECK(tr.state().current == 0);
}

void hysteresis_holds_near_the_middle() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    // Slightly right of centre: nearer to monitor 1, but not by the margin.
    CHECK(!feed(tr, t, Pose{0.02, 0.5}, 2000));
    CHECK(tr.state().nearest == 1);
    CHECK(tr.state().current == 0);
}

void losing_the_face_holds() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    CHECK(!feed(tr, t, Pose{0.2, 0.5}, 300));
    CHECK(!feed(tr, t, std::nullopt, 1000));  // dwell must restart after the gap
    CHECK(!feed(tr, t, Pose{0.2, 0.5}, 300));
    CHECK(feed(tr, t, Pose{0.2, 0.5}, 300).has_value());
}

void looking_far_away_is_rejected() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    // Far down (keyboard): closest to monitor 1 but beyond the reject distance.
    CHECK(!feed(tr, t, Pose{0.25, 1.2}, 2000));
    CHECK(tr.state().rejected);
    CHECK(tr.state().current == 0);
}

void cooldown_limits_ping_pong() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    auto first = feed(tr, t, Pose{0.2, 0.5}, 500);
    CHECK(first && *first == 1);
    const int64_t switched_at = t - 100;
    // Immediately look back: dwell is satisfied at +400 ms, but the cooldown
    // (600 ms) must delay the switch until then.
    std::optional<int> back;
    int64_t back_at = 0;
    for (int i = 0; i < 20 && !back; ++i, t += 100) {
        back = tr.update(Pose{-0.2, 0.5}, t);
        back_at = t;
    }
    CHECK(back && *back == 0);
    CHECK(back_at - switched_at >= 600);
}

void unknown_start_picks_nearest() {
    Tracker tr(params(), kTwo);
    int64_t t = 0;
    auto r = feed(tr, t, Pose{0.2, 0.5}, 500);
    CHECK(r && *r == 1);
}

void three_monitors_and_stacked() {
    // Left, centre, right plus a fourth one above the centre.
    std::vector<Pose> four = {{-0.3, 0.5}, {0.0, 0.5}, {0.3, 0.5}, {0.0, 0.25}};
    Tracker tr(params(), four);
    tr.set_current(1);
    int64_t t = 0;
    auto r = feed(tr, t, Pose{0.01, 0.26}, 600);
    CHECK(r && *r == 3);
    t += 1000;
    r = feed(tr, t, Pose{-0.29, 0.5}, 600);
    CHECK(r && *r == 0);
}

void single_monitor_never_switches() {
    Tracker tr(params(), {{0.0, 0.5}});
    int64_t t = 0;
    CHECK(!feed(tr, t, Pose{0.4, 0.5}, 2000));
}

void pose_math_is_scale_and_roll_invariant() {
    // A frontal face.
    Landmarks f{{100, 100}, {160, 100}, {130, 130}, {110, 160}, {150, 160}};
    Pose p = pose_from_landmarks(f);
    CHECK(std::abs(p.yaw) < 1e-9);
    CHECK(std::abs(p.pitch - 0.5) < 1e-9);

    // Nose moved right: positive yaw.
    Landmarks turned = f;
    turned.nose.x += 12;
    Pose q = pose_from_landmarks(turned);
    CHECK(q.yaw > 0.15);

    // Same face twice as large: same pose.
    Landmarks big;
    for (auto [src, dst] : {std::pair{&turned.right_eye, &big.right_eye}, {&turned.left_eye, &big.left_eye},
                            {&turned.nose, &big.nose}, {&turned.right_mouth, &big.right_mouth},
                            {&turned.left_mouth, &big.left_mouth}})
        *dst = {src->x * 2, src->y * 2};
    Pose b = pose_from_landmarks(big);
    CHECK(std::abs(b.yaw - q.yaw) < 1e-9);
    CHECK(std::abs(b.pitch - q.pitch) < 1e-9);

    // Rolled 30 degrees around the eye midpoint: same pose.
    const double a = 30 * M_PI / 180, c = std::cos(a), s = std::sin(a);
    auto rot = [&](Pt p) {
        Pt o{130, 100};
        return Pt{o.x + (p.x - o.x) * c - (p.y - o.y) * s, o.y + (p.x - o.x) * s + (p.y - o.y) * c};
    };
    Landmarks r{rot(turned.right_eye), rot(turned.left_eye), rot(turned.nose), rot(turned.right_mouth),
                rot(turned.left_mouth)};
    Pose rp = pose_from_landmarks(r);
    CHECK(std::abs(rp.yaw - q.yaw) < 1e-9);
    CHECK(std::abs(rp.pitch - q.pitch) < 1e-9);
}

}  // namespace

int main() {
    switches_after_dwell();
    brief_glance_does_not_switch();
    hysteresis_holds_near_the_middle();
    losing_the_face_holds();
    looking_far_away_is_rejected();
    cooldown_limits_ping_pong();
    unknown_start_picks_nearest();
    three_monitors_and_stacked();
    single_monitor_never_switches();
    pose_math_is_scale_and_roll_invariant();
    TEST_MAIN_END();
}
