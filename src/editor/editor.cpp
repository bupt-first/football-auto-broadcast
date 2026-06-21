#include "editor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <sstream>

namespace {
double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double rectCenterX(const cv::Rect& rect) {
    return rect.x + rect.width * 0.5;
}

double rectCenterY(const cv::Rect& rect) {
    return rect.y + rect.height * 0.5;
}

double safeDuration(double start, double end) {
    return std::max(0.0, end - start);
}

std::string shellQuote(const std::string& value) {
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

std::string timestampForFile() {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return oss.str();
}
}

void FFmpegEngine::clear() {
    inputs.clear();
    filters.clear();
    outputPath.clear();
}

void FFmpegEngine::addInput(const std::string& path) {
    inputs.push_back(path);
}

void FFmpegEngine::addFilter(const std::string& filter) {
    if (!filter.empty()) {
        filters.push_back(filter);
    }
}

void FFmpegEngine::setOutput(const std::string& path) {
    outputPath = path;
}

std::string FFmpegEngine::commandLine() const {
    std::ostringstream cmd;
    cmd << "ffmpeg -y";
    for (const auto& input : inputs) {
        cmd << " -i " << shellQuote(input);
    }

    if (!filters.empty()) {
        cmd << " -filter_complex " << shellQuote(std::accumulate(
            std::next(filters.begin()),
            filters.end(),
            filters.front(),
            [](const std::string& lhs, const std::string& rhs) {
                return lhs + ";" + rhs;
            }
        ));
    }

    cmd << " -r 30 -s 1920x1080 -c:v libx264 -crf 23 -g 60 -pix_fmt yuv420p";
    cmd << " -c:a aac -b:a 192k " << shellQuote(outputPath);
    return cmd.str();
}

bool FFmpegEngine::start(
    const ProgressCallback& progressCallback,
    const FinishCallback& finishCallback
) const {
    if (inputs.empty() || outputPath.empty()) {
        if (finishCallback) {
            finishCallback(false, outputPath);
        }
        return false;
    }

    if (progressCallback) {
        progressCallback(0);
    }

    const int result = std::system(commandLine().c_str());
    const bool ok = result == 0;

    if (progressCallback) {
        progressCallback(ok ? 100 : 0);
    }
    if (finishCallback) {
        finishCallback(ok, outputPath);
    }
    return ok;
}

bool VideoEditorManager::init(int width, int height, int fps) {
    videoWidth = width;
    videoHeight = height;
    videoFps = fps;
    editorConfig.outputWidth = width;
    editorConfig.outputHeight = height;
    editorConfig.outputFps = fps;
    editorConfig.outputDir = CommonTool::finalVideoOutputDir();
    prerollSeconds = editorConfig.preBufferSec;
    postrollSeconds = editorConfig.postBufferSec;
    recordingUntil = -1.0;
    highlightList.clear();
    importedEvents.clear();
    importedPlayerEvents.clear();
    faceList.clear();
    frameCache.clear();
    highlightFrames.clear();
    return true;
}

void VideoEditorManager::setConfig(const EditorConfig& newConfig) {
    editorConfig = newConfig;
    editorConfig.outputDir = CommonTool::finalVideoOutputDir();
    videoWidth = std::max(1, editorConfig.outputWidth);
    videoHeight = std::max(1, editorConfig.outputHeight);
    videoFps = std::max(1, editorConfig.outputFps);
    prerollSeconds = std::max(0, editorConfig.preBufferSec);
    postrollSeconds = std::max(0, editorConfig.postBufferSec);
}

const EditorConfig& VideoEditorManager::config() const {
    return editorConfig;
}

void VideoEditorManager::setVideoSource(const VideoSource& source) {
    videoSource = source;
}

void VideoEditorManager::recordFrame(const cv::Mat& frame, double timestamp) {
    if (frame.empty()) {
        return;
    }

    CachedFrame cached;
    cached.frame = prepareFrame(frame);
    cached.timestamp = timestamp;
    frameCache.push_back(cached);

    while (!frameCache.empty() && timestamp - frameCache.front().timestamp > prerollSeconds) {
        frameCache.pop_front();
    }

    if (timestamp <= recordingUntil) {
        const HighlightInfo* active = highlightList.empty() ? nullptr : &highlightList.back();
        highlightFrames.push_back(renderEffectFrame(cached.frame, timestamp, active));
    }
}

void VideoEditorManager::addHighlight(const HighlightInfo& hl, const std::vector<FaceInfo>& faces) {
    if (hl.type != HighlightType::NONE) {
        highlightList.push_back(hl);
        importedEvents.push_back(toEvent(hl));
        appendPreroll(hl);
        recordingUntil = std::max(recordingUntil, hl.end_time + postrollSeconds);
    }

    faceList.insert(faceList.end(), faces.begin(), faces.end());
}

bool VideoEditorManager::importEvents(const std::vector<HighlightEvent>& fullMatchHighlights) {
    importedEvents = filterEvents(fullMatchHighlights);
    return !importedEvents.empty();
}

bool VideoEditorManager::importPlayerEvents(const std::map<int, std::vector<HighlightEvent>>& playerHighlights) {
    importedPlayerEvents.clear();
    for (const auto& item : playerHighlights) {
        importedPlayerEvents[item.first] = filterEvents(item.second);
    }
    return !importedPlayerEvents.empty();
}

std::vector<HighlightEvent> VideoEditorManager::filterEvents(const std::vector<HighlightEvent>& events) const {
    std::vector<HighlightEvent> filtered;
    for (const auto& event : events) {
        if (event.confidence < editorConfig.minConfidence) {
            continue;
        }
        if (event.endSec <= event.startSec) {
            continue;
        }
        filtered.push_back(enrichEvent(event));
    }

    std::sort(filtered.begin(), filtered.end(), [](const HighlightEvent& lhs, const HighlightEvent& rhs) {
        if (lhs.startSec == rhs.startSec) {
            return lhs.confidence > rhs.confidence;
        }
        return lhs.startSec < rhs.startSec;
    });
    return filtered;
}

std::vector<EDLClip> VideoEditorManager::generateClips(const std::vector<HighlightEvent>& events) const {
    const std::vector<HighlightEvent> filtered = filterEvents(events);
    std::vector<EDLClip> clips;

    for (const auto& event : filtered) {
        const EventScoreBreakdown score = scoreEventBreakdown(event);
        EDLClip clip;
        clip.sourceStartSec = std::max(0.0, event.startSec - editorConfig.preBufferSec);
        clip.sourceEndSec = event.endSec + editorConfig.postBufferSec;
        if (videoSource.durationSec > 0.0) {
            clip.sourceEndSec = std::min(clip.sourceEndSec, videoSource.durationSec);
        }
        clip.playerID = event.playerID;
        clip.eventType = event.eventType;
        clip.highlightScore = score.total;
        clip.sourceCamera = event.sourceCamera.empty() ? "panorama" : event.sourceCamera;
        clip.replayCamera = event.replayCamera.empty() ? clip.sourceCamera : event.replayCamera;
        clip.selectionReason = event.selectionReason.empty() ? score.reason : event.selectionReason;
        clip.description = event.description.empty() ? eventTypeName(event.eventType) : event.description;
        clip.events.push_back(event);

        if (!clips.empty() && clip.sourceStartSec <= clips.back().sourceEndSec) {
            EDLClip& merged = clips.back();
            const double overlapStart = std::max(merged.sourceStartSec, clip.sourceStartSec);
            const double overlapEnd = std::min(merged.sourceEndSec, clip.sourceEndSec);
            const double overlap = safeDuration(overlapStart, overlapEnd);
            const double shorter = std::max(0.001, std::min(
                safeDuration(merged.sourceStartSec, merged.sourceEndSec),
                safeDuration(clip.sourceStartSec, clip.sourceEndSec)
            ));
            merged.sourceEndSec = std::max(merged.sourceEndSec, clip.sourceEndSec);
            merged.highlightScore = std::max(merged.highlightScore, clip.highlightScore);
            merged.redundancyScore = std::max(merged.redundancyScore, clamp01(overlap / shorter));
            if (!clip.description.empty() && merged.description.find(clip.description) == std::string::npos) {
                merged.description += " / " + clip.description;
            }
            if (!clip.selectionReason.empty() && merged.selectionReason.find(clip.selectionReason) == std::string::npos) {
                if (!merged.selectionReason.empty()) {
                    merged.selectionReason += " | ";
                }
                merged.selectionReason += clip.selectionReason;
            }
            merged.events.insert(merged.events.end(), clip.events.begin(), clip.events.end());
            if (clip.highlightScore >= merged.highlightScore || clip.eventType == 1 || merged.eventType == 0) {
                merged.eventType = clip.eventType;
                merged.sourceCamera = clip.sourceCamera;
                merged.replayCamera = clip.replayCamera;
            }
            if (merged.playerID < 0) {
                merged.playerID = clip.playerID;
            }
        } else {
            clips.push_back(clip);
        }
    }

    return clips;
}

std::vector<EDLClip> VideoEditorManager::buildEDL(const std::vector<EDLClip>& clips) const {
    std::vector<EDLClip> edl = clips;
    double timeline = editorConfig.titleDurationSec;
    for (auto& clip : edl) {
        clip.timelineStartSec = timeline;
        timeline += safeDuration(clip.sourceStartSec, clip.sourceEndSec);
        if (clip.eventType == 1) {
            timeline += safeDuration(clip.sourceStartSec, clip.sourceEndSec);
        }
        timeline = std::max(0.0, timeline - editorConfig.transitionSec);
    }
    return edl;
}

bool VideoEditorManager::exportVideo(
    const VideoSource& source,
    const std::vector<EDLClip>& edl,
    const std::string& outputPath,
    const std::string& titleText,
    int playerID,
    const ProgressCallback& progressCallback,
    const FinishCallback& finishCallback
) const {
    const std::string finalOutputPath = CommonTool::finalVideoOutputPath(outputPath);
    if (source.filePath.empty() || edl.empty()) {
        if (finishCallback) {
            finishCallback(false, finalOutputPath);
        }
        return false;
    }

    cv::VideoCapture capture(source.filePath);
    if (!capture.isOpened()) {
        std::cerr << "Failed to open source video: " << source.filePath << std::endl;
        if (finishCallback) {
            finishCallback(false, finalOutputPath);
        }
        return false;
    }

    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::VideoWriter writer;
    if (!writer.open(finalOutputPath, fourcc, videoFps, cv::Size(videoWidth, videoHeight))) {
        std::cerr << "Failed to open video writer: " << finalOutputPath << std::endl;
        if (finishCallback) {
            finishCallback(false, finalOutputPath);
        }
        return false;
    }

    if (progressCallback) {
        progressCallback(0);
    }

    const int titleFrames = static_cast<int>(std::round(editorConfig.titleDurationSec * videoFps));
    writeFrameSequence(
        writer,
        renderTextFrame(titleText, "match-aware automatic director", cv::Scalar(18, 42, 34)),
        titleFrames
    );

    const double sourceFps = capture.get(cv::CAP_PROP_FPS) > 1.0 ? capture.get(cv::CAP_PROP_FPS) : source.fps;
    const double totalWork = std::accumulate(edl.begin(), edl.end(), 0.0, [](double total, const EDLClip& clip) {
        return total + safeDuration(clip.sourceStartSec, clip.sourceEndSec);
    });
    double finishedWork = 0.0;
    int lastProgress = 0;

    for (const auto& clip : edl) {
        const int startFrame = static_cast<int>(std::max(0.0, clip.sourceStartSec * sourceFps));
        const int endFrame = static_cast<int>(std::max(0.0, clip.sourceEndSec * sourceFps));
        capture.set(cv::CAP_PROP_POS_FRAMES, startFrame);

        for (int frameIndex = startFrame; frameIndex <= endFrame; ++frameIndex) {
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                break;
            }

            cv::Mat output = prepareFrame(frame);
            cv::Mat overlay = output.clone();
            cv::rectangle(overlay, cv::Rect(0, output.rows - 118, output.cols, 118), cv::Scalar(0, 0, 0), cv::FILLED);
            cv::addWeighted(overlay, 0.42, output, 0.58, 0, output);

            const std::string eventText = clip.description.empty() ? eventTypeName(clip.eventType) : clip.description;
            cv::putText(output, eventText, cv::Point(36, output.rows - 70), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);

            std::ostringstream meta;
            meta << "score " << std::fixed << std::setprecision(2) << clip.highlightScore;
            if (playerID >= 0) {
                const auto nameIt = editorConfig.playerNames.find(playerID);
                meta << " | player " << (nameIt == editorConfig.playerNames.end() ? std::to_string(playerID) : nameIt->second);
            }
            cv::putText(output, meta.str(), cv::Point(36, output.rows - 30), cv::FONT_HERSHEY_SIMPLEX, 0.72, cv::Scalar(210, 240, 255), 2);

            const double localSec = (frameIndex - startFrame) / std::max(1.0, sourceFps);
            const double clipDuration = safeDuration(clip.sourceStartSec, clip.sourceEndSec);
            const double fade = std::min(0.5, clipDuration * 0.25);
            if (fade > 0.0 && (localSec < fade || localSec > clipDuration - fade)) {
                const double alpha = localSec < fade ? localSec / fade : (clipDuration - localSec) / fade;
                cv::Mat black(output.size(), output.type(), cv::Scalar(0, 0, 0));
                cv::addWeighted(output, clamp01(alpha), black, 1.0 - clamp01(alpha), 0, output);
            }

            writer.write(output);
            if (clip.eventType == 1) {
                writer.write(output);
            }

            if (progressCallback && totalWork > 0.0) {
                finishedWork += 1.0 / std::max(1.0, sourceFps);
                const int progress = std::min(99, static_cast<int>(std::floor(finishedWork / totalWork * 100.0)));
                if (progress > lastProgress) {
                    for (int p = lastProgress + 1; p <= progress; ++p) {
                        progressCallback(p);
                    }
                    lastProgress = progress;
                }
            }
        }
    }

