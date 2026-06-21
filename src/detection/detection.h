#ifndef DETECTION_H
#define DETECTION_H

#include "common.h"
#include "yolo_bytetrack_detector.h"

struct DetectionConfig {
    double motionThreshold = 25.0;
    double minContourArea = 150.0;
    double maxContourArea = 70000.0;
    double ballAreaThreshold = 1200.0;
    double playerAreaThreshold = 18000.0;
    double confidenceThreshold = 0.10;
    double duplicateOverlapThreshold = 0.50;
    double highlightCooldown = 2.0;
    double fastBallVelocity = 450.0;
    double closePlayerDistance = 110.0;
    double ballCandidateConfidenceThreshold = 0.20;
    double confirmedBallConfidenceThreshold = 0.42;
    double ballColorThreshold = 0.12;
    double ballGrassRejectThreshold = 0.55;
    double ballSkinRejectThreshold = 0.38;
    double ballSaturationRejectThreshold = 0.42;
    double ballPredictionGate = 85.0;
    double ballLostTimeout = 0.7;
    double autoBallBootstrapGate = 48.0;
    double autoBallBootstrapMinShift = 4.0;
    double autoBallBootstrapMaxShift = 95.0;
    double autoBallBootstrapMinAspect = 0.78;
    double autoBallBootstrapMaxAspect = 1.42;
    double autoBallBootstrapMaxAreaRatio = 0.70;
    double autoBallBootstrapSideMarginRatio = 0.08;
    double autoBallBootstrapTopRatio = 0.42;
    double autoBallBootstrapBottomRatio = 0.76;
    double autoBallTrackTopRatio = 0.38;
    double autoBallTrackBottomRatio = 0.84;
    double autoBallTrackSideMarginRatio = 0.04;
    int minBallTrackHits = 2;
    int autoBallBootstrapHits = 3;
    int maxTargets = 12;
    bool detectShoot = true;
    bool detectSave = true;
    bool detectGoal = true;
    bool detectDribbling = true;
    bool detectHeader = true;
    bool detectTackle = true;
    bool enableYoloByteTrack = true;
    std::string yoloModelPath = "models/football_yolov8.onnx";
    int yoloInputSize = 640;
    double yoloConfidenceThreshold = 0.25;
    double yoloHighTrackThreshold = 0.45;
    double yoloNmsThreshold = 0.45;
    double byteTrackMatchThreshold = 0.18;
    int byteTrackMaxLostFrames = 20;
    int yoloBallClassId = 32;
    int yoloPlayerClassId = 0;
    int yoloGoalkeeperClassId = -1;
    int yoloRefereeClassId = -1;
};

class TargetDetectionManager {
public:
    bool init();
    bool init(const DetectionConfig& config);

    std::vector<TargetInfo> detect(const cv::Mat& frame);
    std::vector<TargetInfo> detect(const cv::Mat& frame, double timestamp);
    HighlightInfo detectHighlight(const std::vector<TargetInfo>& targets, double timestamp);
    void seedBallTrack(const cv::Point2f& center, double timestamp, double radius);

    void setConfig(const DetectionConfig& config);
    const DetectionConfig& getConfig() const;
    void reset();

private:
    struct MotionHistory {
        cv::Point2f position;
        double timestamp = 0.0;
        double velocity = 0.0;
    };

    struct BallTrack {
        bool active = false;
        cv::Point2f position;
        cv::Point2f velocity;
        cv::Rect box;
        double confidence = 0.0;
        double timestamp = 0.0;
        int hits = 0;
        int misses = 0;
        bool manualSeeded = false;
    };

    cv::Mat preprocessGray(const cv::Mat& frame) const;
    double calculateMotionIntensity(const cv::Mat& diff) const;
    double calculateBallColorScore(const cv::Mat& frame, const cv::Rect& box) const;
    double calculateGrassColorScore(const cv::Mat& frame, const cv::Rect& box) const;
    double calculateSkinColorScore(const cv::Mat& frame, const cv::Rect& box) const;
    double calculateSaturatedColorScore(const cv::Mat& frame, const cv::Rect& box) const;
    std::vector<TargetInfo> refineTargets(const std::vector<TargetInfo>& rawTargets) const;
    std::vector<TargetInfo> applyBallTrajectoryGate(const std::vector<TargetInfo>& targets, double timestamp);
    bool isLikelyPlayerHeadCandidate(const TargetInfo& candidate, const std::vector<TargetInfo>& players) const;
    bool hasNearbyPlayerContext(const TargetInfo& candidate, const std::vector<TargetInfo>& players, double maxDistance) const;
    bool isValidAutoBallBootstrapCandidate(const TargetInfo& candidate) const;
    bool isInsideAutoBallTrackArea(const TargetInfo& candidate) const;
    bool updateAutoBallBootstrap(const TargetInfo& candidate, const std::vector<TargetInfo>& players, double timestamp);
    void updateMotionHistory(const std::vector<TargetInfo>& targets, double timestamp);

    double calculateTargetVelocity(const TargetInfo& target, double timestamp) const;
    double calculateTargetDistance(const TargetInfo& lhs, const TargetInfo& rhs) const;
    bool isNearGoalZone(const TargetInfo& ball) const;
    bool hasNearbyPlayer(const TargetInfo& ball, const std::vector<TargetInfo>& targets, double maxDistance) const;
    bool hasPlayerContact(const std::vector<TargetInfo>& targets) const;
    HighlightInfo makeHighlight(
        HighlightType type,
        const cv::Rect& mainTarget,
        const std::vector<TargetInfo>& targets,
        double timestamp,
        double preroll,
        double postroll
    );

    cv::Mat lastGray;
    cv::Mat lastDiff;
    cv::Size lastFrameSize;
    double lastHighlightTime = -10.0;
    DetectionConfig config;
    int runtimeMaxTargets = 12;

    std::vector<TargetInfo> previousTargets;
    double totalMotionEnergy = 0.0;
    int motionFrameCount = 0;
    double avgMotionEnergy = 0.0;
    std::vector<MotionHistory> ballMotionHistory;
    BallTrack ballTrack;
    YoloByteTrackDetector yoloByteTrackDetector;
    bool yoloByteTrackReady = false;
    TargetInfo pendingBallCandidate;
    cv::Point2f pendingBallStart;
    int pendingBallHits = 0;
    double pendingBallTimestamp = 0.0;
};

#endif
