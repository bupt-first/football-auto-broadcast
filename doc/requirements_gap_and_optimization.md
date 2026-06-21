# Project requirement gap and optimization plan

## Requirement summary

The assignment asks the team to compare existing football automatic broadcasting approaches, design an automatic broadcasting scheme, generate personal and full-match highlights, build an evaluation metric system, choose matching hardware, and implement the supporting software in C++.

## Current implementation

- C++17 desktop program based on OpenCV.
- USB camera or local video file input.
- Motion-based target detection prototype.
- Automatic broadcast states: `normal`, `follow`, `closeup`.
- Manual highlight marking with the `SPACE` key.
- Full-match highlight output: `highlight.mp4` and `highlight_report.json`.
- Personal highlight report output: `personal_highlight_report.json`.
- Evaluation metrics in JSON: highlight count, average duration, event density, target visibility, replay score.

## Main gaps

1. Football semantic understanding is still weak.
   - Current detection is frame-difference based and cannot reliably identify the ball, goals, players, goalkeeper, or shot direction.
   - Shoot/save/goal labels are placeholders.

2. Personal highlight generation is not mature.
   - The current personal report is based on a placeholder `belong` tag.
   - It does not yet support player identity tracking by jersey number, face recognition, or manual player tagging.

3. Automatic broadcast composition is basic.
   - The UI displays broadcast modes, but the output does not yet crop, pan, zoom, or smoothly switch virtual camera views.

4. Evaluation metrics are prototype-level.
   - Current metrics can describe the generated output, but they are not yet validated against manually labeled ground truth.

5. Hardware plan needs field testing.
   - The program supports a USB camera, but the final camera height, lens angle, lighting requirement, and tripod position should be tested on the football field.

## Optimization already added

- Added real `highlight.mp4` export.
- Added pre-roll and post-roll capture around highlight events.
- Added title card, pulsing border, sharpening, and replay label effects.
- Added manual event marking to avoid missing key moments during field recording.
- Added full-match and personal highlight reports.
- Added JSON evaluation metrics and scheme description.
- Added a unified `event_timeline` and EDL-style editing workflow inspired by public football-video analysis projects.
- Added explainable highlight score fields: event type, field zone, attacking threat, motion intensity, player involvement, continuity, confidence, and replay value.
- Added overlap merging and redundancy scoring so adjacent high-motion events become cleaner highlight clips instead of repeated fragments.

## Next technical improvements

1. Replace motion detection with football-specific target detection.
   - Use YOLO/OpenCV DNN for ball, player, goalkeeper, and goal detection.
   - Track targets across frames with Kalman filter or SORT.

2. Add event rules.
   - Shot: ball moves quickly toward goal area.
   - Save: goalkeeper overlaps ball trajectory near goal.
   - Goal: ball enters detected goal area and crowd/player reaction rises.
   - Each detected event should fill the same timeline fields used by the editor, so later algorithm upgrades do not require a new export pipeline.

3. Improve personal highlights.
   - Add manual player selection in the first frame.
   - Track the selected player by bounding box and color histogram.
   - Optionally identify jersey numbers from cropped player images.

4. Improve virtual broadcast.
   - Generate a virtual camera crop around active play.
   - Smooth crop movement with easing.
   - Switch to close-up when a goal/shot/save is detected.

5. Improve evaluation.
   - Compare automatic highlights with manually labeled highlights.
   - Add precision, recall, F1 score, average event delay, clip completeness, and output duration ratio.

## Innovation ideas

1. Hybrid auto/manual highlight mode.
   - Automatic detection runs continuously, while the operator can press `SPACE` for important events.
   - This is practical for student field demos because it reduces the risk of missing goals.

2. Coach review mode.
   - Export not only entertainment highlights, but also training clips such as repeated shots, defensive mistakes, and goalkeeper saves.

3. Player heat and attention map.
   - Accumulate target positions into a field heat map.
   - Use it as an extra evaluation and presentation artifact.

4. Multi-output highlight package.
   - Generate `highlight.mp4`, `highlight_report.json`, `personal_highlight_report.json`, screenshots, and a metric summary table.

5. Low-cost hardware scheme.
   - USB camera plus tripod is the baseline.
   - A later upgrade can use two fixed cameras: one wide-angle full-field camera and one close-up camera.

## Field recording advice

- Place the USB camera on a stable tripod near the midfield line.
- Keep at least half of the field visible.
- Use 1080p/30fps when possible.
- Avoid strong backlight and heavy camera shake.
- Press `SPACE` whenever a shot, save, goal, or strong attacking moment happens.
