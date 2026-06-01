#include "video_stream.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <utility>
#include <opencv2/imgproc.hpp>

namespace {
cv::Point2f averageHistory(const std::deque<cv::Point2f>& history) {
    if (history.empty()) {
        return cv::Point2f(0.0f, 0.0f);
    }

    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& point : history) {
        sumX += point.x;
        sumY += point.y;
    }

    const double count = static_cast<double>(history.size());
    return cv::Point2f(static_cast<float>(sumX / count), static_cast<float>(sumY / count));
}
}

bool VideoStreamManager::init(int camIndex) {
    release();

    if (!openCameraIndex(camIndex)) {
        return false;
    }

    if (!hasVisibleFrame()) {
        std::cerr << "Camera index " << camIndex << " opened but returns black frames." << std::endl;
    }

    return true;
}

bool VideoStreamManager::initAuto(int minCameraIndex, int maxCameraIndex) {
    release();

    for (int index = minCameraIndex; index <= maxCameraIndex; ++index) {
        std::cout << "Testing camera index " << index << "..." << std::endl;
        if (!openCameraIndex(index)) {
            continue;
        }

        if (hasVisibleFrame()) {
            std::cout << "Selected camera index: " << index << std::endl;
            return true;
        }

        std::cout << "Camera index " << index << " returned black frames, trying next." << std::endl;
        release();
    }

    std::cerr << "No visible USB camera found from index "
              << minCameraIndex << " to " << maxCameraIndex << "." << std::endl;
    return false;
}

bool VideoStreamManager::openFile(const std::string& path) {
    release();
    if (!cap.open(path)) {
        std::cerr << "Failed to open video file: " << path << std::endl;
        return false;
    }
    return configureCapture();
}

cv::Mat VideoStreamManager::readFrame() {
    cv::Mat frame;
    if (!cap.isOpened()) {
        return frame;
    }

    cap >> frame;
    if (frame.empty()) {
        std::cerr << "Failed to read frame from camera or video source." << std::endl;
    }
    return frame;
}

void VideoStreamManager::setMode(BroadcastMode mode) {
    currentMode = mode;
}

BroadcastMode VideoStreamManager::mode() const {
    return currentMode;
}

bool VideoStreamManager::pushStream(const std::string& rtmpUrl) {
    std::cout << "RTMP push is not implemented yet: " << rtmpUrl << std::endl;
    return false;
}

void VideoStreamManager::release() {
    if (cap.isOpened()) {
        cap.release();
    }
}

bool VideoStreamManager::configureCapture() {
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_FPS, FPS);
    std::cout << "Capture size: "
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)) << "x"
              << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
              << " @ " << cap.get(cv::CAP_PROP_FPS) << " fps" << std::endl;
    return cap.isOpened();
}

bool VideoStreamManager::openCameraIndex(int camIndex) {
    std::cout << "Opening camera index " << camIndex << " with DirectShow..." << std::endl;
    if (!cap.open(camIndex, cv::CAP_DSHOW)) {
        std::cout << "DirectShow failed. Trying default OpenCV backend..." << std::endl;
        cap.open(camIndex);
    }

    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera index: " << camIndex << std::endl;
        return false;
    }

    std::cout << "Camera opened: " << camIndex << std::endl;
    return configureCapture();
}

bool VideoStreamManager::hasVisibleFrame() {
    cv::Mat frame;
    if (!cap.isOpened()) {
        return false;
    }

    for (int i = 0; i < 20; ++i) {
        cap >> frame;
        if (frame.empty()) {
            continue;
        }

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        const cv::Scalar meanValue = cv::mean(gray);
        if (meanValue[0] > 8.0) {
            return true;
        }
        cv::waitKey(20);
    }
    return false;
}

