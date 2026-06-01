#ifndef COMMON_H
#define COMMON_H

#include <ctime>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

const int VIDEO_WIDTH = 1920;
const int VIDEO_HEIGHT = 1080;
const int FPS = 30;
const int TRANS_DELAY = 200;
const int CLOSEUP_DURATION = 2;

enum class HighlightType {
    NONE,
    SHOOT,
    SAVE,
    GOAL
};

enum class EmotionType {
    UNKNOWN,
    HAPPY,
    SAD,
    CALM
};

enum class TargetType {
    BALL,
    PLAYER,
    GOALKEEPER,
    PERSON
};

struct TargetInfo {
    TargetType type = TargetType::PLAYER;
    cv::Rect box;
    double confidence = 0.0;
    double timestamp = 0.0;
};

struct HighlightInfo {
    HighlightType type = HighlightType::NONE;
    double start_time = 0.0;
    double end_time = 0.0;
    cv::Rect main_target;
    std::vector<TargetInfo> related_targets;
};

struct FaceInfo {
    EmotionType emotion = EmotionType::UNKNOWN;
    cv::Rect face_box;
    cv::Mat closeup_img;
    double timestamp = 0.0;
    std::string belong;
};

struct EvaluationMetrics {
    int highlight_count = 0;
    int rendered_frame_count = 0;
    double average_duration = 0.0;
    double event_density = 0.0;
    double target_visibility = 0.0;
    double replay_score = 0.0;
};

enum class BroadcastMode {
    NORMAL,
    FOLLOW,
    CLOSEUP
};

enum class CameraRole {
    PANORAMA,
    CLOSEUP
};

struct DualCameraFrame {
    cv::Mat panorama;
    cv::Mat closeup;
    double timestamp = 0.0;
};

struct BroadcastDecision {
    BroadcastMode mode = BroadcastMode::NORMAL;
    std::string reason = "panorama";
    double hold_until = 0.0;
};

namespace CommonTool {
    double getCurrentTimestamp();
    cv::Mat imageSharpen(const cv::Mat& img);
    std::string highlightType2Str(HighlightType type);
    std::string emotionType2Str(EmotionType type);
    std::string targetType2Str(TargetType type);
    std::string broadcastMode2Str(BroadcastMode mode);
    std::string cameraRole2Str(CameraRole role);
}

#endif
