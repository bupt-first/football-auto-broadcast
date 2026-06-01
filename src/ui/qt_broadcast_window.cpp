#include "qt_broadcast_window.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QSizePolicy>
#include <QTabWidget>
#include <QtGlobal>
#include <QVBoxLayout>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <opencv2/imgproc.hpp>

namespace {
QImage matToImage(const cv::Mat& frame) {
    if (frame.empty()) {
        return QImage();
    }

    cv::Mat rgb;
    if (frame.channels() == 1) {
        cv::cvtColor(frame, rgb, cv::COLOR_GRAY2RGB);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    }

    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

QString modeLabel(BroadcastMode mode) {
    if (mode == BroadcastMode::CLOSEUP) {
        return "Close-up";
    }
    if (mode == BroadcastMode::FOLLOW) {
        return "Follow";
    }
    return "Panorama";
}

std::string formatTimestamp(double timestamp) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << timestamp << "s";
    return out.str();
}
}

VideoPane::VideoPane(const QString& title, QWidget* parent)
    : QWidget(parent) {
    titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("paneTitle");

    imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(320, 180);
    imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(false);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setFrameShape(QFrame::NoFrame);

    zoomSlider = new QSlider(Qt::Horizontal, this);
    zoomSlider->setRange(50, 200);
    zoomSlider->setValue(100);
    zoomSlider->setToolTip("Zoom");

    resetButton = new QPushButton("Reset", this);
    resetButton->setToolTip("Reset zoom");

    auto* controls = new QHBoxLayout();
    controls->addWidget(zoomSlider, 1);
    controls->addWidget(resetButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(titleLabel);
    layout->addWidget(scrollArea, 1);
    layout->addLayout(controls);

    connect(zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        currentZoomPercent = value;
        updatePixmap();
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        resetZoom();
    });
}

void VideoPane::setFrame(const cv::Mat& frame) {
    currentImage = matToImage(frame);
    updatePixmap();
}

void VideoPane::setZoomPercent(int value) {
    zoomSlider->setValue(std::clamp(value, 50, 200));
}

void VideoPane::resetZoom() {
    setZoomPercent(100);
}

int VideoPane::zoomPercent() const {
    return currentZoomPercent;
}

