#include "ui.h"

#include <algorithm>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

bool UIManager::initAll(int cameraIndex) {
    if (!initModules()) {
        return false;
    }
    return vs.init(cameraIndex);
}

bool UIManager::initAllAutoCamera() {
    if (!initModules()) {
        return false;
    }
    return vs.initAuto(0, 6);
}

bool UIManager::initAllFromFile(const std::string& videoPath) {
    if (!initModules()) {
        return false;
    }
    return vs.openFile(videoPath);
}

void UIManager::run() {
    std::cout << "Football auto broadcast started. Press SPACE to mark a highlight, ESC to exit." << std::endl;

    while (true) {
        cv::Mat frame = vs.readFrame();
        if (frame.empty()) {
            break;
        }

        const double timestamp = CommonTool::getCurrentTimestamp();
        targets = det.detect(frame);
        faces = face.capture(frame, timestamp);

        HighlightInfo highlight = det.detectHighlight(targets, timestamp);
        if (highlight.type != HighlightType::NONE) {
            vs.setMode(BroadcastMode::CLOSEUP);
            highlights.push_back(highlight);
        } else if (!targets.empty()) {
            vs.setMode(BroadcastMode::FOLLOW);
        } else {
            vs.setMode(BroadcastMode::NORMAL);
        }

        draw(frame);
        edit.recordFrame(frame, timestamp);
        edit.addHighlight(highlight, faces);
        cv::imshow("football-auto-broadcast", frame);

        const int key = cv::waitKey(1);
        if (key == 27) {
            break;
        }
        if (key == 32) {
            HighlightInfo manualHighlight;
            manualHighlight.type = HighlightType::GOAL;
            manualHighlight.start_time = std::max(0.0, timestamp - 3.0);
            manualHighlight.end_time = timestamp + 4.0;
            if (!targets.empty()) {
                manualHighlight.main_target = targets.front().box;
                manualHighlight.related_targets = targets;
            } else {
                manualHighlight.main_target = cv::Rect(frame.cols / 4, frame.rows / 4, frame.cols / 2, frame.rows / 2);
            }
            highlights.push_back(manualHighlight);
            edit.addHighlight(manualHighlight, faces);
            std::cout << "Manual highlight marked." << std::endl;
        }
    }

    edit.exportGlobal("highlight_report.json");
    edit.exportPersonal("personal_highlight_report.json", "unknown");
    const std::string highlightVideoPath = CommonTool::finalVideoOutputPath("highlight.mp4");
    edit.exportHighlightVideo(highlightVideoPath);
    vs.release();
    cv::destroyAllWindows();
    std::cout << "Reports saved to highlight_report.json and personal_highlight_report.json" << std::endl;
    std::cout << "Highlight video saved to " << highlightVideoPath
              << " when enough highlight frames are captured." << std::endl;
}

void UIManager::draw(cv::Mat& frame) {
    drawTargets(frame);
    drawFaces(frame);
    drawStatus(frame);
}

bool UIManager::initModules() {
    targets.clear();
    highlights.clear();
    faces.clear();
    return det.init() && face.init() && edit.init();
}

void UIManager::drawTargets(cv::Mat& frame) {
    for (const auto& target : targets) {
        const cv::Rect box = target.box & cv::Rect(0, 0, frame.cols, frame.rows);
        if (box.empty()) {
            continue;
        }
        const cv::Scalar color = target.type == TargetType::BALL
            ? cv::Scalar(0, 220, 255)
            : cv::Scalar(0, 180, 0);
        cv::rectangle(frame, box, color, 2);
        const std::string label = target.semanticLabel.empty()
            ? CommonTool::targetType2Str(target.type)
            : target.semanticLabel;
        cv::putText(
            frame,
            label,
            cv::Point(box.x, std::max(16, box.y - 6)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            color,
            1
        );
    }
}

void UIManager::drawFaces(cv::Mat& frame) {
    for (const auto& item : faces) {
        const cv::Rect box = item.face_box & cv::Rect(0, 0, frame.cols, frame.rows);
        if (box.empty()) {
            continue;
        }
        cv::rectangle(frame, box, cv::Scalar(255, 160, 0), 2);
        cv::putText(
            frame,
            CommonTool::emotionType2Str(item.emotion),
            cv::Point(box.x, std::max(16, box.y - 6)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(255, 160, 0),
            1
        );
    }
}

void UIManager::drawStatus(cv::Mat& frame) {
    std::string mode = "normal";
    if (vs.mode() == BroadcastMode::FOLLOW) {
        mode = "follow";
    } else if (vs.mode() == BroadcastMode::CLOSEUP) {
        mode = "closeup";
    }

    const std::string status = "mode: " + mode +
        " | targets: " + std::to_string(targets.size()) +
        " | faces: " + std::to_string(faces.size()) +
        " | highlights: " + std::to_string(highlights.size()) +
        " | SPACE mark";

    cv::rectangle(frame, cv::Rect(12, 12, 680, 34), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(
        frame,
        status,
        cv::Point(24, 36),
        cv::FONT_HERSHEY_SIMPLEX,
        0.65,
        cv::Scalar(255, 255, 255),
        2
    );
}
