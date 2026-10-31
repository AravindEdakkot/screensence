#include <cstdio>
#include <cstring>
#include <string>

#include "camera.hpp"
#include "commands.hpp"
#include "daemon_bus.hpp"

namespace {

void usage(FILE* out) {
    std::fprintf(out,
        "Usage: screensense [COMMAND] [OPTIONS]\n"
        "\n"
        "Moves keyboard focus and/or the pointer to the monitor you are looking at,\n"
        "using the webcam. Video never leaves this computer and is never saved.\n"
        "\n"
        "Commands:\n"
        "  run          Run the background service (default)\n"
        "  calibrate    Look at each monitor in turn to teach ScreenSense your setup\n"
        "  debug        Show live head direction and decisions, without changing focus\n"
        "  pause        Pause a running service (camera off)\n"
        "  resume       Resume a paused service\n"
        "  toggle       Pause or resume\n"
        "  status       Print the running service's state\n"
        "  cameras      List detected cameras\n"
        "\n"
        "Options:\n"
        "  --config PATH     Use this config file instead of ~/.config/screensense/config.ini\n"
        "  --backend NAME    Desktop backend: auto (default), gnome, null\n"
        "  --act             With debug: actually change focus\n"
        "  --version         Print version\n"
        "  -h, --help        Show this help\n");
}

int control(const char* method) {
    if (!ss::daemon_client::running()) {
        std::fprintf(stderr, "screensense: the service is not running\n");
        return 1;
    }
    return ss::daemon_client::call(method) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string command = "run";
    ss::RunOptions opt;
    bool act = false;
    bool have_command = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "screensense: %s needs a value\n", flag);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        else if (a == "--version") { std::printf("screensense %s\n", SCREENSENSE_VERSION); return 0; }
        else if (a == "--config") opt.config_path = value("--config");
        else if (a == "--backend") opt.backend = value("--backend");
        else if (a == "--act") act = true;
        else if (!a.empty() && a[0] != '-' && !have_command) { command = a; have_command = true; }
        else {
            std::fprintf(stderr, "screensense: unexpected argument '%s'\n\n", a.c_str());
            usage(stderr);
            return 2;
        }
    }

    if (command == "run") return ss::run_daemon(opt);
    if (command == "debug") {
        opt.debug = true;
        opt.dry_run = !act;
        return ss::run_daemon(opt);
    }
    if (command == "calibrate") return ss::run_calibrate(opt);
    if (command == "pause") return control("Pause");
    if (command == "resume") return control("Resume");
    if (command == "toggle") return control("Toggle");
    if (command == "status") {
        std::string s = ss::daemon_client::state();
        std::printf("%s\n", s.empty() ? "not running" : s.c_str());
        return s.empty() ? 3 : 0;
    }
    if (command == "cameras") {
        auto cams = ss::list_cameras();
        if (cams.empty()) std::printf("No cameras found.\n");
        for (const auto& c : cams)
            std::printf("%-14s %-32s %s\n", c.path.c_str(), c.name.c_str(),
                        c.color ? "colour" : "infrared/greyscale (not used by auto)");
        return 0;
    }
    std::fprintf(stderr, "screensense: unknown command '%s'\n\n", command.c_str());
    usage(stderr);
    return 2;
}
