#include "ui/ui.h"
#include "ui/qt_broadcast_window.h"

#include <QApplication>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>

namespace {
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