bool DualVideoStreamManager::init(int panoramaCameraIndex, int closeupCameraIndex) {
    release();
    selectedPanoramaIndex = -1;
    selectedCloseupIndex = -1;
    currentMode = BroadcastMode::NORMAL;

    const bool panoramaReady = panoramaCameraIndex >= 0
        ? openByIndex(panoramaCap, panoramaCameraIndex, CameraRole::PANORAMA)
        : openByName(panoramaCap, "UGREEN Camera 1080P", CameraRole::PANORAMA) ||
              openFirstVisible(panoramaCap, 1, 8, -1, CameraRole::PANORAMA, selectedPanoramaIndex);

    if (!panoramaReady) {
        std::cerr << "Failed to open panorama camera. Expected device: UGREEN Camera 1080P." << std::endl;
        release();
        return false;
    }

    if (panoramaCameraIndex >= 0) {
        selectedPanoramaIndex = panoramaCameraIndex;
    }

    const bool closeupReady = closeupCameraIndex >= 0
        ? openByIndex(closeupCap, closeupCameraIndex, CameraRole::CLOSEUP)
        : openByName(closeupCap, "UVC Camera", CameraRole::CLOSEUP) ||
              openFirstVisible(closeupCap, 1, 8, selectedPanoramaIndex, CameraRole::CLOSEUP, selectedCloseupIndex);

    if (!closeupReady) {
        std::cerr << "Failed to open close-up camera. Expected device: UVC Camera." << std::endl;
        release();
        return false;
    }

    if (closeupCameraIndex >= 0) {
        selectedCloseupIndex = closeupCameraIndex;
    }

    std::cout << "Dual camera input ready. panorama_index=" << selectedPanoramaIndex
              << " closeup_index=" << selectedCloseupIndex << std::endl;
    return true;
}

DualCameraFrame DualVideoStreamManager::readFrame() {
    DualCameraFrame frame;
    frame.timestamp = CommonTool::getCurrentTimestamp();

    if (panoramaCap.isOpened()) {
        panoramaCap >> frame.panorama;
    }
    if (closeupCap.isOpened()) {
        closeupCap >> frame.closeup;
    }

    if (frame.panorama.empty()) {
        std::cerr << "Failed to read panorama frame." << std::endl;
    }
    if (frame.closeup.empty()) {
        std::cerr << "Failed to read close-up frame." << std::endl;
    }

    return frame;
}

BroadcastMode DualVideoStreamManager::mode() const {
    return currentMode;
}

void DualVideoStreamManager::setMode(BroadcastMode mode) {
    currentMode = mode;
}

int DualVideoStreamManager::panoramaIndex() const {
    return selectedPanoramaIndex;
}

int DualVideoStreamManager::closeupIndex() const {
    return selectedCloseupIndex;
}

void DualVideoStreamManager::release() {
    if (panoramaCap.isOpened()) {
        panoramaCap.release();
    }
    if (closeupCap.isOpened()) {
        closeupCap.release();
    }
}

bool DualVideoStreamManager::openByIndex(cv::VideoCapture& capture, int cameraIndex, CameraRole role) {
    if (capture.isOpened()) {
        capture.release();
    }

    std::cout << "Opening " << CommonTool::cameraRole2Str(role)
              << " camera index " << cameraIndex << " with DirectShow..." << std::endl;
    if (!capture.open(cameraIndex, cv::CAP_DSHOW)) {
        std::cout << "DirectShow failed. Trying default OpenCV backend..." << std::endl;
        capture.open(cameraIndex);
    }

    if (!capture.isOpened()) {
        std::cerr << "Failed to open " << CommonTool::cameraRole2Str(role)
                  << " camera index: " << cameraIndex << std::endl;
        return false;
    }

    if (!configureCapture(capture, role)) {
        capture.release();
        return false;
    }

    if (!hasVisibleFrame(capture)) {
        std::cerr << CommonTool::cameraRole2Str(role)
                  << " camera index " << cameraIndex << " opened but returns black frames." << std::endl;
    }

    return true;
}

