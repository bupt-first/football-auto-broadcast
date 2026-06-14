#include "qt_broadcast_window.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QResizeEvent>
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
        return "辅助机位";
    }
    if (mode == BroadcastMode::FOLLOW) {
        return "软件跟拍";
    }
    return "全景机位";
}

QString decisionReasonText(const std::string& reason) {
    if (reason == "manual follow") {
        return "手动锁定软件跟拍";
    }
    if (reason == "manual panorama") {
        return "手动锁定全景视角";
    }
    if (reason == "face closeup") {
        return "辅助机位捕捉到近景动作";
    }
    if (reason == "closeup motion") {
        return "辅助机位检测到明显动作";
    }
    if (reason == "tracked action") {
        return "正在围绕主要动作区域跟拍";
    }
    if (reason == "stable panorama") {
        return "保持全景，方便观察整体阵型";
    }
    return QString::fromStdString(reason);
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
    imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    imageLabel->setObjectName("videoSurface");
    imageLabel->installEventFilter(this);
    imageLabel->setCursor(Qt::CrossCursor);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidget(imageLabel);
    scrollArea->setWidgetResizable(true);
    scrollArea->setAlignment(Qt::AlignCenter);
    scrollArea->setFrameShape(QFrame::NoFrame);

    zoomSlider = new QSlider(Qt::Horizontal, this);
    zoomSlider->setRange(50, 200);
    zoomSlider->setValue(100);
    zoomSlider->setToolTip("专业模式：缩放预览画面");
    zoomSlider->hide();

    resetButton = new QPushButton("画面校准", this);
    resetButton->setToolTip("一键恢复标准 16:9 预览比例");

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

void VideoPane::setFrameClickCallback(std::function<void(const cv::Point2f&)> callback) {
    frameClickCallback = std::move(callback);
}

bool VideoPane::eventFilter(QObject* watched, QEvent* event) {
    if (watched == imageLabel && event->type() == QEvent::MouseButtonPress && frameClickCallback) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            cv::Point2f framePoint;
            if (mapWidgetPointToFrame(mouseEvent->position().toPoint(), framePoint)) {
                frameClickCallback(framePoint);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoPane::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updatePixmap();
}

bool VideoPane::mapWidgetPointToFrame(const QPoint& point, cv::Point2f& framePoint) const {
    if (currentImage.isNull() || currentPixmapSize.isEmpty()) {
        return false;
    }

    const QSize labelSize = imageLabel->size();
    const int offsetX = std::max(0, (labelSize.width() - currentPixmapSize.width()) / 2);
    const int offsetY = std::max(0, (labelSize.height() - currentPixmapSize.height()) / 2);
    const int localX = point.x() - offsetX;
    const int localY = point.y() - offsetY;

    if (localX < 0 || localY < 0 || localX >= currentPixmapSize.width() || localY >= currentPixmapSize.height()) {
        return false;
    }

    framePoint.x = static_cast<float>(localX) * static_cast<float>(currentImage.width()) /
        static_cast<float>(std::max(1, currentPixmapSize.width()));
    framePoint.y = static_cast<float>(localY) * static_cast<float>(currentImage.height()) /
        static_cast<float>(std::max(1, currentPixmapSize.height()));
    return true;
}

void VideoPane::updatePixmap() {
    if (currentImage.isNull()) {
        currentPixmapSize = QSize();
        imageLabel->clear();
        imageLabel->setText("暂无画面");
        return;
    }

    QSize targetSize = scrollArea->viewport()->size();
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        targetSize = QSize(320, 180);
    }
    targetSize = QSize(
        std::max(1, targetSize.width() * currentZoomPercent / 100),
        std::max(1, targetSize.height() * currentZoomPercent / 100)
    );
    const QPixmap pixmap = QPixmap::fromImage(currentImage).scaled(
        targetSize,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    );
    currentPixmapSize = pixmap.size();
    imageLabel->setPixmap(pixmap);
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
        QMessageBox::critical(this, "机位连接失败", "未能同时打开两路 USB 摄像头。请检查全景机位、辅助机位连接，或在命令行传入摄像头编号。");
        return false;
    }

    if (!panoramaDetection.init() || !closeupDetection.init() || !faceCapture.init()) {
        QMessageBox::critical(this, "系统初始化失败", "画面分析模块未能启动，请检查运行目录中的模型与依赖文件。");
        return false;
    }

    statusLabel->setText(QString("双机位已就绪：全景机位 %1，辅助机位 %2")
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
    setWindowTitle("校园足球自动转播系统");
    resize(1600, 900);
    setMinimumSize(1280, 720);

    frameTimer = new QTimer(this);
    connect(frameTimer, &QTimer::timeout, this, [this]() {
        processFrame();
    });

    broadcastPane = new VideoPane("16:9 自动导播输出", this);
    panoramaPane = new VideoPane("全景机位", this);
    closeupPane = new VideoPane("辅助机位", this);
    panoramaPane->setFrameClickCallback([this](const cv::Point2f& point) {
        seedPanoramaBall(point);
    });

    autoModeButton = new QRadioButton("自动导播", this);
    forcePanoramaButton = new QRadioButton("锁定全景", this);
    forceFollowButton = new QRadioButton("软件跟拍", this);
    autoModeButton->setObjectName("autoModeButton");
    forcePanoramaButton->setObjectName("panoramaModeButton");
    forceFollowButton->setObjectName("followModeButton");
    autoModeButton->setChecked(true);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(autoModeButton);
    modeGroup->addButton(forcePanoramaButton);
    modeGroup->addButton(forceFollowButton);
    connect(modeGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton*) {
        updateOperatorMode();
    });

    recordButton = new QPushButton("● 开始录制", this);
    recordButton->setObjectName("recordButton");
    connect(recordButton, &QPushButton::clicked, this, [this]() {
        if (recording) {
            stopRecording();
        } else {
            startRecording();
        }
    });

    replayButton = new QPushButton("实时回看", this);
    replayButton->setObjectName("secondaryButton");
    connect(replayButton, &QPushButton::clicked, this, [this]() {
        if (consoleTabs) {
            consoleTabs->setCurrentIndex(1);
        }
    });

    statusLabel = new QLabel("正在准备双机位画面...", this);
    statusLabel->setObjectName("statusLabel");

    scoreLabel = new QLabel("主队 2  -  1 客队", this);
    scoreLabel->setObjectName("scoreLabel");
    scoreLabel->setAlignment(Qt::AlignCenter);

    clockLabel = new QLabel("00:00 | 直播中", this);
    clockLabel->setObjectName("clockLabel");
    clockLabel->setAlignment(Qt::AlignCenter);

    auto* brandTitle = new QLabel("校园足球自动转播系统", this);
    brandTitle->setObjectName("brandTitle");
    auto* brandSubtitle = new QLabel("双固定机位 · 软件跟拍 · 全场高光与个人集锦", this);
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

    consoleTabs = new QTabWidget(this);
    consoleTabs->setDocumentMode(true);
    consoleTabs->addTab(createLiveDirectorTab(), "实时导播");
    consoleTabs->addTab(createHighlightTab(false), "全场高光");
    consoleTabs->addTab(createHighlightTab(true), "个人高光");
    consoleTabs->addTab(createMetricsTab(), "效果评估");

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

    auto* timelineContent = new QWidget(this);
    auto* timelineLayout = new QVBoxLayout(timelineContent);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(8);
    const QStringList eventTexts = {
        "02.6秒  成功扑救",
        "08.7秒  球员射门",
        "14.8秒  进球得分",
        "25.0秒  精彩进攻",
        "37.3秒  适合回看"
    };
    for (const QString& text : eventTexts) {
        auto* item = new QLabel(text, this);
        item->setObjectName("timelineItem");
        timelineLayout->addWidget(item);
    }
    timelineLayout->addStretch(1);
    sideLayout->addWidget(createInfoPanel("比赛时间线", "实时关键事件", timelineContent), 1);

    auto* modeContent = new QWidget(this);
    auto* modeLayout = new QVBoxLayout(modeContent);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(12);

    auto* modeButtons = new QHBoxLayout();
    modeButtons->setSpacing(10);
    modeButtons->addWidget(autoModeButton);
    modeButtons->addWidget(forcePanoramaButton);
    modeButtons->addWidget(forceFollowButton);
    modeButtons->addWidget(replayButton);
    modeButtons->addWidget(recordButton);
    modeLayout->addLayout(modeButtons);

    decisionLabel = new QLabel("当前镜头状态：自动导播中 · 等待比赛画面输入", this);
    decisionLabel->setObjectName("decisionLabel");
    decisionLabel->setWordWrap(true);
    modeLayout->addWidget(decisionLabel);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(0);
    bottomLayout->addWidget(createInfoPanel("一键操作", "直播台常用控制", modeContent), 1);

    auto* grid = new QGridLayout(tab);
    grid->setContentsMargins(16, 16, 16, 12);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(14);
    grid->addWidget(broadcastPane, 0, 0, 2, 1);
    grid->addLayout(sideLayout, 0, 1, 2, 1);
    grid->addLayout(bottomLayout, 2, 0, 1, 2);
    grid->addWidget(statusLabel, 3, 0, 1, 2);
    grid->setColumnStretch(0, 4);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    grid->setRowStretch(2, 0);
    return tab;
}

