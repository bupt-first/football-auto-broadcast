#include "ui/ui.h"
#include "ui/qt_broadcast_window.h"
#include "editor/editor.h"

#include <QApplication>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
struct MotionSample {
    double timeSec = 0.0;
    double intensity = 0.0;
};

bool readVisibleFrame(cv::VideoCapture& cap, double& brightness, cv::Size& frameSize) {
    cv::Mat frame;
    brightness = 0.0;
    frameSize = {};

    for (int i = 0; i < 24; ++i) {
        cap >> frame;
        if (frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        frameSize = frame.size();

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        brightness = cv::mean(gray)[0];

        if (brightness > 8.0) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return false;
}

bool openProbeCamera(cv::VideoCapture& cap, int index, std::string& backend) {
    backend = "DirectShow";
    if (!cap.open(index, cv::CAP_DSHOW)) {
        backend = "Default";
        cap.open(index);
    }

    if (!cap.isOpened()) {
        backend = "-";
        return false;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);
    return true;
}

int probeCameras(int minIndex, int maxIndex) {
    std::cout << "OpenCV camera probe, indexes " << minIndex << "-" << maxIndex << std::endl;
    std::cout << "Tip: built-in laptop cameras are often index 0; USB cameras are usually 1 or higher." << std::endl;

    bool foundVisibleCamera = false;
    for (int index = minIndex; index <= maxIndex; ++index) {
        cv::VideoCapture cap;
        std::string backend;

        const bool opened = openProbeCamera(cap, index, backend);
        if (!opened) {
            std::cout << "index=" << index << " opened=no" << std::endl;
            continue;
        }

        double brightness = 0.0;
        cv::Size frameSize;
        const bool visible = readVisibleFrame(cap, brightness, frameSize);
        foundVisibleCamera = foundVisibleCamera || visible;

        std::cout << "index=" << index
                  << " opened=yes"
                  << " visible=" << (visible ? "yes" : "no")
                  << " backend=" << backend
                  << " frame=" << frameSize.width << "x" << frameSize.height
                  << " brightness=" << std::fixed << std::setprecision(1) << brightness
                  << " capture=" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH))
                  << "x" << static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT))
                  << " fps=" << std::setprecision(1) << cap.get(cv::CAP_PROP_FPS)
                  << std::endl;
    }

    if (!foundVisibleCamera) {
        std::cout << "No visible camera frames were detected. Check privacy permission, cable, and whether another app is using the camera." << std::endl;
        return 1;
    }

    return 0;
}

std::string makeActualVideoOutputName(const std::string& inputPath) {
    std::string stem = "actual_field_highlights";
    const std::size_t slash = inputPath.find_last_of("\\/");
    const std::string name = slash == std::string::npos ? inputPath : inputPath.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = name.substr(0, dot) + "_edited";
    }
    return stem + ".mp4";
}

std::vector<HighlightEvent> buildMotionEvents(const std::string& videoPath, double& fps, double& durationSec, int& width, int& height) {
    std::vector<HighlightEvent> events;
    cv::VideoCapture capture(videoPath);
    if (!capture.isOpened()) {
        std::cerr << "Failed to open source video: " << videoPath << std::endl;
        return events;
    }

    fps = capture.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0) {
        fps = FPS;
    }
    width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double frameCount = capture.get(cv::CAP_PROP_FRAME_COUNT);
    durationSec = frameCount > 0.0 ? frameCount / fps : 0.0;

    std::vector<MotionSample> samples;
    cv::Mat previousGray;
    cv::Mat frame;
    int frameIndex = 0;
    const int sampleStep = std::max(1, static_cast<int>(std::round(fps / 3.0)));

    while (capture.read(frame)) {
        if (frame.empty()) {
            ++frameIndex;
            continue;
        }

        cv::Mat small;
        cv::resize(frame, small, cv::Size(320, 180));
        cv::Mat gray;
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

        if (!previousGray.empty() && frameIndex % sampleStep == 0) {
            cv::Mat diff;
            cv::absdiff(previousGray, gray, diff);
            samples.push_back({frameIndex / fps, cv::mean(diff)[0]});
        }
        previousGray = gray;
        ++frameIndex;
    }

    if (durationSec <= 0.0 && frameIndex > 0) {
        durationSec = frameIndex / fps;
    }
    if (samples.empty()) {
        return events;
    }

    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0, [](double total, const MotionSample& item) {
        return total + item.intensity;
    }) / samples.size();
    const double variance = std::accumulate(samples.begin(), samples.end(), 0.0, [mean](double total, const MotionSample& item) {
        const double delta = item.intensity - mean;
        return total + delta * delta;
    }) / samples.size();
    const double threshold = std::max(mean * 1.12, mean + std::sqrt(variance) * 0.35);

    for (const auto& sample : samples) {
        if (sample.intensity < threshold) {
            continue;
        }

        HighlightEvent event;
        event.eventType = sample.intensity > threshold * 1.35 ? 2 : 0;
        event.startSec = std::max(0.0, sample.timeSec - 2.0);
        event.endSec = durationSec > 0.0 ? std::min(durationSec, sample.timeSec + 3.5) : sample.timeSec + 3.5;
        event.confidence = static_cast<float>(std::min(1.0, 0.55 + sample.intensity / std::max(1.0, threshold) * 0.25));
        event.description = event.eventType == 2 ? "strong football action" : "match motion highlight";

        if (!events.empty() && event.startSec <= events.back().endSec + 1.5) {
            events.back().endSec = std::max(events.back().endSec, event.endSec);
            events.back().confidence = std::max(events.back().confidence, event.confidence);
            if (event.eventType == 2) {
                events.back().eventType = 2;
                events.back().description = "strong football action";
            }
        } else {
            events.push_back(event);
        }
    }

    if (events.empty() && durationSec > 0.0) {
        HighlightEvent fallback;
        fallback.eventType = 0;
        fallback.startSec = 0.0;
        fallback.endSec = std::min(durationSec, 20.0);
        fallback.confidence = 0.6f;
        fallback.description = "actual match video segment";
        events.push_back(fallback);
    }

    return events;
}