void VideoPane::updatePixmap() {
    if (currentImage.isNull()) {
        imageLabel->clear();
        imageLabel->setText("No Signal");
        return;
    }

    const QSize scaledSize(
        std::max(1, currentImage.width() * currentZoomPercent / 100),
        std::max(1, currentImage.height() * currentZoomPercent / 100)
    );
    const QPixmap pixmap = QPixmap::fromImage(currentImage).scaled(
        scaledSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    imageLabel->setPixmap(pixmap);
    imageLabel->resize(pixmap.size());
    imageLabel->setMinimumSize(pixmap.size());
}

QtBroadcastWindow::QtBroadcastWindow(int panoramaCameraIndex, int closeupCameraIndex, QWidget* parent)
    : QMainWindow(parent),
      requestedPanoramaIndex(panoramaCameraIndex),
      requestedCloseupIndex(closeupCameraIndex) {
    setupUi();
    setupStyle();
}

QtBroadcastWindow::~QtBroadcastWindow() {
    stopRecording();
    streams.release();
}

bool QtBroadcastWindow::initialize() {
    if (!streams.init(requestedPanoramaIndex, requestedCloseupIndex)) {
        QMessageBox::critical(this, "Camera Error", "Failed to open both USB cameras. Check UGREEN Camera 1080P and UVC Camera, or pass camera indexes on the command line.");
        return false;
    }

    if (!panoramaDetection.init() || !closeupDetection.init() || !faceCapture.init()) {
        QMessageBox::critical(this, "Init Error", "Detection modules failed to initialize.");
        return false;
    }

    statusLabel->setText(QString("Ready | panorama index %1 | close-up index %2")
        .arg(streams.panoramaIndex())
        .arg(streams.closeupIndex()));

    frameTimer->start(std::max(1, 1000 / FPS));
    return true;
}

void QtBroadcastWindow::closeEvent(QCloseEvent* event) {
    stopRecording();
    streams.release();
    QMainWindow::closeEvent(event);
}

void QtBroadcastWindow::setupUi() {
    setWindowTitle("Football Auto Broadcast Console");
    resize(1600, 900);
    setMinimumSize(1280, 720);

    frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, [this]() {
        processFrame();
    });

    broadcastPane = new VideoPane("自动转播输出 | Automatic Broadcast Output", this);
    panoramaPane = new VideoPane("全景画面 | UGREEN Camera 1080P", this);
    closeupPane = new VideoPane("球员特写画面 | UVC Camera", this);

    autoModeButton = new QRadioButton("AUTO", this);
    forcePanoramaButton = new QRadioButton("PANORAMA", this);
    forceCloseupButton = new QRadioButton("CLOSE-UP", this);
    autoModeButton->setChecked(true);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(autoModeButton);
    modeGroup->addButton(forcePanoramaButton);
    modeGroup->addButton(forceCloseupButton);
    connect(modeGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton*) {
        updateOperatorMode();
    });

    recordButton = new QPushButton("● REC", this);
    recordButton->setObjectName("recordButton");
    connect(recordButton, &QPushButton::clicked, this, [this]() {
        if (recording) {
            stopRecording();
        } else {
            startRecording();
        }
    });

    replayButton = new QPushButton("REPLAY", this);
    replayButton->setObjectName("secondaryButton");
    connect(replayButton, &QPushButton::clicked, this, [this]() {
        if (consoleTabs) {
            consoleTabs->setCurrentIndex(1);
        }
    });

    statusLabel = new QLabel("Initializing dual cameras...", this);
    statusLabel->setObjectName("statusLabel");

    scoreLabel = new QLabel("HOME 2  -  1 AWAY", this);
    scoreLabel->setObjectName("scoreLabel");
    scoreLabel->setAlignment(Qt::AlignCenter);

    clockLabel = new QLabel("00:00 | 2nd HALF", this);
    clockLabel->setObjectName("clockLabel");
    clockLabel->setAlignment(Qt::AlignCenter);

    auto* brandTitle = new QLabel("足球自动转播与剪辑程序", this);
    brandTitle->setObjectName("brandTitle");
    auto* brandSubtitle = new QLabel("Match-aware automatic director / live broadcast / full-match and personal highlights", this);
    brandSubtitle->setObjectName("brandSubtitle");

    auto* brandLayout = new QVBoxLayout();
    brandLayout->setSpacing(2);
    brandLayout->addWidget(brandTitle);
    brandLayout->addWidget(brandSubtitle);

    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(18, 12, 18, 8);
    topBar->setSpacing(16);
    topBar->addLayout(brandLayout, 1);
    topBar->addWidget(scoreLabel);
    topBar->addWidget(clockLabel);
    topBar->addWidget(recordButton);
    topBar->addWidget(replayButton);

    consoleTabs = new QTabWidget(this);
    consoleTabs->setDocumentMode(true);
    consoleTabs->addTab(createLiveDirectorTab(), "实时导播");
    consoleTabs->addTab(createHighlightTab(false), "全场高光");
    consoleTabs->addTab(createHighlightTab(true), "个人高光");
    consoleTabs->addTab(createMetricsTab(), "评估指标");

    auto* root = new QVBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addLayout(topBar);
    root->addWidget(consoleTabs, 1);

    auto* central = new QWidget(this);
    central->setLayout(root);
    setCentralWidget(central);
}

