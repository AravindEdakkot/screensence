#pragma once

#include <string>

namespace ss {

struct CommonOptions {
    std::string config_path;    // empty = default
    std::string backend = "auto";
};

struct RunOptions : CommonOptions {
    bool debug = false;   // print live pose and decisions
    bool dry_run = false; // decide but never change focus
};

int run_daemon(const RunOptions& opt);
int run_calibrate(const CommonOptions& opt);

}  // namespace ss
