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

void input_holds_a_due_switch_until_it_stops() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    bool typing = true;
    int asked = 0;
    const auto input_active = [&] { ++asked; return typing; };
    const Pose right{0.20, 0.50};

    // Not due yet: the desktop is not even asked, since asking costs a round trip.
    CHECK(!tr.update(right, t, input_active));
    t += 100;
    CHECK(!tr.update(right, t, input_active));
    CHECK(asked == 0);
    CHECK(!tr.state().input_hold);

    // Due, but typing: held for as long as it lasts.
    for (int i = 0; i < 50; ++i) {
        t += 100;
        CHECK(!tr.update(right, t, input_active));
    }
    CHECK(asked > 0);
    CHECK(tr.state().input_hold);
    CHECK(tr.state().current == 0);

    // Typing stops while still looking there: the switch happens straight away.
    typing = false;
    t += 100;
    auto r = tr.update(right, t, input_active);
    CHECK(r && *r == 1);
    CHECK(!tr.state().input_hold);
}

void looking_back_during_a_hold_cancels_the_switch() {
    Tracker tr(params(), kTwo);
    tr.set_current(0);
    int64_t t = 0;
    bool typing = true;
    const auto input_active = [&] { return typing; };
    for (int i = 0; i < 20; ++i, t += 100) tr.update(Pose{0.20, 0.50}, t, input_active);
    CHECK(tr.state().input_hold);
    // Head drifted back to the monitor being typed on, then typing stopped.
    tr.update(Pose{-0.20, 0.50}, t, input_active);
    t += 100;
    typing = false;
    auto r = tr.update(Pose{-0.20, 0.50}, t, input_active);
    CHECK(!r);
    CHECK(tr.state().pending == -1);
    CHECK(tr.state().current == 0);
}

void our_own_pointer_move_is_not_user_input() {
    const int hold = 1500;
    const int64_t long_ago = 1000000;
    CHECK(user_input_recent(200, long_ago, hold));    // typing
    CHECK(!user_input_recent(2000, long_ago, hold));  // hands off for a while
    CHECK(!user_input_recent(100, 100, hold));        // that "input" was our own move
    CHECK(!user_input_recent(150, 100, hold));        // ...even if the clocks disagree slightly
    CHECK(user_input_recent(100, 1000, hold));        // a real key press after our move
}

void eye_reading_measures_the_iris_within_the_eye() {
    const Pt a{20, 32}, b{44, 32}, up{32, 26}, low{32, 38};
    // Iris right of centre, by a quarter of the eye width.
    auto r = eye_reading(a, b, up, low, Pt{38, 32}, false);
    CHECK(r && std::abs(r->offset - 0.25) < 1e-9);
    CHECK(r->width == 24);
    // The same crop from a mirrored eye means the iris is left of centre in the image.
    r = eye_reading(a, b, up, low, Pt{38, 32}, true);
    CHECK(r && std::abs(r->offset + 0.25) < 1e-9);
    // A closed eye and a nonsense reading give nothing.
    CHECK(!eye_reading(a, b, Pt{32, 31}, Pt{32, 32}, Pt{32, 32}, false));
    CHECK(!eye_reading(a, b, up, low, Pt{60, 32}, false));
    // An eye seen nearly edge-on gives nothing.
    CHECK(!eye_reading(Pt{30, 32}, Pt{34, 32}, up, low, Pt{32, 32}, false));
}

void combining_eyes_favours_the_better_seen_one() {
    const EyeReading wide{0.2, 30}, narrow{-0.1, 10};
    auto c = combine_eyes(wide, narrow);
    CHECK(c && std::abs(*c - 0.125) < 1e-9);
    c = combine_eyes(std::nullopt, narrow);
    CHECK(c && *c == -0.1);
    CHECK(!combine_eyes(std::nullopt, std::nullopt));
}

