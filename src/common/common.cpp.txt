#include "common.h"
#include <opencv2/imgproc.hpp>

namespace CommonTool {
    double getCurrentTimestamp() {
        return (double)clock() / CLOCKS_PER_SEC;
    }

    cv::Mat imageSharpen(const cv::Mat& img) {
        if (img.empty()) return cv::Mat();
        cv::Mat blur, sharpen;
        cv::GaussianBlur(img, blur, cv::Size(3, 3), 0);
        cv::addWeighted(img, 1.5, blur, -0.5, 0, sharpen);
        return sharpen;
    }

    std::string highlightType2Str(HighlightType type) {
        switch (type) {
            case HighlightType::SHOOT: return "射门";
            case HighlightType::SAVE: return "扑救";
            case HighlightType::GOAL: return "进球";
            default: return "无";
        }
    }

    std::string emotionType2Str(EmotionType type) {
        switch (type) {
            case EmotionType::HAPPY: return "开心";
            case EmotionType::SAD: return "失落";
            case EmotionType::CALM: return "平静";
            default: return "未知";
        }
    }
}