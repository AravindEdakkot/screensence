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
    // New tracking settings and per-monitor eye values round trip.
    {
        Config c;
        c.input_hold_ms = 900;
        c.use_eyes = false;
        c.eye_gain = 1.25;
        c.calibration.layout = {{0, 0, 1920, 1080}, {1920, 0, 1920, 1080}};
        c.calibration.centers = {{0.1, 0.5}, {-0.7, 0.5}};
        c.calibration.eyes = {0.04, -0.05};
        std::vector<std::string> w;
        Config d = parse_config(serialize_config(c), &w);
        CHECK(w.empty());
        CHECK(d.input_hold_ms == 900 && !d.use_eyes && d.eye_gain == 1.25);
        CHECK(d.calibration.has_eyes());
        CHECK(d.calibration.eyes[1] == -0.05);
    }
    // A calibration from before eye tracking still works, without eyes.
    {
        std::vector<std::string> w;
        Config c = parse_config(
            "[calibration]\nmonitor0 = 0,0,1920,1080\npose0 = 0.1,0.5\nmonitor1 = 1920,0,1920,1080\npose1 = -0.7,0.5\n", &w);
        CHECK(w.empty());
        CHECK(c.calibration.valid() && !c.calibration.has_eyes());
    }
    // Eye values for only some monitors are ignored, with a warning.
    {
        std::vector<std::string> w;
        Config c = parse_config(
            "[calibration]\nmonitor0 = 0,0,1920,1080\npose0 = 0.1,0.5\neye0 = 0.1\n"
            "monitor1 = 1920,0,1920,1080\npose1 = -0.7,0.5\n", &w);
        CHECK(w.size() == 1);
        CHECK(c.calibration.valid() && !c.calibration.has_eyes());
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
    // Changing one value leaves every other line alone, comments included.
    {
        const std::string before =
            "# my note\n[focus]\n# about windows\nactivate_window = true\n  move_pointer=yes\n\n"
            "[camera]\nmove_pointer = kept\ndevice = auto # odd but mine\n";
        const std::string after = set_config_value(before, "focus", "move_pointer", "false");
        CHECK(after ==
              "# my note\n[focus]\n# about windows\nactivate_window = true\nmove_pointer = false\n\n"
              "[camera]\nmove_pointer = kept\ndevice = auto # odd but mine\n");
        CHECK(set_config_value(after, "focus", "move_pointer", "false") == after);
    }
    // A commented-out key is not the key. The real one goes under the header.
    {
        const std::string after = set_config_value("[focus]\n# move_pointer = true\n", "focus", "move_pointer", "false");
        CHECK(after == "[focus]\nmove_pointer = false\n# move_pointer = true\n");
        CHECK(!parse_config(after).move_pointer);
    }
    // Duplicates: the parser takes the last one, so all of them must change.
    {
        const std::string after =
            set_config_value("[focus]\nmove_pointer = true\n[focus]\nmove_pointer = true\n", "focus", "move_pointer", "false");
        CHECK(!parse_config(after).move_pointer);
    }
    // A missing section is added, and an empty file becomes a valid one.
    {
        std::vector<std::string> w;
        Config c = parse_config(set_config_value("", "focus", "move_pointer", "false"), &w);
        CHECK(w.empty());
        CHECK(!c.move_pointer && c.activate_window);
        const std::string after = set_config_value("[camera]\nfps = 7.5", "focus", "activate_window", "false");
        c = parse_config(after, &w);
        CHECK(w.empty());
        CHECK(!c.activate_window && c.fps == 7.5);
    }
    TEST_MAIN_END();
}
