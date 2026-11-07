// SPDX-License-Identifier: GPL-3.0-or-later
// Face detection (YuNet) and head direction estimation.
#pragma once

#include <optional>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>

#include "pose_math.hpp"

namespace ss {

struct FaceObservation {
    Pose pose;
    float score = 0;
    cv::Rect2f box;
    Landmarks landmarks;  // in the coordinates of the frame passed in
};

// Finds a bundled model by file name: installed data dir, then next to the
// binary, then the source tree (for development builds).
std::optional<std::string> find_bundled_model(const std::string& file);

class HeadPoseEstimator {
public:
    // Search order: explicit path, $SCREENSENSE_MODEL, installed data dir,
    // source tree (for development builds).
    static std::optional<std::string> find_model(const std::string& configured);

    bool load(const std::string& model_path, int detect_width, std::string* error);
    // Uses the largest face in the frame, since that is almost always the
    // person sitting at the computer.
    std::optional<FaceObservation> estimate(const cv::Mat& frame_bgr, double min_score);

private:
    cv::Ptr<cv::FaceDetectorYN> detector_;
    cv::Size input_size_{0, 0};
    int detect_width_ = 240;
    cv::Mat small_;
};

}  // namespace ss
