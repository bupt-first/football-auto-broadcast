#include "detection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/imgproc.hpp>

namespace {
double rectArea(const cv::Rect& rect) {
    return static_cast<double>(std::max(0, rect.width) * std::max(0, rect.height));
}

cv::Point2f rectCenter(const cv::Rect& rect) {
    return cv::Point2f(
        rect.x + rect.width * 0.5f,
        rect.y + rect.height * 0.5f
    );
}

double pointDistance(const cv::Point2f& lhs, const cv::Point2f& rhs) {
    const double dx = lhs.x - rhs.x;
    const double dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

double clampDouble(double value, double lower, double upper) {
    return std::max(lower, std::min(value, upper));
}
}

bool TargetDetectionManager::init() {
    DetectionConfig defaultConfig;
    return init(defaultConfig);
}

bool TargetDetectionManager::init(const DetectionConfig& newConfig) {
    config = newConfig;
    runtimeMaxTargets = std::max(1, config.maxTargets);
    lastGray.release();
    lastDiff.release();
    lastFrameSize = cv::Size();
    lastHighlightTime = -10.0;
    totalMotionEnergy = 0.0;
    motionFrameCount = 0;
    avgMotionEnergy = 0.0;
    previousTargets.clear();
    ballMotionHistory.clear();
    return true;
}

void TargetDetectionManager::setConfig(const DetectionConfig& newConfig) {
    config = newConfig;
    runtimeMaxTargets = std::max(1, config.maxTargets);
}

const DetectionConfig& TargetDetectionManager::getConfig() const {
    return config;
}

void TargetDetectionManager::reset() {
    init(config);
}

cv::Mat TargetDetectionManager::preprocessGray(const cv::Mat& frame) const {
    cv::Mat gray;
    if (frame.channels() == 1) {
        gray = frame.clone();
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    }

    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    return gray;
}

double TargetDetectionManager::calculateMotionIntensity(const cv::Mat& diff) const {
    if (diff.empty()) {
        return 0.0;
    }
    return cv::mean(diff)[0];
}

std::vector<TargetInfo> TargetDetectionManager::detect(const cv::Mat& frame) {
    std::vector<TargetInfo> targets;
    if (frame.empty()) {
        return targets;
    }

    const double startTime = CommonTool::getCurrentTimestamp();
    const cv::Mat gray = preprocessGray(frame);
    lastFrameSize = frame.size();

    if (lastGray.empty() || lastGray.size() != gray.size()) {
        lastGray = gray.clone();
        previousTargets.clear();
        ballMotionHistory.clear();
        return targets;
    }

    cv::Mat diff;
    cv::absdiff(lastGray, gray, diff);

    const double motionIntensity = calculateMotionIntensity(diff);
    ++motionFrameCount;
    totalMotionEnergy += motionIntensity;
    avgMotionEnergy = totalMotionEnergy / motionFrameCount;

    double adaptiveThreshold = config.motionThreshold;
    if (motionFrameCount > FPS) {
        adaptiveThreshold = std::max(config.motionThreshold, avgMotionEnergy * 1.5);
        adaptiveThreshold = std::min(adaptiveThreshold, 60.0);
    }

    cv::Mat mask;
    cv::threshold(diff, mask, adaptiveThreshold, 255, cv::THRESH_BINARY);

    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);
    cv::dilate(mask, mask, kernel, cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double timestamp = CommonTool::getCurrentTimestamp();
    const cv::Rect frameBounds(0, 0, frame.cols, frame.rows);
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < config.minContourArea || area > config.maxContourArea) {
            continue;
        }

        cv::Rect box = cv::boundingRect(contour) & frameBounds;
        if (box.empty()) {
            continue;
        }

        const double aspect = static_cast<double>(box.width) / std::max(1, box.height);
        const double fillRatio = area / std::max(1.0, rectArea(box));
        const bool ballLike = area <= config.ballAreaThreshold &&
            aspect > 0.45 && aspect < 2.2 &&
            fillRatio > 0.18;

        TargetInfo target;
        target.type = ballLike ? TargetType::BALL : TargetType::PLAYER;
        target.box = box;
        target.timestamp = timestamp;