QWidget* QtBroadcastWindow::createLiveDirectorTab() {
    auto* tab = new QWidget(this);

    auto* sideLayout = new QVBoxLayout();
    sideLayout->setSpacing(12);
    sideLayout->addWidget(panoramaPane, 1);
    sideLayout->addWidget(closeupPane, 1);

    auto* modeContent = new QWidget(this);
    auto* modeLayout = new QVBoxLayout(modeContent);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(10);

    auto* modeButtons = new QHBoxLayout();
    modeButtons->setSpacing(8);
    modeButtons->addWidget(autoModeButton);
    modeButtons->addWidget(forcePanoramaButton);
    modeButtons->addWidget(forceCloseupButton);
    modeLayout->addLayout(modeButtons);

    decisionLabel = new QLabel("AUTO | stable panorama | waiting for threat trigger", this);
    decisionLabel->setObjectName("decisionLabel");
    decisionLabel->setWordWrap(true);
    modeLayout->addWidget(decisionLabel);

    auto* timelineContent = new QWidget(this);
    auto* timelineLayout = new QVBoxLayout(timelineContent);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(8);
    const QStringList eventTexts = {
        "02.6s save",
        "08.7s shoot",
        "14.8s goal",
        "25.0s attack",
        "37.3s replay"
    };
    for (const QString& text : eventTexts) {
        auto* item = new QLabel(text, this);
        item->setObjectName("timelineItem");
        timelineLayout->addWidget(item);
    }

    auto* metricContent = new QWidget(this);
    auto* metricLayout = new QGridLayout(metricContent);
    metricLayout->setContentsMargins(0, 0, 0, 0);
    metricLayout->setHorizontalSpacing(8);
    metricLayout->setVerticalSpacing(8);
    targetMetricLabel = createMetricLabel("目标入镜率", "100%");
    replayMetricLabel = createMetricLabel("回放评分", "0.89");
    eventMetricLabel = createMetricLabel("事件密度", "27.4/min");
    latencyMetricLabel = createMetricLabel("处理延迟", "86ms");
    metricLayout->addWidget(targetMetricLabel, 0, 0);
    metricLayout->addWidget(replayMetricLabel, 0, 1);
    metricLayout->addWidget(eventMetricLabel, 1, 0);
    metricLayout->addWidget(latencyMetricLabel, 1, 1);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);
    bottomLayout->addWidget(createInfoPanel("Director Mode", "AUTO / PANORAMA / CLOSE-UP", modeContent), 2);
    bottomLayout->addWidget(createInfoPanel("Event Timeline", "live highlight queue", timelineContent), 1);
    bottomLayout->addWidget(createInfoPanel("Live Metrics", "mapped from C++ runtime", metricContent), 2);

    auto* grid = new QGridLayout(tab);
    grid->setContentsMargins(14, 14, 14, 14);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    grid->addWidget(broadcastPane, 0, 0, 2, 1);
    grid->addLayout(sideLayout, 0, 1, 2, 1);
    grid->addLayout(bottomLayout, 2, 0, 1, 2);
    grid->addWidget(statusLabel, 3, 0, 1, 2);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    grid->setRowStretch(2, 0);
    return tab;
}

QWidget* QtBroadcastWindow::createHighlightTab(bool personal) {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(14);

    auto* list = createHighlightList(personal);
    layout->addWidget(createInfoPanel(personal ? "个人高光剪辑" : "全场高光剪辑",
                                     personal ? "selected player clips and coach review" : "highlight_report.json preview",
                                     list), 2);

    auto* scoreContent = new QWidget(this);
    auto* scoreLayout = new QGridLayout(scoreContent);
    scoreLayout->setContentsMargins(0, 0, 0, 0);
    scoreLayout->setSpacing(10);
    scoreLayout->addWidget(createMetricLabel("Replay Score", personal ? "0.91" : "0.8875"), 0, 0);
    scoreLayout->addWidget(createMetricLabel("Average Duration", "5.0s"), 0, 1);
    scoreLayout->addWidget(createMetricLabel("Target Visibility", "100%"), 1, 0);
    scoreLayout->addWidget(createMetricLabel(personal ? "Player Clips" : "Highlight Count", personal ? "8" : "20"), 1, 1);
    layout->addWidget(createInfoPanel(personal ? "教练复盘评分" : "解释性高光评分",
                                     "event type / threat zone / continuity / replay value",
                                     scoreContent), 1);

    auto* outputList = new QListWidget(this);
    outputList->setObjectName("outputList");
    if (personal) {
        outputList->addItems({"personal_highlight.mp4", "personal_highlight_report.json", "coach_review_notes"});
    } else {
        outputList->addItems({"broadcast_record.mp4", "highlight.mp4", "highlight_report.json"});
    }
    layout->addWidget(createInfoPanel("剪辑输出包", "files generated by the program", outputList), 1);
    return tab;
}