void eyes_cancel_head_drift_once_calibrated() {
    // Calibrated: left monitor head -0.2 eye +0.05, right monitor head +0.2 eye -0.05.
    Config cfg;
    cfg.calibration.layout = {{0, 0, 1920, 1080}, {1920, 0, 1920, 1080}};
    cfg.calibration.centers = kTwo;
    cfg.calibration.eyes = {0.05, -0.05};
    cfg.eye_gain = 1.0;
    Tracker tr(params(), gaze_centers(cfg));
    tr.set_current(0);
    int64_t t = 0;

    // The head turns most of the way to the right monitor, the eyes stay put by
    // turning the other way: focus must stay. Head alone would have switched.
    const Pose drifted{0.10, 0.50};
    CHECK(!feed(tr, t, with_eyes(drifted, -0.25, cfg.eye_gain), 3000));
    Tracker head_only(params(), kTwo);
    head_only.set_current(0);
    int64_t t2 = 0;
    CHECK(feed(head_only, t2, drifted, 3000).has_value());

    // Head and eyes both go to the right monitor: focus follows.
    auto r = feed(tr, t, with_eyes(Pose{0.20, 0.50}, -0.05, cfg.eye_gain), 1000);
    CHECK(r && *r == 1);

    // Turned off, or with an old calibration, the centres are head direction only.
    cfg.use_eyes = false;
    CHECK(gaze_centers(cfg)[0].yaw == kTwo[0].yaw);
    cfg.use_eyes = true;
    cfg.calibration.eyes.clear();
    CHECK(gaze_centers(cfg)[1].yaw == kTwo[1].yaw);
}

void eyes_are_read_only_when_the_head_has_moved() {
    Calibration c;
    c.layout = {{0, 0, 1920, 1080}, {1920, 0, 1920, 1080}};
    c.centers = {{0.0, 0.5}, {-0.8, 0.5}};
    CHECK(head_near_monitor(Pose{-0.1, 0.5}, c, 0));
    CHECK(!head_near_monitor(Pose{-0.3, 0.5}, c, 0));
    CHECK(head_near_monitor(Pose{-0.7, 0.5}, c, 1));
    CHECK(!head_near_monitor(Pose{0.0, 0.5}, c, -1));  // focus unknown: must read them
}

void eye_fit_trusts_only_eyes_that_oppose_the_head() {
    std::vector<double> yaw, eye;
    // A head swinging about, with the eyes turning the opposite way plus some noise.
    for (int i = 0; i < 80; ++i) {
        const double y = 0.3 * std::sin(i * 0.2);
        yaw.push_back(y);
        eye.push_back(-y / 1.5 + 0.03 * std::cos(i * 1.7));
    }
    EyeFit f = fit_eye_gain(yaw, eye);
    CHECK(f.head_moved && f.usable);
    CHECK(f.correlation < -0.9);
    CHECK(std::abs(f.gain - 1.5) < 0.1);

    // Eyes moving with the head are not fixed on anything.
    std::vector<double> same = yaw;
    f = fit_eye_gain(yaw, same);
    CHECK(f.head_moved && !f.usable);

    // Noise unrelated to the head.
    std::vector<double> noise;
    for (int i = 0; i < 80; ++i) noise.push_back(0.06 * std::sin(i * 2.399));
    f = fit_eye_gain(yaw, noise);
    CHECK(f.head_moved && !f.usable);

    // A head that did not move proves nothing, and says so.
    std::vector<double> still(80, 0.1);
    f = fit_eye_gain(still, noise);
    CHECK(!f.head_moved && !f.usable);

    // Too few samples.
    f = fit_eye_gain({0.1, -0.1}, {-0.1, 0.1});
    CHECK(!f.usable);
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
    input_holds_a_due_switch_until_it_stops();
    looking_back_during_a_hold_cancels_the_switch();
    our_own_pointer_move_is_not_user_input();
    eye_reading_measures_the_iris_within_the_eye();
    combining_eyes_favours_the_better_seen_one();
    eyes_cancel_head_drift_once_calibrated();
    eyes_are_read_only_when_the_head_has_moved();
    eye_fit_trusts_only_eyes_that_oppose_the_head();
    TEST_MAIN_END();
}
