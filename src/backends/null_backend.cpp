// SPDX-License-Identifier: GPL-3.0-or-later
// Does nothing except log. For testing the camera and tracking on any
// desktop. Monitors come from $SCREENSENSE_FAKE_MONITORS, for example
// "0,0,1920,1080;1920,0,1920,1080", or default to two side-by-side 1080p.
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "backends/backend.hpp"

namespace ss {
namespace {

class NullBackend final : public Backend {
public:
    std::string name() const override { return "null"; }
    bool connect(std::string*) override { return true; }
    bool can_activate_window() const override { return true; }
    bool can_move_pointer() const override { return true; }

    std::vector<Monitor> monitors() override {
        std::vector<Monitor> out;
        const char* env = std::getenv("SCREENSENSE_FAKE_MONITORS");
        std::string spec = env && *env ? env : "0,0,1920,1080;1920,0,1920,1080";
        std::istringstream in(spec);
        std::string item;
        while (std::getline(in, item, ';')) {
            Monitor m;
            char c;
            std::istringstream f(item);
            if (f >> m.geometry.x >> c >> m.geometry.y >> c >> m.geometry.width >> c >> m.geometry.height) {
                m.index = static_cast<int>(out.size());
                m.primary = out.empty();
                out.push_back(m);
            }
        }
        return out;
    }

    bool focus_monitor(int index, bool activate, bool pointer) override {
        std::printf("null backend: focus monitor %d (activate=%d, pointer=%d)\n", index + 1, activate, pointer);
        std::fflush(stdout);
        current_ = index;
        return true;
    }

    std::optional<int> current_monitor(bool) override { return current_; }

private:
    int current_ = 0;
};

}  // namespace

std::unique_ptr<Backend> make_null_backend() { return std::make_unique<NullBackend>(); }

}  // namespace ss
