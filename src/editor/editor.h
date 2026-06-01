#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"

#include <deque>
#include <functional>
#include <map>
#include <string>

struct HighlightEvent {
    int eventType = 0;
    double startSec = 0.0;
    double endSec = 0.0;
    int playerID = -1;
    float confidence = 1.0f;
    std::string description;
};

struct VideoSource {
    std::string filePath;
    double fps = FPS;
    int width = VIDEO_WIDTH;
    int height = VIDEO_HEIGHT;
    double durationSec = 0.0;
};

struct EditorConfig {
    std::string outputDir = ".";
    bool enableBGM = false;
    std::string bgmPath;
    std::string titleText = "Full Match Highlights";
    int preBufferSec = 3;
    int postBufferSec = 5;
    double minConfidence = 0.5;
    double transitionSec = 0.3;
    double titleDurationSec = 5.0;
    double endingDurationSec = 3.0;
    int outputWidth = VIDEO_WIDTH;
    int outputHeight = VIDEO_HEIGHT;
    int outputFps = FPS;
    std::string logoPath;
    std::string fontPath;
    std::map<int, std::string> playerNames;
    std::map<int, std::string> playerAvatarPaths;
};

struct EDLClip {
    double sourceStartSec = 0.0;
    double sourceEndSec = 0.0;
    double timelineStartSec = 0.0;
    int playerID = -1;
    int eventType = 0;
    double highlightScore = 0.0;
    std::string description;
    std::vector<HighlightEvent> events;
};

using ProgressCallback = std::function<void(int)>;
using FinishCallback = std::function<void(bool, const std::string&)>;

class FFmpegEngine {
public:
    void clear();
    void addInput(const std::string& path);
    void addFilter(const std::string& filter);
    void setOutput(const std::string& path);
    std::string commandLine() const;
    bool start(
        const ProgressCallback& progressCallback = nullptr,
        const FinishCallback& finishCallback = nullptr
    ) const;

private:
    std::vector<std::string> inputs;
    std::vector<std::string> filters;
    std::string outputPath;
};

class VideoEditorManager {
public:
    bool init(int width = VIDEO_WIDTH, int height = VIDEO_HEIGHT, int fps = FPS);
    void setConfig(const EditorConfig& newConfig);
    const EditorConfig& config() const;
    void setVideoSource(const VideoSource& source);

    void recordFrame(const cv::Mat& frame, double timestamp);
    void addHighlight(const HighlightInfo& hl, const std::vector<FaceInfo>& faces);

    bool importEvents(const std::vector<HighlightEvent>& fullMatchHighlights);
    bool importPlayerEvents(const std::map<int, std::vector<HighlightEvent>>& playerHighlights);
    std::vector<HighlightEvent> filterEvents(const std::vector<HighlightEvent>& events) const;
    std::vector<EDLClip> generateClips(const std::vector<HighlightEvent>& events) const;
    std::vector<EDLClip> buildEDL(const std::vector<EDLClip>& clips) const;
    bool exportVideo(
        const VideoSource& source,
        const std::vector<EDLClip>& edl,
        const std::string& outputPath,
        const std::string& titleText,
        int playerID = -1,
        const ProgressCallback& progressCallback = nullptr,
        const FinishCallback& finishCallback = nullptr
    ) const;

    bool exportFullMatchHighlights(const std::string& outputPath);
    bool exportPlayerHighlights(const std::string& outputPath, int playerID);
    bool exportGlobal(const std::string& path);
    bool exportHighlightVideo(const std::string& path);
    bool exportPersonal(const std::string& path, const std::string& belong);
    const std::vector<HighlightInfo>& highlights() const;

private:
    struct CachedFrame {
        cv::Mat frame;
        double timestamp = 0.0;
    };

    struct MatchAwareScore {
        double eventType = 0.0;
        double fieldZone = 0.0;
        double attackingThreat = 0.0;
        double playerInvolvement = 0.0;
        double continuity = 0.0;
        double scoreImpact = 0.0;
        double replayValue = 0.0;
        double total = 0.0;
        std::string reason;
    };

    bool writeReport(const std::string& path, const std::vector<HighlightInfo>& items);
    EvaluationMetrics evaluate(const std::vector<HighlightInfo>& items) const;
    MatchAwareScore scoreHighlight(const HighlightInfo& item) const;
    double scoreEvent(const HighlightEvent& event) const;
    BroadcastDecision decideCamera(const HighlightInfo& item) const;
    HighlightEvent toEvent(const HighlightInfo& item) const;
    cv::Mat prepareFrame(const cv::Mat& frame) const;
    cv::Mat renderEffectFrame(const cv::Mat& frame, double timestamp, const HighlightInfo* item = nullptr) const;
    cv::Mat renderTitleFrame(const HighlightInfo& hl, const cv::Mat& baseFrame) const;
    cv::Mat renderTextFrame(const std::string& title, const std::string& subtitle, const cv::Scalar& bg) const;
    void appendPreroll(const HighlightInfo& hl);
    void writeFrameSequence(cv::VideoWriter& writer, const cv::Mat& frame, int count) const;
    static std::string jsonEscape(const std::string& value);
    static std::string eventTypeName(int eventType);
    static int highlightTypeCode(HighlightType type);

    int videoWidth = VIDEO_WIDTH;
    int videoHeight = VIDEO_HEIGHT;
    int videoFps = FPS;
    double prerollSeconds = 3.0;
    double postrollSeconds = 5.0;
    double recordingUntil = -1.0;
    EditorConfig editorConfig;
    VideoSource videoSource;
    std::vector<HighlightInfo> highlightList;
    std::vector<HighlightEvent> importedEvents;
    std::map<int, std::vector<HighlightEvent>> importedPlayerEvents;
    std::vector<FaceInfo> faceList;
    std::deque<CachedFrame> frameCache;
    std::vector<cv::Mat> highlightFrames;
};

#endif
