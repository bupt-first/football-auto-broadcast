#ifndef YOLO_BYTETRACK_DETECTOR_H
#define YOLO_BYTETRACK_DETECTOR_H

#include "common.h"

#include <opencv2/dnn.hpp>

#include <string>

class YoloByteTrackDetector {
public:
    struct Config {
        bool enabled = true;
        std::string modelPath = "models/football_yolov8.onnx";
        cv::Size inputSize = cv::Size(640, 640);
        float confidenceThreshold = 0.25f;
        float highTrackThreshold = 0.45f;
        float nmsThreshold = 0.45f;
        float trackMatchThreshold = 0.18f;
        int maxLostFrames = 20;
        int ballClassId = 32;
        int playerClassId = 0;
        int goalkeeperClassId = -1;
        int refereeClassId = -1;
    };

    bool init(const Config& newConfig);
    bool isReady() const;
    void reset();

    std::vector<TargetInfo> detectAndTrack(const cv::Mat& frame, double timestamp);

private:
    struct Detection {
        TargetInfo target;
        int classId = -1;
    };

    struct Track {
        int id = -1;
        TargetType type = TargetType::PLAYER;
        cv::Rect2f box;
        cv::Point2f velocity;
        float score = 0.0f;
        double timestamp = 0.0;
        int lostFrames = 0;
        int classId = -1;
        int teamId = -1;
        std::string semanticLabel;
    };

    struct JerseyFeature {
        int targetIndex = -1;
        cv::Vec3f color;
    };

    std::vector<Detection> runYolo(const cv::Mat& frame);
    std::vector<TargetInfo> updateTracks(const std::vector<Detection>& detections, double timestamp);
    void postProcessFootballSemantics(std::vector<TargetInfo>& targets, const cv::Mat& frame) const;
    bool extractJerseyFeature(const cv::Mat& frame, const TargetInfo& target, cv::Vec3f& feature) const;
    std::string makeSemanticLabel(const TargetInfo& target) const;
    TargetType classToTargetType(int classId) const;

    static float intersectionOverUnion(const cv::Rect2f& lhs, const cv::Rect2f& rhs);
    static cv::Rect clampRectToFrame(const cv::Rect2f& rect, const cv::Size& frameSize);

    cv::dnn::Net net;
    Config config;
    cv::Size lastFrameSize;
    std::vector<Track> tracks;
    int nextTrackId = 1;
    bool ready = false;
};

#endif