int exportActualVideo(const std::string& inputPath, const std::string& outputName) {
    double fps = FPS;
    double durationSec = 0.0;
    int width = VIDEO_WIDTH;
    int height = VIDEO_HEIGHT;
    const std::vector<HighlightEvent> events = buildMotionEvents(inputPath, fps, durationSec, width, height);
    if (events.empty()) {
        std::cerr << "No readable frames or edit events were generated." << std::endl;
        return 1;
    }

    VideoEditorManager editor;
    editor.init(VIDEO_WIDTH, VIDEO_HEIGHT, FPS);

    VideoSource source;
    source.filePath = inputPath;
    source.fps = fps;
    source.width = width;
    source.height = height;
    source.durationSec = durationSec;
    editor.setVideoSource(source);

    EditorConfig config = editor.config();
    config.titleText = "Actual Field Match Highlights";
    config.preBufferSec = 2;
    config.postBufferSec = 4;
    config.minConfidence = 0.5;
    config.titleDurationSec = 3.0;
    config.endingDurationSec = 2.0;
    config.outputWidth = VIDEO_WIDTH;
    config.outputHeight = VIDEO_HEIGHT;
    config.outputFps = FPS;
    editor.setConfig(config);

    if (!editor.importEvents(events)) {
        std::cerr << "Generated events were rejected by editor filters." << std::endl;
        return 1;
    }

    std::cout << "Motion events: " << events.size() << std::endl;

    const bool exportThreeVideos = outputName.empty() || outputName == "all";
    if (!exportThreeVideos) {
        const std::string finalName = outputName.empty() ? makeActualVideoOutputName(inputPath) : outputName;
        const bool ok = editor.exportFullMatchHighlights(finalName);
        std::cout << "Output video: " << CommonTool::finalVideoOutputPath(finalName) << std::endl;
        return ok ? 0 : 1;
    }

    bool ok = true;
    const std::vector<EDLClip> broadcastEdl = editor.buildEDL(editor.generateClips(events));
    ok = editor.exportVideo(source, broadcastEdl, "broadcast_record.mp4", "Automatic Broadcast Output") && ok;
    ok = editor.exportFullMatchHighlights("highlight.mp4") && ok;

    std::vector<HighlightEvent> playerEvents = events;
    for (auto& event : playerEvents) {
        event.playerID = 11;
        if (event.description.empty() || event.description == "match motion highlight") {
            event.description = "No.11 player involvement";
        }
    }
    editor.importPlayerEvents({{11, playerEvents}});
    ok = editor.exportPlayerHighlights("personal_highlight.mp4", 11) && ok;

    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("broadcast_record.mp4") << std::endl;
    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("highlight.mp4") << std::endl;
    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("personal_highlight.mp4") << std::endl;
    return ok ? 0 : 1;
}

HighlightType eventTypeToHighlightType(int eventType) {
    if (eventType == 1) {
        return HighlightType::GOAL;
    }
    if (eventType == 5) {
        return HighlightType::SAVE;
    }
    return HighlightType::SHOOT;
}

