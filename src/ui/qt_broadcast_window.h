#ifndef QT_BROADCAST_WINDOW_H
#define QT_BROADCAST_WINDOW_H

#include "common.h"
#include "detection/detection.h"
#include "face_capture/face_capture.h"
#include "video_stream/video_stream.h"

#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <opencv2/videoio.hpp>

class QCloseEvent;
class QResizeEvent;

class VideoPane : public QWidget {
public:
    explicit VideoPane(const QString& title, QWidget* parent = nullptr);

    void setFrame(const cv::Mat& frame);
    void setZoomPercent(int value);
    void resetZoom();
    int zoomPercent() const;
    void setFrameClickCallback(std::function<void(const cv::Point2f&)> callback);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool mapWidgetPointToFrame(const QPoint& point, cv::Point2f& framePoint) const;
    void updatePixmap();

    QLabel* titleLabel = nullptr;
    QLabel* imageLabel = nullptr;
    QScrollArea* scrollArea = nullptr;
    QSlider* zoomSlider = nullptr;
    QPushButton* resetButton = nullptr;
    QImage currentImage;
    QSize currentPixmapSize;
    int currentZoomPercent = 100;
    std::function<void(const cv::Point2f&)> frameClickCallback;
};

enum class OperatorMode {
    AUTO,
    FORCE_PANORAMA,
    FORCE_FOLLOW
};

class QtBroadcastWindow : public QMainWindow {
public:
    explicit QtBroadcastWindow(int panoramaCameraIndex = -1, int closeupCameraIndex = -1, QWidget* parent = nullptr);
    ~QtBroadcastWindow() override;

    bool initialize();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupStyle();
    QWidget* createLiveDirectorTab();
    QWidget* createHighlightTab(bool personal);
    QWidget* createMetricsTab();
    QWidget* createInfoPanel(const QString& title, const QString& subtitle, QWidget* content);
    QLabel* createPreviewPlaceholder(const QString& title, const QString& subtitle);
    QLabel* createMetricLabel(const QString& name, const QString& value);
    QListWidget* createHighlightList(bool personal);
    void processFrame();
    void startRecording();
    void stopRecording();
    void updateOperatorMode();
    BroadcastDecision decideBroadcast(
        const std::vector<TargetInfo>& panoramaTargets,
        const std::vector<TargetInfo>& closeupTargets,
        const std::vector<FaceInfo>& faces,
        double timestamp
    );
    cv::Mat renderBroadcastFrame(
        const DualCameraFrame& frame,
        const BroadcastDecision& decision,
        const std::vector<TargetInfo>& panoramaTargets,
        const std::vector<TargetInfo>& closeupTargets,
        const std::vector<FaceInfo>& faces
    );
    cv::Mat renderVirtualFollowFrame(const cv::Mat& panorama, const std::vector<TargetInfo>& targets) const;
    cv::Mat normalizeFrame(const cv::Mat& frame) const;
    void drawTargets(cv::Mat& frame, const std::vector<TargetInfo>& targets) const;
    void drawFaces(cv::Mat& frame, const std::vector<FaceInfo>& faces) const;
    void drawStatus(cv::Mat& frame, const BroadcastDecision& decision, std::size_t faceCount) const;
    void writeRecordingFrames(const cv::Mat& panorama, const cv::Mat& closeup, const cv::Mat& broadcast);
    void updateStatusText(const BroadcastDecision& decision, const DualCameraFrame& frame);
    void seedPanoramaBall(const cv::Point2f& normalizedPoint);
    void drawPanoramaSeed(cv::Mat& frame) const;
    bool openWriter(cv::VideoWriter& writer, const std::string& path) const;
    void releaseWriters();

    int requestedPanoramaIndex = -1;
    int requestedCloseupIndex = -1;

    DualVideoStreamManager streams;
    TargetDetectionManager panoramaDetection;
    TargetDetectionManager closeupDetection;
    FaceCaptureManager faceCapture;

    QTimer* frameTimer = nullptr;
    VideoPane* broadcastPane = nullptr;
    VideoPane* panoramaPane = nullptr;
    VideoPane* closeupPane = nullptr;
    QTabWidget* consoleTabs = nullptr;
    QPushButton* recordButton = nullptr;
    QPushButton* replayButton = nullptr;
    QRadioButton* autoModeButton = nullptr;
    QRadioButton* forcePanoramaButton = nullptr;
    QRadioButton* forceFollowButton = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* scoreLabel = nullptr;
    QLabel* clockLabel = nullptr;
    QLabel* decisionLabel = nullptr;
    QLabel* targetMetricLabel = nullptr;
    QLabel* replayMetricLabel = nullptr;
    QLabel* eventMetricLabel = nullptr;
    QLabel* latencyMetricLabel = nullptr;

    OperatorMode operatorMode = OperatorMode::AUTO;
    BroadcastDecision lastDecision;
    bool recording = false;
    double lastCloseupTriggerTime = -10.0;
    double lastFrameTimestamp = 0.0;
    cv::Size lastPanoramaFrameSize;
    bool panoramaSeedActive = false;
    cv::Point2f panoramaSeedDisplayCenter;
    double panoramaSeedDisplayRadius = 180.0;

    cv::VideoWriter panoramaWriter;
    cv::VideoWriter closeupWriter;
    cv::VideoWriter broadcastWriter;
};

#endif
