# Hardware and field test plan

## Baseline hardware

- Camera: USB camera, 1080p/30fps or above.
- Mount: tripod, fixed position.
- Computer: Windows laptop with Visual Studio, CMake, OpenCV runtime, and enough disk space for video output.
- Optional: power bank or extension cable for long recording.

## Camera position

- Recommended position: sideline near the midfield line.
- Height: 1.5m to 2.2m.
- Angle: slightly downward, keeping the main play area in view.
- Field of view: at least half-field for small-side football; full-field if the camera lens is wide enough.

## Test cases

1. Static camera test
   - Record 1 minute of players passing.
   - Expected output: stable preview, target boxes on moving players, no crash.

2. Manual highlight test
   - Press `SPACE` during a shot.
   - Expected output: `highlight.mp4` contains the event with pre-roll and post-roll.

3. Automatic highlight test
   - Record a strong attacking moment with clear motion.
   - Expected output: report contains at least one automatic highlight.

4. Full-match report test
   - Record 3 to 5 minutes.
   - Expected output: `highlight_report.json` includes metrics and event list.

5. Personal report test
   - Run the program and export `personal_highlight_report.json`.
   - Expected output: report file exists and can be extended later with player tags.

## Evaluation checklist

- The program opens the USB camera successfully.
- The preview window is smooth enough for recording.
- `ESC` exits normally.
- `highlight.mp4` can be opened by a media player.
- The JSON report contains `metrics`.
- The generated clips include useful football actions.

## Known risks

- Strong sunlight or backlight can reduce detection quality.
- Heavy camera shake may create false highlights.
- A single fixed USB camera cannot capture close-up reactions as well as a multi-camera setup.
- Current automatic event recognition is prototype-level and should be improved with real football detection models.
