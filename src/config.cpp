// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ss {
namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(s);
    while (std::getline(in, cur, sep)) out.push_back(trim(cur));
    return out;
}

std::optional<bool> parse_bool(const std::string& v) {
    if (v == "true" || v == "yes" || v == "on" || v == "1") return true;
    if (v == "false" || v == "no" || v == "off" || v == "0") return false;
    return std::nullopt;
}

std::optional<double> parse_double(const std::string& v) {
    if (v.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    double d = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == v.c_str() || *end != '\0') return std::nullopt;
    return d;
}

std::optional<int> parse_int(const std::string& v) {
    auto d = parse_double(v);
    if (!d || *d != static_cast<int>(*d)) return std::nullopt;
    return static_cast<int>(*d);
}

std::string fmt_double(double d) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", d);
    return buf;
}

}  // namespace

std::string default_config_path() {
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "screensense" / "config.ini").string();
    const char* home = std::getenv("HOME");
    return (fs::path(home ? home : ".") / ".config" / "screensense" / "config.ini").string();
}

Config parse_config(const std::string& text, std::vector<std::string>* warnings) {
    Config cfg;
    auto warn = [&](const std::string& msg) {
        if (warnings) warnings->push_back(msg);
    };

    std::string section;
    std::map<int, MonitorGeometry> layout;
    std::map<int, Pose> centers;
    std::istringstream in(text);
    std::string raw;
    int lineno = 0;
    while (std::getline(in, raw)) {
        ++lineno;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            warn("line " + std::to_string(lineno) + ": expected key = value");
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        std::string where = "line " + std::to_string(lineno) + " (" + key + ")";

        auto set_bool = [&](bool& dst) {
            if (auto b = parse_bool(val)) dst = *b; else warn(where + ": expected true/false");
        };
        auto set_double = [&](double& dst, double lo, double hi) {
            auto d = parse_double(val);
            if (d && *d >= lo && *d <= hi) dst = *d;
            else warn(where + ": expected a number in [" + fmt_double(lo) + ", " + fmt_double(hi) + "]");
        };
        auto set_int = [&](int& dst, int lo, int hi) {
            auto i = parse_int(val);
            if (i && *i >= lo && *i <= hi) dst = *i;
            else warn(where + ": expected an integer in [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        };

        if (section == "focus") {
            if (key == "activate_window") set_bool(cfg.activate_window);
            else if (key == "move_pointer") set_bool(cfg.move_pointer);
            else warn(where + ": unknown key");
        } else if (section == "camera") {
            if (key == "device") cfg.camera = val.empty() ? "auto" : val;
            else if (key == "width") set_int(cfg.capture_width, 160, 4096);
            else if (key == "height") set_int(cfg.capture_height, 120, 4096);
            else if (key == "detect_width") set_int(cfg.detect_width, 96, 1280);
            else if (key == "fps") set_double(cfg.fps, 0.5, 60);
            else if (key == "idle_fps") set_double(cfg.idle_fps, 0.1, 60);
            else if (key == "idle_after_s") set_double(cfg.idle_after_s, 0, 3600);
            else if (key == "model") cfg.model_path = val;
            else warn(where + ": unknown key");
        } else if (section == "tracking") {
            if (key == "dwell_ms") set_int(cfg.dwell_ms, 0, 10000);
            else if (key == "cooldown_ms") set_int(cfg.cooldown_ms, 0, 10000);
            else if (key == "hysteresis") set_double(cfg.hysteresis, 0, 0.9);
            else if (key == "smoothing") set_double(cfg.smoothing, 0.01, 1);
            else if (key == "min_face_score") set_double(cfg.min_face_score, 0, 1);
            else warn(where + ": unknown key");
        } else if (section == "calibration") {
            // monitorN = x,y,width,height  and  poseN = yaw,pitch
            auto index_of = [&](const std::string& prefix) -> std::optional<int> {
                if (key.rfind(prefix, 0) != 0) return std::nullopt;
                return parse_int(key.substr(prefix.size()));
            };
            if (auto i = index_of("monitor"); i && *i >= 0) {
                auto parts = split(val, ',');
                std::optional<int> n[4];
                if (parts.size() == 4)
                    for (int k = 0; k < 4; ++k) n[k] = parse_int(parts[k]);
                if (parts.size() == 4 && n[0] && n[1] && n[2] && n[3])
                    layout[*i] = {*n[0], *n[1], *n[2], *n[3]};
                else
                    warn(where + ": expected x,y,width,height");
            } else if (auto j = index_of("pose"); j && *j >= 0) {
                auto parts = split(val, ',');
                std::optional<double> a, b;
                if (parts.size() == 2) { a = parse_double(parts[0]); b = parse_double(parts[1]); }
                if (a && b) centers[*j] = {*a, *b};
                else warn(where + ": expected yaw,pitch");
            } else {
                warn(where + ": unknown key");
            }
        } else {
            warn(where + ": key outside a known section");
        }
    }

    // Accept calibration only if indices are contiguous from 0 and match.
    bool ok = !layout.empty() && layout.size() == centers.size();
    for (int i = 0; ok && i < static_cast<int>(layout.size()); ++i)
        ok = layout.count(i) && centers.count(i);
    if (ok) {
        for (auto& [i, g] : layout) cfg.calibration.layout.push_back(g);
        for (auto& [i, p] : centers) cfg.calibration.centers.push_back(p);
    } else if (!layout.empty() || !centers.empty()) {
        warn("calibration section is incomplete; run `screensense calibrate`");
    }
    return cfg;
}

Config load_config(const std::string& path, std::vector<std::string>* warnings) {
    std::ifstream f(path);
    if (!f) return Config{};
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_config(ss.str(), warnings);
}

std::string serialize_config(const Config& c) {
    std::ostringstream o;
    auto b = [](bool v) { return v ? "true" : "false"; };
    o << "# ScreenSense configuration. Edit freely; unknown keys are reported on start.\n\n"
      << "[focus]\n"
      << "# Make the most recently used window on the monitor you look at active.\n"
      << "activate_window = " << b(c.activate_window) << "\n"
      << "# Move the mouse pointer to the monitor you look at.\n"
      << "move_pointer = " << b(c.move_pointer) << "\n\n"
      << "[camera]\n"
      << "# auto, an index such as 0, or a device path such as /dev/video0\n"
      << "device = " << c.camera << "\n"
      << "width = " << c.capture_width << "\n"
      << "height = " << c.capture_height << "\n"
      << "# Face detection input width. Larger is more precise but costs more CPU.\n"
      << "detect_width = " << c.detect_width << "\n"
      << "fps = " << fmt_double(c.fps) << "\n"
      << "idle_fps = " << fmt_double(c.idle_fps) << "\n"
      << "idle_after_s = " << fmt_double(c.idle_after_s) << "\n";
    if (!c.model_path.empty()) o << "model = " << c.model_path << "\n";
    o << "\n[tracking]\n"
      << "dwell_ms = " << c.dwell_ms << "\n"
      << "cooldown_ms = " << c.cooldown_ms << "\n"
      << "hysteresis = " << fmt_double(c.hysteresis) << "\n"
      << "smoothing = " << fmt_double(c.smoothing) << "\n"
      << "min_face_score = " << fmt_double(c.min_face_score) << "\n";
    if (c.calibration.valid()) {
        o << "\n[calibration]\n"
          << "# Written by `screensense calibrate`.\n";
        for (size_t i = 0; i < c.calibration.layout.size(); ++i) {
            const auto& g = c.calibration.layout[i];
            const auto& p = c.calibration.centers[i];
            o << "monitor" << i << " = " << g.x << "," << g.y << "," << g.width << "," << g.height << "\n"
              << "pose" << i << " = " << fmt_double(p.yaw) << "," << fmt_double(p.pitch) << "\n";
        }
    }
    return o.str();
}

bool save_config(const Config& cfg, const std::string& path, std::string* error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (ec) {
        if (error) *error = "cannot create " + fs::path(path).parent_path().string() + ": " + ec.message();
        return false;
    }
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) {
            if (error) *error = "cannot write " + tmp + ": " + std::strerror(errno);
            return false;
        }
        f << serialize_config(cfg);
        if (!f.flush()) {
            if (error) *error = "write failed for " + tmp;
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

}  // namespace ss
