# Football YOLOv8 ONNX Model

Place the advanced detector model here:

```text
models/football_yolov8.onnx
```

The C++ pipeline loads this path by default through `DetectionConfig::yoloModelPath`.
If the file is missing or cannot be loaded, the application automatically falls back
to the existing OpenCV motion-based detector.

Current bundled baseline model:

```text
models/football_yolov8.onnx
```

This file is a lightweight YOLOv8n COCO ONNX baseline exported with a fixed
640x640 input so the C++ path can run immediately through OpenCV DNN. It is not
a football-specific fine-tuned model.

Default class IDs used by the C++ code for this baseline:

```text
32 = sports ball
0  = player/person
-1 = goalkeeper disabled
-1 = referee disabled
```

If your exported model uses a different class order, adjust these fields in
`DetectionConfig`:

```cpp
yoloBallClassId
yoloPlayerClassId
yoloGoalkeeperClassId
yoloRefereeClassId
```

Recommended export shape for a later football-specific model:

```text
YOLOv8 ONNX, input 640x640 or 1280x1280, output [1, 84, N] or [1, N, 84]
```

The detector accepts common YOLOv8 output layouts and then applies C++ ByteTrack.
For the current COCO baseline, player/person detections are mainly used as
tracked context, while the small football is still protected by the existing
trajectory gate when the ONNX sports-ball class is unreliable.