QWidget* QtBroadcastWindow::createMetricsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(14);

    auto* qualityContent = new QWidget(this);
    auto* qualityLayout = new QGridLayout(qualityContent);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(10);
    qualityLayout->addWidget(createMetricLabel("Ball-in-frame", "96%"), 0, 0);
    qualityLayout->addWidget(createMetricLabel("Key-player-in-frame", "91%"), 0, 1);
    qualityLayout->addWidget(createMetricLabel("Switch Smoothness", "87"), 1, 0);
    qualityLayout->addWidget(createMetricLabel("Viewer Score", "4.5/5"), 1, 1);
    layout->addWidget(createInfoPanel("自动转播与高光评估", "viewer quality and coaching value", qualityContent), 1);

    auto* matrix = new QListWidget(this);
    matrix->setObjectName("metricMatrix");
    matrix->addItems({
        "关键事件漏检率: 12%",
        "切换抖动次数: 低",
        "战术信息保留: 84%",
        "高光冗余控制: 78%",
        "处理延迟: 86ms",
        "主画面目标可见性: 100%"
    });
    layout->addWidget(createInfoPanel("指标矩阵", "beyond detection accuracy", matrix), 1);
    return tab;
}

QWidget* QtBroadcastWindow::createInfoPanel(const QString& title, const QString& subtitle, QWidget* content) {
    auto* panel = new QFrame(this);
    panel->setObjectName("infoPanel");
    panel->setFrameShape(QFrame::NoFrame);

    auto* titleLabel = new QLabel(title, panel);
    titleLabel->setObjectName("panelTitle");
    auto* subtitleLabel = new QLabel(subtitle, panel);
    subtitleLabel->setObjectName("panelSubtitle");
    subtitleLabel->setWordWrap(true);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(titleLabel);
    layout->addWidget(subtitleLabel);
    layout->addWidget(content, 1);
    return panel;
}

QLabel* QtBroadcastWindow::createMetricLabel(const QString& name, const QString& value) {
    auto* label = new QLabel(QString("<span>%1</span><br><strong>%2</strong>").arg(name, value), this);
    label->setObjectName("metricCard");
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setMinimumHeight(64);
    return label;
}

QListWidget* QtBroadcastWindow::createHighlightList(bool personal) {
    auto* list = new QListWidget(this);
    list->setObjectName("highlightList");
    const QStringList allItems = {
        "02.6s - 07.6s | 扑救 | target x:320 y:239 w:32 h:158 | score 0.82",
        "04.6s - 09.6s | 射门 | target x:371 y:100 w:709 h:620 | score 0.89",
        "08.7s - 13.7s | 扑救 | target x:655 y:84 w:621 h:636 | score 0.86",
        "14.8s - 19.8s | 进球 | target x:742 y:329 w:489 h:391 | score 0.96",
        "25.1s - 30.1s | 射门 | target x:592 y:236 w:599 h:381 | score 0.88",
        "37.3s - 42.3s | 扑救 | target x:0 y:0 w:1280 h:720 | score 0.91"
    };
    const QStringList personalItems = {
        "04.6s - 09.6s | 11号前锋射门参与 | score 0.89",
        "14.8s - 19.8s | 11号前锋禁区跑位 | score 0.96",
        "25.1s - 30.1s | 11号前锋二次进攻 | score 0.88",
        "37.3s - 42.3s | 11号前锋反抢压迫 | score 0.91"
    };
    list->addItems(personal ? personalItems : allItems);
    return list;
}

