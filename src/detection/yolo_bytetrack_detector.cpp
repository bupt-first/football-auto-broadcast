#include "yolo_bytetrack_detector.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <numeric>

namespace {
float detectionValue(const float* data, int row, int col, int rows, int cols, bool channelsFirst) {
    return channelsFirst ? data[col * rows + row] : data[row * cols + col];
}

cv::Rect2f toRect2f(const cv::Rect& rect) {
    return cv::Rect2f(
        static_cast<float>(rect.x),
        static_cast<float>(rect.y),
        static_cast<float>(rect.width),
        static_cast<float>(rect.height)
    );
}
}

bool YoloByteTrackDetector::init(const Config& newConfig) {
    config = newConfig;
    reset();

    if (!config.enabled || config.modelPath.empty()) {
        ready = false;
        return false;
    }

    std::filesystem::path modelPath(config.modelPath);
    if (!std::filesystem::exists(modelPath) && modelPath.is_relative()) {
        const std::filesystem::path current = std::filesystem::current_path();
        const std::vector<std::filesystem::path> candidates = {
            current / modelPath,
            current.parent_path() / modelPath,
            current.parent_path().parent_path() / modelPath
        };
        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                modelPath = candidate;
                break;
            }
        }
    }
    if (!std::filesystem::exists(modelPath)) {
        ready = false;
        return false;
    }

    try {
        net = cv::dnn::readNet(modelPath.string());
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        ready = !net.empty();
    } catch (const cv::Exception&) {
        ready = false;
    }

    return ready;
}

bool YoloByteTrackDetector::isReady() const {
    return ready;
}

void YoloByteTrackDetector::reset() {
    tracks.clear();
    nextTrackId = 1;
    lastFrameSize = cv::Size();
}

TargetType YoloByteTrackDetector::classToTargetType(int classId) const {
    if (classId == config.ballClassId) {
        return TargetType::BALL;
    }
    if (classId == config.goalkeeperClassId) {
        return TargetType::GOALKEEPER;
    }
    if (classId == config.refereeClassId) {
        return TargetType::PERSON;
    }
    return TargetType::PLAYER;
}

std::vector<YoloByteTrackDetector::Detection> YoloByteTrackDetector::runYolo(const cv::Mat& frame) {
    std::vector<Detection> detections;
    if (!ready || frame.empty()) {
        return detections;
    }

    cv::Mat blob = cv::dnn::blobFromImage(
        frame,
        1.0 / 255.0,
        config.inputSize,
        cv::Scalar(),
        true,
        false
    );

    std::vector<cv::Mat> outputs;
    try {
        net.setInput(blob);
        net.forward(outputs, net.getUnconnectedOutLayersNames());
    } catch (const cv::Exception&) {
        return detections;
    }
    if (outputs.empty()) {
        return detections;
    }

    const cv::Mat& output = outputs.front();
    if (output.empty() || output.dims != 3) {
        return detections;
    }

    const int dim1 = output.size[1];
    const int dim2 = output.size[2];
    const bool channelsFirst = dim1 < dim2;
    const int rows = channelsFirst ? dim2 : dim1;
    const int cols = channelsFirst ? dim1 : dim2;
    if (cols < 6) {
        return detections;
    }

    const float* data = reinterpret_cast<const float*>(output.data);
    const float scaleX = static_cast<float>(frame.cols) / std::max(1, config.inputSize.width);
    const float scaleY = static_cast<float>(frame.rows) / std::max(1, config.inputSize.height);

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int row = 0; row < rows; ++row) {
        const float cx = detectionValue(data, row, 0, rows, cols, channelsFirst);
        const float cy = detectionValue(data, row, 1, rows, cols, channelsFirst);
        const float width = detectionValue(data, row, 2, rows, cols, channelsFirst);
        const float height = detectionValue(data, row, 3, rows, cols, channelsFirst);

        float bestScore = 0.0f;
        int bestClass = -1;
        for (int col = 4; col < cols; ++col) {
            const float classScore = detectionValue(data, row, col, rows, cols, channelsFirst);
            if (classScore > bestScore) {
                bestScore = classScore;
                bestClass = col - 4;
            }
        }

        if (bestClass < 0 || bestScore < config.confidenceThreshold) {
            continue;
        }
        if (bestClass != config.ballClassId &&
            bestClass != config.playerClassId &&
            bestClass != config.goalkeeperClassId &&
            bestClass != config.refereeClassId) {
            continue;
        }

        const float left = (cx - width * 0.5f) * scaleX;
        const float top = (cy - height * 0.5f) * scaleY;
        const float boxWidth = width * scaleX;
        const float boxHeight = height * scaleY;
        const cv::Rect box = clampRectToFrame(
            cv::Rect2f(left, top, boxWidth, boxHeight),
            frame.size()
        );
        if (box.empty()) {
            continue;
        }

        boxes.push_back(box);
        scores.push_back(bestScore);
        classIds.push_back(bestClass);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, scores, config.confidenceThreshold, config.nmsThreshold, keep);
    detections.reserve(keep.size());
    for (int index : keep) {
        Detection detection;
        detection.classId = classIds[index];
        detection.target.type = classToTargetType(detection.classId);
        detection.target.box = boxes[index];
        detection.target.confidence = scores[index];
        detection.target.classId = detection.classId;
        detection.target.semanticLabel = makeSemanticLabel(detection.target);
        detections.push_back(detection);
    }

    return detections;
}

