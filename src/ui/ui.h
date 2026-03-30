#ifndef UI_H
#define UI_H
#include "common.h"
#include "video_stream/video_stream.h"
#include "detection/detection.h"
#include "face_capture/face_capture.h"
#include "editor/editor.h"

class UIManager {
public:
    bool initAll();
    void run();
    void draw(cv::Mat& frame);
private:
    VideoStreamManager vs;
    TargetDetectionManager det;
    FaceCaptureManager face;
    VideoEditorManager edit;
    std::vector<HighlightInfo> highlights;
    std::vector<FaceInfo> faces;
};

#endif