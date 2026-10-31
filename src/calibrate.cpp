// `screensense calibrate`: record the head pose for each monitor.
//
// Camera placement differs on every desk (laptop on the left, centred under
// a monitor, webcam on top of the big screen), so we never guess. The user
// looks at each monitor in turn while we sample their head pose.
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>

#include <glib-unix.h>

#include "backends/backend.hpp"
#include "camera.hpp"
#include "commands.hpp"
#include "config.hpp"
#include "daemon_bus.hpp"
#include "engine_util.hpp"
#include "head_pose.hpp"

namespace ss {
namespace {

constexpr int kSettleMs = 2000;   // time to turn and settle on the target
constexpr int kSampleMs = 2500;   // time spent recording
constexpr size_t kMinSamples = 8;

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

struct Measured {
    Pose center;
    double spread = 0;  // median distance of samples from the centre
    size_t samples = 0;
};

// Pauses a running daemon so we can use the camera, and restores it on exit.
struct DaemonBorrow {
    std::string prev;
    bool paused = false;
    DaemonBorrow() {
        prev = daemon_client::state();
        if (!prev.empty()) paused = daemon_client::call("Pause");
    }
    ~DaemonBorrow() {
        if (!paused) return;
        daemon_client::call("Reload");
        if (prev != "paused") daemon_client::call("Resume");
    }
};

}  // namespace

int run_calibrate(const CommonOptions& opt) {
    const std::string config_path = opt.config_path.empty() ? default_config_path() : opt.config_path;
    std::vector<std::string> warnings;
    Config cfg = load_config(config_path, &warnings);
    for (const auto& w : warnings) std::fprintf(stderr, "%s: %s\n", config_path.c_str(), w.c_str());

    std::string err;
    auto backend = create_backend(opt.backend, &err);
    if (!backend || !backend->connect(&err)) {
        std::fprintf(stderr, "screensense: %s\n", err.c_str());
        return 1;
    }
    auto mons = backend->monitors();
    if (mons.size() < 2) {
        std::printf("Only one monitor is connected, so there is nothing to switch between.\n");
        return 1;
    }

    auto model = HeadPoseEstimator::find_model(cfg.model_path);
    HeadPoseEstimator estimator;
    if (!model || !estimator.load(*model, cfg.detect_width, &err)) {
        std::fprintf(stderr, "screensense: %s\n", model ? err.c_str() : "face model not found");
        return 1;
    }

    DaemonBorrow borrow;
    Camera camera;
    if (!camera.open(cfg.camera, cfg.capture_width, cfg.capture_height, 15, &err)) {
        std::fprintf(stderr, "screensense: camera: %s\n", err.c_str());
        return 1;
    }

    bool stop = false;
    auto on_stop = [](gpointer p) -> gboolean { *static_cast<bool*>(p) = true; return G_SOURCE_CONTINUE; };
    g_unix_signal_add(SIGINT, on_stop, &stop);
    g_unix_signal_add(SIGTERM, on_stop, &stop);

    // Prompt left to right, then top to bottom, which is how people scan.
    std::vector<Monitor> order = mons;
    std::sort(order.begin(), order.end(), [](const Monitor& a, const Monitor& b) {
        return a.geometry.x != b.geometry.x ? a.geometry.x < b.geometry.x : a.geometry.y < b.geometry.y;
    });

    std::printf("Calibrating %zu monitors with camera %s.\n"
                "Sit as you normally do. When a screen asks, look at its centre\n"
                "the way you would while working there, and hold still.\n\n",
                mons.size(), camera.device().c_str());

    std::vector<Measured> result(mons.size());
    for (const auto& m : order) {
        if (stop) break;
        Measured best;
        for (int attempt = 0; attempt < 2 && !stop; ++attempt) {
            backend->show_calibration_target(m.index, "Look at this screen");
            const int64_t settle_end = now_ms() + kSettleMs;
            cv::Mat frame;
            while (!stop && now_ms() < settle_end) {  // keep the camera's exposure adapting
                camera.read(frame);
                wait_ms(30, stop);
            }
            backend->show_calibration_target(m.index, "Hold still...");
            std::vector<double> yaw, pitch;
            const int64_t sample_end = now_ms() + kSampleMs;
            while (!stop && now_ms() < sample_end) {
                if (!camera.read(frame)) continue;
                if (auto obs = estimator.estimate(frame, cfg.min_face_score)) {
                    yaw.push_back(obs->pose.yaw);
                    pitch.push_back(obs->pose.pitch);
                }
                g_main_context_iteration(nullptr, FALSE);
            }
            if (yaw.size() >= kMinSamples) {
                best.center = {median(yaw), median(pitch)};
                std::vector<double> d;
                for (size_t i = 0; i < yaw.size(); ++i)
                    d.push_back(std::hypot(yaw[i] - best.center.yaw, pitch[i] - best.center.pitch));
                best.spread = median(d);
                best.samples = yaw.size();
                break;
            }
            std::printf("  monitor %d: face not seen clearly (%zu frames), trying again\n", m.index + 1, yaw.size());
        }
        backend->hide_calibration_target();
        if (best.samples == 0) {
            if (!stop)
                std::fprintf(stderr,
                             "screensense: could not see your face while you looked at monitor %d.\n"
                             "Check that the camera faces you and the room is lit, then try again.\n",
                             m.index + 1);
            return 1;
        }
        result[m.index] = best;
        std::printf("  monitor %d: yaw %+.3f  pitch %.3f  (spread %.3f, %zu frames)\n", m.index + 1,
                    best.center.yaw, best.center.pitch, best.spread, best.samples);
    }
    backend->hide_calibration_target();
    if (stop) {
        std::printf("Cancelled; the previous calibration was kept.\n");
        return 130;
    }

    // Can we tell the monitors apart? Compare the gap between the two
    // closest monitors with the jitter seen while holding still.
    double min_gap = INFINITY, max_spread = 0;
    for (size_t i = 0; i < result.size(); ++i) {
        max_spread = std::max(max_spread, result[i].spread);
        for (size_t j = i + 1; j < result.size(); ++j)
            min_gap = std::min(min_gap, std::hypot(result[i].center.yaw - result[j].center.yaw,
                                                   result[i].center.pitch - result[j].center.pitch));
    }
    std::printf("\nSeparation between closest monitors: %.3f, jitter: %.3f\n", min_gap, max_spread);
    if (min_gap < 4 * max_spread || min_gap < 0.05)
        std::printf("Warning: these monitors look similar from the camera. Switching may be\n"
                    "unreliable. Turning your head a little more, or moving the camera so it\n"
                    "sits between or below the screens, will help.\n");

    cfg.calibration.layout.clear();
    cfg.calibration.centers.clear();
    for (const auto& m : mons) {
        cfg.calibration.layout.push_back(m.geometry);
        cfg.calibration.centers.push_back(result[m.index].center);
    }
    if (!save_config(cfg, config_path, &err)) {
        std::fprintf(stderr, "screensense: %s\n", err.c_str());
        return 1;
    }
    std::printf("Saved to %s\n", config_path.c_str());
    return 0;
}

}  // namespace ss