bool DualVideoStreamManager::openByName(cv::VideoCapture& capture, const std::string& deviceName, CameraRole role) {
    if (capture.isOpened()) {
        capture.release();
    }

    const std::string directShowName = "video=" + deviceName;
    const std::string roleName = CommonTool::cameraRole2Str(role);

    std::cout << "Trying " << roleName << " camera by device name: " << deviceName << std::endl;
    if (!capture.open(directShowName, cv::CAP_FFMPEG) && !capture.open(deviceName, cv::CAP_DSHOW)) {
        std::cout << "Device-name open failed for " << deviceName << ". Falling back to camera indexes." << std::endl;
        capture.release();
        return false;
    }

    if (!configureCapture(capture, role)) {
        capture.release();
        return false;
    }

    if (!hasVisibleFrame(capture)) {
        std::cout << "Device-name open produced no visible frames for " << deviceName
                  << ". Falling back to camera indexes." << std::endl;
        capture.release();
        return false;
    }

    std::cout << "Opened " << roleName << " camera by name: " << deviceName << std::endl;
    return true;
}

bool DualVideoStreamManager::openFirstVisible(
    cv::VideoCapture& capture,
    int minCameraIndex,
    int maxCameraIndex,
    int forbiddenIndex,
    CameraRole role,
    int& selectedIndex
) {
    for (int index = minCameraIndex; index <= maxCameraIndex; ++index) {
        if (index == forbiddenIndex) {
            continue;
        }

        cv::VideoCapture candidate;
        if (!openByIndex(candidate, index, role)) {
            continue;
        }

        if (!hasVisibleFrame(candidate)) {
            candidate.release();
            continue;
        }

        capture = std::move(candidate);
        selectedIndex = index;
        return true;
    }

    return false;
}

bool DualVideoStreamManager::configureCapture(cv::VideoCapture& capture, CameraRole role) {
    if (!capture.isOpened()) {
        return false;
    }

    capture.set(cv::CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT);
    capture.set(cv::CAP_PROP_FPS, FPS);

    std::cout << CommonTool::cameraRole2Str(role) << " capture size: "
              << static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH)) << "x"
              << static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT))
              << " @ " << capture.get(cv::CAP_PROP_FPS) << " fps" << std::endl;
    return capture.isOpened();
}

bool DualVideoStreamManager::hasVisibleFrame(cv::VideoCapture& capture) const {
    cv::Mat frame;
    if (!capture.isOpened()) {
        return false;
    }

    for (int i = 0; i < 20; ++i) {
        capture >> frame;
        if (frame.empty()) {
            continue;
        }

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        if (cv::mean(gray)[0] > 8.0) {
            return true;
        }
        cv::waitKey(20);
    }
    return false;
}

bool FootballAutoBroadcastProcessor::processFile(
    const std::string& inputPath,
    const std::string& outputPath,
    const RoiCallback& roiCallback,
    const Config& config
) {
    centerHistory.clear();
    lastSmoothedCenter = cv::Point2f(0.0f, 0.0f);
    hasSmoothedCenter = false;

    cv::VideoCapture cap(inputPath);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open input video: " << inputPath << std::endl;
        return false;
    }

    cv::Mat firstFrame;
    if (!cap.read(firstFrame) || firstFrame.empty()) {
        std::cerr << "Input video contains no readable frame: " << inputPath << std::endl;
        return false;
    }

    cv::Size outputSize = config.outputSize;
    if (outputSize.width <= 0 || outputSize.height <= 0) {
        outputSize = firstFrame.size();
    }

    const double inputFps = cap.get(cv::CAP_PROP_FPS);
    const double outputFps = inputFps > 1.0 ? inputFps : FPS;
    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');

    cv::VideoWriter writer;
    if (!writer.open(outputPath, fourcc, outputFps, outputSize)) {
        std::cerr << "Failed to open output video: " << outputPath << std::endl;
        return false;
    }

    int frameIndex = 0;
    cv::Mat frame = firstFrame;
    do {
        const double timestamp = static_cast<double>(frameIndex) / outputFps;

        std::optional<cv::Point2f> roiCenter;
        if (roiCallback) {
            roiCenter = roiCallback(frameIndex, timestamp, frame);
        }

        cv::Point2f activeCenter;
        if (roiCenter.has_value() && isValidCenter(*roiCenter)) {
            activeCenter = *roiCenter;
        } else if (hasSmoothedCenter) {
            activeCenter = lastSmoothedCenter;
        } else {
            activeCenter = defaultCenter(frame);
        }

        const cv::Point2f smoothedCenter = smoothCenter(activeCenter, config.smoothingWindow);
        const cv::Mat outputFrame = cropAndResize(frame, smoothedCenter, config);
        writer.write(outputFrame);

        ++frameIndex;
    } while (cap.read(frame));

    writer.release();
    return true;
}

