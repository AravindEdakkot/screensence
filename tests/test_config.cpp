// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.hpp"
#include "config.hpp"

using namespace ss;

int main() {
    // Defaults.
    {
        Config c = parse_config("");
        CHECK(c.activate_window && c.move_pointer);
        CHECK(!c.calibration.valid());
    }
    // Round trip, including calibration.
    {
        Config c;
        c.activate_window = false;
        c.fps = 7.5;
        c.camera = "/dev/video2";
        c.calibration.layout = {{0, 116, 1920, 1080}, {1920, 0, 1920, 1080}};
        c.calibration.centers = {{-0.123, 0.51}, {0.234, 0.49}};
        std::vector<std::string> w;
        Config d = parse_config(serialize_config(c), &w);
        CHECK(w.empty());
        CHECK(!d.activate_window && d.move_pointer);
        CHECK(d.fps == 7.5);
        CHECK(d.camera == "/dev/video2");
        CHECK(d.calibration.valid());
        CHECK(d.calibration.layout == c.calibration.layout);
        CHECK(d.calibration.centers.size() == 2 && d.calibration.centers[1].yaw == 0.234);
    }
    // Bad values warn and keep defaults; unknown keys warn.
    {
        std::vector<std::string> w;
        Config c = parse_config("[tracking]\ndwell_ms = soon\nhysteresis = 5\nbogus = 1\n[focus]\nmove_pointer = no\n", &w);
        CHECK(w.size() == 3);
        CHECK(c.dwell_ms == Config{}.dwell_ms);
        CHECK(c.hysteresis == Config{}.hysteresis);
        CHECK(!c.move_pointer);
    }
    // Incomplete calibration is rejected.
    {
        std::vector<std::string> w;
        Config c = parse_config("[calibration]\nmonitor0 = 0,0,1920,1080\npose0 = 0.1,0.5\nmonitor1 = 1920,0,1920,1080\n", &w);
        CHECK(!c.calibration.valid());
        CHECK(!w.empty());
    }
    TEST_MAIN_END();
}
