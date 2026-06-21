#include "detection.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
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
    ballTrack = BallTrack();
    YoloByteTrackDetector::Config yoloConfig;
    yoloConfig.enabled = config.enableYoloByteTrack;
    yoloConfig.modelPath = config.yoloModelPath;
    yoloConfig.inputSize = cv::Size(
        std::max(320, config.yoloInputSize),
        std::max(320, config.yoloInputSize)
    );
    yoloConfig.confidenceThreshold = static_cast<float>(config.yoloConfidenceThreshold);
    yoloConfig.highTrackThreshold = static_cast<float>(config.yoloHighTrackThreshold);
    yoloConfig.nmsThreshold = static_cast<float>(config.yoloNmsThreshold);
    yoloConfig.trackMatchThreshold = static_cast<float>(config.byteTrackMatchThreshold);
    yoloConfig.maxLostFrames = config.byteTrackMaxLostFrames;
    yoloConfig.ballClassId = config.yoloBallClassId;
    yoloConfig.playerClassId = config.yoloPlayerClassId;
    yoloConfig.goalkeeperClassId = config.yoloGoalkeeperClassId;
    yoloConfig.refereeClassId = config.yoloRefereeClassId;
    yoloByteTrackReady = yoloByteTrackDetector.init(yoloConfig);
    pendingBallCandidate = TargetInfo();
    pendingBallStart = cv::Point2f();
    pendingBallHits = 0;
    pendingBallTimestamp = 0.0;
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

void TargetDetectionManager::seedBallTrack(const cv::Point2f& center, double timestamp, double radius) {
    const int boxSize = std::max(8, static_cast<int>(std::round(std::max(8.0, radius) * 0.16)));
    ballTrack.active = true;
    ballTrack.position = center;
    ballTrack.velocity = cv::Point2f(0.0f, 0.0f);
    ballTrack.box = cv::Rect(
        static_cast<int>(std::round(center.x)) - boxSize / 2,
        static_cast<int>(std::round(center.y)) - boxSize / 2,
        boxSize,
        boxSize
    );
    ballTrack.confidence = config.confirmedBallConfidenceThreshold;
    ballTrack.timestamp = timestamp;
    ballTrack.hits = std::max(1, config.minBallTrackHits);
    ballTrack.misses = 0;
    ballTrack.manualSeeded = true;
    pendingBallCandidate = TargetInfo();
    pendingBallHits = 0;
    pendingBallTimestamp = 0.0;
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

double TargetDetectionManager::calculateBallColorScore(const cv::Mat& frame, const cv::Rect& box) const {
    if (frame.empty() || box.empty()) {
        return 0.0;
    }

    const cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safeBox.empty()) {
        return 0.0;
    }

    cv::Mat roi = frame(safeBox);
    cv::Mat hsv;
    cv::cvtColor(roi, hsv, cv::COLOR_BGR2HSV);

    cv::Mat brightMask;
    cv::Mat whiteMask;
    cv::inRange(hsv, cv::Scalar(0, 0, 135), cv::Scalar(179, 95, 255), whiteMask);
    cv::inRange(hsv, cv::Scalar(0, 0, 175), cv::Scalar(179, 180, 255), brightMask);

    const double pixels = std::max(1, safeBox.width * safeBox.height);
    const double whiteRatio = cv::countNonZero(whiteMask) / pixels;
    const double brightRatio = cv::countNonZero(brightMask) / pixels;
    return clampDouble(0.72 * whiteRatio + 0.28 * brightRatio, 0.0, 1.0);
}

double TargetDetectionManager::calculateGrassColorScore(const cv::Mat& frame, const cv::Rect& box) const {
    if (frame.empty() || box.empty()) {
        return 0.0;
    }

    const cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safeBox.empty()) {
        return 0.0;
    }

    cv::Mat hsv;
    cv::cvtColor(frame(safeBox), hsv, cv::COLOR_BGR2HSV);

    cv::Mat grassMask;
    cv::inRange(hsv, cv::Scalar(35, 35, 35), cv::Scalar(95, 255, 230), grassMask);
    return clampDouble(
        cv::countNonZero(grassMask) / static_cast<double>(std::max(1, safeBox.width * safeBox.height)),
        0.0,
        1.0
    );
}

double TargetDetectionManager::calculateSkinColorScore(const cv::Mat& frame, const cv::Rect& box) const {
    if (frame.empty() || box.empty()) {
        return 0.0;
    }

    const cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safeBox.empty()) {
        return 0.0;
    }

    cv::Mat hsv;
    cv::cvtColor(frame(safeBox), hsv, cv::COLOR_BGR2HSV);

    cv::Mat skinMask;
    cv::inRange(hsv, cv::Scalar(0, 20, 55), cv::Scalar(28, 190, 255), skinMask);
    return clampDouble(
        cv::countNonZero(skinMask) / static_cast<double>(std::max(1, safeBox.width * safeBox.height)),
        0.0,
        1.0
    );
}