std::vector<TargetInfo> YoloByteTrackDetector::detectAndTrack(const cv::Mat& frame, double timestamp) {
    lastFrameSize = frame.size();
    std::vector<TargetInfo> targets = updateTracks(runYolo(frame), timestamp);
    postProcessFootballSemantics(targets, frame);
    return targets;
}

std::vector<TargetInfo> YoloByteTrackDetector::updateTracks(
    const std::vector<Detection>& detections,
    double timestamp
) {
    std::vector<TargetInfo> trackedTargets;
    if (detections.empty()) {
        for (auto& track : tracks) {
            ++track.lostFrames;
        }
        tracks.erase(
            std::remove_if(tracks.begin(), tracks.end(), [this](const Track& track) {
                return track.lostFrames > config.maxLostFrames;
            }),
            tracks.end()
        );
        return trackedTargets;
    }

    std::vector<int> detectionOrder(detections.size());
    std::iota(detectionOrder.begin(), detectionOrder.end(), 0);
    std::sort(detectionOrder.begin(), detectionOrder.end(), [&detections](int lhs, int rhs) {
        return detections[lhs].target.confidence > detections[rhs].target.confidence;
    });

    std::vector<bool> usedTracks(tracks.size(), false);
    std::vector<bool> usedDetections(detections.size(), false);

    for (int detectionIndex : detectionOrder) {
        const TargetInfo& target = detections[detectionIndex].target;
        const cv::Rect2f targetBox = toRect2f(target.box);

        float bestIou = 0.0f;
        int bestTrack = -1;
        for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            if (usedTracks[trackIndex] || tracks[trackIndex].type != target.type) {
                continue;
            }

            const double dt = std::max(1.0 / FPS, timestamp - tracks[trackIndex].timestamp);
            cv::Rect2f predictedBox = tracks[trackIndex].box;
            predictedBox.x += tracks[trackIndex].velocity.x * static_cast<float>(dt);
            predictedBox.y += tracks[trackIndex].velocity.y * static_cast<float>(dt);

            const float iou = intersectionOverUnion(predictedBox, targetBox);
            if (iou > bestIou) {
                bestIou = iou;
                bestTrack = static_cast<int>(trackIndex);
            }
        }

        if (bestTrack >= 0 && bestIou >= config.trackMatchThreshold) {
            Track& track = tracks[bestTrack];
            const double dt = std::max(1.0 / FPS, timestamp - track.timestamp);
            const cv::Point2f oldCenter(
                track.box.x + track.box.width * 0.5f,
                track.box.y + track.box.height * 0.5f
            );
            const cv::Point2f newCenter(
                targetBox.x + targetBox.width * 0.5f,
                targetBox.y + targetBox.height * 0.5f
            );

            track.velocity = track.velocity * 0.60f + (newCenter - oldCenter) * static_cast<float>(0.40 / dt);
            track.box = targetBox;
            track.score = static_cast<float>(target.confidence);
            track.timestamp = timestamp;
            track.lostFrames = 0;
            track.classId = detections[detectionIndex].classId;
            usedTracks[bestTrack] = true;
            usedDetections[detectionIndex] = true;
        }
    }

    for (int detectionIndex : detectionOrder) {
        if (usedDetections[detectionIndex] ||
            detections[detectionIndex].target.confidence < config.highTrackThreshold) {
            continue;
        }

        Track track;
        track.id = nextTrackId++;
        track.type = detections[detectionIndex].target.type;
        track.box = toRect2f(detections[detectionIndex].target.box);
        track.score = static_cast<float>(detections[detectionIndex].target.confidence);
        track.timestamp = timestamp;
        track.classId = detections[detectionIndex].classId;
        tracks.push_back(track);
    }

    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        if (trackIndex < usedTracks.size() && !usedTracks[trackIndex]) {
            ++tracks[trackIndex].lostFrames;
        }
    }
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(), [this](const Track& track) {
            return track.lostFrames > config.maxLostFrames;
        }),
        tracks.end()
    );

    for (const auto& track : tracks) {
        if (track.lostFrames > 0) {
            continue;
        }

        TargetInfo target;
        target.type = track.type;
        target.box = clampRectToFrame(track.box, lastFrameSize);
        target.confidence = track.score;
        target.timestamp = timestamp;
        target.trackId = track.id;
        target.classId = track.classId;
        target.teamId = track.teamId;
        target.semanticLabel = track.semanticLabel.empty() ? makeSemanticLabel(target) : track.semanticLabel;
        if (!target.box.empty()) {
            trackedTargets.push_back(target);
        }
    }

    return trackedTargets;
}

