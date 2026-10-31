// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <regex>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <opencv2/core/utility.hpp>

namespace ss {
namespace {

int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do r = ioctl(fd, req, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

bool probe(const std::string& path, CameraInfo& info) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    v4l2_capability cap{};
    bool ok = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    if (ok) {
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        ok = (caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING);
    }
    if (ok) {
        info.path = path;
        info.name = reinterpret_cast<const char*>(cap.card);
        info.color = false;
        v4l2_fmtdesc fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        for (fmt.index = 0; xioctl(fd, VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
            switch (fmt.pixelformat) {
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_MJPEG:
                case V4L2_PIX_FMT_NV12:
                case V4L2_PIX_FMT_RGB24:
                case V4L2_PIX_FMT_BGR24:
                    info.color = true;
                    break;
                default:
                    break;  // e.g. GREY on infrared face-unlock cameras
            }
        }
    }
    ::close(fd);
    return ok;
}

int index_of(const std::string& path) {
    std::smatch m;
    static const std::regex re(R"(/dev/video(\d+)$)");
    return std::regex_search(path, m, re) ? std::stoi(m[1]) : -1;
}

}  // namespace

std::vector<CameraInfo> list_cameras() {
    std::vector<CameraInfo> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/dev", ec)) {
        const std::string p = e.path().string();
        if (index_of(p) < 0) continue;
        CameraInfo info;
        if (probe(p, info)) out.push_back(info);
    }
    std::sort(out.begin(), out.end(), [](const CameraInfo& a, const CameraInfo& b) {
        if (a.color != b.color) return a.color;
        return index_of(a.path) < index_of(b.path);
    });
    return out;
}

bool Camera::open(const std::string& spec, int width, int height, double fps, std::string* error) {
    close();
    std::vector<std::string> candidates;
    if (spec.empty() || spec == "auto") {
        for (const auto& c : list_cameras())
            if (c.color) candidates.push_back(c.path);
        if (candidates.empty()) {
            if (error) *error = "no colour webcam found (checked /dev/video*)";
            return false;
        }
    } else if (std::all_of(spec.begin(), spec.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
        candidates.push_back("/dev/video" + spec);
    } else {
        candidates.push_back(spec);
    }

    std::string last_error;
    for (const auto& path : candidates) {
        const int idx = index_of(path);
        bool opened = idx >= 0 ? cap_.open(idx, cv::CAP_V4L2) : cap_.open(path, cv::CAP_V4L2);
        if (!opened) {
            last_error = "cannot open " + path;
            continue;
        }
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        cap_.set(cv::CAP_PROP_FPS, std::max(fps, 5.0));
        cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
        cv::Mat probe_frame;
        if (!cap_.read(probe_frame) || probe_frame.empty()) {
            last_error = path + " opened but delivered no frames";
            cap_.release();
            continue;
        }
        device_ = path;
        fps_ = fps;
        last_read_ticks_ = cv::getTickCount();
        return true;
    }
    if (error) *error = last_error;
    return false;
}

void Camera::close() {
    if (cap_.isOpened()) cap_.release();
    device_.clear();
}

bool Camera::read(cv::Mat& frame) {
    if (!cap_.isOpened()) return false;
    // If we slept longer than a couple of camera frames, the driver queue
    // holds an old frame. Grabbing without decoding is cheap.
    const double idle_s = double(cv::getTickCount() - last_read_ticks_) / cv::getTickFrequency();
    if (idle_s > 0.15) cap_.grab();
    bool ok = cap_.read(frame) && !frame.empty();
    last_read_ticks_ = cv::getTickCount();
    return ok;
}

}  // namespace ss