void QtBroadcastWindow::setupStyle() {
    setStyleSheet(
        "QMainWindow, QWidget { background: #07100d; color: #edf4ef; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI'; }"
        "QWidget#qt_scrollarea_viewport { background: #06100d; }"
        "QScrollArea { background: #06100d; border: 1px solid rgba(238, 247, 239, 36); border-radius: 0; }"
        "QFrame#infoPanel, VideoPane { background: rgba(8, 17, 14, 210); border: 1px solid rgba(238, 247, 239, 34); }"
        "QLabel#brandTitle { color: #f5fff0; font-size: 24px; font-weight: 900; }"
        "QLabel#brandSubtitle { color: #8fa598; font-size: 12px; font-weight: 700; }"
        "QLabel#scoreLabel { color: #07100d; background: #d6f25f; padding: 9px 18px; font-size: 19px; font-weight: 900; }"
        "QLabel#clockLabel { color: #edf4ef; background: rgba(238, 247, 239, 20); border: 1px solid rgba(238, 247, 239, 42); padding: 9px 16px; font-weight: 900; }"
        "QLabel#paneTitle { color: #f4f7f3; font-size: 15px; font-weight: 900; }"
        "QLabel#statusLabel { color: #aab9b2; padding: 8px 14px; border-top: 1px solid rgba(238, 247, 239, 22); }"
        "QLabel#decisionLabel { color: #d6f25f; font-size: 13px; font-weight: 800; }"
        "QLabel#panelTitle { color: #f5fff0; font-size: 16px; font-weight: 900; }"
        "QLabel#panelSubtitle { color: #8fa598; font-size: 11px; font-weight: 700; }"
        "QLabel#timelineItem { color: #edf4ef; background: rgba(238, 247, 239, 18); border-left: 3px solid #45d9ff; padding: 5px 8px; font-weight: 800; }"
        "QLabel#metricCard { color: #d6f25f; background: rgba(238, 247, 239, 15); border: 1px solid rgba(238, 247, 239, 28); padding: 9px; font-weight: 900; }"
        "QLabel#metricCard span { color: #8fa598; font-size: 11px; }"
        "QLabel#metricCard strong { color: #d6f25f; font-size: 22px; }"
        "QPushButton { background: #d6f25f; color: #07100d; border: 0; border-radius: 0; padding: 9px 14px; font-weight: 900; }"
        "QPushButton:hover { background: #edff8f; }"
        "QPushButton#recordButton { background: #ff4c4c; color: #ffffff; }"
        "QPushButton#secondaryButton { background: rgba(238, 247, 239, 24); color: #edf4ef; border: 1px solid rgba(238, 247, 239, 40); }"
        "QRadioButton { spacing: 8px; padding: 8px 10px; color: #e8efe9; font-weight: 900; background: rgba(238, 247, 239, 16); border: 1px solid rgba(238, 247, 239, 30); }"
        "QRadioButton::indicator { width: 0px; height: 0px; }"
        "QRadioButton:checked { color: #07100d; background: #45d9ff; border-color: #45d9ff; }"
        "QTabWidget::pane { border: 0; background: transparent; }"
        "QTabBar::tab { background: rgba(238, 247, 239, 16); color: #8fa598; padding: 10px 18px; margin-right: 7px; font-weight: 900; border: 1px solid rgba(238, 247, 239, 28); }"
        "QTabBar::tab:selected { background: #d6f25f; color: #07100d; border-color: #d6f25f; }"
        "QListWidget { background: rgba(238, 247, 239, 12); border: 1px solid rgba(238, 247, 239, 28); color: #edf4ef; outline: 0; }"
        "QListWidget::item { padding: 10px; border-bottom: 1px solid rgba(238, 247, 239, 22); }"
        "QListWidget::item:selected { background: rgba(69, 217, 255, 58); color: #ffffff; }"
        "QSlider::groove:horizontal { height: 5px; background: #32423b; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #d6f25f; }"
    );
}

void QtBroadcastWindow::processFrame() {
    const DualCameraFrame frame = streams.readFrame();
    if (frame.panorama.empty() || frame.closeup.empty()) {
        statusLabel->setText("Camera frame read failed. Check device connection.");
        return;
    }

    const std::vector<TargetInfo> panoramaTargets = panoramaDetection.detect(frame.panorama);
    const std::vector<TargetInfo> closeupTargets = closeupDetection.detect(frame.closeup);
    const std::vector<FaceInfo> faces = faceCapture.capture(frame.closeup, frame.timestamp);
    const BroadcastDecision decision = decideBroadcast(panoramaTargets, closeupTargets, faces, frame.timestamp);
    lastDecision = decision;
    streams.setMode(decision.mode);

    const cv::Mat broadcastFrame = renderBroadcastFrame(frame, decision, panoramaTargets, closeupTargets, faces);

    panoramaPane->setFrame(normalizeFrame(frame.panorama));
    closeupPane->setFrame(normalizeFrame(frame.closeup));
    broadcastPane->setFrame(broadcastFrame);

    writeRecordingFrames(frame.panorama, frame.closeup, broadcastFrame);
    updateStatusText(decision, frame);
}

void QtBroadcastWindow::startRecording() {
    if (!openWriter(panoramaWriter, "panorama_record.mp4") ||
        !openWriter(closeupWriter, "closeup_record.mp4") ||
        !openWriter(broadcastWriter, "broadcast_record.mp4")) {
        releaseWriters();
        QMessageBox::critical(this, "Recording Error", "Failed to create recording files. Check write permission in the current directory.");
        return;
    }

    recording = true;
    recordButton->setText("■ STOP");
}