    writeFrameSequence(
        writer,
        renderTextFrame("END", "auto broadcast and highlight output", cv::Scalar(8, 8, 8)),
        static_cast<int>(std::round(editorConfig.endingDurationSec * videoFps))
    );

    writer.release();
    if (progressCallback && lastProgress < 100) {
        for (int p = lastProgress + 1; p <= 100; ++p) {
            progressCallback(p);
        }
    }
    if (finishCallback) {
        finishCallback(true, finalOutputPath);
    }
    return true;
}

bool VideoEditorManager::exportFullMatchHighlights(const std::string& outputPath) {
    const std::vector<EDLClip> edl = buildEDL(generateClips(importedEvents));
    return exportVideo(videoSource, edl, outputPath, editorConfig.titleText);
}

bool VideoEditorManager::exportPlayerHighlights(const std::string& outputPath, int playerID) {
    std::vector<HighlightEvent> events;
    const auto it = importedPlayerEvents.find(playerID);
    if (it != importedPlayerEvents.end()) {
        events = it->second;
    } else {
        for (const auto& event : importedEvents) {
            if (event.playerID == playerID) {
                events.push_back(event);
            }
        }
    }

    const auto nameIt = editorConfig.playerNames.find(playerID);
    const std::string name = nameIt == editorConfig.playerNames.end() ? std::to_string(playerID) : nameIt->second;
    const std::vector<EDLClip> edl = buildEDL(generateClips(events));
    return exportVideo(videoSource, edl, outputPath, "Player Highlights: " + name, playerID);
}

