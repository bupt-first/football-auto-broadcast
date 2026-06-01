# football-auto-broadcast

A C++17/OpenCV prototype for football automatic broadcast and highlight analysis.

## Current status

This repository now contains a runnable project skeleton:

- `video_stream`: opens a camera or video file and reads frames.
- `detection`: uses frame-difference motion detection as a placeholder for ball/player detection.
- `face_capture`: optionally uses OpenCV Haar cascade face detection when the XML model is available.
- `editor`: stores highlight metadata and exports `highlight.mp4` plus a JSON report.
- `ui`: provides a Qt director console for dual-camera automatic broadcasting. The legacy OpenCV preview is still available with `--legacy-ui`.

The detection, emotion, RTMP push, and real video highlight cutting logic are intentionally simple placeholders. They are ready for team members to replace with stronger algorithms.

## Team work split

1. Architecture and GitHub management
   - Keep module interfaces stable.
   - Maintain branches, README, and final integration.
   - Check demo flow before merging.

2. Video processing and auto broadcast
   - Improve `src/video_stream`.
   - Add video writer and RTMP push support.
   - Implement smooth mode switching and crop/follow camera logic.

3. Target detection and event analysis
   - Improve `src/detection`.
   - Detect field area, ball, players, and goal area.
   - Classify shoot, save, goal, and other key events.

4. Highlight generation and metrics
   - Improve `src/editor`.
   - Export real highlight videos.
   - Add global/personal highlight scoring and JSON report fields.

5. Hardware, documentation, and testing
   - Prepare camera setup, tripod/fixed-position plan, test videos, screenshots, and PPT materials.
   - Record manual broadcast vs fixed camera vs automatic broadcast comparison results.

## Build

The current CMake file expects local dependencies under:

- `lib/opencv`
- `lib/dlib`
- `lib/ffmpeg`
- Qt 6 Widgets, configured through `CMAKE_PREFIX_PATH` when Qt is not in the default CMake search path.

Example:

```powershell
.\build_qt.bat
```

Manual NMake build:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 && E:\CMAKE\bin\cmake.exe -G ""NMake Makefiles"" -S . -B build\nmake-qt -DCMAKE_PREFIX_PATH=""D:\Qt\6.7.3\msvc2019_64"" -DCMAKE_BUILD_TYPE=Release && E:\CMAKE\bin\cmake.exe --build build\nmake-qt"
D:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe --release build\bin\football_auto_broadcast.exe
```

## Run

Use the Qt dual-camera director console:

```powershell
.\build\bin\football_auto_broadcast.exe
```

By default, the program tries to open `UGREEN Camera 1080P` as the panorama camera and `UVC Camera` as the close-up camera. If device-name opening is not available in the local OpenCV backend, it scans visible USB camera indexes.

Specify known indexes manually:

```powershell
.\build\bin\football_auto_broadcast.exe --panorama-camera 1 --closeup-camera 2
```

The Qt console shows three live panes:

- automatic broadcast output as the main screen;
- panorama camera monitor;
- close-up camera monitor.

Each pane has its own zoom slider and reset button. Press `Start Recording` to write:

- `panorama_record.mp4`
- `closeup_record.mp4`
- `broadcast_record.mp4`

Use the legacy single-camera OpenCV window:

```powershell
.\build\bin\football_auto_broadcast.exe --legacy-ui
```

You can also double-click:

```text
run_usb_camera.bat
```

If your USB camera is known to be index `1`, double-click:

```text
run_usb_camera_1.bat
```

Use a video file:

```powershell
.\build\bin\football_auto_broadcast.exe --legacy-ui path\to\match.mp4
```

Press `ESC` to exit. The program writes `highlight_report.json` in the current working directory.

During recording:

- Press `SPACE` to manually mark a highlight when something important happens.
- Automatic motion highlights are also captured.
- On exit, the program writes `highlight.mp4` when at least one highlight has enough captured frames.
- The generated highlight video includes detection boxes, a title card, a pulsing border, sharpening, and a replay label.
- The program also writes `personal_highlight_report.json` and evaluation metrics in the report.

## Documents

- `doc/requirements_gap_and_optimization.md`: requirement comparison, current gaps, optimization plan, and innovation ideas.
- `doc/hardware_test_plan.md`: USB camera hardware plan and field test checklist.
- `doc/video_stream_auto_broadcast_optimization.md`: dynamic crop, ROI smoothing, and video output design for football auto broadcast.

## Notes

- For face detection, place `haarcascade_frontalface_default.xml` next to the executable or in the run directory.
- The `lib`, `doc`, and `test` directories are currently empty and should be filled by the team as the project grows.
- For football field recording, use a tripod or fixed position, keep the whole play area in frame, and prefer 1080p/30fps with stable lighting.
