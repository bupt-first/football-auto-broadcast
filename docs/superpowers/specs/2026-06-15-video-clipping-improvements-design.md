# Video Clipping Improvements Design

## Goal

Use the existing offline clipping workflow to process `D:\cpp_work\测试\6月15日.mp4` and produce the project outputs:

- `broadcast_record.mp4`
- `highlight.mp4`
- `personal_highlight.mp4`

At the same time, improve the clipping quality so the generated highlights are less noisy and more useful for a football match demo.

## Current Constraints

The repository currently has no built executable under `build/bin`. The hard-coded paths in `build_qt.bat` for Visual Studio, CMake, Qt, and OpenCV are not present in the current environment. The existing clipping path lives in `main.cpp` behind `--export-actual-video`, but the project build currently requires the Qt UI target as part of the same executable.

## Recommended Approach

Keep the existing offline clipping command as the main user-facing workflow:

```powershell
football_auto_broadcast.exe --export-actual-video "D:\cpp_work\测试\6月15日.mp4" all
```

If the Qt dependency blocks building the command-line workflow, add a headless CLI build path so the offline editor can compile without launching or linking the Qt UI. This preserves the C++ implementation while making the clipping feature usable in environments that only need video processing.

## Components

### Offline Event Detection

Improve `buildMotionEvents()` in `src/main.cpp`:

- sample motion over the video at a stable interval;
- smooth motion intensity over neighboring samples;
- detect local peaks rather than every above-threshold sample;
- merge nearby peaks into one football event;
- clamp clip duration with minimum and maximum bounds;
- keep a fallback segment when no strong motion is found.

### Clip Generation

Continue using `VideoEditorManager`:

- import generated events;
- build EDL clips with pre-roll and post-roll;
- export automatic broadcast, full-match highlight, and personal highlight videos;
- keep score overlays and title/ending cards.

### Build Usability

Prefer minimal build changes:

- first try existing CMake/build scripts;
- if Qt blocks command-line clipping, introduce an option such as `ENABLE_QT_UI` or a separate CLI target;
- avoid changing the live Qt director behavior unless necessary.

### Output Handling

Write generated media through `CommonTool::finalVideoOutputPath()` so outputs land in the existing final output directory convention. Log exact output paths after export.

## Error Handling

The clipping command should fail clearly when:

- the input video cannot be opened;
- no readable frames are found;
- the output writer cannot be opened;
- required build dependencies are missing.

When event detection finds no peaks, it should still produce a short fallback clip so the command remains demonstrable.

## Testing

Use `D:\cpp_work\测试\6月15日.mp4` as the acceptance video. Validate that:

- the command exits successfully;
- all requested MP4 outputs exist and are non-empty;
- the generated events are merged rather than excessively fragmented;
- the output videos can be opened by OpenCV or a media player;
- no unrelated generated files are committed.

## Scope Boundaries

This work does not attempt to replace the motion detector with YOLO or add jersey/player identity recognition. Personal highlight output may continue to use the current placeholder player ID for this pass, as long as the export pipeline works and the resulting clip is coherent.