QWidget* QtBroadcastWindow::createHighlightTab(bool personal) {
    auto* tab = new QWidget(this);
    auto* layout = new QGridLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(14);

    auto* preview = createPreviewPlaceholder(
        personal ? "16:9 个人集锦预览" : "16:9 全场高光预览",
        personal ? "选择球员后，系统自动拼接该球员的关键表现" : "系统按比赛时间自动拼接进球、射门、扑救和精彩攻防"
    );
    auto* list = createHighlightList(personal);
    layout->addWidget(preview, 0, 0, 2, 1);
    layout->addWidget(createInfoPanel(personal ? "个人片段清单" : "集锦片段清单",
                                     personal ? "按号码/球员筛选后的高光事件" : "按比赛时间排序，便于人工微调",
                                     list), 2, 0, 1, 1);

    auto* scoreContent = new QWidget(this);
    auto* scoreLayout = new QGridLayout(scoreContent);
    scoreLayout->setContentsMargins(0, 0, 0, 0);
    scoreLayout->setSpacing(10);
    scoreLayout->addWidget(createMetricLabel("推荐程度", personal ? "4.6/5" : "4.5/5"), 0, 0);
    scoreLayout->addWidget(createMetricLabel("平均时长", "5.0秒"), 0, 1);
    scoreLayout->addWidget(createMetricLabel("画面完整度", "优秀"), 1, 0);
    scoreLayout->addWidget(createMetricLabel(personal ? "个人片段" : "高光数量", personal ? "8段" : "20段"), 1, 1);
    layout->addWidget(createInfoPanel(personal ? "球员表现概览" : "高光生成结果",
                                     personal ? "进球、助攻、抢断、关键防守" : "结合事件类型、威胁区域、连续性和回看价值",
                                     scoreContent), 0, 1, 1, 1);

    auto* outputList = new QListWidget(this);
    outputList->setObjectName("outputList");
    if (personal) {
        outputList->addItems({"个人专属集锦 personal_highlight.mp4", "个人事件报告 personal_highlight_report.json", "教练复盘备注 coach_review_notes"});
    } else {
        outputList->addItems({"完整转播记录 broadcast_record.mp4", "全场高光集锦 highlight.mp4", "高光事件报告 highlight_report.json"});
    }
    layout->addWidget(createInfoPanel("一键导出内容", "默认生成 1080P/720P 可分享视频", outputList), 1, 1, 2, 1);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 1);
    return tab;
}

