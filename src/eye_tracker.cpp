// SPDX-License-Identifier: GPL-3.0-or-later
#include "eye_tracker.hpp"

#include <cmath>

#include <opencv2/imgproc.hpp>

namespace ss {
namespace {

constexpr int kInputSize = 64;          // the model's input is 64 x 64 pixels
constexpr double kCropPerEyeGap = 1.05;  // crop side, as a share of the distance between the eyes
constexpr int kBlinkHoldMs = 500;       // how long a blink may repeat the last reading

// Layout of the model's outputs: x, y, z per point, in input pixels.
constexpr int kContourValues = 71 * 3;
constexpr int kCornerA = 0, kCornerB = 8, kLidLower = 4, kLidUpper = 12;

}  // namespace

bool EyeTracker::load(const std::string& model_path, std::string* error) {
    try {
        net_ = cv::dnn::readNetFromONNX(model_path);
        outputs_ = net_.getUnconnectedOutLayersNames();
    } catch (const cv::Exception& e) {
        if (error) *error = "cannot load eye model " + model_path + ": " + e.what();
        return false;
    }
    loaded_ = true;
    return true;
}

std::optional<EyeReading> EyeTracker::read_eye(const cv::Mat& frame, Pt center, double angle, double side,
                                               bool is_right_eye) {
    // Maps the image to a level, square crop around the eye. The model only
    // knows left eyes, so the right eye is mirrored, as MediaPipe does.
    const double scale = kInputSize / side;
    const double c = std::cos(angle) * scale, s = std::sin(angle) * scale;
    const double mirror = is_right_eye ? -1 : 1;
    const double half = kInputSize / 2.0;
    const cv::Matx23d m(mirror * c, mirror * s, half - mirror * (c * center.x + s * center.y),
                        -s, c, half - (-s * center.x + c * center.y));
    cv::Mat crop;
    cv::warpAffine(frame, crop, cv::Mat(m), cv::Size(kInputSize, kInputSize), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    const cv::Mat blob = cv::dnn::blobFromImage(crop, 1.0 / 255, crop.size(), cv::Scalar(), /*swapRB=*/true, false);
    net_.setInput(blob);
    std::vector<cv::Mat> out;
    net_.forward(out, outputs_);
    if (out.size() != 2) return std::nullopt;
    const bool contour_first = static_cast<int>(out[0].total()) == kContourValues;
    const float* contour = out[contour_first ? 0 : 1].ptr<float>();
    const float* iris = out[contour_first ? 1 : 0].ptr<float>();
    const auto point = [](const float* values, int i) { return Pt{values[i * 3], values[i * 3 + 1]}; };

    return eye_reading(point(contour, kCornerA), point(contour, kCornerB), point(contour, kLidUpper),
                       point(contour, kLidLower), point(iris, 0), is_right_eye);
}

std::optional<double> EyeTracker::offset(const cv::Mat& frame, const Landmarks& face, int64_t now_ms) {
    if (!loaded_ || frame.empty()) return std::nullopt;
    cv::Mat bgr;
    if (frame.channels() == 1) cv::cvtColor(frame, bgr, cv::COLOR_GRAY2BGR);
    else bgr = frame;

    const double dx = face.left_eye.x - face.right_eye.x, dy = face.left_eye.y - face.right_eye.y;
    const double gap = std::hypot(dx, dy);
    if (gap < 8) return std::nullopt;  // face too small for the eyes to mean anything
    const double angle = std::atan2(dy, dx);
    const double side = gap * kCropPerEyeGap;

    const auto right = read_eye(bgr, face.right_eye, angle, side, true);
    const auto left = read_eye(bgr, face.left_eye, angle, side, false);
    if (const auto combined = combine_eyes(right, left)) {
        last_offset_ = *combined;
        last_offset_ms_ = now_ms;
        return combined;
    }
    if (now_ms - last_offset_ms_ <= kBlinkHoldMs) return last_offset_;
    return std::nullopt;
}

}  // namespace ss
