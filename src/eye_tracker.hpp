// SPDX-License-Identifier: GPL-3.0-or-later
// Where the eyes are pointing within the head, from the iris position.
//
// Uses MediaPipe's iris landmark model on a small crop around each eye. The
// head can turn while the eyes stay on the screen, which is what happens when
// you shift in your seat while typing. Adding this to the head direction
// (see with_eyes in pose_math.hpp) cancels that movement out.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include "pose_math.hpp"

namespace ss {

class EyeTracker {
public:
    bool load(const std::string& model_path, std::string* error);
    bool loaded() const { return loaded_; }

    // Iris offset averaged over both eyes, in eye widths, positive towards
    // the right of the image. During a blink, repeats the last value for a
    // moment. Empty when the eyes cannot be read at all.
    std::optional<double> offset(const cv::Mat& frame_bgr, const Landmarks& face, int64_t now_ms);

private:
    std::optional<EyeReading> read_eye(const cv::Mat& frame, Pt center, double angle, double side, bool is_right_eye);

    cv::dnn::Net net_;
    std::vector<std::string> outputs_;
    bool loaded_ = false;
    double last_offset_ = 0;
    int64_t last_offset_ms_ = INT64_MIN / 2;
};

}  // namespace ss
