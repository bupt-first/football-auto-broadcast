#ifndef FACE_CAPTURE_H
#define FACE_CAPTURE_H

#include "common.h"

#include <map>
#include <opencv2/objdetect.hpp>
#include <utility>

class FaceCaptureManager {
public:
    bool init();
    std::vector<FaceInfo> capture(const cv::Mat& frame, double timestamp);
    std::vector<FaceInfo> process(const cv::Mat& frame);
    int getFaceCount() const;

private:
    bool detectFaces(const cv::Mat& frame, std::vector<cv::Rect>& faces);
    std::vector<std::pair<int, cv::Rect>> updateTracking(const std::vector<cv::Rect>& newFaces);
    float calcIOU(const cv::Rect& a, const cv::Rect& b) const;
    EmotionType recognizeEmotion(const cv::Mat& face);

    cv::CascadeClassifier faceCascade;
    std::map<int, cv::Rect> trackedFaces;
    int nextId = 1;
    bool cascadeLoaded = false;
};

#endif
