# Agent Instructions

## Project Requirement

The project must address the football automatic broadcasting assignment requirement:

- Compare the advantages and disadvantages of existing football match automatic broadcasting solutions.
- Design an automatic broadcasting scheme that can automatically switch views and generate broadcast output.
- Design automatic editing workflows for both personal highlights and full-match highlights.
- Build an evaluation metric system for the automatic broadcast and highlight outputs.
- Select suitable supporting hardware for the proposed solution.
- Implement the matching software in C++.

## Scope And Constraints

- The implementation language is C++.
- The core deliverable software and assignment-facing implementation should remain C++.
- Other languages may be used when useful during development, testing, model conversion, data preparation, document generation, automation, or other supporting workflows, as long as they do not replace the core C++ implementation.
- The solution should focus on football match automatic broadcasting and highlight generation.
- The design should cover both system architecture and practical hardware/software integration.
- Documentation should make the comparison, scheme design, evaluation indicators, hardware choices, and implementation plan clear enough for assignment review.
- Team submission limit: at most 3 groups.
- Weight: 1.2.

## Innovation Perspective

When comparing against existing football match production modes, the project should emphasize the following innovative position:

- Traditional professional football broadcasting has high visual quality and rich manual camera language, but it depends on many camera operators, directors, replay staff, and expensive venue infrastructure. This project should target a lower-cost and repeatable automatic broadcast workflow for campus, amateur, and training matches.
- Existing AI sports cameras can automatically record, track the ball, stream matches, and generate simple clips, but many solutions mainly solve "follow the action" rather than "understand the match". This project should highlight match-aware directing: camera switching should consider ball position, attack direction, player density, penalty-area threat, set pieces, counterattacks, and tactical context.
- Existing football technologies such as VAR, goal-line technology, and semi-automated offside focus on referee decision support and accuracy, not on audience-facing broadcast storytelling. This project should reuse the idea of structured tracking data, but redirect it toward automatic viewing, replay selection, and highlight generation.
- The proposed system should combine live broadcasting and post-match editing in one event timeline. Real-time detection results should drive camera switching during the match, then be reused after the match to produce full-match highlights and player-specific personal highlights.
- The system should introduce an explainable highlight score instead of only cutting around goals or shots. The score may combine event type, field zone, attacking threat, player involvement, action continuity, score impact, and replay value.
- The project should support both broadcast quality and coaching value. A normal viewer needs smooth view switching and clear key events, while coaches and players need panoramic tactical context, personal clips, and measurable performance evidence.
- The evaluation should not stop at detection accuracy. It should also measure ball-in-frame rate, key-player-in-frame rate, missed key events, switching smoothness, tactical information retention, highlight relevance, clip redundancy, processing latency, and subjective viewer satisfaction.

Recommended core innovation statement: build a C++ football automatic broadcasting and highlight system that is not just an automatic camera tracker, but a match-aware, explainable, low-cost automatic director for live output, full-match highlights, and player-specific personal highlights.

## Final Technical Direction

The final solution should use a **dual fixed-camera plus software virtual director** architecture as the main delivery path. Do not make the project depend on the UVC camera's physical self-rotation, because the camera may expose only the video stream through UVC while keeping motor tracking inside its own firmware, mobile app, or private protocol. If the camera turns by itself but becomes fixed when streaming data to the C++ program, treat it as an uncontrolled fixed camera for this assignment.

### Recommended System Positioning

The project should be described as:

> A low-cost C++ football automatic broadcasting system that uses two fixed camera views, real-time detection, software virtual follow, match-aware view switching, and a unified event timeline to generate live broadcast output, full-match highlights, and player-specific personal highlights. Physical PTZ tracking is reserved as an optional future hardware upgrade when a camera with an open SDK, ONVIF PTZ, serial control, or another stable control protocol is available.

This direction keeps the project stable for demonstration while still preserving the innovation of automatic directing. The system is not merely a camera tracker; it is a match-aware director that decides when to show panorama, when to crop-follow action, when to switch to another angle, and which events deserve replay or highlight output.

