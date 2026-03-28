#ifndef DETECTION_H
#define DETECTION_H
#include "common.h"

class TargetDetectionManager {
public:
    bool init();
    std::vector<TargetInfo> detect(const cv::Mat& frame);
    HighlightInfo detectHighlight(const std::vector<TargetInfo>& targets, double timestamp);
};

#endif