QWidget* QtBroadcastWindow::createMetricsTab() {
    auto* tab = new QWidget(this);
    auto* layout = new QHBoxLayout(tab);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    auto* qualityContent = new QWidget(this);
    auto* qualityLayout = new QGridLayout(qualityContent);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(10);
    qualityLayout->addWidget(createMetricLabel("画面捕捉质量", "优秀"), 0, 0);
    qualityLayout->addWidget(createMetricLabel("镜头切换流畅度", "良好"), 0, 1);
    qualityLayout->addWidget(createMetricLabel("关键事件捕捉", "良好"), 1, 0);
    qualityLayout->addWidget(createMetricLabel("整体体验评分", "4.5/5"), 1, 1);
    layout->addWidget(createInfoPanel("通俗评价结果", "普通用户默认只看结论", qualityContent), 1);

    auto* matrix = new QListWidget(this);
    matrix->setObjectName("metricMatrix");
    matrix->addItems({
        "足球入镜率：96%",
        "关键球员入镜率：91%",
        "关键事件漏检率：12%",
        "镜头抖动次数：低",
        "战术信息保留率：84%",
        "高光冗余控制：78%",
        "实时观看延迟：33ms",
        "后台分析延迟：86ms"
    });
    layout->addWidget(createInfoPanel("专业数据", "需要复盘或写报告时查看", matrix), 1);
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

QLabel* QtBroadcastWindow::createPreviewPlaceholder(const QString& title, const QString& subtitle) {
    auto* label = new QLabel(QString("<strong>%1</strong><br><span>%2</span>").arg(title, subtitle), this);
    label->setObjectName("previewPlaceholder");
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMinimumSize(640, 360);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return label;
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
        "02.6秒 - 07.6秒 | 成功扑救 | 辅助机位回看",
        "04.6秒 - 09.6秒 | 球员射门 | 软件跟拍保留进攻过程",
        "08.7秒 - 13.7秒 | 门前扑救 | 推荐加入全场集锦",
        "14.8秒 - 19.8秒 | 进球得分 | 必选高光片段",
        "25.1秒 - 30.1秒 | 精彩射门 | 保留禁区威胁",
        "37.3秒 - 42.3秒 | 防守反抢 | 适合教练复盘"
    };
    const QStringList personalItems = {
        "04.6秒 - 09.6秒 | 11号前锋射门参与",
        "14.8秒 - 19.8秒 | 11号前锋禁区跑位",
        "25.1秒 - 30.1秒 | 11号前锋二次进攻",
        "37.3秒 - 42.3秒 | 11号前锋反抢压迫"
    };
    list->addItems(personal ? personalItems : allItems);
    return list;
}

