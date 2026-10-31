// SPDX-License-Identifier: GPL-3.0-or-later
// Helpers shared by the daemon loop and calibration.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include <glib.h>

#include "backends/backend.hpp"
#include "config.hpp"

namespace ss {

inline int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Sleeps while still dispatching D-Bus calls and signals on the default
// main context, so the daemon stays responsive and costs nothing while idle.
inline void wait_ms(int64_t ms, const bool& stop) {
    const int64_t deadline = now_ms() + ms;
    while (!stop) {
        const int64_t left = deadline - now_ms();
        if (left <= 0) break;
        bool fired = false;
        guint id = g_timeout_add(static_cast<guint>(left), [](gpointer p) -> gboolean {
            *static_cast<bool*>(p) = true;
            return G_SOURCE_REMOVE;
        }, &fired);
        g_main_context_iteration(nullptr, TRUE);
        if (!fired) g_source_remove(id);
    }
}

// For each calibrated monitor, the backend index of the monitor with the
// same geometry. Empty if the layout changed since calibration.
inline std::optional<std::vector<int>> match_layout(const Calibration& cal, const std::vector<Monitor>& mons) {
    if (!cal.valid() || cal.layout.size() != mons.size()) return std::nullopt;
    std::vector<int> map;
    for (const auto& g : cal.layout) {
        int found = -1;
        for (const auto& m : mons)
            if (m.geometry == g) found = m.index;
        if (found < 0) return std::nullopt;
        map.push_back(found);
    }
    return map;
}

}  // namespace ss
