#include "ui/ui.h"
#include "ui/qt_broadcast_window.h"
#include "editor/editor.h"

#include <QApplication>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
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

struct DebugBallSeed {
    int frame = 0;
    double x = -1.0;
    double y = -1.0;
    double radius = 160.0;
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
        event.motionIntensityScore = std::min(1.0, sample.intensity / std::max(1.0, threshold * 1.8));
        event.fieldZoneScore = event.eventType == 2 ? 0.68 : 0.42;
        event.attackingThreatScore = std::min(1.0, 0.55 * event.fieldZoneScore + 0.45 * event.motionIntensityScore);
        event.playerInvolvementScore = 0.56;
        event.continuityScore = std::min(1.0, (event.endSec - event.startSec) / 8.0);
        event.replayValueScore = std::min(1.0,
            0.38 * event.attackingThreatScore +
            0.30 * event.motionIntensityScore +
            0.20 * event.confidence +
            0.12 * event.continuityScore
        );
        event.fieldZone = event.eventType == 2 ? "motion_inferred_attacking_third" : "motion_inferred_middle_third";
        event.sourceCamera = "panorama";
        event.replayCamera = event.eventType == 2 ? "virtual_follow" : "panorama";
        event.involvedTargetCount = 0;
        event.selectionReason = event.eventType == 2
            ? "strong motion peak selected as possible shot or fast attack"
            : "above-threshold match motion selected for full-match context";
        event.description = event.eventType == 2 ? "strong football action" : "match motion highlight";