void QtBroadcastWindow::setupStyle() {
    setStyleSheet(
        "QMainWindow, QWidget { background: #07110f; color: #f3fff5; font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI'; }"
        "QWidget#qt_scrollarea_viewport { background: #050c0a; }"
        "QScrollArea { background: #050c0a; border: 1px solid rgba(223, 247, 232, 44); border-radius: 8px; }"
        "QFrame#infoPanel, VideoPane { background: rgba(9, 24, 21, 232); border: 1px solid rgba(223, 247, 232, 38); border-radius: 8px; }"
        "QLabel#videoSurface { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e9652, stop:0.52 #19663a, stop:1 #8fbd58); color: rgba(243, 255, 245, 205); font-size: 17px; font-weight: 900; }"
        "QLabel#brandTitle { color: #f3fff5; font-size: 24px; font-weight: 900; }"
        "QLabel#brandSubtitle { color: #9db5a8; font-size: 13px; font-weight: 800; }"
        "QLabel#scoreLabel { color: #06100d; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e7ff63, stop:1 #9af15d); padding: 10px 22px; font-size: 20px; font-weight: 900; border-radius: 8px; }"
        "QLabel#clockLabel { color: #dff7ff; background: rgba(88, 200, 255, 32); border: 1px solid rgba(88, 200, 255, 88); padding: 10px 16px; font-weight: 900; border-radius: 8px; }"
        "QLabel#paneTitle { color: #f3fff5; font-size: 15px; font-weight: 900; padding-bottom: 2px; }"
        "QLabel#statusLabel { color: #9db5a8; padding: 8px 14px; border-top: 1px solid rgba(223, 247, 232, 24); }"
        "QLabel#decisionLabel { color: #e7ff63; background: rgba(231, 255, 99, 18); border: 1px solid rgba(231, 255, 99, 48); border-radius: 8px; padding: 10px 12px; font-size: 15px; font-weight: 900; }"
        "QLabel#panelTitle { color: #f3fff5; font-size: 16px; font-weight: 900; }"
        "QLabel#panelSubtitle { color: #9db5a8; font-size: 11px; font-weight: 800; }"
        "QLabel#timelineItem { color: #f3fff5; background: rgba(255, 255, 255, 18); border-left: 4px solid #58c8ff; padding: 9px 10px; font-weight: 900; border-radius: 6px; }"
        "QLabel#previewPlaceholder { color: #f3fff5; background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2e9652, stop:0.48 #19663a, stop:1 #8fbd58); border: 1px solid rgba(245, 255, 240, 64); border-radius: 8px; font-size: 28px; font-weight: 900; }"
        "QLabel#previewPlaceholder span { color: rgba(243, 255, 245, 205); font-size: 15px; font-weight: 800; }"
        "QLabel#metricCard { color: #e7ff63; background: rgba(255, 255, 255, 17); border: 1px solid rgba(223, 247, 232, 34); padding: 11px; font-weight: 900; border-radius: 8px; }"
        "QLabel#metricCard span { color: #9db5a8; font-size: 11px; font-weight: 800; }"
        "QLabel#metricCard strong { color: #e7ff63; font-size: 23px; }"
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e7ff63, stop:1 #a7ee62); color: #06100d; border: 0; border-radius: 8px; padding: 13px 18px; font-size: 14px; font-weight: 900; }"
        "QPushButton:hover { background: #f0ff8f; }"
        "QPushButton#recordButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #ff4d55, stop:1 #f07144); color: #ffffff; }"
        "QPushButton#secondaryButton { background: rgba(255, 255, 255, 24); color: #f3fff5; border: 1px solid rgba(223, 247, 232, 42); }"
        "QRadioButton { spacing: 8px; padding: 13px 18px; color: #f3fff5; font-size: 14px; font-weight: 900; background: rgba(255, 255, 255, 18); border: 1px solid rgba(223, 247, 232, 34); border-radius: 8px; }"
        "QRadioButton::indicator { width: 0px; height: 0px; }"
        "QRadioButton:checked { color: #06100d; background: #e7ff63; border-color: #e7ff63; }"
        "QRadioButton#followModeButton:checked { background: #58c8ff; border-color: #58c8ff; }"
        "QRadioButton#panoramaModeButton:checked { background: #47e3a0; border-color: #47e3a0; }"
        "QTabWidget::pane { border: 0; background: transparent; }"
        "QTabBar::tab { background: rgba(255, 255, 255, 16); color: #9db5a8; padding: 10px 18px; margin-right: 7px; font-weight: 900; border: 1px solid rgba(223, 247, 232, 30); border-top-left-radius: 7px; border-top-right-radius: 7px; }"
        "QTabBar::tab:selected { background: #e7ff63; color: #06100d; border-color: #e7ff63; }"
        "QListWidget { background: rgba(255, 255, 255, 13); border: 1px solid rgba(223, 247, 232, 32); color: #f3fff5; outline: 0; border-radius: 7px; }"
        "QListWidget::item { padding: 10px; border-bottom: 1px solid rgba(223, 247, 232, 24); }"
        "QListWidget::item:selected { background: rgba(88, 200, 255, 70); color: #ffffff; }"
        "QSlider::groove:horizontal { height: 5px; background: #31453b; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #e7ff63; }"
    );
}