cv::Point2f FootballAutoBroadcastProcessor::defaultCenter(const cv::Mat& frame) {
    if (frame.empty()) {
        return cv::Point2f(0.0f, 0.0f);
    }

    return cv::Point2f(
        static_cast<float>(frame.cols / 2.0),
        static_cast<float>(frame.rows / 2.0)
    );
}

bool FootballAutoBroadcastProcessor::isValidCenter(const cv::Point2f& center) {
    return std::isfinite(center.x) && std::isfinite(center.y);
}

cv::Size FootballAutoBroadcastProcessor::deriveCropSize(
    const cv::Size& frameSize,
    const cv::Size& outputSize,
    double zoomFactor
) {
    const double safeZoom = std::max(1.0, zoomFactor);
    const double outputAspect = outputSize.height > 0
        ? static_cast<double>(outputSize.width) / static_cast<double>(outputSize.height)
        : static_cast<double>(frameSize.width) / std::max(1, frameSize.height);

    double cropW = static_cast<double>(frameSize.width) / safeZoom;
    double cropH = cropW / outputAspect;

    if (cropH > static_cast<double>(frameSize.height)) {
        cropH = static_cast<double>(frameSize.height) / safeZoom;
        cropW = cropH * outputAspect;
    }

    cropW = std::clamp(cropW, 1.0, static_cast<double>(frameSize.width));
    cropH = std::clamp(cropH, 1.0, static_cast<double>(frameSize.height));

    return cv::Size(
        std::max(1, static_cast<int>(std::round(cropW))),
        std::max(1, static_cast<int>(std::round(cropH)))
    );
}

cv::Point2f FootballAutoBroadcastProcessor::smoothCenter(const cv::Point2f& center, int smoothingWindow) {
    centerHistory.push_back(center);

    const int window = std::max(1, smoothingWindow);
    while (centerHistory.size() > static_cast<std::size_t>(window)) {
        centerHistory.pop_front();
    }

    lastSmoothedCenter = averageHistory(centerHistory);
    hasSmoothedCenter = true;
    return lastSmoothedCenter;
}

cv::Mat FootballAutoBroadcastProcessor::cropAndResize(
    const cv::Mat& frame,
    const cv::Point2f& center,
    const Config& config
) const {
    if (frame.empty()) {
        return cv::Mat();
    }

    cv::Size outputSize = config.outputSize;
    if (outputSize.width <= 0 || outputSize.height <= 0) {
        outputSize = frame.size();
    }

    const cv::Size cropSize = deriveCropSize(frame.size(), outputSize, config.zoomFactor);
    const int cropW = cropSize.width;
    const int cropH = cropSize.height;

    int left = static_cast<int>(std::round(center.x - cropW / 2.0));
    int top = static_cast<int>(std::round(center.y - cropH / 2.0));

    int padLeft = std::max(0, -left);
    int padTop = std::max(0, -top);
    int padRight = std::max(0, left + cropW - frame.cols);
    int padBottom = std::max(0, top + cropH - frame.rows);

    cv::Mat workingFrame = frame;
    if (padLeft > 0 || padTop > 0 || padRight > 0 || padBottom > 0) {
        if (config.useReplicateBorder) {
            cv::copyMakeBorder(
                frame,
                workingFrame,
                padTop,
                padBottom,
                padLeft,
                padRight,
                cv::BORDER_REPLICATE
            );
        } else {
            left = std::clamp(left, 0, std::max(0, frame.cols - cropW));
            top = std::clamp(top, 0, std::max(0, frame.rows - cropH));
            padLeft = padTop = padRight = padBottom = 0;
        }
    }

    const cv::Rect cropRect(left + padLeft, top + padTop, cropW, cropH);
    cv::Mat cropped = workingFrame(cropRect).clone();

    if (cropped.size() == outputSize) {
        return cropped;
    }

    cv::Mat output;
    cv::resize(cropped, output, outputSize, 0.0, 0.0, cv::INTER_LINEAR);
    return output;
}