### Camera Setup

- **Camera A: panorama / tactical camera**
  - Place near the midfield sideline, preferably higher than eye level.
  - Cover the largest possible field area.
  - Use as the default safe broadcast view and the main source for tactical context.

- **Camera B: auxiliary angle camera**
  - Place at a front, diagonal, goal-side, or opposite-side angle according to the available venue.
  - Use as an alternate angle for attacks, set pieces, penalty-area actions, replays, and visual variety.
  - It does not need to physically follow the ball; it provides multi-angle evidence and richer broadcast language.

The original "panorama plus close-up" idea should be retained logically, but the close-up should mainly be produced by software crop-follow from the panorama view and by switching to the auxiliary fixed camera when appropriate.

### Live Broadcast Logic

The automatic director should output one final broadcast stream while recording both raw camera streams. Suggested decision modes:

- **Panorama mode:** default mode; used when detection confidence is low or tactical context is more important than close detail.
- **Virtual follow mode:** crop and smooth the panorama frame around the detected ball, player cluster, or main action region to simulate automatic following without moving the physical camera.
- **Auxiliary angle mode:** switch to Camera B when the ball or attacking action enters its useful area, especially for shots, corners, free kicks, counterattacks, goal-mouth scrambles, and replay-worthy events.
- **Fallback mode:** immediately return to the panorama view when detection becomes unstable, the crop would lose key players, or the auxiliary camera does not contain useful action.

Switching should be match-aware rather than purely motion-aware. Consider ball position, attack direction, player density, penalty-area threat, set pieces, fast transitions, and event continuity. Add hysteresis and minimum shot duration to avoid rapid flickering between views.

### Highlight And Editing Workflow

The system should maintain a unified event timeline during live processing. Each event should include:

- timestamp and duration;
- event type, such as shot, save, goal, tackle, fast attack, set piece, or strong motion;
- camera source and recommended replay source;
- field zone and action region;
- involved targets or player labels when available;
- confidence and explainable highlight score.

Full-match highlights should select high-scoring, non-redundant events across the whole match. Personal highlights should filter the same timeline by a chosen player, manual tag, jersey number, or tracked target identity when available.

Use an explainable score instead of only cutting around goals:

```text
highlight_score =
    event_type_score
  + field_zone_score
  + attacking_threat_score
  + motion_intensity_score
  + player_involvement_score
  + continuity_score
  + replay_value_score
```

### Evaluation Metrics

Evaluation should compare at least three outputs when possible:

- single fixed-camera recording;
- dual fixed-camera automatic broadcast;
- manually selected reference clips or teacher/team subjective reference.

Metrics should include:

- ball-in-frame rate;
- key-player-in-frame rate;
- missed key event count;
- false highlight count;
- switching smoothness;
- average shot duration;
- tactical information retention;
- highlight relevance;
- clip redundancy;
- live processing latency;
- subjective viewer satisfaction and coaching usefulness.

### Hardware Strategy

For the assignment delivery, choose stable and low-risk hardware:

- two 1080p/30fps or better USB cameras;
- two tripods or fixed mounts;
- one Windows laptop running the C++/Qt/OpenCV program;
- sufficient storage for raw dual-camera recording and final broadcast output;
- optional power bank, extension cable, or USB hub when the venue requires it.

Do not spend the main schedule reverse-engineering the current UVC camera's private motor behavior. If automatic physical following is still desired, keep it as a separate future extension:

- prefer a PTZ camera with ONVIF PTZ support;
- or use a camera/gimbal with an official SDK;
- or use an external pan-tilt base controlled through serial commands;
- convert the detection module's ROI center into pan/tilt control only after the video pipeline and software director are already stable.

### Delivery Priority

Implementation and documentation should prioritize:

1. stable dual-camera capture and recording;
2. software virtual follow from the panorama view;
3. automatic view switching rules;
4. unified event timeline;
5. full-match and personal highlight generation;
6. evaluation report and hardware comparison;
7. optional physical PTZ control only if a stable, open control interface is confirmed.