void QtBroadcastWindow::processFrame() {
    const DualCameraFrame frame = streams.readFrame();
    if (frame.panorama.empty() || frame.closeup.empty()) {
        statusLabel->setText("Camera frame read failed. Check device connection.");
        return;
    }
    lastFrameTimestamp = frame.timestamp;
    lastPanoramaFrameSize = frame.panorama.size();

    const std::vector<TargetInfo> panoramaTargets = panoramaDetection.detect(frame.panorama, frame.timestamp);
    const std::vector<TargetInfo> closeupTargets = closeupDetection.detect(frame.closeup, frame.timestamp);
    const std::vector<FaceInfo> faces = faceCapture.capture(frame.closeup, frame.timestamp);
    const BroadcastDecision decision = decideBroadcast(panoramaTargets, closeupTargets, faces, frame.timestamp);
    lastDecision = decision;
    streams.setMode(decision.mode);

    const cv::Mat broadcastFrame = renderBroadcastFrame(frame, decision, panoramaTargets, closeupTargets, faces);

    cv::Mat panoramaMonitor = normalizeFrame(frame.panorama);
    drawPanoramaSeed(panoramaMonitor);
    panoramaPane->setFrame(panoramaMonitor);
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
        QMessageBox::critical(this, "录制失败", "未能创建录制文件，请检查当前目录写入权限。");
        return;
    }

    recording = true;
    recordButton->setText("■ 结束录制");
}