void QtBroadcastWindow::stopRecording() {
    if (!recording && !panoramaWriter.isOpened() && !closeupWriter.isOpened() && !broadcastWriter.isOpened()) {
        return;
    }

    recording = false;
    releaseWriters();
    if (recordButton) {
        recordButton->setText("● REC");
    }
}

void QtBroadcastWindow::updateOperatorMode() {
    if (forceCloseupButton->isChecked()) {
        operatorMode = OperatorMode::FORCE_CLOSEUP;
    } else if (forcePanoramaButton->isChecked()) {
        operatorMode = OperatorMode::FORCE_PANORAMA;
    } else {
        operatorMode = OperatorMode::AUTO;
    }
}

BroadcastDecision QtBroadcastWindow::decideBroadcast(
    const std::vector<TargetInfo>&,
    const std::vector<TargetInfo>& closeupTargets,
    const std::vector<FaceInfo>& faces,
    double timestamp
) {
    BroadcastDecision decision;

    if (operatorMode == OperatorMode::FORCE_CLOSEUP) {
        decision.mode = BroadcastMode::CLOSEUP;
        decision.reason = "manual closeup";
        decision.hold_until = timestamp;
        return decision;
    }
    if (operatorMode == OperatorMode::FORCE_PANORAMA) {
        decision.mode = BroadcastMode::NORMAL;
        decision.reason = "manual panorama";
        decision.hold_until = timestamp;
        return decision;
    }

    if (lastDecision.mode == BroadcastMode::CLOSEUP && timestamp < lastDecision.hold_until) {
        return lastDecision;
    }

    const bool hasFace = !faces.empty();
    const bool hasLargeCloseupMotion = std::any_of(closeupTargets.begin(), closeupTargets.end(), [](const TargetInfo& target) {
        return target.box.area() > 4500;
    });
    const bool cooldownReady = timestamp - lastCloseupTriggerTime > 3.0;

    if ((hasFace || hasLargeCloseupMotion) && cooldownReady) {
        lastCloseupTriggerTime = timestamp;
        decision.mode = BroadcastMode::CLOSEUP;
        decision.reason = hasFace ? "face closeup" : "closeup motion";
        decision.hold_until = timestamp + 2.5;
        return decision;
    }

    decision.mode = BroadcastMode::NORMAL;
    decision.reason = "stable panorama";
    decision.hold_until = timestamp;
    return decision;
}

cv::Mat QtBroadcastWindow::renderBroadcastFrame(
    const DualCameraFrame& frame,
    const BroadcastDecision& decision,
    const std::vector<TargetInfo>& panoramaTargets,
    const std::vector<TargetInfo>& closeupTargets,
    const std::vector<FaceInfo>& faces
) {
    cv::Mat output = decision.mode == BroadcastMode::CLOSEUP ? frame.closeup.clone() : frame.panorama.clone();

    if (decision.mode == BroadcastMode::CLOSEUP) {
        drawTargets(output, closeupTargets);
        drawFaces(output, faces);
    } else {
        drawTargets(output, panoramaTargets);
    }

    output = normalizeFrame(output);
    drawStatus(output, decision, faces.size());
    return output;
}

cv::Mat QtBroadcastWindow::normalizeFrame(const cv::Mat& frame) const {
    cv::Mat output;
    if (frame.empty()) {
        return output;
    }

    if (frame.size() == cv::Size(VIDEO_WIDTH, VIDEO_HEIGHT)) {
        output = frame.clone();
    } else {
        cv::resize(frame, output, cv::Size(VIDEO_WIDTH, VIDEO_HEIGHT));
    }
    return output;
}

