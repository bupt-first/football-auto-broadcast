#ifndef UI_H
#define UI_H

#include "common.h"
#include "detection/detection.h"
#include "editor/editor.h"
#include "face_capture/face_capture.h"
#include "video_stream/video_stream.h"

class UIManager {
public:
    bool initAll(int cameraIndex = 0);
    bool initAllAutoCamera();
    bool initAllFromFile(const std::string& videoPath);
    void run();
    void draw(cv::Mat& frame);

private:
    bool initModules();
    void drawTargets(cv::Mat& frame);
    void drawFaces(cv::Mat& frame);
    void drawStatus(cv::Mat& frame);

    VideoStreamManager vs;
    TargetDetectionManager det;
    FaceCaptureManager face;
    VideoEditorManager edit;
    std::vector<TargetInfo> targets;
    std::vector<HighlightInfo> highlights;
    std::vector<FaceInfo> faces;
};

#endif
