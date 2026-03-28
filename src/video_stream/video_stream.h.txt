#ifndef VIDEO_STREAM_H
#define VIDEO_STREAM_H
#include "common.h"

class VideoStreamManager {
public:
    bool init(int camIndex = 0);
    cv::Mat readFrame();
    void setMode(BroadcastMode mode);
    bool pushStream(const std::string& rtmpUrl);
    void release();
private:
    cv::VideoCapture cap;
    BroadcastMode currentMode = BroadcastMode::NORMAL;
};

#endif