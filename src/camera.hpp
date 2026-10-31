// SPDX-License-Identifier: GPL-3.0-or-later
// Webcam capture through OpenCV's V4L2 backend.
#pragma once

#include <string>
#include <vector>

#include <opencv2/videoio.hpp>

namespace ss {

struct CameraInfo {
    std::string path;    // /dev/videoN
    std::string name;    // driver card name
    bool color = false;  // offers a colour format (not an IR-only sensor)
};

// Video capture devices, excluding metadata nodes. Colour cameras first.
std::vector<CameraInfo> list_cameras();

class Camera {
public:
    ~Camera() { close(); }

    // spec: "auto", an index ("0") or a path ("/dev/video0").
    bool open(const std::string& spec, int width, int height, double fps, std::string* error);
    void close();
    bool is_open() const { return cap_.isOpened(); }
    // Returns the newest frame, dropping frames queued while we were idle.
    bool read(cv::Mat& frame);
    const std::string& device() const { return device_; }

private:
    cv::VideoCapture cap_;
    std::string device_;
    double fps_ = 10;
    int64_t last_read_ticks_ = 0;
};

}  // namespace ss
