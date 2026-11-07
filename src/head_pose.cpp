// SPDX-License-Identifier: GPL-3.0-or-later
#include "head_pose.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <opencv2/imgproc.hpp>

namespace ss {

// OpenCV 4.8 changed FaceDetectorYN to a new model format, and each model
// works only with its own side of that change. Ubuntu 24.04 and Debian 12
// ship OpenCV 4.6, so both are bundled and the build picks one.
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 8)
static constexpr const char* kModelFile = "face_detection_yunet_2023mar.onnx";
#else
static constexpr const char* kModelFile = "face_detection_yunet_2022mar.onnx";
#endif

std::optional<std::string> find_bundled_model(const std::string& file) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    paths.emplace_back(fs::path(SCREENSENSE_INSTALLED_MODEL_DIR) / file);
    // Relocated installs (/opt, AppImage) and build trees: look next to the binary.
    std::error_code ec;
    const fs::path exe_dir = fs::read_symlink("/proc/self/exe", ec).parent_path();
    if (!ec) {
        paths.emplace_back(exe_dir / ".." / "share" / "screensense" / "models" / file);
        paths.emplace_back(exe_dir / ".." / "models" / file);
    }
    paths.emplace_back(fs::path(SCREENSENSE_SOURCE_MODEL_DIR) / file);
    for (const auto& p : paths) {
        std::error_code exists_ec;
        if (fs::is_regular_file(p, exists_ec)) return p.string();
    }
    return std::nullopt;
}

std::optional<std::string> HeadPoseEstimator::find_model(const std::string& configured) {
    namespace fs = std::filesystem;
    std::vector<fs::path> paths;
    if (!configured.empty()) paths.emplace_back(configured);
    if (const char* env = std::getenv("SCREENSENSE_MODEL"); env && *env) paths.emplace_back(env);
    for (const auto& p : paths) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    return find_bundled_model(kModelFile);
}

bool HeadPoseEstimator::load(const std::string& model_path, int detect_width, std::string* error) {
    detect_width_ = detect_width;
    // Head pose is a tiny workload. One thread keeps the footprint small, and
    // must be set before the model loads, or OpenCV starts a worker per core.
    cv::setNumThreads(1);
    try {
        // Low internal threshold; the caller applies its own min_score.
        detector_ = cv::FaceDetectorYN::create(model_path, "", cv::Size(detect_width_, detect_width_ * 3 / 4), 0.5f, 0.3f, 5);
    } catch (const cv::Exception& e) {
        if (error) *error = "cannot load face model " + model_path + ": " + e.what();
        return false;
    }
    return true;
}

std::optional<FaceObservation> HeadPoseEstimator::estimate(const cv::Mat& frame, double min_score) {
    if (!detector_ || frame.empty()) return std::nullopt;
    // Detection runs on a downscaled frame; never upscale.
    const int width = std::min(detect_width_, frame.cols);
    const double scale = double(width) / frame.cols;
    const cv::Size size(width, int(frame.rows * scale + 0.5));
    if (size != input_size_) {
        detector_->setInputSize(size);
        input_size_ = size;
    }
    cv::Mat input;
    const cv::Mat* src = &frame;
    if (frame.channels() == 1) {
        cv::cvtColor(frame, small_, cv::COLOR_GRAY2BGR);
        src = &small_;
    }
    if (size == src->size()) input = *src;
    else cv::resize(*src, input, size, 0, 0, cv::INTER_AREA);

    cv::Mat faces;
    detector_->detect(input, faces);
    if (faces.empty()) return std::nullopt;

    // Row layout: x, y, w, h, then (x, y) for right eye, left eye, nose tip,
    // right mouth corner, left mouth corner, then score.
    int best = -1;
    float best_area = 0;
    for (int i = 0; i < faces.rows; ++i) {
        const float* r = faces.ptr<float>(i);
        if (r[14] < min_score) continue;
        const float area = r[2] * r[3];
        if (area > best_area) { best_area = area; best = i; }
    }
    if (best < 0) return std::nullopt;

    const float* r = faces.ptr<float>(best);
    Landmarks lm{{r[4], r[5]}, {r[6], r[7]}, {r[8], r[9]}, {r[10], r[11]}, {r[12], r[13]}};
    FaceObservation obs;
    obs.pose = pose_from_landmarks(lm);
    obs.score = r[14];
    obs.box = cv::Rect2f(r[0] / scale, r[1] / scale, r[2] / scale, r[3] / scale);
    const auto to_frame = [&](Pt p) { return Pt{p.x / scale, p.y / scale}; };
    obs.landmarks = {to_frame(lm.right_eye), to_frame(lm.left_eye), to_frame(lm.nose), to_frame(lm.right_mouth),
                     to_frame(lm.left_mouth)};
    return obs;
}

}  // namespace ss