double TargetDetectionManager::calculateSaturatedColorScore(const cv::Mat& frame, const cv::Rect& box) const {
    if (frame.empty() || box.empty()) {
        return 0.0;
    }

    const cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);
    if (safeBox.empty()) {
        return 0.0;
    }

    cv::Mat hsv;
    cv::cvtColor(frame(safeBox), hsv, cv::COLOR_BGR2HSV);

    cv::Mat saturatedMask;
    cv::inRange(hsv, cv::Scalar(0, 90, 70), cv::Scalar(179, 255, 255), saturatedMask);
    return clampDouble(
        cv::countNonZero(saturatedMask) / static_cast<double>(std::max(1, safeBox.width * safeBox.height)),
        0.0,
        1.0
    );
}

std::vector<TargetInfo> TargetDetectionManager::detect(const cv::Mat& frame) {
    return detect(frame, CommonTool::getCurrentTimestamp());
}

std::vector<TargetInfo> TargetDetectionManager::detect(const cv::Mat& frame, double timestamp) {
    std::vector<TargetInfo> targets;
    if (frame.empty()) {
        return targets;
    }

    const double startTime = CommonTool::getCurrentTimestamp();
    const cv::Mat gray = preprocessGray(frame);
    lastFrameSize = frame.size();

    std::vector<TargetInfo> yoloContextTargets;
    auto mergeYoloContext = [this, &yoloContextTargets](std::vector<TargetInfo>& base) {
        for (const auto& context : yoloContextTargets) {
            if (context.type == TargetType::BALL || context.box.empty()) {
                continue;
            }

            bool duplicate = false;
            for (const auto& existing : base) {
                if (existing.type != context.type) {
                    continue;
                }

                const cv::Rect overlapRect = existing.box & context.box;
                const double baseArea = std::min(rectArea(existing.box), rectArea(context.box));
                const double overlap = baseArea > 0.0 ? rectArea(overlapRect) / baseArea : 0.0;
                if (overlap > config.duplicateOverlapThreshold) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                base.push_back(context);
            }
        }
    };
    auto sortAndLimit = [this](std::vector<TargetInfo>& items) {
        std::sort(items.begin(), items.end(), [](const TargetInfo& lhs, const TargetInfo& rhs) {
            if (lhs.type != rhs.type) {
                return lhs.type == TargetType::BALL;
            }
            return lhs.confidence > rhs.confidence;
        });
        if (items.size() > static_cast<std::size_t>(runtimeMaxTargets)) {
            items.resize(runtimeMaxTargets);
        }
    };

    if (yoloByteTrackReady) {
        targets = yoloByteTrackDetector.detectAndTrack(frame, timestamp);
        if (!targets.empty()) {
            const bool hasRawYoloBall = std::any_of(targets.begin(), targets.end(), [](const TargetInfo& target) {
                return target.type == TargetType::BALL;
            });
            if (hasRawYoloBall) {
                targets = applyBallTrajectoryGate(targets, timestamp);
                const bool hasYoloBall = std::any_of(targets.begin(), targets.end(), [](const TargetInfo& target) {
                    return target.type == TargetType::BALL;
                });
                if (hasYoloBall) {
                    sortAndLimit(targets);
                    updateMotionHistory(targets, timestamp);
                    lastGray = gray.clone();
                    return targets;
                }
            }
            yoloContextTargets = targets;
            targets.clear();
        }
    }

    if (lastGray.empty() || lastGray.size() != gray.size()) {
        lastGray = gray.clone();
        previousTargets.clear();
        ballMotionHistory.clear();
        if (ballTrack.active && ballTrack.hits >= config.minBallTrackHits) {
            TargetInfo seededBall;
            seededBall.type = TargetType::BALL;
            seededBall.box = ballTrack.box;
            seededBall.confidence = ballTrack.confidence;
            seededBall.timestamp = timestamp;
            targets.push_back(seededBall);
            previousTargets = targets;
        } else {
            ballTrack = BallTrack();
        }
        mergeYoloContext(targets);
        sortAndLimit(targets);
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
        const double ballColorScore = calculateBallColorScore(frame, box);
        const double grassScore = calculateGrassColorScore(frame, box);
        const double skinScore = calculateSkinColorScore(frame, box);
        const double saturatedScore = calculateSaturatedColorScore(frame, box);
        const bool ballLike = area <= config.ballAreaThreshold &&
            aspect > 0.45 && aspect < 2.2 &&
            fillRatio > 0.18 &&
            ballColorScore >= config.ballColorThreshold &&
            grassScore < config.ballGrassRejectThreshold &&
            skinScore < config.ballSkinRejectThreshold &&
            saturatedScore < config.ballSaturationRejectThreshold;

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
        if (target.type == TargetType::BALL) {
            const double sizeScore = 1.0 - std::abs(area - config.ballAreaThreshold * 0.45) /
                std::max(1.0, config.ballAreaThreshold);
            const double compactScore = clampDouble(fillRatio, 0.0, 1.0);
            target.confidence = clampDouble(
                0.34 * clampDouble(sizeScore, 0.0, 1.0) +
                0.30 * clampDouble(shapeScore, 0.0, 1.0) +
                0.20 * ballColorScore +
                0.10 * compactScore +
                0.06 * motionScore -
                0.18 * grassScore -
                0.16 * skinScore -
                0.12 * saturatedScore,
                0.0,
                1.0
            );
        } else {
            target.confidence = clampDouble(0.50 * areaScore + 0.30 * shapeScore + 0.20 * motionScore, 0.0, 1.0);
        }

        targets.push_back(target);
    }

    targets = refineTargets(targets);
    targets = applyBallTrajectoryGate(targets, timestamp);
    mergeYoloContext(targets);
    sortAndLimit(targets);

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

bool TargetDetectionManager::isLikelyPlayerHeadCandidate(
    const TargetInfo& candidate,
    const std::vector<TargetInfo>& players
) const {
    const cv::Point2f candidateCenter = rectCenter(candidate.box);
    const double candidateArea = rectArea(candidate.box);
    if (candidateArea <= 0.0) {
        return false;
    }

    for (const auto& player : players) {
        if (player.box.empty()) {
            continue;
        }

        const cv::Rect expandedPlayer(
            player.box.x - player.box.width / 5,
            player.box.y - player.box.height / 8,
            player.box.width + player.box.width * 2 / 5,
            player.box.height + player.box.height / 4
        );
        if (!expandedPlayer.contains(cv::Point(
                static_cast<int>(std::round(candidateCenter.x)),
                static_cast<int>(std::round(candidateCenter.y))))) {
            continue;
        }

        const double playerArea = rectArea(player.box);
        const double relativeArea = candidateArea / std::max(1.0, playerArea);
        const double headBandBottom = player.box.y + player.box.height * 0.42;
        if (candidateCenter.y <= headBandBottom && relativeArea >= 0.006 && relativeArea <= 0.12) {
            return true;
        }
    }

    return false;
}

bool TargetDetectionManager::hasNearbyPlayerContext(
    const TargetInfo& candidate,
    const std::vector<TargetInfo>& players,
    double maxDistance
) const {
    if (players.empty()) {
        return true;
    }

    const cv::Point2f candidateCenter = rectCenter(candidate.box);
    for (const auto& player : players) {
        if (player.box.empty()) {
            continue;
        }

        const cv::Point2f playerCenter = rectCenter(player.box);
        if (pointDistance(candidateCenter, playerCenter) <= maxDistance) {
            return true;
        }
    }

    return false;
}

bool TargetDetectionManager::isValidAutoBallBootstrapCandidate(const TargetInfo& candidate) const {
    if (candidate.box.empty() || lastFrameSize.width <= 0 || lastFrameSize.height <= 0) {
        return false;
    }

    const cv::Point2f center = rectCenter(candidate.box);
    const double sideMargin = lastFrameSize.width * config.autoBallBootstrapSideMarginRatio;
    if (center.x < sideMargin || center.x > lastFrameSize.width - sideMargin) {
        return false;
    }

    const double upperPlayLimit = lastFrameSize.height * config.autoBallBootstrapTopRatio;
    const double lowerPlayLimit = lastFrameSize.height * config.autoBallBootstrapBottomRatio;
    if (center.y < upperPlayLimit || center.y > lowerPlayLimit) {
        return false;
    }

    const double aspect = static_cast<double>(candidate.box.width) / std::max(1, candidate.box.height);
    if (aspect < config.autoBallBootstrapMinAspect || aspect > config.autoBallBootstrapMaxAspect) {
        return false;
    }

    const double areaRatio = rectArea(candidate.box) / std::max(1.0, config.ballAreaThreshold);
    return areaRatio <= config.autoBallBootstrapMaxAreaRatio;
}

bool TargetDetectionManager::isInsideAutoBallTrackArea(const TargetInfo& candidate) const {
    if (candidate.box.empty() || lastFrameSize.width <= 0 || lastFrameSize.height <= 0) {
        return false;
    }

    const cv::Point2f center = rectCenter(candidate.box);
    const double sideMargin = lastFrameSize.width * config.autoBallTrackSideMarginRatio;
    if (center.x < sideMargin || center.x > lastFrameSize.width - sideMargin) {
        return false;
    }

    const double upperLimit = lastFrameSize.height * config.autoBallTrackTopRatio;
    const double lowerLimit = lastFrameSize.height * config.autoBallTrackBottomRatio;
    return center.y >= upperLimit && center.y <= lowerLimit;
}

bool TargetDetectionManager::updateAutoBallBootstrap(
    const TargetInfo& candidate,
    const std::vector<TargetInfo>& players,
    double timestamp
) {
    if (!isValidAutoBallBootstrapCandidate(candidate)) {
        pendingBallCandidate = TargetInfo();
        pendingBallHits = 0;
        pendingBallTimestamp = timestamp;
        return false;
    }

    if (!hasNearbyPlayerContext(candidate, players, config.closePlayerDistance * 2.8)) {
        pendingBallCandidate = TargetInfo();
        pendingBallHits = 0;
        pendingBallTimestamp = timestamp;
        return false;
    }

    const cv::Point2f center = rectCenter(candidate.box);
    if (pendingBallHits <= 0 || timestamp - pendingBallTimestamp > 0.35) {
        pendingBallCandidate = candidate;
        pendingBallStart = center;
        pendingBallHits = 1;
        pendingBallTimestamp = timestamp;
        return false;
    }

    const cv::Point2f previousCenter = rectCenter(pendingBallCandidate.box);
    const double shiftFromPrevious = pointDistance(center, previousCenter);
    const double shiftFromStart = pointDistance(center, pendingBallStart);
    if (shiftFromPrevious > config.autoBallBootstrapGate ||
        shiftFromStart < config.autoBallBootstrapMinShift ||
        shiftFromStart > config.autoBallBootstrapMaxShift) {
        pendingBallCandidate = candidate;
        pendingBallStart = center;
        pendingBallHits = 1;
        pendingBallTimestamp = timestamp;
        return false;
    }

    pendingBallCandidate = candidate;
    ++pendingBallHits;
    pendingBallTimestamp = timestamp;
    return pendingBallHits >= config.autoBallBootstrapHits;
}

std::vector<TargetInfo> TargetDetectionManager::applyBallTrajectoryGate(
    const std::vector<TargetInfo>& targets,
    double timestamp
) {
    std::vector<TargetInfo> filtered;
    std::vector<TargetInfo> ballCandidates;

    for (const auto& target : targets) {
        if (target.type == TargetType::BALL) {
            if (target.confidence >= config.ballCandidateConfidenceThreshold) {
                ballCandidates.push_back(target);
            }
        } else {
            filtered.push_back(target);
        }
    }

    TargetInfo bestCandidate;
    bool hasCandidate = false;
    double bestScore = -1.0;

    const double dt = ballTrack.active
        ? std::max(timestamp - ballTrack.timestamp, 1.0 / FPS)
        : 1.0 / FPS;
    const cv::Point2f predicted = ballTrack.active
        ? ballTrack.position + ballTrack.velocity * static_cast<float>(dt)
        : cv::Point2f();
    const double predictedSpeed = ballTrack.active
        ? std::sqrt(ballTrack.velocity.x * ballTrack.velocity.x + ballTrack.velocity.y * ballTrack.velocity.y)
        : 0.0;
    const double gate = config.ballPredictionGate +
        clampDouble(predictedSpeed * dt * 0.65, 0.0, 80.0) +
        ballTrack.misses * 25.0;

    for (const auto& candidate : ballCandidates) {
        if (isLikelyPlayerHeadCandidate(candidate, filtered)) {
            continue;
        }

        const cv::Point2f center = rectCenter(candidate.box);
        double trajectoryScore = 0.45;
        if (ballTrack.active) {
            const double distance = pointDistance(center, predicted);
            const double allowedGate = predictedSpeed < 80.0 && ballTrack.hits >= config.minBallTrackHits
                ? std::min(gate, 32.0)
                : gate;
            if (distance > allowedGate && ballTrack.hits >= config.minBallTrackHits) {
                continue;
            }
            trajectoryScore = 1.0 - clampDouble(distance / std::max(1.0, allowedGate), 0.0, 1.0);
        }

        const double area = rectArea(candidate.box);
        const double areaScore = 1.0 - std::abs(area - config.ballAreaThreshold * 0.45) /
            std::max(1.0, config.ballAreaThreshold);
        const double score = 0.58 * candidate.confidence +
            0.28 * trajectoryScore +
            0.14 * clampDouble(areaScore, 0.0, 1.0);

        if (!ballTrack.active) {
            const double bootstrapScore = score +
                (hasNearbyPlayerContext(candidate, filtered, config.closePlayerDistance * 1.8) ? 0.08 : -0.18);
            if (!isValidAutoBallBootstrapCandidate(candidate) ||
                bootstrapScore < config.confirmedBallConfidenceThreshold + 0.12) {
                continue;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestCandidate = candidate;
            hasCandidate = true;
        }
    }

    if (hasCandidate) {
        if (!ballTrack.active && !updateAutoBallBootstrap(bestCandidate, filtered, timestamp)) {
            return filtered;
        }

        TargetInfo confirmed = bestCandidate;
        confirmed.confidence = clampDouble(bestScore, 0.0, 1.0);
        if (ballTrack.active && !ballTrack.manualSeeded && !isInsideAutoBallTrackArea(confirmed)) {
            ballTrack = BallTrack();
            pendingBallCandidate = TargetInfo();
            pendingBallHits = 0;
            pendingBallTimestamp = 0.0;
            return filtered;
        }

        const cv::Point2f center = rectCenter(confirmed.box);
        if (ballTrack.active) {
            const double safeDt = std::max(timestamp - ballTrack.timestamp, 1.0 / FPS);
            const cv::Point2f measuredVelocity = (center - ballTrack.position) * static_cast<float>(1.0 / safeDt);
            ballTrack.velocity = ballTrack.velocity * 0.55f + measuredVelocity * 0.45f;
        } else {
            ballTrack.velocity = cv::Point2f(0.0f, 0.0f);
        }

        ballTrack.active = true;
        ballTrack.position = center;
        ballTrack.box = confirmed.box;
        ballTrack.confidence = confirmed.confidence;
        ballTrack.timestamp = timestamp;
        ballTrack.hits = std::min(ballTrack.hits + 1, FPS * 2);
        ballTrack.misses = 0;
        if (!ballTrack.manualSeeded) {
            ballTrack.manualSeeded = false;
        }
        pendingBallCandidate = TargetInfo();
        pendingBallHits = 0;
        pendingBallTimestamp = 0.0;

        if (ballTrack.hits >= config.minBallTrackHits &&
            confirmed.confidence >= config.confirmedBallConfidenceThreshold) {
            filtered.push_back(confirmed);
        }
    } else if (ballTrack.active && ballTrack.hits >= config.minBallTrackHits && ballTrack.misses <= 2) {
        const double predictDt = std::max(timestamp - ballTrack.timestamp, 1.0 / FPS);
        const cv::Point2f predictedCenter = ballTrack.position + ballTrack.velocity * static_cast<float>(predictDt);
        const cv::Point2f clampedCenter(
            clampDouble(predictedCenter.x, 0.0, std::max(0, lastFrameSize.width - 1)),
            clampDouble(predictedCenter.y, 0.0, std::max(0, lastFrameSize.height - 1))
        );
        TargetInfo confirmed;
        confirmed.type = TargetType::BALL;
        confirmed.box = cv::Rect(
            static_cast<int>(std::round(clampedCenter.x - ballTrack.box.width * 0.5f)),
            static_cast<int>(std::round(clampedCenter.y - ballTrack.box.height * 0.5f)),
            ballTrack.box.width,
            ballTrack.box.height
        ) & cv::Rect(0, 0, lastFrameSize.width, lastFrameSize.height);
        confirmed.confidence = ballTrack.confidence * 0.82;
        confirmed.timestamp = timestamp;
        if (!ballTrack.manualSeeded && !isInsideAutoBallTrackArea(confirmed)) {
            ballTrack = BallTrack();
            return filtered;
        }
        ++ballTrack.misses;
        ballTrack.position = clampedCenter;
        ballTrack.box = confirmed.box;
        ballTrack.confidence = confirmed.confidence;
        ballTrack.timestamp = timestamp;
        if (!confirmed.box.empty() && confirmed.confidence >= config.confirmedBallConfidenceThreshold * 0.75) {
            filtered.push_back(confirmed);
        }
    } else if (ballTrack.active) {
        const double elapsed = timestamp - ballTrack.timestamp;
        ++ballTrack.misses;
        if (elapsed > config.ballLostTimeout) {
            ballTrack = BallTrack();
        }
    }

    return filtered;
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