        const double areaScore = target.type == TargetType::BALL
            ? area / std::max(1.0, config.ballAreaThreshold)
            : area / std::max(1.0, config.playerAreaThreshold);
        const double shapeScore = target.type == TargetType::BALL
            ? 1.0 - std::abs(1.0 - aspect) * 0.35
            : clampDouble(static_cast<double>(box.height) / std::max(1, box.width), 0.2, 2.0) / 2.0;
        const double motionScore = clampDouble(motionIntensity / std::max(1.0, adaptiveThreshold), 0.0, 1.0);
        target.confidence = clampDouble(0.50 * areaScore + 0.30 * shapeScore + 0.20 * motionScore, 0.0, 1.0);

        targets.push_back(target);
    }

    targets = refineTargets(targets);
    std::sort(targets.begin(), targets.end(), [](const TargetInfo& lhs, const TargetInfo& rhs) {
        if (lhs.type != rhs.type) {
            return lhs.type == TargetType::BALL;
        }
        return lhs.confidence > rhs.confidence;
    });

    if (targets.size() > static_cast<std::size_t>(runtimeMaxTargets)) {
        targets.resize(runtimeMaxTargets);
    }

    updateMotionHistory(targets, timestamp);
    lastGray = gray.clone();
    lastDiff = diff.clone();

    const double processingTime = CommonTool::getCurrentTimestamp() - startTime;
    if (processingTime > 0.08 && runtimeMaxTargets > 4) {
        runtimeMaxTargets = std::max(4, runtimeMaxTargets - 2);
    } else if (processingTime < 0.04 && runtimeMaxTargets < config.maxTargets) {
        ++runtimeMaxTargets;
    }

    return targets;
}

