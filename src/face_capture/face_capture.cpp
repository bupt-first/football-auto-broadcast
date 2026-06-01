#include "face_capture.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <utility>

bool FaceCaptureManager::init() {
    const std::array<std::string, 8> candidates = {
        "haarcascade_frontalface_default.xml",
        "haarcascade_frontalface_alt2.xml",
        "etc/haarcascades/haarcascade_frontalface_default.xml",
        "etc/haarcascades/haarcascade_frontalface_alt2.xml",
        "E:/opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml",
        "E:/opencv/build/etc/haarcascades/haarcascade_frontalface_alt2.xml",
        "E:/opencv/sources/data/haarcascades/haarcascade_frontalface_default.xml",
        "E:/opencv/sources/data/haarcascades/haarcascade_frontalface_alt2.xml"
    };

    cascadeLoaded = false;
    trackedFaces.clear();
    nextId = 1;

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path) && faceCascade.load(path)) {
            cascadeLoaded = true;
            std::cout << "Face cascade loaded: " << path << std::endl;
            break;
        }
    }

    if (!cascadeLoaded) {
        for (const auto& samplePath : {
                 "haarcascades/haarcascade_frontalface_alt2.xml",
                 "haarcascades/haarcascade_frontalface_default.xml"
             }) {
            const std::string resolved = cv::samples::findFile(samplePath, false, true);
            if (!resolved.empty() && faceCascade.load(resolved)) {
                cascadeLoaded = true;
                std::cout << "Face cascade loaded: " << resolved << std::endl;
                break;
            }
        }
    }

    if (!cascadeLoaded) {
        std::cout << "Face cascade not found. Face close-up detection is disabled." << std::endl;
    }

    return true;
}

std::vector<FaceInfo> FaceCaptureManager::capture(const cv::Mat& frame, double timestamp) {
    std::vector<FaceInfo> faces;
    if (frame.empty() || !cascadeLoaded) {
        return faces;
    }

    std::vector<cv::Rect> boxes;
    detectFaces(frame, boxes);
    const std::vector<std::pair<int, cv::Rect>> tracked = updateTracking(boxes);
    const cv::Rect frameBounds(0, 0, frame.cols, frame.rows);

    for (const auto& item : tracked) {
        const cv::Rect safeBox = item.second & frameBounds;
        if (safeBox.empty()) {
            continue;
        }

        FaceInfo info;
        info.face_box = safeBox;
        info.timestamp = timestamp;
        info.belong = "face_" + std::to_string(item.first);
        info.closeup_img = frame(safeBox).clone();
        info.emotion = recognizeEmotion(info.closeup_img);
        faces.push_back(info);
    }

    return faces;
}

std::vector<FaceInfo> FaceCaptureManager::process(const cv::Mat& frame) {
    return capture(frame, CommonTool::getCurrentTimestamp());
}

int FaceCaptureManager::getFaceCount() const {
    return static_cast<int>(trackedFaces.size());
}

bool FaceCaptureManager::detectFaces(const cv::Mat& frame, std::vector<cv::Rect>& faces) {
    if (frame.empty() || !cascadeLoaded) {
        faces.clear();
        return false;
    }

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    faceCascade.detectMultiScale(gray, faces, 1.1, 4, 0, cv::Size(40, 40));
    return !faces.empty();
}

std::vector<std::pair<int, cv::Rect>> FaceCaptureManager::updateTracking(const std::vector<cv::Rect>& newFaces) {
    std::map<int, cv::Rect> newTracked;
    std::vector<std::pair<int, cv::Rect>> result;
    std::set<int> usedIds;

    for (const auto& newFace : newFaces) {
        int bestId = -1;
        float bestIou = 0.2f;

        for (const auto& tracked : trackedFaces) {
            if (usedIds.find(tracked.first) != usedIds.end()) {
                continue;
            }

            const float iou = calcIOU(newFace, tracked.second);
            if (iou > bestIou) {
                bestIou = iou;
                bestId = tracked.first;
            }
        }

        if (bestId == -1) {
            bestId = nextId++;
        }

        newTracked[bestId] = newFace;
        usedIds.insert(bestId);
        result.push_back({bestId, newFace});
    }

    trackedFaces.swap(newTracked);
    return result;
}

float FaceCaptureManager::calcIOU(const cv::Rect& a, const cv::Rect& b) const {
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.width, b.x + b.width);
    const int y2 = std::min(a.y + a.height, b.y + b.height);

    const int width = std::max(0, x2 - x1);
    const int height = std::max(0, y2 - y1);
    const float overlap = static_cast<float>(width * height);
    if (overlap <= 0.0f) {
        return 0.0f;
    }

    const float areaA = static_cast<float>(a.width * a.height);
    const float areaB = static_cast<float>(b.width * b.height);
    const float unionArea = areaA + areaB - overlap;
    return unionArea > 0.0f ? overlap / unionArea : 0.0f;
}

EmotionType FaceCaptureManager::recognizeEmotion(const cv::Mat& face) {
    if (face.empty()) {
        return EmotionType::UNKNOWN;
    }

    cv::Scalar meanColor = cv::mean(face);
    const double brightness = (meanColor[0] + meanColor[1] + meanColor[2]) / 3.0;
    if (brightness > 150.0) {
        return EmotionType::HAPPY;
    }
    if (brightness < 70.0) {
        return EmotionType::SAD;
    }
    return EmotionType::CALM;
}