void YoloByteTrackDetector::postProcessFootballSemantics(
    std::vector<TargetInfo>& targets,
    const cv::Mat& frame
) const {
    if (targets.empty()) {
        return;
    }

    for (auto& target : targets) {
        target.semanticLabel = makeSemanticLabel(target);
    }

    std::vector<JerseyFeature> jerseyFeatures;
    jerseyFeatures.reserve(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        TargetInfo& target = targets[i];
        if (target.type != TargetType::PLAYER || target.box.empty()) {
            continue;
        }

        cv::Vec3f feature;
        if (extractJerseyFeature(frame, target, feature)) {
            JerseyFeature jerseyFeature;
            jerseyFeature.targetIndex = static_cast<int>(i);
            jerseyFeature.color = feature;
            jerseyFeatures.push_back(jerseyFeature);
        }
    }

    if (jerseyFeatures.size() < 4) {
        return;
    }

    cv::Mat samples(static_cast<int>(jerseyFeatures.size()), 3, CV_32F);
    for (int row = 0; row < samples.rows; ++row) {
        samples.at<float>(row, 0) = jerseyFeatures[row].color[0];
        samples.at<float>(row, 1) = jerseyFeatures[row].color[1];
        samples.at<float>(row, 2) = jerseyFeatures[row].color[2];
    }

    cv::Mat labels;
    cv::Mat centers;
    try {
        cv::kmeans(
            samples,
            2,
            labels,
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 12, 1.0),
            3,
            cv::KMEANS_PP_CENTERS,
            centers
        );
    } catch (const cv::Exception&) {
        return;
    }

    if (labels.rows != static_cast<int>(jerseyFeatures.size())) {
        return;
    }

    float centerBrightness[2] = {0.0f, 0.0f};
    for (int cluster = 0; cluster < 2; ++cluster) {
        if (centers.rows > cluster && centers.cols >= 3) {
            centerBrightness[cluster] =
                centers.at<float>(cluster, 0) +
                centers.at<float>(cluster, 1) +
                centers.at<float>(cluster, 2);
        }
    }
    const bool swapTeams = centerBrightness[0] > centerBrightness[1];

    for (std::size_t i = 0; i < jerseyFeatures.size(); ++i) {
        const int targetIndex = jerseyFeatures[i].targetIndex;
        if (targetIndex < 0 || targetIndex >= static_cast<int>(targets.size())) {
            continue;
        }

        int team = labels.at<int>(static_cast<int>(i), 0);
        if (team < 0 || team > 1) {
            continue;
        }
        if (swapTeams) {
            team = 1 - team;
        }

        targets[targetIndex].teamId = team;
        targets[targetIndex].semanticLabel = makeSemanticLabel(targets[targetIndex]);
    }
}