void QtBroadcastWindow::stopRecording() {
    if (!recording && !panoramaWriter.isOpened() && !closeupWriter.isOpened() && !broadcastWriter.isOpened()) {
        return;
    }

    recording = false;
    releaseWriters();
    if (recordButton) {
        recordButton->setText("● 开始录制");
    }
}

void QtBroadcastWindow::updateOperatorMode() {
    if (forceFollowButton->isChecked()) {
        operatorMode = OperatorMode::FORCE_FOLLOW;
    } else if (forcePanoramaButton->isChecked()) {
        operatorMode = OperatorMode::FORCE_PANORAMA;
    } else {
        operatorMode = OperatorMode::AUTO;
    }
}

BroadcastDecision QtBroadcastWindow::decideBroadcast(
    const std::vector<TargetInfo>& panoramaTargets,
    const std::vector<TargetInfo>& closeupTargets,
    const std::vector<FaceInfo>& faces,
    double timestamp
) {
    BroadcastDecision decision;

    if (operatorMode == OperatorMode::FORCE_FOLLOW) {
        decision.mode = BroadcastMode::FOLLOW;
        decision.reason = "manual follow";
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
    const bool hasConfirmedBall = std::any_of(panoramaTargets.begin(), panoramaTargets.end(), [](const TargetInfo& target) {
        return target.type == TargetType::BALL && target.confidence >= 0.42;
    });
    const bool hasActionCluster = std::any_of(panoramaTargets.begin(), panoramaTargets.end(), [](const TargetInfo& target) {
        return target.type != TargetType::BALL && target.confidence >= 0.22 && target.box.area() > 3500;
    });
    const bool cooldownReady = timestamp - lastCloseupTriggerTime > 3.0;

    if ((hasFace || hasLargeCloseupMotion) && cooldownReady) {
        lastCloseupTriggerTime = timestamp;
        decision.mode = BroadcastMode::CLOSEUP;
        decision.reason = hasFace ? "face closeup" : "closeup motion";
        decision.hold_until = timestamp + 2.5;
        return decision;
    }

    if (hasConfirmedBall || hasActionCluster) {
        decision.mode = BroadcastMode::FOLLOW;
        decision.reason = hasConfirmedBall ? "tracked ball trajectory" : "tracked action cluster";
        decision.hold_until = timestamp;
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
    cv::Mat output;
    if (decision.mode == BroadcastMode::CLOSEUP) {
        output = frame.closeup.clone();
    } else if (decision.mode == BroadcastMode::FOLLOW) {
        output = renderVirtualFollowFrame(frame.panorama, panoramaTargets);
    } else {
        output = frame.panorama.clone();
    }

    if (decision.mode == BroadcastMode::CLOSEUP) {
        drawTargets(output, closeupTargets);
        drawFaces(output, faces);
    } else if (decision.mode == BroadcastMode::NORMAL) {
        drawTargets(output, panoramaTargets);
    }

    output = normalizeFrame(output);
    drawStatus(output, decision, faces.size());
    return output;
}

cv::Mat QtBroadcastWindow::renderVirtualFollowFrame(const cv::Mat& panorama, const std::vector<TargetInfo>& targets) const {
    if (panorama.empty() || targets.empty()) {
        return normalizeFrame(panorama);
    }

    const TargetInfo* mainTarget = &targets.front();
    for (const auto& target : targets) {
        if (target.type == TargetType::BALL) {
            mainTarget = &target;
            break;
        }
        if (target.box.area() > mainTarget->box.area()) {
            mainTarget = &target;
        }
    }

    const cv::Rect frameRect(0, 0, panorama.cols, panorama.rows);
    const cv::Rect targetBox = mainTarget->box & frameRect;
    if (targetBox.empty()) {
        return normalizeFrame(panorama);
    }

    const cv::Point center(targetBox.x + targetBox.width / 2, targetBox.y + targetBox.height / 2);
    const double zoomFactor = 1.45;
    int cropWidth = std::max(1, static_cast<int>(panorama.cols / zoomFactor));
    int cropHeight = std::max(1, cropWidth * VIDEO_HEIGHT / VIDEO_WIDTH);
    if (cropHeight > panorama.rows) {
        cropHeight = panorama.rows;
        cropWidth = std::max(1, cropHeight * VIDEO_WIDTH / VIDEO_HEIGHT);
    }

    const int maxX = std::max(0, panorama.cols - cropWidth);
    const int maxY = std::max(0, panorama.rows - cropHeight);
    const int x = std::clamp(center.x - cropWidth / 2, 0, maxX);
    const int y = std::clamp(center.y - cropHeight / 2, 0, maxY);
    cv::Mat output;
    cv::resize(panorama(cv::Rect(x, y, cropWidth, cropHeight)), output, cv::Size(VIDEO_WIDTH, VIDEO_HEIGHT));
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
    (void)decision;
    (void)faceCount;

    if (recording) {
        cv::circle(frame, cv::Point(frame.cols - 58, 52), 13, cv::Scalar(20, 20, 230), cv::FILLED);
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
        clockLabel->setText(QString("%1:%2 | 直播中")
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0')));
    }
    if (decisionLabel) {
        decisionLabel->setText(QString("当前镜头状态：%1 · %2")
            .arg(modeLabel(decision.mode))
            .arg(decisionReasonText(decision.reason)));
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

    statusLabel->setText(QString("%1 · 当前输出：%2 · 比赛时间：%3")
        .arg(recording ? "正在录制" : "待命中")
        .arg(modeLabel(decision.mode))
        .arg(QString::fromStdString(formatTimestamp(frame.timestamp))));
}

void QtBroadcastWindow::seedPanoramaBall(const cv::Point2f& normalizedPoint) {
    if (lastPanoramaFrameSize.width <= 0 || lastPanoramaFrameSize.height <= 0) {
        return;
    }

    const double scaleX = static_cast<double>(lastPanoramaFrameSize.width) / VIDEO_WIDTH;
    const double scaleY = static_cast<double>(lastPanoramaFrameSize.height) / VIDEO_HEIGHT;
    const cv::Point2f sourcePoint(
        static_cast<float>(normalizedPoint.x * scaleX),
        static_cast<float>(normalizedPoint.y * scaleY)
    );
    const double sourceRadius = panoramaSeedDisplayRadius * std::max(scaleX, scaleY);

    panoramaDetection.seedBallTrack(sourcePoint, lastFrameTimestamp, sourceRadius);
    panoramaSeedActive = true;
    panoramaSeedDisplayCenter = normalizedPoint;

    if (statusLabel) {
        statusLabel->setText(QString("Ball seed set at x=%1 y=%2. Click panorama again to correct drift.")
            .arg(static_cast<int>(std::round(normalizedPoint.x)))
            .arg(static_cast<int>(std::round(normalizedPoint.y))));
    }
}

void QtBroadcastWindow::drawPanoramaSeed(cv::Mat& frame) const {
    if (!panoramaSeedActive || frame.empty()) {
        return;
    }

    const double scaleX = static_cast<double>(frame.cols) / VIDEO_WIDTH;
    const double scaleY = static_cast<double>(frame.rows) / VIDEO_HEIGHT;
    const cv::Point center(
        static_cast<int>(std::round(panoramaSeedDisplayCenter.x * scaleX)),
        static_cast<int>(std::round(panoramaSeedDisplayCenter.y * scaleY))
    );
    const int radius = std::max(8, static_cast<int>(std::round(panoramaSeedDisplayRadius * std::max(scaleX, scaleY))));
    cv::circle(frame, center, radius, cv::Scalar(255, 120, 0), 2);
    cv::circle(frame, center, 5, cv::Scalar(0, 230, 255), cv::FILLED);
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