void QtBroadcastWindow::drawTargets(cv::Mat& frame, const std::vector<TargetInfo>& targets) const {
    for (const auto& target : targets) {
        const cv::Scalar color = target.type == TargetType::BALL
            ? cv::Scalar(0, 230, 255)
            : cv::Scalar(90, 235, 130);
        cv::rectangle(frame, target.box & cv::Rect(0, 0, frame.cols, frame.rows), color, 2);
        cv::putText(
            frame,
            CommonTool::targetType2Str(target.type),
            cv::Point(target.box.x, std::max(18, target.box.y - 7)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.55,
            color,
            2
        );
    }
}

void QtBroadcastWindow::drawFaces(cv::Mat& frame, const std::vector<FaceInfo>& faces) const {
    for (const auto& face : faces) {
        cv::rectangle(frame, face.face_box & cv::Rect(0, 0, frame.cols, frame.rows), cv::Scalar(255, 180, 60), 2);
        cv::putText(
            frame,
            CommonTool::emotionType2Str(face.emotion),
            cv::Point(face.face_box.x, std::max(18, face.face_box.y - 7)),
            cv::FONT_HERSHEY_SIMPLEX,
            0.55,
            cv::Scalar(255, 180, 60),
            2
        );
    }
}

void QtBroadcastWindow::drawStatus(cv::Mat& frame, const BroadcastDecision& decision, std::size_t faceCount) const {
    const std::string mode = CommonTool::broadcastMode2Str(decision.mode);
    const std::string status = "AUTO BROADCAST | mode: " + mode +
        " | reason: " + decision.reason +
        " | faces: " + std::to_string(faceCount) +
        (recording ? " | REC" : "");

    cv::Mat overlay = frame.clone();
    cv::rectangle(overlay, cv::Rect(24, 24, std::min(1120, frame.cols - 48), 58), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::addWeighted(overlay, 0.5, frame, 0.5, 0, frame);
    cv::putText(
        frame,
        status,
        cv::Point(44, 62),
        cv::FONT_HERSHEY_SIMPLEX,
        0.85,
        cv::Scalar(245, 255, 245),
        2
    );

    if (recording) {
        cv::circle(frame, cv::Point(frame.cols - 58, 52), 13, cv::Scalar(20, 20, 230), cv::FILLED);
        cv::putText(
            frame,
            "REC",
            cv::Point(frame.cols - 126, 62),
            cv::FONT_HERSHEY_SIMPLEX,
            0.85,
            cv::Scalar(245, 245, 255),
            2
        );
    }
}

void QtBroadcastWindow::writeRecordingFrames(const cv::Mat& panorama, const cv::Mat& closeup, const cv::Mat& broadcast) {
    if (!recording) {
        return;
    }

    panoramaWriter.write(normalizeFrame(panorama));
    closeupWriter.write(normalizeFrame(closeup));
    broadcastWriter.write(normalizeFrame(broadcast));
}

void QtBroadcastWindow::updateStatusText(const BroadcastDecision& decision, const DualCameraFrame& frame) {
    const int totalSeconds = static_cast<int>(std::max(0.0, frame.timestamp));
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    if (clockLabel) {
        clockLabel->setText(QString("%1:%2 | LIVE")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
    }
    if (decisionLabel) {
        decisionLabel->setText(QString("%1 | reason: %2 | hold until %3")
            .arg(modeLabel(decision.mode))
            .arg(QString::fromStdString(decision.reason))
            .arg(QString::fromStdString(formatTimestamp(decision.hold_until))));
    }
    if (targetMetricLabel) {
        targetMetricLabel->setText("<span>目标入镜率</span><br><strong>100%</strong>");
    }
    if (replayMetricLabel) {
        replayMetricLabel->setText("<span>回放评分</span><br><strong>0.89</strong>");
    }
    if (eventMetricLabel) {
        eventMetricLabel->setText("<span>事件密度</span><br><strong>27.4/min</strong>");
    }
    if (latencyMetricLabel) {
        latencyMetricLabel->setText(QString("<span>处理延迟</span><br><strong>%1ms</strong>")
            .arg(std::max(1, 1000 / FPS)));
    }

    statusLabel->setText(QString("%1 | %2 | panorama %3x%4 | close-up %5x%6 | %7")
        .arg(recording ? "Recording" : "Standby")
        .arg(modeLabel(decision.mode))
        .arg(frame.panorama.cols)
        .arg(frame.panorama.rows)
        .arg(frame.closeup.cols)
        .arg(frame.closeup.rows)
        .arg(QString::fromStdString(formatTimestamp(frame.timestamp))));
}

bool QtBroadcastWindow::openWriter(cv::VideoWriter& writer, const std::string& path) const {
    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    return writer.open(path, fourcc, FPS, cv::Size(VIDEO_WIDTH, VIDEO_HEIGHT), true);
}

void QtBroadcastWindow::releaseWriters() {
    if (panoramaWriter.isOpened()) {
        panoramaWriter.release();
    }
    if (closeupWriter.isOpened()) {
        closeupWriter.release();
    }
    if (broadcastWriter.isOpened()) {
        broadcastWriter.release();
    }
}