std::vector<TargetInfo> TargetDetectionManager::refineTargets(const std::vector<TargetInfo>& rawTargets) const {
    std::vector<TargetInfo> refined;
    for (const auto& target : rawTargets) {
        if (target.confidence < config.confidenceThreshold || target.box.empty()) {
            continue;
        }

        bool duplicate = false;
        for (auto& existing : refined) {
            const cv::Rect overlapRect = target.box & existing.box;
            const double baseArea = std::min(rectArea(target.box), rectArea(existing.box));
            const double overlap = baseArea > 0.0 ? rectArea(overlapRect) / baseArea : 0.0;
            if (overlap > config.duplicateOverlapThreshold) {
                if (target.confidence > existing.confidence) {
                    existing = target;
                }
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            refined.push_back(target);
        }
    }
    return refined;
}

void TargetDetectionManager::updateMotionHistory(const std::vector<TargetInfo>& targets, double timestamp) {
    auto ballIt = std::find_if(targets.begin(), targets.end(), [](const TargetInfo& target) {
        return target.type == TargetType::BALL;
    });

    if (ballIt != targets.end()) {
        MotionHistory history;
        history.position = rectCenter(ballIt->box);
        history.timestamp = timestamp;

        if (!ballMotionHistory.empty()) {
            const auto& previous = ballMotionHistory.back();
            const double dt = std::max(timestamp - previous.timestamp, 1.0 / FPS);
            history.velocity = pointDistance(history.position, previous.position) / dt;
        }

        ballMotionHistory.push_back(history);
        if (ballMotionHistory.size() > static_cast<std::size_t>(FPS * 2)) {
            ballMotionHistory.erase(ballMotionHistory.begin());
        }
    }

    previousTargets = targets;
}

HighlightInfo TargetDetectionManager::detectHighlight(const std::vector<TargetInfo>& targets, double timestamp) {
    HighlightInfo highlight;
    if (targets.empty() || timestamp - lastHighlightTime < config.highlightCooldown) {
        return highlight;
    }

    auto ballIt = std::find_if(targets.begin(), targets.end(), [](const TargetInfo& target) {
        return target.type == TargetType::BALL;
    });

    const bool hasBall = ballIt != targets.end();
    const double ballVelocity = hasBall ? calculateTargetVelocity(*ballIt, timestamp) : 0.0;
    const bool fastBall = ballVelocity >= config.fastBallVelocity;

    if (hasBall && config.detectGoal && fastBall && isNearGoalZone(*ballIt)) {
        return makeHighlight(HighlightType::GOAL, ballIt->box, targets, timestamp, 2.0, 4.0);
    }

    if (hasBall && config.detectShoot && fastBall) {
        return makeHighlight(HighlightType::SHOOT, ballIt->box, targets, timestamp, 1.5, 3.0);
    }

    if (hasBall && config.detectSave && hasNearbyPlayer(*ballIt, targets, config.closePlayerDistance * 0.75) && isNearGoalZone(*ballIt)) {
        return makeHighlight(HighlightType::SAVE, ballIt->box, targets, timestamp, 1.0, 3.0);
    }

    if (config.detectTackle && hasPlayerContact(targets)) {
        const auto playerIt = std::find_if(targets.begin(), targets.end(), [](const TargetInfo& target) {
            return target.type == TargetType::PLAYER;
        });
        if (playerIt != targets.end()) {
            return makeHighlight(HighlightType::SAVE, playerIt->box, targets, timestamp, 0.8, 2.0);
        }
    }

    if (hasBall && (config.detectDribbling || config.detectHeader) &&
        hasNearbyPlayer(*ballIt, targets, config.closePlayerDistance) &&
        ballMotionHistory.size() > static_cast<std::size_t>(FPS / 2)) {
        return makeHighlight(HighlightType::SHOOT, ballIt->box, targets, timestamp, 1.0, 2.0);
    }

    return highlight;
}

double TargetDetectionManager::calculateTargetVelocity(const TargetInfo& target, double timestamp) const {
    if (target.type == TargetType::BALL && !ballMotionHistory.empty()) {
        double maxVelocity = 0.0;
        const std::size_t start = ballMotionHistory.size() > 8 ? ballMotionHistory.size() - 8 : 0;
        for (std::size_t i = start; i < ballMotionHistory.size(); ++i) {
            maxVelocity = std::max(maxVelocity, ballMotionHistory[i].velocity);
        }
        return maxVelocity;
    }

    if (previousTargets.empty()) {
        return 0.0;
    }

    const cv::Point2f currentCenter = rectCenter(target.box);
    double bestDistance = std::numeric_limits<double>::max();
    double bestVelocity = 0.0;
    for (const auto& previous : previousTargets) {
        if (previous.type != target.type) {
            continue;
        }

        const double distance = pointDistance(currentCenter, rectCenter(previous.box));
        if (distance < bestDistance) {
            const double dt = std::max(timestamp - previous.timestamp, 1.0 / FPS);
            bestDistance = distance;
            bestVelocity = distance / dt;
        }
    }

    return bestVelocity;
}

double TargetDetectionManager::calculateTargetDistance(const TargetInfo& lhs, const TargetInfo& rhs) const {
    return pointDistance(rectCenter(lhs.box), rectCenter(rhs.box));
}

bool TargetDetectionManager::isNearGoalZone(const TargetInfo& ball) const {
    if (lastFrameSize.width <= 0 || lastFrameSize.height <= 0) {
        return false;
    }

    const cv::Point2f center = rectCenter(ball.box);
    const double xRatio = center.x / lastFrameSize.width;
    const double yRatio = center.y / lastFrameSize.height;
    return (xRatio < 0.16 || xRatio > 0.84) && yRatio > 0.18 && yRatio < 0.82;
}

bool TargetDetectionManager::hasNearbyPlayer(
    const TargetInfo& ball,
    const std::vector<TargetInfo>& targets,
    double maxDistance
) const {
    return std::any_of(targets.begin(), targets.end(), [&](const TargetInfo& target) {
        return target.type == TargetType::PLAYER &&
            calculateTargetDistance(ball, target) <= maxDistance;
    });
}

bool TargetDetectionManager::hasPlayerContact(const std::vector<TargetInfo>& targets) const {
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].type != TargetType::PLAYER) {
            continue;
        }

        for (std::size_t j = i + 1; j < targets.size(); ++j) {
            if (targets[j].type != TargetType::PLAYER) {
                continue;
            }

            const cv::Rect overlapRect = targets[i].box & targets[j].box;
            const double baseArea = std::min(rectArea(targets[i].box), rectArea(targets[j].box));
            const double overlap = baseArea > 0.0 ? rectArea(overlapRect) / baseArea : 0.0;
            if (overlap > 0.25 || calculateTargetDistance(targets[i], targets[j]) < config.closePlayerDistance) {
                return true;
            }
        }
    }

    return false;
}

HighlightInfo TargetDetectionManager::makeHighlight(
    HighlightType type,
    const cv::Rect& mainTarget,
    const std::vector<TargetInfo>& targets,
    double timestamp,
    double preroll,
    double postroll
) {
    HighlightInfo highlight;
    highlight.type = type;
    highlight.start_time = std::max(0.0, timestamp - preroll);
    highlight.end_time = timestamp + postroll;
    highlight.main_target = mainTarget;
    highlight.related_targets = targets;
    lastHighlightTime = timestamp;
    return highlight;
}