bool VideoEditorManager::exportGlobal(const std::string& path) {
    return writeReport(path, highlightList);
}

bool VideoEditorManager::exportHighlightVideo(const std::string& path) {
    const std::string finalOutputPath = CommonTool::finalVideoOutputPath(path);
    if (highlightFrames.empty()) {
        std::cerr << "No highlight frames captured, skip video export." << std::endl;
        return false;
    }

    cv::VideoWriter writer;
    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    if (!writer.open(finalOutputPath, fourcc, videoFps, cv::Size(videoWidth, videoHeight))) {
        std::cerr << "Failed to open video writer: " << finalOutputPath << std::endl;
        return false;
    }

    writeFrameSequence(
        writer,
        renderTextFrame(editorConfig.titleText, "full-match highlight timeline", cv::Scalar(18, 42, 34)),
        std::max(1, videoFps * 2)
    );

    for (const auto& frame : highlightFrames) {
        writer.write(prepareFrame(frame));
    }

    writeFrameSequence(
        writer,
        renderTextFrame("END", "generated by C++ editor module", cv::Scalar(8, 8, 8)),
        std::max(1, videoFps)
    );

    writer.release();
    return true;
}

bool VideoEditorManager::exportPersonal(const std::string& path, const std::string& belong) {
    std::vector<HighlightInfo> personalHighlights;
    for (const auto& highlight : highlightList) {
        bool matched = false;
        for (const auto& face : faceList) {
            if (face.belong == belong &&
                face.timestamp >= highlight.start_time &&
                face.timestamp <= highlight.end_time) {
                matched = true;
                break;
            }
        }

        if (matched) {
            personalHighlights.push_back(highlight);
        }
    }

    return writeReport(path, personalHighlights);
}

