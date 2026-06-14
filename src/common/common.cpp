#include "common.h"

#include <filesystem>
#include <iostream>
#include <opencv2/imgproc.hpp>

namespace CommonTool {
    double getCurrentTimestamp() {
        return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
    }

    cv::Mat imageSharpen(const cv::Mat& img) {
        if (img.empty()) {
            return cv::Mat();
        }

        cv::Mat blur;
        cv::Mat sharpen;
        cv::GaussianBlur(img, blur, cv::Size(3, 3), 0);
        cv::addWeighted(img, 1.5, blur, -0.5, 0, sharpen);
        return sharpen;
    }

    std::string finalVideoOutputDir() {
#ifdef FINAL_OUTPUT_VIDEO_DIR
        std::filesystem::path outputDir(FINAL_OUTPUT_VIDEO_DIR);
#else
        const std::filesystem::path currentDir = std::filesystem::current_path();
        std::filesystem::path outputDir = currentDir.filename() == "bin" &&
                currentDir.parent_path().filename() == "build"
            ? currentDir / "output video"
            : currentDir / "build" / "bin" / "output video";
#endif
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec) {
            std::cerr << "Failed to create final video output directory: "
                      << outputDir.string() << " (" << ec.message() << ")" << std::endl;
        }
        return outputDir.generic_string();
    }

    std::string finalVideoOutputPath(const std::string& fileNameOrPath) {
        const std::filesystem::path source(fileNameOrPath);
        const std::filesystem::path fileName = source.filename().empty()
            ? std::filesystem::path("highlight.mp4")
            : source.filename();
        return (std::filesystem::path(finalVideoOutputDir()) / fileName).generic_string();
    }

    std::string highlightType2Str(HighlightType type) {
        switch (type) {
            case HighlightType::SHOOT:
                return "shoot";
            case HighlightType::SAVE:
                return "save";
            case HighlightType::GOAL:
                return "goal";
            case HighlightType::NONE:
            default:
                return "none";
        }
    }

    std::string emotionType2Str(EmotionType type) {
        switch (type) {
            case EmotionType::HAPPY:
                return "happy";
            case EmotionType::SAD:
                return "sad";
            case EmotionType::CALM:
                return "calm";
            case EmotionType::UNKNOWN:
            default:
                return "unknown";
        }
    }

    std::string targetType2Str(TargetType type) {
        switch (type) {
            case TargetType::BALL:
                return "ball";
            case TargetType::PLAYER:
                return "player";
            case TargetType::GOALKEEPER:
                return "goalkeeper";
            case TargetType::PERSON:
                return "person";
            default:
                return "unknown";
        }
    }

    std::string broadcastMode2Str(BroadcastMode mode) {
        switch (mode) {
            case BroadcastMode::FOLLOW:
                return "follow";
            case BroadcastMode::CLOSEUP:
                return "closeup";
            case BroadcastMode::NORMAL:
            default:
                return "panorama";
        }
    }

    std::string cameraRole2Str(CameraRole role) {
        switch (role) {
            case CameraRole::PANORAMA:
                return "panorama";
            case CameraRole::CLOSEUP:
                return "closeup";
            default:
                return "unknown";
        }
    }
}
