// SPDX-License-Identifier: GPL-3.0-or-later
// The daemon: camera -> head pose -> tracker -> desktop backend.
#include <csignal>
#include <cstdio>
#include <memory>

#include <glib-unix.h>

#include "backends/backend.hpp"
#include "camera.hpp"
#include "commands.hpp"
#include "config.hpp"
#include "daemon_bus.hpp"
#include "engine_util.hpp"
#include "head_pose.hpp"
#include "tracker.hpp"

namespace ss {
namespace {

void log(const char* fmt, auto... args) {
    std::fprintf(stderr, "screensense: ");
    if constexpr (sizeof...(args) == 0) std::fputs(fmt, stderr);
    else std::fprintf(stderr, fmt, args...);
    std::fputc('\n', stderr);
}

// Logs a message only when it differs from the previous one, so a daemon
// stuck waiting (no extension, no camera) does not flood the journal.
struct OnceLogger {
    std::string last;
    void operator()(const std::string& msg) {
        if (msg != last) log("%s", msg.c_str());
        last = msg;
    }
};

}  // namespace

int run_daemon(const RunOptions& opt) {
    const std::string config_path = opt.config_path.empty() ? default_config_path() : opt.config_path;
    auto load = [&] {
        std::vector<std::string> warnings;
        Config c = load_config(config_path, &warnings);
        for (const auto& w : warnings) log("%s: %s", config_path.c_str(), w.c_str());
        return c;
    };
    Config cfg = load();

    std::string err;
    auto backend = create_backend(opt.backend, &err);
    if (!backend) {
        log("%s", err.c_str());
        return 1;
    }

    auto model = HeadPoseEstimator::find_model(cfg.model_path);
    if (!model) {
        log("face model not found; set [camera] model in %s or $SCREENSENSE_MODEL", config_path.c_str());
        return 1;
    }
    HeadPoseEstimator estimator;
    if (!estimator.load(*model, cfg.detect_width, &err)) {
        log("%s", err.c_str());
        return 1;
    }

    bool stop = false, reload = false, paused = false;
    Camera camera;

    // The debug view borrows the camera from a running daemon instead of
    // competing with it, and does not claim the daemon's bus name.
    DaemonBus bus;
    bool borrowed = false;
    std::string borrowed_prev_state;
    if (opt.debug) {
        borrowed_prev_state = daemon_client::state();
        if (!borrowed_prev_state.empty()) {
            borrowed = daemon_client::call("Pause");
            if (borrowed) log("paused the running daemon while debugging");
        }
    } else {
        auto handler = [&](DaemonBus::Command c) {
            switch (c) {
                case DaemonBus::Command::Pause: paused = true; break;
                case DaemonBus::Command::Resume: paused = false; break;
                case DaemonBus::Command::Toggle: paused = !paused; break;
                case DaemonBus::Command::Reload: reload = true; break;
            }
            if (paused) {
                camera.close();  // release immediately: privacy, and calibration needs it
                bus.set_state("paused");
            }
        };
        if (!bus.start(handler, &err)) {
            log("%s", err.c_str());
            return 1;
        }
    }

    auto on_stop = [](gpointer p) -> gboolean { *static_cast<bool*>(p) = true; return G_SOURCE_CONTINUE; };
    g_unix_signal_add(SIGINT, on_stop, &stop);
    g_unix_signal_add(SIGTERM, on_stop, &stop);
    g_unix_signal_add(SIGHUP, on_stop, &reload);

    OnceLogger status;
    auto set_state = [&](const std::string& s, const std::string& why = {}) {
        if (!opt.debug) bus.set_state(s);
        if (!why.empty()) status(why);
    };

    bool backend_ok = false;
    int64_t next_backend_try = 0;
    std::vector<int> calib_to_backend;      // calibration index -> backend monitor index
    bool calibrated = false;
    int64_t next_layout_check = 0;
    std::unique_ptr<Tracker> tracker;
    int last_observed = -1;                 // calibration index where focus was last seen, -1 unknown
    int64_t next_sync = 0;
    int64_t last_face = now_ms();
    int64_t next_camera_try = 0;

    auto backend_to_calib = [&](std::optional<int> b) -> std::optional<int> {
        if (!b) return std::nullopt;
        for (size_t i = 0; i < calib_to_backend.size(); ++i)
            if (calib_to_backend[i] == *b) return static_cast<int>(i);
        return std::nullopt;
    };

    log("started (backend %s, model %s)%s", backend->name().c_str(), model->c_str(),
        opt.dry_run ? ", dry run: focus will not change" : "");

    while (!stop) {
        const int64_t frame_start = now_ms();

        if (reload) {
            reload = false;
            const int old_detect_width = cfg.detect_width;
            cfg = load();
            if (cfg.detect_width != old_detect_width && !estimator.load(*model, cfg.detect_width, &err))
                log("%s", err.c_str());
            tracker.reset();
            next_layout_check = 0;
            camera.close();  // camera settings may have changed
            log("configuration reloaded");
        }

        if (!backend_ok) {
            if (frame_start >= next_backend_try) {
                backend_ok = backend->connect(&err);
                next_backend_try = frame_start + 5000;
                if (backend_ok) status("connected to the " + backend->name() + " desktop backend");
            }
            if (!backend_ok) {
                camera.close();
                set_state("no-desktop", err);
                wait_ms(1000, stop);
                continue;
            }
        }

        if (paused) {
            camera.close();
            set_state("paused", "paused");
            wait_ms(500, stop);
            continue;
        }
        if (backend->session_locked()) {
            camera.close();
            set_state("locked", "screen locked, camera off");
            wait_ms(500, stop);
            continue;
        }

        if (frame_start >= next_layout_check) {
            next_layout_check = frame_start + 2000;
            auto mons = backend->monitors();
            if (mons.empty()) {  // extension disabled or shell restarting
                backend_ok = false;
                continue;
            }
            auto map = match_layout(cfg.calibration, mons);
            if (map && (!calibrated || *map != calib_to_backend || !tracker)) {
                calib_to_backend = *map;
                tracker = std::make_unique<Tracker>(TrackerParams::from_config(cfg), cfg.calibration.centers);
                last_observed = -1;
                next_sync = 0;
            }
            calibrated = map.has_value();
            if (!calibrated) tracker.reset();
        }
        if (!calibrated && !opt.debug) {
            camera.close();
            set_state("uncalibrated", cfg.calibration.valid()
                                          ? "monitor layout changed since calibration; run `screensense calibrate`"
                                          : "not calibrated yet; run `screensense calibrate`");
            wait_ms(2000, stop);
            continue;
        }

        if (!camera.is_open()) {
            if (frame_start < next_camera_try) {
                wait_ms(next_camera_try - frame_start, stop);
                continue;
            }
            if (!camera.open(cfg.camera, cfg.capture_width, cfg.capture_height, cfg.fps, &err)) {
                set_state("no-camera", "camera: " + err);
                next_camera_try = frame_start + 5000;
                continue;
            }
            status("camera " + camera.device() + " on");
            last_face = now_ms();
        }

        cv::Mat frame;
        if (!camera.read(frame)) {
            camera.close();
            set_state("no-camera", "camera stopped delivering frames; reopening");
            next_camera_try = frame_start + 2000;
            continue;
        }

        auto obs = estimator.estimate(frame, cfg.min_face_score);
        const int64_t t = now_ms();
        if (obs) last_face = t;
        const bool idle = t - last_face > static_cast<int64_t>(cfg.idle_after_s * 1000);
        set_state(idle ? "idle" : "tracking", idle ? "no face visible, slowing down" : "tracking");

        if (tracker) {
            // Follow focus changes the user makes by hand, so we do not fight them.
            if (tracker->state().pending < 0 && t >= next_sync) {
                next_sync = t + 500;
                const int observed = backend_to_calib(backend->current_monitor(cfg.activate_window)).value_or(-1);
                if (observed >= 0 && observed != last_observed) {
                    last_observed = observed;
                    tracker->set_current(observed);
                }
            }

            auto target = tracker->update(obs ? std::optional<Pose>(obs->pose) : std::nullopt, t);
            if (target) {
                const int bidx = calib_to_backend[*target];
                if (!opt.dry_run) {
                    const bool activate = cfg.activate_window && backend->can_activate_window();
                    const bool pointer = cfg.move_pointer && backend->can_move_pointer();
                    if (!backend->focus_monitor(bidx, activate, pointer)) backend_ok = false;
                }
                // Record where focus really is now. If the target monitor has
                // no windows, focus stays put, and we must not retry forever.
                last_observed = opt.dry_run
                                    ? *target
                                    : backend_to_calib(backend->current_monitor(cfg.activate_window)).value_or(-1);
                if (opt.debug) std::printf("\n-> monitor %d%s\n", *target + 1, opt.dry_run ? " (dry run)" : "");
            }
        }

        if (opt.debug) {
            if (obs) {
                std::printf("\rface %.2f  yaw %+.3f  pitch %.3f", obs->score, obs->pose.yaw, obs->pose.pitch);
                if (tracker) {
                    const auto& s = tracker->state();
                    std::printf("  | dist");
                    for (double d : s.distances) std::printf(" %.3f", d);
                    std::printf("  nearest %d%s  focus %d  pending %s   ", s.nearest + 1, s.rejected ? " (too far)" : "",
                                s.current + 1, s.pending < 0 ? "-" : std::to_string(s.pending + 1).c_str());
                } else {
                    std::printf("  | not calibrated              ");
                }
            } else {
                std::printf("\rno face%-90s", "");
            }
            std::fflush(stdout);
        }

        const double fps = idle ? cfg.idle_fps : cfg.fps;
        const int64_t period = static_cast<int64_t>(1000.0 / fps);
        wait_ms(period - (now_ms() - frame_start), stop);
    }

    camera.close();
    if (opt.debug) std::printf("\n");
    if (borrowed && daemon_client::call("Reload") && borrowed_prev_state != "paused") daemon_client::call("Resume");
    log("stopped");
    return 0;
}

}  // namespace ss