        if (!events.empty() && event.startSec <= events.back().endSec + 1.5) {
            events.back().endSec = std::max(events.back().endSec, event.endSec);
            events.back().confidence = std::max(events.back().confidence, event.confidence);
            events.back().motionIntensityScore = std::max(events.back().motionIntensityScore, event.motionIntensityScore);
            events.back().attackingThreatScore = std::max(events.back().attackingThreatScore, event.attackingThreatScore);
            events.back().continuityScore = std::min(1.0, (events.back().endSec - events.back().startSec) / 8.0);
            events.back().replayValueScore = std::max(events.back().replayValueScore, event.replayValueScore);
            if (event.eventType == 2) {
                events.back().eventType = 2;
                events.back().fieldZoneScore = event.fieldZoneScore;
                events.back().fieldZone = event.fieldZone;
                events.back().replayCamera = event.replayCamera;
                events.back().selectionReason = event.selectionReason;
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
        fallback.fieldZoneScore = 0.35;
        fallback.attackingThreatScore = 0.25;
        fallback.motionIntensityScore = 0.20;
        fallback.playerInvolvementScore = 0.35;
        fallback.continuityScore = std::min(1.0, (fallback.endSec - fallback.startSec) / 8.0);
        fallback.replayValueScore = 0.32;
        fallback.fieldZone = "fallback_match_context";
        fallback.sourceCamera = "panorama";
        fallback.replayCamera = "panorama";
        fallback.selectionReason = "fallback segment used because no motion peak passed the event threshold";
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
    editor.exportGlobal("highlight_report.json");

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
        event.playerInvolvementScore = std::max(event.playerInvolvementScore, 0.90);
        event.replayValueScore = std::max(event.replayValueScore, 0.72);
        event.selectionReason += " | reused for No.11 personal highlight package";
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

    editor.exportGlobal("highlight_report.json");
    editor.exportPersonal("personal_highlight_report.json", "unknown");

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

int debugBallDetection(
    const std::string& inputPath,
    std::vector<DebugBallSeed> seeds
) {
    cv::VideoCapture capture(inputPath);
    if (!capture.isOpened()) {
        std::cerr << "Failed to open source video: " << inputPath << std::endl;
        return 1;
    }

    double fps = capture.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0) {
        fps = FPS;
    }
    const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
    if (width <= 0 || height <= 0) {
        std::cerr << "Input video has invalid frame size." << std::endl;
        return 1;
    }

    const std::filesystem::path outputDir(CommonTool::finalVideoOutputDir());
    const std::filesystem::path sampleDir = outputDir / "ball_detection_samples";
    std::filesystem::create_directories(sampleDir);
    for (const auto& entry : std::filesystem::directory_iterator(sampleDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
            std::filesystem::remove(entry.path());
        }
    }

    const std::string outputVideo = CommonTool::finalVideoOutputPath("ball_detection_debug.mp4");
    cv::VideoWriter writer;
    writer.open(outputVideo, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, cv::Size(width, height));
    if (!writer.isOpened()) {
        std::cerr << "Failed to create debug video: " << outputVideo << std::endl;
        return 1;
    }

    std::sort(seeds.begin(), seeds.end(), [](const DebugBallSeed& lhs, const DebugBallSeed& rhs) {
        return lhs.frame < rhs.frame;
    });
    seeds.erase(
        std::remove_if(seeds.begin(), seeds.end(), [](const DebugBallSeed& seed) {
            return seed.frame < 0 || seed.x < 0.0 || seed.y < 0.0 || seed.radius <= 0.0;
        }),
        seeds.end()
    );

    const double firstSeedRadius = seeds.empty() ? 160.0 : seeds.front().radius;

    DetectionConfig config;
    config.confidenceThreshold = 0.08;
    config.ballCandidateConfidenceThreshold = 0.20;
    config.confirmedBallConfidenceThreshold = 0.42;
    config.ballGrassRejectThreshold = 0.50;
    config.ballSaturationRejectThreshold = 0.36;
    config.ballPredictionGate = std::max(55.0, firstSeedRadius * 0.35);
    config.yoloInputSize = 640;
    config.minBallTrackHits = 2;
    config.autoBallBootstrapHits = 3;

    TargetDetectionManager detector;
    detector.init(config);

    cv::Mat frame;
    int frameIndex = 0;
    int framesWithBall = 0;
    int framesWithTargets = 0;
    double confidenceTotal = 0.0;
    std::vector<cv::Point> ballTrail;
    int trailMisses = 0;
    bool trackingActive = false;
    std::size_t nextSeedIndex = 0;
    cv::Point searchCenter;
    double activeSeedRadius = firstSeedRadius;
    cv::Mat ballTemplate;
    bool templateReady = false;
    int templateMisses = 0;
    int templateSize = std::max(14, static_cast<int>(std::round(activeSeedRadius * 0.16)));
    const int sampleStep = std::max(1, static_cast<int>(std::round(fps * 2.0)));

    while (capture.read(frame)) {
        if (frame.empty()) {
            ++frameIndex;
            continue;
        }

        const double timestamp = frameIndex / fps;
        while (nextSeedIndex < seeds.size() && seeds[nextSeedIndex].frame == frameIndex) {
            const DebugBallSeed& seed = seeds[nextSeedIndex];
            activeSeedRadius = seed.radius;
            templateSize = std::max(14, static_cast<int>(std::round(activeSeedRadius * 0.16)));
            searchCenter = cv::Point(
                static_cast<int>(std::round(seed.x)),
                static_cast<int>(std::round(seed.y))
            );
            trackingActive = true;
            detector.reset();
            detector.seedBallTrack(cv::Point2f(static_cast<float>(seed.x), static_cast<float>(seed.y)), timestamp, activeSeedRadius);
            ballTrail.clear();
            trailMisses = 0;
            templateReady = false;
            const cv::Rect seedBox(
                std::clamp(searchCenter.x - templateSize / 2, 0, std::max(0, frame.cols - templateSize)),
                std::clamp(searchCenter.y - templateSize / 2, 0, std::max(0, frame.rows - templateSize)),
                std::min(templateSize, frame.cols),
                std::min(templateSize, frame.rows)
            );
            if (seedBox.width > 4 && seedBox.height > 4) {
                cv::cvtColor(frame(seedBox), ballTemplate, cv::COLOR_BGR2GRAY);
                templateReady = true;
                templateMisses = 0;
            }
            ++nextSeedIndex;
        }

        std::vector<TargetInfo> targets;
        if (seeds.empty() || trackingActive) {
            targets = detector.detect(frame, timestamp);
        }
        if (templateReady && trackingActive) {
            const int searchRadius = std::max(45, static_cast<int>(std::round(activeSeedRadius * 0.35)));
            const int left = std::clamp(searchCenter.x - searchRadius, 0, frame.cols - 1);
            const int top = std::clamp(searchCenter.y - searchRadius, 0, frame.rows - 1);
            const int right = std::clamp(searchCenter.x + searchRadius, 0, frame.cols - 1);
            const int bottom = std::clamp(searchCenter.y + searchRadius, 0, frame.rows - 1);
            const cv::Rect searchBox(left, top, std::max(1, right - left + 1), std::max(1, bottom - top + 1));

            if (searchBox.width >= ballTemplate.cols && searchBox.height >= ballTemplate.rows) {
                cv::Mat searchGray;
                cv::cvtColor(frame(searchBox), searchGray, cv::COLOR_BGR2GRAY);
                cv::Mat result;
                cv::matchTemplate(searchGray, ballTemplate, result, cv::TM_CCOEFF_NORMED);

                double minVal = 0.0;
                double maxVal = 0.0;
                cv::Point minLoc;
                cv::Point maxLoc;
                cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
                if (maxVal >= 0.34) {
                    searchCenter = cv::Point(
                        searchBox.x + maxLoc.x + ballTemplate.cols / 2,
                        searchBox.y + maxLoc.y + ballTemplate.rows / 2
                    );
                    templateMisses = 0;

                    TargetInfo templateBall;
                    templateBall.type = TargetType::BALL;
                    templateBall.box = cv::Rect(
                        searchCenter.x - ballTemplate.cols / 2,
                        searchCenter.y - ballTemplate.rows / 2,
                        ballTemplate.cols,
                        ballTemplate.rows
                    );
                    templateBall.confidence = std::clamp(maxVal, 0.0, 1.0);
                    templateBall.timestamp = timestamp;

                    targets.erase(
                        std::remove_if(targets.begin(), targets.end(), [](const TargetInfo& target) {
                            return target.type == TargetType::BALL;
                        }),
                        targets.end()
                    );
                    targets.push_back(templateBall);
                } else {
                    ++templateMisses;
                    if (templateMisses > FPS / 2) {
                        targets.erase(
                            std::remove_if(targets.begin(), targets.end(), [](const TargetInfo& target) {
                                return target.type == TargetType::BALL;
                            }),
                            targets.end()
                        );
                    }
                }
            }
        }

        if (trackingActive) {
            cv::Point ballCenter = searchCenter;
            for (const auto& target : targets) {
                if (target.type == TargetType::BALL) {
                    const cv::Rect box = target.box & cv::Rect(0, 0, frame.cols, frame.rows);
                    if (!box.empty()) {
                        ballCenter = cv::Point(box.x + box.width / 2, box.y + box.height / 2);
                        break;
                    }
                }
            }

            struct PlayerCandidate {
                std::size_t index = 0;
                double distance = 0.0;
            };

            std::vector<PlayerCandidate> activePlayers;
            const double maxPlayerDistance = std::max(activeSeedRadius * 2.75, frame.cols * 0.10);
            const double maxFootBallVerticalGap = std::max(activeSeedRadius * 1.10, frame.rows * 0.11);
            const double minPlayerHeight = frame.rows * 0.055;
            const double maxPlayerHeight = frame.rows * 0.55;
            const int pitchFootLine = static_cast<int>(std::round(frame.rows * 0.42));

            for (std::size_t i = 0; i < targets.size(); ++i) {
                if (targets[i].type != TargetType::PLAYER) {
                    continue;
                }

                const cv::Rect box = targets[i].box & cv::Rect(0, 0, frame.cols, frame.rows);
                if (box.empty()) {
                    continue;
                }

                const double aspect = static_cast<double>(box.height) / std::max(1, box.width);
                const cv::Point footPoint(box.x + box.width / 2, box.y + box.height);
                const double distance = cv::norm(cv::Point2d(footPoint) - cv::Point2d(ballCenter));
                const bool plausibleShape = box.height >= minPlayerHeight
                    && box.height <= maxPlayerHeight
                    && box.width >= 10
                    && aspect >= 1.05
                    && aspect <= 5.8;
                const bool onPitch = footPoint.y >= pitchFootLine;
                const bool nearBall = distance <= maxPlayerDistance;
                const bool sameGroundBand = std::abs(footPoint.y - ballCenter.y) <= maxFootBallVerticalGap;

                if (plausibleShape && onPitch && nearBall && sameGroundBand) {
                    activePlayers.push_back({i, distance});
                }
            }

            std::sort(activePlayers.begin(), activePlayers.end(), [](const PlayerCandidate& lhs, const PlayerCandidate& rhs) {
                return lhs.distance < rhs.distance;
            });
            if (activePlayers.size() > 2) {
                activePlayers.resize(2);
            }

            std::vector<TargetInfo> filteredTargets;
            filteredTargets.reserve(targets.size());
            for (std::size_t i = 0; i < targets.size(); ++i) {
                if (targets[i].type != TargetType::PLAYER) {
                    filteredTargets.push_back(targets[i]);
                    continue;
                }

                const bool keepPlayer = std::find_if(activePlayers.begin(), activePlayers.end(), [i](const PlayerCandidate& candidate) {
                    return candidate.index == i;
                }) != activePlayers.end();
                if (keepPlayer) {
                    filteredTargets.push_back(targets[i]);
                }
            }
            targets.swap(filteredTargets);
        }

        if (!targets.empty()) {
            ++framesWithTargets;
        }

        cv::Mat annotated = frame.clone();
        if (trackingActive) {
            cv::circle(annotated, searchCenter, static_cast<int>(std::round(activeSeedRadius)), cv::Scalar(255, 120, 0), 2);
        }
        bool hasBall = false;
        for (const auto& target : targets) {
            const cv::Rect box = target.box & cv::Rect(0, 0, annotated.cols, annotated.rows);
            if (box.empty()) {
                continue;
            }

            const bool isBall = target.type == TargetType::BALL;
            const cv::Scalar color = isBall ? cv::Scalar(0, 230, 255) : cv::Scalar(80, 230, 120);
            cv::rectangle(annotated, box, color, isBall ? 3 : 2);

            std::ostringstream label;
            label << (target.semanticLabel.empty() ? CommonTool::targetType2Str(target.type) : target.semanticLabel)
                  << " " << std::fixed << std::setprecision(2) << target.confidence;
            if (target.trackId >= 0) {
                label << " #" << target.trackId;
            }
            cv::putText(
                annotated,
                label.str(),
                cv::Point(box.x, std::max(22, box.y - 8)),
                cv::FONT_HERSHEY_SIMPLEX,
                0.65,
                color,
                2
            );

            if (isBall) {
                hasBall = true;
                confidenceTotal += target.confidence;
                searchCenter = cv::Point(box.x + box.width / 2, box.y + box.height / 2);
                ballTrail.push_back(searchCenter);
                if (ballTrail.size() > static_cast<std::size_t>(FPS * 4)) {
                    ballTrail.erase(ballTrail.begin());
                }
            }
        }

        if (hasBall) {
            ++framesWithBall;
            trailMisses = 0;
        } else if (!ballTrail.empty()) {
            ++trailMisses;
            if (trailMisses > FPS / 2) {
                ballTrail.clear();
            }
        }

        for (std::size_t i = 1; i < ballTrail.size(); ++i) {
            cv::line(annotated, ballTrail[i - 1], ballTrail[i], cv::Scalar(0, 180, 255), 2);
        }

        std::ostringstream status;
        status << "ball trajectory debug | t=" << std::fixed << std::setprecision(2) << timestamp
               << "s | targets=" << targets.size()
               << " | ball=" << (hasBall ? "yes" : "no")
               << " | seed=" << (trackingActive ? "yes" : "no");
        cv::rectangle(annotated, cv::Rect(16, 16, std::min(900, annotated.cols - 32), 52), cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(annotated, status.str(), cv::Point(32, 51), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(245, 245, 245), 2);

        writer.write(annotated);

        if (frameIndex % sampleStep == 0 || hasBall) {
            std::ostringstream name;
            name << "frame_" << std::setw(6) << std::setfill('0') << frameIndex << ".jpg";
            cv::imwrite((sampleDir / name.str()).string(), annotated);
        }

        ++frameIndex;
    }

    writer.release();
    const double ballRate = frameIndex > 0 ? static_cast<double>(framesWithBall) / frameIndex : 0.0;
    const double avgConfidence = framesWithBall > 0 ? confidenceTotal / framesWithBall : 0.0;

    std::cout << "Debug frames: " << frameIndex << std::endl;
    std::cout << "Frames with any target: " << framesWithTargets << std::endl;
    std::cout << "Frames with confirmed ball: " << framesWithBall << std::endl;
    std::cout << "Confirmed ball rate: " << std::fixed << std::setprecision(3) << ballRate << std::endl;
    std::cout << "Average ball confidence: " << std::fixed << std::setprecision(3) << avgConfidence << std::endl;
    std::cout << "Debug video: " << outputVideo << std::endl;
    std::cout << "Sample frames: " << sampleDir.string() << std::endl;
    if (!seeds.empty()) {
        for (const DebugBallSeed& seed : seeds) {
            std::cout << "Seed frame: " << seed.frame
                      << " x=" << seed.x
                      << " y=" << seed.y
                      << " radius=" << seed.radius << std::endl;
        }
    }
    return 0;
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

    if (argc > 2 && std::string(argv[1]) == "--debug-ball-detection") {
        std::vector<DebugBallSeed> seeds;
        const int seedArgCount = argc - 3;
        if (seedArgCount > 0 && seedArgCount % 3 != 0 && seedArgCount % 4 != 0) {
            std::cerr << "Seed arguments must be repeated as frame x y or frame x y radius." << std::endl;
            return 1;
        }

        const int seedStride = (seedArgCount > 0 && seedArgCount % 4 == 0) ? 4 : 3;
        for (int argIndex = 3; argIndex < argc; argIndex += seedStride) {
            DebugBallSeed seed;
            seed.frame = std::stoi(argv[argIndex]);
            seed.x = std::stod(argv[argIndex + 1]);
            seed.y = std::stod(argv[argIndex + 2]);
            if (seedStride == 4) {
                seed.radius = std::stod(argv[argIndex + 3]);
            }
            seeds.push_back(seed);
        }
        return debugBallDetection(argv[2], seeds);
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
