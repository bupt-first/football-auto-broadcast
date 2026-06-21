#ifndef VIDEO_STREAM_H
#define VIDEO_STREAM_H

#include "common.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>

class VideoStreamManager {
public:
    bool init(int camIndex = 0);
    bool initAuto(int minCameraIndex = 1, int maxCameraIndex = 6);
    bool openFile(const std::string& path);
    cv::Mat readFrame();
    void setMode(BroadcastMode mode);
    BroadcastMode mode() const;
    bool pushStream(const std::string& rtmpUrl);
    void release();

private:
    bool configureCapture();
    bool openCameraIndex(int camIndex);
    bool hasVisibleFrame();

    cv::VideoCapture cap;
    BroadcastMode currentMode = BroadcastMode::NORMAL;
};

class DualVideoStreamManager {
public:
    bool init(int panoramaCameraIndex = -1, int closeupCameraIndex = -1);
    DualCameraFrame readFrame();
    BroadcastMode mode() const;
    void setMode(BroadcastMode mode);
    int panoramaIndex() const;
    int closeupIndex() const;
    std::string panoramaSourceName() const;
    std::string closeupSourceName() const;
    void release();

private:
    bool openByIndex(cv::VideoCapture& capture, int cameraIndex, CameraRole role);
    bool openByName(cv::VideoCapture& capture, const std::string& deviceName, CameraRole role);
    bool openFirstVisible(
        cv::VideoCapture& capture,
        int minCameraIndex,
        int maxCameraIndex,
        int forbiddenIndex,
        CameraRole role,
        int& selectedIndex
    );
    bool configureCapture(cv::VideoCapture& capture, CameraRole role);
    bool hasVisibleFrame(cv::VideoCapture& capture) const;

    cv::VideoCapture panoramaCap;
    cv::VideoCapture closeupCap;
    BroadcastMode currentMode = BroadcastMode::NORMAL;
    int selectedPanoramaIndex = -1;
    int selectedCloseupIndex = -1;
    std::string selectedPanoramaSourceName = "未连接";
    std::string selectedCloseupSourceName = "未连接";
};

class FootballAutoBroadcastProcessor {
public:
    using RoiCallback = std::function<std::optional<cv::Point2f>(
        int frameIndex,
        double timestamp,
        const cv::Mat& frame
    )>;

    struct Config {
        cv::Size outputSize = cv::Size(1280, 720);
        int smoothingWindow = 5;
        double zoomFactor = 1.35;
        bool useReplicateBorder = true;
    };

    bool processFile(
        const std::string& inputPath,
        const std::string& outputPath,
        const RoiCallback& roiCallback,
        const Config& config = Config()
    );

    static cv::Point2f defaultCenter(const cv::Mat& frame);

private:
    static bool isValidCenter(const cv::Point2f& center);
    static cv::Size deriveCropSize(const cv::Size& frameSize, const cv::Size& outputSize, double zoomFactor);

    cv::Point2f smoothCenter(const cv::Point2f& center, int smoothingWindow);
    cv::Mat cropAndResize(const cv::Mat& frame, const cv::Point2f& center, const Config& config) const;

    std::deque<cv::Point2f> centerHistory;
    cv::Point2f lastSmoothedCenter = cv::Point2f(0.0f, 0.0f);
    bool hasSmoothedCenter = false;
};

#endif