int exportCurrentEditorClips(const std::string& inputPath) {
    double fps = FPS;
    double durationSec = 0.0;
    int width = VIDEO_WIDTH;
    int height = VIDEO_HEIGHT;
    std::vector<HighlightEvent> events = buildMotionEvents(inputPath, fps, durationSec, width, height);
    if (events.empty()) {
        std::cerr << "No readable frames or edit events were generated." << std::endl;
        return 1;
    }

    VideoEditorManager editor;
    editor.init(VIDEO_WIDTH, VIDEO_HEIGHT, FPS);

    EditorConfig config = editor.config();
    config.preBufferSec = 3;
    config.postBufferSec = 5;
    config.titleDurationSec = 2.0;
    config.endingDurationSec = 1.0;
    config.outputWidth = VIDEO_WIDTH;
    config.outputHeight = VIDEO_HEIGHT;
    config.outputFps = FPS;
    editor.setConfig(config);

    cv::VideoCapture capture(inputPath);
    if (!capture.isOpened()) {
        std::cerr << "Failed to open source video: " << inputPath << std::endl;
        return 1;
    }

    cv::Mat frame;
    int frameIndex = 0;
    std::size_t nextEventIndex = 0;
    while (capture.read(frame)) {
        if (frame.empty()) {
            ++frameIndex;
            continue;
        }

        const double timestamp = frameIndex / std::max(1.0, fps);
        editor.recordFrame(frame, timestamp);

        while (nextEventIndex < events.size() && timestamp >= events[nextEventIndex].startSec) {
            const HighlightEvent& event = events[nextEventIndex];
            HighlightInfo highlight;
            highlight.type = eventTypeToHighlightType(event.eventType);
            highlight.start_time = event.startSec;
            highlight.end_time = event.endSec;
            highlight.main_target = cv::Rect(
                std::max(0, width / 2 - width / 8),
                std::max(0, height / 2 - height / 8),
                std::max(1, width / 4),
                std::max(1, height / 4)
            );

            TargetInfo related;
            related.type = TargetType::PLAYER;
            related.box = highlight.main_target;
            related.confidence = event.confidence;
            related.timestamp = timestamp;
            highlight.related_targets.push_back(related);

            editor.addHighlight(highlight, {});
            ++nextEventIndex;
        }

        ++frameIndex;
    }

    bool ok = true;
    config.titleText = "Automatic Broadcast Output";
    editor.setConfig(config);
    ok = editor.exportHighlightVideo("broadcast_record.mp4") && ok;

    config.titleText = "Full Match Highlights";
    editor.setConfig(config);
    ok = editor.exportHighlightVideo("highlight.mp4") && ok;

    config.titleText = "Personal Highlights: No.11";
    editor.setConfig(config);
    ok = editor.exportHighlightVideo("personal_highlight.mp4") && ok;

    std::cout << "Editor clip events: " << events.size() << std::endl;
    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("broadcast_record.mp4") << std::endl;
    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("highlight.mp4") << std::endl;
    std::cout << "Output video: " << CommonTool::finalVideoOutputPath("personal_highlight.mp4") << std::endl;
    return ok ? 0 : 1;
}
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--probe-cameras") {
        int minIndex = 0;
        int maxIndex = 6;
        if (argc > 2) {
            minIndex = std::stoi(argv[2]);
        }
        if (argc > 3) {
            maxIndex = std::stoi(argv[3]);
        }
        return probeCameras(minIndex, maxIndex);
    }

    if (argc > 2 && std::string(argv[1]) == "--export-actual-video") {
        const std::string inputPath = argv[2];
        const std::string outputName = argc > 3 ? argv[3] : "";
        return exportActualVideo(inputPath, outputName);
    }

    if (argc > 2 && std::string(argv[1]) == "--export-current-editor-clips") {
        return exportCurrentEditorClips(argv[2]);
    }

    bool useLegacyUi = false;
    int panoramaCameraIndex = -1;
    int closeupCameraIndex = -1;
    int legacyCameraIndex = -1;
    std::string legacyVideoPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--legacy-ui") {
            useLegacyUi = true;
        } else if (arg == "--panorama-camera" && i + 1 < argc) {
            panoramaCameraIndex = std::stoi(argv[++i]);
        } else if (arg == "--closeup-camera" && i + 1 < argc) {
            closeupCameraIndex = std::stoi(argv[++i]);
        } else if (arg == "--camera" && i + 1 < argc) {
            useLegacyUi = true;
            legacyCameraIndex = std::stoi(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            useLegacyUi = true;
            legacyVideoPath = arg;
        }
    }

    if (!useLegacyUi) {
        QApplication qtApp(argc, argv);
        QtBroadcastWindow window(panoramaCameraIndex, closeupCameraIndex);
        if (!window.initialize()) {
            return 1;
        }
        window.show();
        return qtApp.exec();
    }

    UIManager app;
    bool ready = false;

    if (legacyCameraIndex >= 0) {
        ready = app.initAll(legacyCameraIndex);
    } else if (!legacyVideoPath.empty()) {
        ready = app.initAllFromFile(legacyVideoPath);
    } else {
        ready = app.initAllAutoCamera();
    }

    if (!ready) {
        std::cerr << "Failed to initialize football auto broadcast." << std::endl;
        return 1;
    }

    app.run();
    return 0;
}
