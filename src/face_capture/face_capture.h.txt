#ifndef FACE_CAPTURE_H
#define FACE_CAPTURE_H
#include "common.h"

class FaceCaptureManager {
public:
    bool init();
    std::vector<FaceInfo> capture(const cv::Mat& frame, double timestamp);
private:
    EmotionType recognizeEmotion(const cv::Mat& face);
};

#endif