const std::vector<HighlightInfo>& VideoEditorManager::highlights() const {
    return highlightList;
}

bool VideoEditorManager::writeReport(const std::string& path, const std::vector<HighlightInfo>& items) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "Failed to write report: " << path << std::endl;
        return false;
    }

    const EvaluationMetrics metrics = evaluate(items);
    std::vector<HighlightEvent> reportEvents;
    if (items.empty()) {
        reportEvents = importedEvents;
    } else {
        reportEvents.reserve(items.size());
        for (const auto& item : items) {
            reportEvents.push_back(toEvent(item));
        }
    }
    const std::vector<EDLClip> edl = buildEDL(generateClips(reportEvents));

    out << "{\n";
    out << "  \"video\": {\"width\": " << videoWidth << ", \"height\": " << videoHeight
        << ", \"fps\": " << videoFps << "},\n";
    out << "  \"comparison\": {\n";
    out << "    \"professional_broadcasting\": \"best visual quality and rich manual camera language, but requires many operators, replay staff, and expensive venue infrastructure\",\n";
    out << "    \"ai_sports_cameras\": \"low-cost automatic recording and ball following, but often only follows action instead of understanding football context\",\n";
    out << "    \"referee_technology\": \"VAR, goal-line, and offside systems provide structured tracking for decisions, but are not designed for audience-facing storytelling\"\n";
    out << "  },\n";
    out << "  \"scheme\": {\n";
    out << "    \"positioning\": \"match-aware, explainable, low-cost C++ automatic director for campus, amateur, and training matches\",\n";
    out << "    \"live_broadcast\": \"panorama as the stable base view, follow mode when the ball/player cluster moves, closeup/replay mode for goal-zone threat and high-score events\",\n";
    out << "    \"global_highlight\": \"filter confidence >= 0.5, add pre/post buffer, merge overlapping events, score events, then render a ranked full-match timeline\",\n";
    out << "    \"personal_highlight\": \"reuse the same event timeline and select events containing the requested player or face tag\",\n";
    out << "    \"hardware\": \"one 1080p/30fps panoramic camera on tripod, optional close-up USB/IP camera, laptop with OpenCV/FFmpeg, stable network for streaming\",\n";
    out << "    \"current_environment_adapter\": \"OpenCV frame cache and VideoWriter are used for reliable local export; FFmpegEngine is provided as an optional command wrapper for H.264/AAC pipelines\"\n";
    out << "  },\n";
    out << "  \"workflow\": {\n";
    out << "    \"automatic_directing\": [\"detect targets\", \"estimate event threat\", \"choose panorama/follow/closeup\", \"record decision reason\"],\n";
    out << "    \"full_match_editing\": [\"filterEvents\", \"enrichEvent\", \"scoreEventBreakdown\", \"generateClips\", \"buildEDL\", \"exportVideo\"],\n";
    out << "    \"personal_editing\": [\"group by playerID or face tag\", \"reuse clip buffers\", \"add player name strip\", \"export player file\"]\n";
    out << "  },\n";
    out << "  \"evaluation_system\": {\n";
    out << "    \"detection\": [\"event recall\", \"event precision\", \"missed key events\"],\n";
    out << "    \"broadcast\": [\"ball-in-frame rate\", \"key-player-in-frame rate\", \"switching smoothness\", \"tactical information retention\", \"latency\"],\n";
    out << "    \"highlight\": [\"highlight relevance\", \"clip redundancy\", \"replay value\", \"personal involvement\", \"subjective satisfaction\"]\n";
    out << "  },\n";
    out << "  \"highlight_count\": " << items.size() << ",\n";
    out << "  \"rendered_frame_count\": " << highlightFrames.size() << ",\n";
    out << "  \"metrics\": {\n";
    out << "    \"average_duration_seconds\": " << metrics.average_duration << ",\n";
    out << "    \"event_density_per_minute\": " << metrics.event_density << ",\n";
    out << "    \"target_visibility\": " << metrics.target_visibility << ",\n";
    out << "    \"replay_score\": " << metrics.replay_score << "\n";
    out << "  },\n";
    out << "  \"event_timeline\": [\n";
    for (std::size_t i = 0; i < reportEvents.size(); ++i) {
        const auto event = enrichEvent(reportEvents[i]);
        const EventScoreBreakdown score = scoreEventBreakdown(event);
        out << "    {\"timestamp\": " << event.startSec
            << ", \"duration\": " << safeDuration(event.startSec, event.endSec)
            << ", \"event_type\": \"" << eventTypeName(event.eventType)
            << "\", \"player_id\": " << event.playerID
            << ", \"field_zone\": \"" << jsonEscape(event.fieldZone)
            << "\", \"source_camera\": \"" << jsonEscape(event.sourceCamera)
            << "\", \"replay_camera\": \"" << jsonEscape(event.replayCamera)
            << "\", \"confidence\": " << event.confidence
            << ", \"highlight_score\": " << score.total
            << ", \"score_breakdown\": {"
            << "\"event_type\": " << score.eventType
            << ", \"field_zone\": " << score.fieldZone
            << ", \"attacking_threat\": " << score.attackingThreat
            << ", \"motion_intensity\": " << score.motionIntensity
            << ", \"player_involvement\": " << score.playerInvolvement
            << ", \"continuity\": " << score.continuity
            << ", \"replay_value\": " << score.replayValue
            << ", \"confidence\": " << score.confidence
            << "}, \"selection_reason\": \"" << jsonEscape(event.selectionReason.empty() ? score.reason : event.selectionReason)
            << "\", \"description\": \"" << jsonEscape(event.description) << "\"}";
        if (i + 1 < reportEvents.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"edl\": [\n";
    for (std::size_t i = 0; i < edl.size(); ++i) {
        const auto& clip = edl[i];
        out << "    {\"source_start\": " << clip.sourceStartSec
            << ", \"source_end\": " << clip.sourceEndSec
            << ", \"timeline_start\": " << clip.timelineStartSec
            << ", \"event_type\": \"" << eventTypeName(clip.eventType)
            << "\", \"player_id\": " << clip.playerID
            << ", \"highlight_score\": " << clip.highlightScore
            << ", \"redundancy_score\": " << clip.redundancyScore
            << ", \"source_camera\": \"" << jsonEscape(clip.sourceCamera)
            << "\", \"replay_camera\": \"" << jsonEscape(clip.replayCamera)
            << "\", \"selection_reason\": \"" << jsonEscape(clip.selectionReason)
            << "\", \"description\": \"" << jsonEscape(clip.description) << "\"}";
        if (i + 1 < edl.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"highlights\": [\n";

    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const MatchAwareScore score = scoreHighlight(item);
        const BroadcastDecision decision = decideCamera(item);

        out << "    {";
        out << "\"type\": \"" << CommonTool::highlightType2Str(item.type) << "\", ";
        out << "\"start_time\": " << item.start_time << ", ";
        out << "\"end_time\": " << item.end_time << ", ";
        out << "\"score\": " << score.total << ", ";
        out << "\"score_breakdown\": {"
            << "\"event_type\": " << score.eventType
            << ", \"field_zone\": " << score.fieldZone
            << ", \"attacking_threat\": " << score.attackingThreat
            << ", \"motion_intensity\": " << score.motionIntensity
            << ", \"player_involvement\": " << score.playerInvolvement
            << ", \"continuity\": " << score.continuity
            << ", \"score_impact\": " << score.scoreImpact
            << ", \"replay_value\": " << score.replayValue
            << "}, ";
        out << "\"score_reason\": \"" << jsonEscape(score.reason) << "\", ";
        out << "\"broadcast_mode\": \"" << CommonTool::broadcastMode2Str(decision.mode) << "\", ";
        out << "\"broadcast_reason\": \"" << jsonEscape(decision.reason) << "\", ";
        out << "\"target\": {\"x\": " << item.main_target.x
            << ", \"y\": " << item.main_target.y
            << ", \"width\": " << item.main_target.width
            << ", \"height\": " << item.main_target.height << "}, ";
        out << "\"related_target_count\": " << item.related_targets.size();
        out << "}";
        if (i + 1 < items.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ],\n";
    out << "  \"suggested_outputs\": {\n";
    out << "    \"full_match\": \"" << jsonEscape(CommonTool::finalVideoOutputPath("full_highlights_" + timestampForFile() + ".mp4")) << "\",\n";
    out << "    \"personal\": \"" << jsonEscape(CommonTool::finalVideoOutputPath("player_{playerID}_{name}_" + timestampForFile() + ".mp4")) << "\"\n";
    out << "  }\n";
    out << "}\n";
    return true;
}

EvaluationMetrics VideoEditorManager::evaluate(const std::vector<HighlightInfo>& items) const {
    EvaluationMetrics metrics;
    metrics.highlight_count = static_cast<int>(items.size());
    metrics.rendered_frame_count = static_cast<int>(highlightFrames.size());

    if (items.empty()) {
        return metrics;
    }

    double totalDuration = 0.0;
    double visibleTargets = 0.0;
    double scoreSum = 0.0;
    double start = items.front().start_time;
    double end = items.front().end_time;

    for (const auto& item : items) {
        totalDuration += safeDuration(item.start_time, item.end_time);
        visibleTargets += item.main_target.area() > 0 ? 1.0 : 0.0;
        scoreSum += scoreHighlight(item).total;
        start = std::min(start, item.start_time);
        end = std::max(end, item.end_time);
    }

    const double matchMinutes = std::max(1.0 / 60.0, (end - start) / 60.0);
    metrics.average_duration = totalDuration / items.size();
    metrics.event_density = items.size() / matchMinutes;
    metrics.target_visibility = visibleTargets / items.size();
    metrics.replay_score = scoreSum / items.size();
    return metrics;
}

VideoEditorManager::MatchAwareScore VideoEditorManager::scoreHighlight(const HighlightInfo& item) const {
    MatchAwareScore score;
    switch (item.type) {
        case HighlightType::GOAL:
            score.eventType = 1.0;
            score.scoreImpact = 1.0;
            break;
        case HighlightType::SAVE:
            score.eventType = 0.82;
            score.scoreImpact = 0.72;
            break;
        case HighlightType::SHOOT:
            score.eventType = 0.68;
            score.scoreImpact = 0.58;
            break;
        case HighlightType::NONE:
        default:
            score.eventType = 0.0;
            score.scoreImpact = 0.0;
            break;
    }

    if (item.main_target.area() > 0) {
        const double x = rectCenterX(item.main_target) / std::max(1, videoWidth);
        const double y = rectCenterY(item.main_target) / std::max(1, videoHeight);
        const double goalZone = std::max(0.0, std::abs(x - 0.5) * 2.0 - 0.55) / 0.45;
        const double centralLane = 1.0 - std::min(1.0, std::abs(y - 0.5) * 2.0);
        score.fieldZone = clamp01(0.68 * goalZone + 0.32 * centralLane);
    }

    int playerCount = 0;
    int ballCount = 0;
    double confidenceSum = 0.0;
    for (const auto& target : item.related_targets) {
        if (target.type == TargetType::PLAYER || target.type == TargetType::GOALKEEPER) {
            ++playerCount;
        }
        if (target.type == TargetType::BALL) {
            ++ballCount;
        }
        confidenceSum += target.confidence;
    }

    score.playerInvolvement = clamp01(playerCount / 8.0);
    score.attackingThreat = clamp01(0.45 * score.fieldZone + 0.35 * (ballCount > 0 ? 1.0 : 0.0) + 0.20 * score.playerInvolvement);
    score.motionIntensity = clamp01((ballCount > 0 ? 0.55 : 0.20) + item.related_targets.size() / 14.0);
    score.continuity = clamp01(safeDuration(item.start_time, item.end_time) / 8.0);
    const double avgConfidence = item.related_targets.empty() ? 0.5 : confidenceSum / item.related_targets.size();
    score.replayValue = clamp01(0.45 * avgConfidence + 0.35 * score.attackingThreat + 0.20 * score.continuity);
    score.total = clamp01(
        0.22 * score.eventType +
        0.13 * score.fieldZone +
        0.18 * score.attackingThreat +
        0.11 * score.motionIntensity +
        0.11 * score.playerInvolvement +
        0.09 * score.continuity +
        0.08 * score.scoreImpact +
        0.08 * score.replayValue
    );

    std::ostringstream reason;
    reason << eventTypeName(highlightTypeCode(item.type))
           << ", zone=" << std::fixed << std::setprecision(2) << score.fieldZone
           << ", threat=" << score.attackingThreat
           << ", motion=" << score.motionIntensity
           << ", involvement=" << score.playerInvolvement;
    score.reason = reason.str();
    return score;
}

VideoEditorManager::EventScoreBreakdown VideoEditorManager::scoreEventBreakdown(const HighlightEvent& event) const {
    EventScoreBreakdown score;
    double typeScore = 0.4;
    switch (event.eventType) {
        case 1:
            typeScore = 1.0;
            break;
        case 2:
        case 5:
            typeScore = 0.78;
            break;
        case 3:
        case 4:
            typeScore = 0.55;
            break;
        default:
            typeScore = 0.45;
            break;
    }

    score.eventType = typeScore;
    score.fieldZone = clamp01(event.fieldZoneScore);
    score.attackingThreat = clamp01(event.attackingThreatScore);
    score.motionIntensity = clamp01(event.motionIntensityScore);
    score.playerInvolvement = clamp01(event.playerInvolvementScore);
    score.continuity = event.continuityScore > 0.0
        ? clamp01(event.continuityScore)
        : clamp01(safeDuration(event.startSec, event.endSec) / 8.0);
    score.replayValue = clamp01(event.replayValueScore);
    score.confidence = clamp01(event.confidence);
    score.personal = event.playerID >= 0 ? 1.0 : 0.55;

    score.total = clamp01(
        0.24 * score.eventType +
        0.14 * score.fieldZone +
        0.17 * score.attackingThreat +
        0.12 * score.motionIntensity +
        0.10 * score.playerInvolvement +
        0.08 * score.continuity +
        0.08 * score.replayValue +
        0.05 * score.confidence +
        0.02 * score.personal
    );

    std::ostringstream reason;
    reason << eventTypeName(event.eventType)
           << " selected by score=" << std::fixed << std::setprecision(2) << score.total
           << " type=" << score.eventType
           << " zone=" << score.fieldZone
           << " threat=" << score.attackingThreat
           << " motion=" << score.motionIntensity
           << " replay=" << score.replayValue;
    score.reason = reason.str();
    return score;
}

HighlightEvent VideoEditorManager::enrichEvent(const HighlightEvent& event) const {
    HighlightEvent enriched = event;
    const double duration = safeDuration(enriched.startSec, enriched.endSec);
    const bool isScoringEvent = enriched.eventType == 1 || enriched.eventType == 2 || enriched.eventType == 5;

    if (enriched.description.empty()) {
        enriched.description = eventTypeName(enriched.eventType);
    }
    if (enriched.fieldZone == "unknown") {
        enriched.fieldZone = isScoringEvent ? "attacking_third" : "middle_third";
    }
    if (enriched.sourceCamera.empty()) {
        enriched.sourceCamera = "panorama";
    }
    if (enriched.replayCamera.empty()) {
        enriched.replayCamera = isScoringEvent ? "auxiliary_or_virtual_follow" : enriched.sourceCamera;
    }
    if (enriched.fieldZoneScore <= 0.0) {
        enriched.fieldZoneScore = isScoringEvent ? 0.72 : 0.42;
    }
    if (enriched.attackingThreatScore <= 0.0) {
        enriched.attackingThreatScore = isScoringEvent ? 0.70 : 0.38;
    }
    if (enriched.motionIntensityScore <= 0.0) {
        enriched.motionIntensityScore = clamp01(0.35 + duration / 10.0);
    }
    if (enriched.playerInvolvementScore <= 0.0) {
        enriched.playerInvolvementScore = enriched.playerID >= 0 ? 0.88 : 0.50;
    }
    if (enriched.continuityScore <= 0.0) {
        enriched.continuityScore = clamp01(duration / 8.0);
    }
    if (enriched.replayValueScore <= 0.0) {
        enriched.replayValueScore = clamp01(
            0.42 * enriched.attackingThreatScore +
            0.26 * enriched.motionIntensityScore +
            0.20 * enriched.confidence +
            0.12 * enriched.continuityScore
        );
    }
    if (enriched.involvedTargetCount <= 0) {
        enriched.involvedTargetCount = enriched.playerID >= 0 ? 1 : 0;
    }

    const EventScoreBreakdown score = scoreEventBreakdown(enriched);
    if (enriched.selectionReason.empty()) {
        enriched.selectionReason = score.reason;
    }
    return enriched;
}

double VideoEditorManager::scoreEvent(const HighlightEvent& event) const {
    return scoreEventBreakdown(enrichEvent(event)).total;
}

BroadcastDecision VideoEditorManager::decideCamera(const HighlightInfo& item) const {
    BroadcastDecision decision;
    const MatchAwareScore score = scoreHighlight(item);
    decision.hold_until = item.end_time + postrollSeconds;

    if (item.type == HighlightType::GOAL || score.attackingThreat > 0.72) {
        decision.mode = BroadcastMode::CLOSEUP;
        decision.reason = "penalty-area threat or decisive event";
    } else if (score.playerInvolvement > 0.35 || item.main_target.area() > 0) {
        decision.mode = BroadcastMode::FOLLOW;
        decision.reason = "ball/player cluster needs action following";
    } else {
        decision.mode = BroadcastMode::NORMAL;
        decision.reason = "panoramic tactical context retained";
    }

    return decision;
}

HighlightEvent VideoEditorManager::toEvent(const HighlightInfo& item) const {
    const MatchAwareScore score = scoreHighlight(item);
    const BroadcastDecision decision = decideCamera(item);
    HighlightEvent event;
    event.eventType = highlightTypeCode(item.type);
    event.startSec = item.start_time;
    event.endSec = item.end_time;
    event.playerID = -1;
    event.confidence = static_cast<float>(std::max(0.5, score.total));
    event.fieldZoneScore = score.fieldZone;
    event.attackingThreatScore = score.attackingThreat;
    event.motionIntensityScore = score.motionIntensity;
    event.playerInvolvementScore = score.playerInvolvement;
    event.continuityScore = score.continuity;
    event.replayValueScore = score.replayValue;
    event.involvedTargetCount = static_cast<int>(item.related_targets.size());
    event.fieldZone = score.fieldZone > 0.62 ? "attacking_third" : "middle_or_defensive_third";
    event.sourceCamera = decision.mode == BroadcastMode::CLOSEUP ? "auxiliary" : "panorama";
    event.replayCamera = decision.mode == BroadcastMode::NORMAL ? "panorama" : "virtual_follow";
    event.selectionReason = score.reason + " | " + decision.reason;
    event.description = CommonTool::highlightType2Str(item.type);
    return event;
}

cv::Mat VideoEditorManager::prepareFrame(const cv::Mat& frame) const {
    cv::Mat output;
    if (frame.size() == cv::Size(videoWidth, videoHeight)) {
        output = frame.clone();
    } else {
        cv::resize(frame, output, cv::Size(videoWidth, videoHeight));
    }
    return output;
}

cv::Mat VideoEditorManager::renderEffectFrame(const cv::Mat& frame, double timestamp, const HighlightInfo* item) const {
    cv::Mat output = prepareFrame(frame);

    cv::Mat sharpened = CommonTool::imageSharpen(output);
    if (!sharpened.empty()) {
        cv::addWeighted(sharpened, 0.68, output, 0.32, 0, output);
    }

    const MatchAwareScore score = item == nullptr ? MatchAwareScore{} : scoreHighlight(*item);
    const BroadcastDecision decision = item == nullptr ? BroadcastDecision{} : decideCamera(*item);

    const double pulse = std::sin(timestamp * 8.0) * 0.5 + 0.5;
    const int border = 6 + static_cast<int>(pulse * 8.0);
    cv::rectangle(output, cv::Rect(0, 0, output.cols, output.rows), cv::Scalar(0, 210, 255), border);

    cv::Mat overlay = output.clone();
    cv::rectangle(overlay, cv::Rect(0, output.rows - 116, output.cols, 116), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::addWeighted(overlay, 0.46, output, 0.54, 0, output);

    const std::string type = item == nullptr ? "HIGHLIGHT REPLAY" : CommonTool::highlightType2Str(item->type);
    cv::putText(output, "AUTO DIRECTOR | " + type, cv::Point(36, output.rows - 70), cv::FONT_HERSHEY_SIMPLEX, 0.95, cv::Scalar(255, 255, 255), 2);

    std::ostringstream detail;
    detail << "mode: " << CommonTool::broadcastMode2Str(decision.mode)
           << " | score: " << std::fixed << std::setprecision(2) << score.total
           << " | " << decision.reason;
    cv::putText(output, detail.str(), cv::Point(36, output.rows - 30), cv::FONT_HERSHEY_SIMPLEX, 0.68, cv::Scalar(210, 240, 255), 2);
    return output;
}

cv::Mat VideoEditorManager::renderTitleFrame(const HighlightInfo& hl, const cv::Mat& baseFrame) const {
    cv::Mat output = prepareFrame(baseFrame);
    cv::Mat overlay = output.clone();

    cv::rectangle(overlay, cv::Rect(0, 0, output.cols, output.rows), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::addWeighted(overlay, 0.58, output, 0.42, 0, output);

    const MatchAwareScore score = scoreHighlight(hl);
    cv::rectangle(output, cv::Rect(0, output.rows / 2 - 84, output.cols, 168), cv::Scalar(0, 185, 220), cv::FILLED);
    cv::putText(output, "MATCH-AWARE HIGHLIGHT", cv::Point(output.cols / 2 - 390, output.rows / 2 - 22), cv::FONT_HERSHEY_SIMPLEX, 1.35, cv::Scalar(0, 0, 0), 4);

    std::ostringstream subtitle;
    subtitle << CommonTool::highlightType2Str(hl.type) << " | explainable score " << std::fixed << std::setprecision(2) << score.total;
    cv::putText(output, subtitle.str(), cv::Point(output.cols / 2 - 300, output.rows / 2 + 44), cv::FONT_HERSHEY_SIMPLEX, 0.92, cv::Scalar(0, 0, 0), 3);
    return output;
}

cv::Mat VideoEditorManager::renderTextFrame(const std::string& title, const std::string& subtitle, const cv::Scalar& bg) const {
    cv::Mat output(videoHeight, videoWidth, CV_8UC3, bg);
    cv::rectangle(output, cv::Rect(0, videoHeight / 2 - 96, videoWidth, 192), cv::Scalar(0, 190, 220), cv::FILLED);
    cv::putText(output, title, cv::Point(80, videoHeight / 2 - 18), cv::FONT_HERSHEY_SIMPLEX, 1.55, cv::Scalar(0, 0, 0), 4);
    cv::putText(output, subtitle, cv::Point(84, videoHeight / 2 + 52), cv::FONT_HERSHEY_SIMPLEX, 0.82, cv::Scalar(20, 20, 20), 2);
    return output;
}

void VideoEditorManager::appendPreroll(const HighlightInfo& hl) {
    if (frameCache.empty()) {
        return;
    }

    const cv::Mat title = renderTitleFrame(hl, frameCache.back().frame);
    const int titleFrames = std::max(1, videoFps / 2);
    for (int i = 0; i < titleFrames; ++i) {
        highlightFrames.push_back(title);
    }

    for (const auto& cached : frameCache) {
        highlightFrames.push_back(renderEffectFrame(cached.frame, cached.timestamp, &hl));
    }
}

void VideoEditorManager::writeFrameSequence(cv::VideoWriter& writer, const cv::Mat& frame, int count) const {
    for (int i = 0; i < count; ++i) {
        writer.write(frame);
    }
}

std::string VideoEditorManager::jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

std::string VideoEditorManager::eventTypeName(int eventType) {
    switch (eventType) {
        case 1:
            return "goal";
        case 2:
            return "shot";
        case 3:
            return "foul";
        case 4:
            return "corner";
        case 5:
            return "save";
        default:
            return "event";
    }
}

int VideoEditorManager::highlightTypeCode(HighlightType type) {
    switch (type) {
        case HighlightType::GOAL:
            return 1;
        case HighlightType::SHOOT:
            return 2;
        case HighlightType::SAVE:
            return 5;
        case HighlightType::NONE:
        default:
            return 0;
    }
}
