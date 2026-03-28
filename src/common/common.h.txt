
#ifndef COMMON_H
#define COMMON_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <ctime>

// 全局常量
const int VIDEO_WIDTH = 1920;
const int VIDEO_HEIGHT = 1080;
const int FPS = 30;
const int TRANS_DELAY = 200;     // 转播切换延迟(ms)
const int CLOSEUP_DURATION = 2;  // 特写持续时间(s)

// 高光事件类型
enum class HighlightType {
    NONE,
    SHOOT,   // 射门
    SAVE,    // 扑救
    GOAL     // 进球
};

// 表情类型
enum class EmotionType {
    UNKNOWN,
    HAPPY,   // 开心/庆祝
    SAD,     // 失落/遗憾
    CALM     // 平静
};

// 目标类型
enum class TargetType {
    BALL,        // 足球
    PLAYER,      // 射门球员
    GOALKEEPER,  // 门将
    PERSON       // 观众
};

// 目标检测结果
struct TargetInfo {
    TargetType type;
    cv::Rect box;
    double confidence;
    double timestamp;
};

// 高光事件信息
struct HighlightInfo {
    HighlightType type;
    double start_time;
    double end_time;
    cv::Rect main_target;
    std::vector<TargetInfo> related_targets;
};

// 人脸/特写信息
struct FaceInfo {
    EmotionType emotion;
    cv::Rect face_box;
    cv::Mat closeup_img;
    double timestamp;
    std::string belong; // "player"/"goalkeeper"/"audience"
};

// 转播模式
enum class BroadcastMode {
    NORMAL,   // 常规近景
    FOLLOW,   // 重点跟拍
    CLOSEUP   // 高光特写
};

// 公共工具函数
namespace CommonTool {
    double getCurrentTimestamp();
    cv::Mat imageSharpen(const cv::Mat& img);
    std::string highlightType2Str(HighlightType type);
    std::string emotionType2Str(EmotionType type);
}

#endif // COMMON_H