bool YoloByteTrackDetector::extractJerseyFeature(
    const cv::Mat& frame,
    const TargetInfo& target,
    cv::Vec3f& feature
) const {
    if (frame.empty() || target.box.empty()) {
        return false;
    }

    const cv::Rect frameBounds(0, 0, frame.cols, frame.rows);
    const cv::Rect box = target.box & frameBounds;
    if (box.width < 10 || box.height < 18) {
        return false;
    }

    const int jerseyX = box.x + static_cast<int>(std::round(box.width * 0.15));
    const int jerseyY = box.y + static_cast<int>(std::round(box.height * 0.18));
    const int jerseyW = std::max(4, static_cast<int>(std::round(box.width * 0.70)));
    const int jerseyH = std::max(6, static_cast<int>(std::round(box.height * 0.34)));
    const cv::Rect jerseyBox = cv::Rect(jerseyX, jerseyY, jerseyW, jerseyH) & frameBounds;
    if (jerseyBox.empty()) {
        return false;
    }

    const cv::Mat roi = frame(jerseyBox);
    cv::Mat hsv;
    cv::cvtColor(roi, hsv, cv::COLOR_BGR2HSV);

    cv::Vec3d total(0.0, 0.0, 0.0);
    int count = 0;
    for (int y = 0; y < roi.rows; ++y) {
        const cv::Vec3b* bgrRow = roi.ptr<cv::Vec3b>(y);
        const cv::Vec3b* hsvRow = hsv.ptr<cv::Vec3b>(y);
        for (int x = 0; x < roi.cols; ++x) {
            const int hue = hsvRow[x][0];
            const int saturation = hsvRow[x][1];
            const int value = hsvRow[x][2];
            const bool grassLike = hue >= 35 && hue <= 95 && saturation >= 35 && value >= 35;
            const bool tooDark = value < 35;
            if (grassLike || tooDark) {
                continue;
            }

            total[0] += bgrRow[x][0];
            total[1] += bgrRow[x][1];
            total[2] += bgrRow[x][2];
            ++count;
        }
    }

    if (count < std::max(12, jerseyBox.area() / 12)) {
        return false;
    }

    feature = cv::Vec3f(
        static_cast<float>(total[0] / count),
        static_cast<float>(total[1] / count),
        static_cast<float>(total[2] / count)
    );
    return true;
}

std::string YoloByteTrackDetector::makeSemanticLabel(const TargetInfo& target) const {
    std::string label = CommonTool::targetType2Str(target.type);
    if (target.type == TargetType::PLAYER && target.teamId >= 0) {
        label = "team " + std::to_string(target.teamId + 1);
    }
    if (target.type == TargetType::GOALKEEPER) {
        label = "goalkeeper";
    } else if (target.type == TargetType::PERSON) {
        label = "referee";
    }
    if (target.classId >= 0) {
        label += " c" + std::to_string(target.classId);
    }
    return label;
}

float YoloByteTrackDetector::intersectionOverUnion(const cv::Rect2f& lhs, const cv::Rect2f& rhs) {
    const cv::Rect2f overlap = lhs & rhs;
    const float overlapArea = overlap.area();
    const float unionArea = lhs.area() + rhs.area() - overlapArea;
    return unionArea > 0.0f ? overlapArea / unionArea : 0.0f;
}

cv::Rect YoloByteTrackDetector::clampRectToFrame(const cv::Rect2f& rect, const cv::Size& frameSize) {
    const cv::Rect frameBounds(0, 0, frameSize.width, frameSize.height);
    const cv::Rect rounded(
        static_cast<int>(std::round(rect.x)),
        static_cast<int>(std::round(rect.y)),
        static_cast<int>(std::round(rect.width)),
        static_cast<int>(std::round(rect.height))
    );
    return rounded & frameBounds;
}
