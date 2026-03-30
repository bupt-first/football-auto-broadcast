#ifndef EDITOR_H
#define EDITOR_H
#include "common.h"

class VideoEditorManager {
public:
    bool init(int width = 1920, int height = 1080, int fps = 30);
    void addHighlight(const HighlightInfo& hl, const std::vector<FaceInfo>& faces);
    bool exportGlobal(const std::string& path);
    bool exportPersonal(const std::string& path, const std::string& belong);
};

#endif