#ifndef DETECTION_H
#define DETECTION_H

#include "common.h"

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
    int maxTargets = 12;
    bool detectShoot = true;
    bool detectSave = true;
    bool detectGoal = true;
    bool detectDribbling = true;
    bool detectHeader = true;
    bool detectTackle = true;
};

class TargetDetectionManager {
public:
    bool init();
    bool init(const DetectionConfig& config);

    std::vector<TargetInfo> detect(const cv::Mat& frame);
    HighlightInfo detectHighlight(const std::vector<TargetInfo>& targets, double timestamp);

    void setConfig(const DetectionConfig& config);
    const DetectionConfig& getConfig() const;
    void reset();

private:
    struct MotionHistory {
        cv::Point2f position;
        double timestamp = 0.0;
        double velocity = 0.0;
    };

    cv::Mat preprocessGray(const cv::Mat& frame) const;
    double calculateMotionIntensity(const cv::Mat& diff) const;
    std::vector<TargetInfo> refineTargets(const std::vector<TargetInfo>& rawTargets) const;
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
};

#endif
