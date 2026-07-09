import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


FFMPEG = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffmpeg.exe"
)

TRACK_VERSION = "v6"
TRACKED_VIDEO_NAME = f"highlight_tracked_precise_{TRACK_VERSION}.mp4"
CONTACT_SHEET_NAME = f"tracking_precise_{TRACK_VERSION}_contact_sheet.jpg"
REPORT_NAME = f"tracking_precise_{TRACK_VERSION}_report.json"
PERSON_STYLES = {
    "attacker": ((40, 220, 255, 235), (17, 103, 128, 225)),
    "defender": ((42, 236, 121, 230), (22, 143, 75, 220)),
    "goalkeeper": ((118, 141, 255, 235), (55, 67, 160, 225)),
    "goal player": ((118, 141, 255, 235), (55, 67, 160, 225)),
    "foreground": ((255, 120, 205, 225), (145, 55, 120, 220)),
}
DEFAULT_PERSON_STYLE = ((42, 236, 121, 230), (22, 143, 75, 220))
FFPROBE = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffprobe.exe"
)


BALL_SEGMENTS = {
    "main": [
        [
            (0, 925.5, 521.5, 20.0),
            (15, 923.0, 526.0, 20.0),
            (30, 878.5, 535.5, 18.0),
            (45, 838.0, 536.5, 17.0),
            (60, 805.5, 532.0, 14.0),
            (75, 807.5, 537.0, 14.0),
            (90, 818.5, 545.5, 19.0),
            (95, 812.0, 547.0, 19.0),
            (100, 795.0, 552.0, 19.0),
            (105, 684.0, 520.0, 16.0),
            (110, 524.0, 511.0, 17.0),
            (115, 446.0, 493.0, 18.0),
            (120, 389.0, 456.0, 18.0),
            (125, 355.0, 462.0, 19.0),
            (130, 326.0, 454.0, 20.0),
            (135, 319.0, 453.0, 20.0),
            (140, 320.0, 455.0, 20.0),
            (150, 353.5, 454.0, 21.0),
            (165, 336.0, 437.0, 20.0),
            (180, 327.0, 438.0, 20.0),
            (195, 326.0, 440.0, 20.0),
            (210, 329.0, 439.0, 20.0),
            (220, 327.0, 440.0, 20.0),
        ]
    ],
    "aux": [
        [
            (0, 353.5, 263.0, 15.0),
            (15, 383.0, 247.0, 15.0),
            (30, 424.0, 248.5, 16.0),
            (45, 449.0, 256.5, 16.0),
            (60, 478.0, 257.5, 16.0),
            (75, 484.5, 261.0, 16.0),
            (90, 481.0, 258.0, 16.0),
        ],
        [
            (170, 662.0, 90.0, 34.0),
            (175, 704.0, 86.0, 39.0),
            (180, 724.0, 34.0, 42.0),
            (195, 724.0, 34.0, 42.0),
            (200, 724.0, 34.0, 42.0),
            (204, 724.0, 34.0, 42.0),
        ],
    ],
}


PERSON_TRACKS = {
    "main": [
        {
            "id": "goalkeeper",
            "anchors": [
                (0, 225, 320, 292, 456),
                (90, 292, 348, 352, 490),
                (180, 258, 350, 330, 518),
                (220, 258, 360, 328, 518),
            ],
        },
        {
            "id": "shooter",
            "anchors": [
                (0, 955, 310, 1022, 548),
                (90, 768, 318, 864, 582),
                (180, 766, 345, 902, 590),
                (220, 968, 345, 1065, 590),
            ],
        },
        {
            "id": "support_player",
            "anchors": [
                (0, 884, 380, 930, 496),
                (90, 1080, 382, 1155, 548),
                (180, 1065, 392, 1138, 560),
                (220, 1070, 390, 1140, 560),
            ],
        },
    ],
    "aux": [
        {
            "id": "foreground_player",
            "anchors": [
                (0, 560, 0, 925, 540),
                (90, 455, 0, 760, 455),
                (150, 425, 0, 855, 540),
                (200, 735, 0, 960, 540),
            ],
        },
        {
            "id": "distant_player",
            "anchors": [
                (0, 336, 128, 402, 300),
                (90, 505, 108, 548, 272),
                (150, 530, 126, 585, 298),
                (200, 445, 92, 520, 292),
            ],
        },
        {
            "id": "right_observer",
            "anchors": [
                (70, 800, 150, 872, 292),
                (120, 810, 152, 875, 292),
                (200, 805, 150, 875, 292),
            ],
        },
    ],
}


SAMPLE_FRAMES = {
    "main": [0, 15, 30, 60, 90, 95, 100, 105, 110, 115, 120, 125, 130, 135, 150, 165, 180, 210, 220],
    "aux": [0, 30, 60, 90, 105, 135, 165, 170, 175, 180, 195, 200, 204],
}


BREAKTHROUGH_BALL_SEGMENTS = {
    "main": [
        [
            (0, 302.0, 414.0, 18.0),
            (5, 303.0, 424.0, 18.0),
            (10, 302.0, 436.0, 18.0),
            (15, 286.0, 454.0, 18.0),
            (20, 274.0, 461.0, 18.0),
            (25, 270.0, 470.0, 18.0),
            (30, 267.0, 477.0, 18.0),
            (35, 267.0, 482.0, 18.0),
            (40, 264.0, 488.0, 18.0),
            (45, 267.0, 493.0, 18.0),
            (50, 265.0, 498.0, 18.0),
            (60, 255.0, 507.0, 18.0),
            (70, 255.0, 515.0, 18.0),
            (80, 248.0, 525.0, 18.0),
            (90, 246.0, 529.0, 18.0),
            (100, 245.0, 531.0, 19.0),
            (110, 246.0, 531.0, 19.0),
            (120, 248.0, 535.0, 19.0),
            (130, 249.0, 535.0, 19.0),
            (140, 253.0, 538.0, 19.0),
            (150, 247.0, 541.0, 19.0),
            (160, 251.0, 539.0, 19.0),
            (170, 250.0, 542.0, 19.0),
            (180, 236.0, 546.0, 18.0),
            (190, 243.0, 546.0, 18.0),
        ],
        [
            (200, 377.0, 532.0, 15.0),
            (205, 418.0, 526.0, 15.0),
            (210, 453.0, 522.0, 15.0),
            (215, 478.0, 517.0, 15.0),
            (220, 503.0, 513.0, 15.0),
            (225, 529.0, 511.0, 15.0),
            (230, 562.0, 507.0, 15.0),
            (235, 618.0, 507.0, 15.0),
            (240, 664.0, 510.0, 15.0),
            (245, 688.0, 508.0, 15.0),
            (250, 716.0, 508.0, 15.0),
            (255, 746.0, 506.0, 15.0),
            (260, 842.0, 494.0, 14.0),
            (265, 909.0, 489.0, 13.0),
            (270, 1235.0, 514.0, 10.0),
            (275, 1235.0, 514.0, 10.0),
        ]
    ],
    "aux": [
        [
            (50, 532.0, 148.0, 9.0),
            (55, 525.0, 158.0, 10.0),
            (60, 543.0, 156.0, 10.0),
            (65, 548.0, 158.0, 10.0),
            (70, 550.0, 160.0, 11.0),
            (75, 562.0, 158.0, 11.0),
            (80, 584.0, 158.0, 12.0),
            (85, 616.0, 160.0, 12.0),
            (90, 630.0, 160.0, 13.0),
            (95, 642.0, 164.0, 13.0),
        ],
        [
            (125, 580.0, 184.0, 13.0),
            (130, 580.0, 185.0, 14.0),
            (135, 614.0, 201.0, 18.0),
            (138, 676.0, 226.0, 23.0),
            (140, 740.0, 260.0, 29.0),
        ]
    ],
}


BREAKTHROUGH_PERSON_TRACKS = {
    "main": [
        {
            "id": "attacker",
            "label": "attacker",
            "anchors": [
                (0, 77, 300, 165, 535),
                (40, 114, 300, 198, 535),
                (80, 146, 300, 232, 540),
                (120, 180, 302, 268, 552),
                (140, 205, 304, 290, 558),
                (160, 170, 310, 288, 620),
                (180, 132, 318, 288, 675),
                (200, 318, 328, 470, 638),
                (210, 420, 325, 552, 622),
                (220, 480, 328, 603, 606),
                (230, 540, 330, 650, 595),
                (240, 600, 330, 715, 585),
                (250, 650, 326, 765, 580),
                (260, 690, 320, 820, 585),
                (280, 790, 320, 925, 585),
                (320, 875, 318, 1015, 585),
                (400, 995, 318, 1110, 590),
                (440, 1050, 320, 1160, 590),
            ],
        },
        {
            "id": "defender",
            "label": "defender",
            "anchors": [
                (0, 365, 300, 455, 535),
                (80, 372, 300, 465, 545),
                (140, 380, 300, 472, 542),
                (160, 360, 305, 455, 555),
                (180, 330, 310, 430, 565),
                (200, 270, 315, 365, 570),
                (220, 215, 325, 312, 575),
                (260, 210, 325, 306, 575),
                (320, 225, 330, 320, 575),
                (400, 270, 330, 370, 575),
                (440, 290, 330, 390, 575),
            ],
        },
        {
            "id": "goalkeeper",
            "label": "goalkeeper",
            "anchors": [
                (0, 1085, 270, 1160, 505),
                (160, 1080, 270, 1165, 505),
                (220, 1065, 280, 1155, 525),
                (240, 1045, 285, 1135, 540),
                (260, 1020, 305, 1115, 565),
                (300, 1085, 300, 1175, 565),
                (440, 1020, 285, 1115, 555),
            ],
        },
        {
            "id": "goal_player",
            "label": "goal player",
            "draw": False,
            "anchors": [
                (220, 1130, 445, 1250, 640),
                (300, 1110, 448, 1255, 642),
                (360, 1100, 448, 1250, 642),
            ],
        },
    ],
    "aux": [
        {
            "id": "foreground_player",
            "label": "foreground",
            "draw": False,
            "anchors": [
                (0, 610, 0, 860, 540),
                (30, 602, 0, 865, 540),
                (70, 610, 0, 892, 540),
                (90, 545, 0, 960, 540),
                (120, 455, 0, 960, 540),
                (135, 420, 0, 960, 540),
                (145, 520, 0, 945, 540),
                (160, 610, 0, 960, 540),
                (180, 545, 0, 960, 540),
                (190, 650, 0, 960, 540),
            ],
        },
        {
            "id": "attacker",
            "label": "attacker",
            "anchors": [
                (0, 405, 125, 470, 285),
                (50, 510, 132, 580, 300),
                (70, 520, 135, 590, 305),
                (90, 555, 130, 635, 318),
                (100, 590, 130, 668, 322),
            ],
        },
        {
            "id": "attacker",
            "label": "attacker",
            "draw": False,
            "anchors": [
                (110, 600, 132, 700, 340),
                (120, 640, 128, 735, 338),
                (125, 655, 130, 750, 350),
                (135, 640, 138, 750, 370),
                (145, 610, 120, 735, 365),
            ],
        },
        {
            "id": "attacker",
            "label": "attacker",
            "anchors": [
                (160, 500, 135, 615, 390),
                (180, 390, 135, 510, 420),
                (190, 350, 140, 470, 420),
            ],
        },
        {
            "id": "defender",
            "label": "defender",
            "anchors": [
                (0, 355, 125, 425, 290),
                (50, 455, 130, 525, 300),
                (70, 450, 132, 520, 302),
                (90, 420, 130, 500, 310),
                (100, 395, 130, 465, 310),
            ],
        },
        {
            "id": "defender",
            "label": "defender",
            "anchors": [
                (158, 575, 128, 682, 398),
                (170, 510, 135, 620, 415),
                (180, 455, 138, 565, 420),
                (190, 420, 140, 535, 420),
            ],
        },
    ],
}


BREAKTHROUGH_SAMPLE_FRAMES = {
    "main": [0, 40, 80, 120, 140, 160, 180, 200, 210, 220, 230, 240, 250, 255, 260, 265, 270, 275, 280, 320, 400, 440],
    "aux": [0, 30, 50, 55, 60, 70, 80, 90, 95, 110, 120, 125, 130, 135, 138, 140, 145, 160, 180, 190],
}


SCENARIO_CONFIGS = {
    "shot": {
        "ball_segments": BALL_SEGMENTS,
        "person_tracks": PERSON_TRACKS,
        "sample_frames": SAMPLE_FRAMES,
        "tracked_video": TRACKED_VIDEO_NAME,
        "contact_sheet": CONTACT_SHEET_NAME,
        "report": REPORT_NAME,
        "style": "B v6: low-latency ball boxes, refined ball radius, smoother visible-only football trajectory, higher-bitrate output",
        "method": "manual high-resolution ball anchors refined per frame with multi-template/color/ring scoring, adaptive radius search, and temporal cleanup; occluded/out-of-frame gaps are not connected",
        "lock_keyframes": True,
    },
    "breakthrough": {
        "ball_segments": BREAKTHROUGH_BALL_SEGMENTS,
        "person_tracks": BREAKTHROUGH_PERSON_TRACKS,
        "sample_frames": BREAKTHROUGH_SAMPLE_FRAMES,
        "tracked_video": "highlight_tracked_breakthrough_v12.mp4",
        "contact_sheet": "tracking_breakthrough_v12_contact_sheet.jpg",
        "report": "tracking_breakthrough_v12_report.json",
        "style": "B breakthrough v12: v11 tracking with auxiliary-view person boxes shifted slightly upward",
        "method": "breakthrough-specific dense visible ball anchors retained from v7; v11 visible-only role-colored person tracks are retained, with only auxiliary-view person boxes shifted slightly upward at draw time; the main shot segment remains corrected through the goal-mouth finish, auxiliary partial/occluded ball frames are trimmed, and only a short recent trajectory is drawn",
        "lock_keyframes": True,
    },
}


ACTIVE_STYLE = SCENARIO_CONFIGS["shot"]["style"]
ACTIVE_METHOD = SCENARIO_CONFIGS["shot"]["method"]
ACTIVE_REPORT_NAME = SCENARIO_CONFIGS["shot"]["report"]
LOCK_KEYFRAMES = SCENARIO_CONFIGS["shot"]["lock_keyframes"]


def run_json(args):
    result = subprocess.run(args, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def probe(video_path):
    data = run_json(
        [
            FFPROBE,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,r_frame_rate,nb_frames,duration",
            "-of",
            "json",
            str(video_path),
        ]
    )
    stream = data["streams"][0]
    num, den = stream["r_frame_rate"].split("/")
    fps = float(num) / float(den)
    return {
        "width": int(stream["width"]),
        "height": int(stream["height"]),
        "fps": fps,
        "frames": int(stream.get("nb_frames") or round(float(stream["duration"]) * fps)),
        "duration": float(stream["duration"]),
    }


def read_frames(video_path, meta):
    proc = subprocess.Popen(
        [
            FFMPEG,
            "-v",
            "error",
            "-i",
            str(video_path),
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ],
        stdout=subprocess.PIPE,
    )
    frame_size = meta["width"] * meta["height"] * 3
    frames = []
    assert proc.stdout is not None
    while True:
        raw = proc.stdout.read(frame_size)
        if len(raw) < frame_size:
            break
        frame = np.frombuffer(raw, dtype=np.uint8).reshape((meta["height"], meta["width"], 3)).copy()
        frames.append(frame)
    proc.wait()
    return frames


def interpolate_keyframes(points, frame_index):
    points = sorted(points, key=lambda item: item[0])
    if frame_index <= points[0][0]:
        return points[0][1:]
    if frame_index >= points[-1][0]:
        return points[-1][1:]

    for idx in range(len(points) - 1):
        if points[idx][0] <= frame_index <= points[idx + 1][0]:
            p1 = points[idx]
            p2 = points[idx + 1]
            p0 = points[max(0, idx - 1)]
            p3 = points[min(len(points) - 1, idx + 2)]
            denom = max(1, p2[0] - p1[0])
            t = (frame_index - p1[0]) / denom
            t2 = t * t
            t3 = t2 * t
            values = []
            for col in range(1, 4):
                v0, v1, v2, v3 = p0[col], p1[col], p2[col], p3[col]
                values.append(
                    0.5
                    * (
                        (2 * v1)
                        + (-v0 + v2) * t
                        + (2 * v0 - 5 * v1 + 4 * v2 - v3) * t2
                        + (-v0 + 3 * v1 - 3 * v2 + v3) * t3
                    )
                )
            return tuple(values)
    return points[-1][1:]


def interpolate_box(anchors, frame_index):
    anchors = sorted(anchors, key=lambda item: item[0])
    if frame_index < anchors[0][0] or frame_index > anchors[-1][0]:
        return None
    if frame_index <= anchors[0][0]:
        return anchors[0][1:]
    for idx in range(len(anchors) - 1):
        start = anchors[idx]
        end = anchors[idx + 1]
        if start[0] <= frame_index <= end[0]:
            t = (frame_index - start[0]) / max(1, end[0] - start[0])
            return tuple(start[col] + (end[col] - start[col]) * t for col in range(1, 5))
    return anchors[-1][1:]


def extract_patch(frame, cx, cy, radius):
    radius = int(math.ceil(radius))
    size = radius * 2 + 1
    patch = np.zeros((size, size, 3), dtype=np.uint8)
    valid = np.zeros((size, size), dtype=bool)
    x0 = int(round(cx)) - radius
    y0 = int(round(cy)) - radius
    x1 = x0 + size
    y1 = y0 + size
    sx0 = max(0, x0)
    sy0 = max(0, y0)
    sx1 = min(frame.shape[1], x1)
    sy1 = min(frame.shape[0], y1)
    if sx1 > sx0 and sy1 > sy0:
        dx0 = sx0 - x0
        dy0 = sy0 - y0
        patch[dy0 : dy0 + (sy1 - sy0), dx0 : dx0 + (sx1 - sx0)] = frame[sy0:sy1, sx0:sx1]
        valid[dy0 : dy0 + (sy1 - sy0), dx0 : dx0 + (sx1 - sx0)] = True
    yy, xx = np.ogrid[:size, :size]
    circle = (xx - radius) ** 2 + (yy - radius) ** 2 <= radius * radius
    return patch, valid & circle


def ncc_score(candidate_patch, candidate_mask, template_patch, template_mask):
    if candidate_patch.shape[:2] != template_patch.shape[:2]:
        height = min(candidate_patch.shape[0], template_patch.shape[0])
        width = min(candidate_patch.shape[1], template_patch.shape[1])

        def crop_center(array):
            y0 = (array.shape[0] - height) // 2
            x0 = (array.shape[1] - width) // 2
            return array[y0 : y0 + height, x0 : x0 + width]

        candidate_patch = crop_center(candidate_patch)
        candidate_mask = crop_center(candidate_mask)
        template_patch = crop_center(template_patch)
        template_mask = crop_center(template_mask)

    mask = candidate_mask & template_mask
    if int(mask.sum()) < 40:
        return 0.0
    cand = (
        0.299 * candidate_patch[:, :, 0]
        + 0.587 * candidate_patch[:, :, 1]
        + 0.114 * candidate_patch[:, :, 2]
    )[mask].astype(np.float32)
    temp = (
        0.299 * template_patch[:, :, 0]
        + 0.587 * template_patch[:, :, 1]
        + 0.114 * template_patch[:, :, 2]
    )[mask].astype(np.float32)
    cand -= cand.mean()
    temp -= temp.mean()
    denom = float(np.sqrt((cand * cand).sum() * (temp * temp).sum()))
    if denom <= 1e-6:
        return 0.0
    return float((cand * temp).sum() / denom)


def visual_score(frame, cx, cy, radius, template_bundle):
    patch, mask = extract_patch(frame, cx, cy, radius)
    if int(mask.sum()) < max(30, int(math.pi * radius * radius * 0.25)):
        return -10.0

    pix = patch[mask].astype(np.float32)
    r = pix[:, 0]
    g = pix[:, 1]
    b = pix[:, 2]
    lum = 0.299 * r + 0.587 * g + 0.114 * b

    white = ((r > 125) & (g > 125) & (b > 115) & ((np.maximum.reduce([r, g, b]) - np.minimum.reduce([r, g, b])) < 95)).mean()
    blue_pattern = ((b > 90) & (b > r * 0.9) & (b > g * 0.75)).mean()
    grass_inside = ((g > r * 1.12) & (g > b * 1.12) & (g > 75)).mean()

    rr = int(math.ceil(radius * 1.55))
    x0 = int(round(cx)) - rr
    y0 = int(round(cy)) - rr
    x1 = int(round(cx)) + rr + 1
    y1 = int(round(cy)) + rr + 1
    sx0 = max(0, x0)
    sy0 = max(0, y0)
    sx1 = min(frame.shape[1], x1)
    sy1 = min(frame.shape[0], y1)
    ring_contrast = 0.0
    if sx1 > sx0 and sy1 > sy0:
        sub = frame[sy0:sy1, sx0:sx1].astype(np.float32)
        yy, xx = np.ogrid[sy0:sy1, sx0:sx1]
        dist = (xx - cx) ** 2 + (yy - cy) ** 2
        ring = (dist > (radius * 1.08) ** 2) & (dist <= (radius * 1.55) ** 2)
        if int(ring.sum()) > 20:
            ring_lum = 0.299 * sub[:, :, 0] + 0.587 * sub[:, :, 1] + 0.114 * sub[:, :, 2]
            ring_contrast = float(lum.mean() - ring_lum[ring].mean()) / 255.0

    weighted_template_value = 0.0
    total_weight = 0.0
    best_template_value = -1.0
    for weight, template_patch, template_mask in template_bundle:
        value = ncc_score(patch, mask, template_patch, template_mask)
        weighted_template_value += weight * value
        total_weight += weight
        best_template_value = max(best_template_value, value)
    if total_weight > 0:
        template_value = 0.55 * best_template_value + 0.45 * (weighted_template_value / total_weight)
    else:
        template_value = 0.0

    return (
        2.5 * float(white)
        + 0.8 * float(blue_pattern)
        + 0.9 * float(ring_contrast)
        + 0.45 * (float(lum.std()) / 64.0)
        + 1.55 * template_value
        - 1.15 * float(grass_inside)
    )


def nearby_templates(frame_index, templates, limit=2):
    selected = sorted(templates, key=lambda item: abs(item[0] - frame_index))[:limit]
    bundle = []
    for anchor_frame, template_patch, template_mask in selected:
        distance = abs(anchor_frame - frame_index)
        bundle.append((1.0 / (1.0 + distance / 18.0), template_patch, template_mask))
    return bundle


def refine_position(frame, prior, template_bundle, search_radius):
    px, py, pr = prior
    best = (px, py, pr)
    best_score = -1e9
    h, w = frame.shape[:2]
    int_search = int(search_radius)
    radius_candidates = [pr, max(4.0, pr - 1.0), pr + 1.0]
    for dy in range(-int_search, int_search + 1):
        cy = py + dy
        if cy < -pr * 0.4 or cy >= h + pr * 0.4:
            continue
        for dx in range(-int_search, int_search + 1):
            cx = px + dx
            if cx < -pr * 0.4 or cx >= w + pr * 0.4:
                continue
            prior_penalty = 0.65 * ((dx * dx + dy * dy) / max(1.0, int_search * int_search))
            for radius in radius_candidates:
                radius_penalty = 0.16 * abs(radius - pr)
                score = visual_score(frame, cx, cy, radius, template_bundle) - prior_penalty - radius_penalty
                if score > best_score:
                    best_score = score
                    best = (cx, cy, radius)
    return best, best_score


def build_dense_ball_track(frames, role):
    dense = {}
    keyframe_lookup = {}
    segment_ranges = []
    for segment_index, segment in enumerate(BALL_SEGMENTS[role]):
        for item in segment:
            keyframe_lookup[item[0]] = item[1:]
        templates = []
        for frame_index, cx, cy, radius in segment:
            patch, mask = extract_patch(frames[frame_index], cx, cy, radius)
            templates.append((frame_index, patch, mask))

        start = segment[0][0]
        end = min(segment[-1][0], len(frames) - 1)
        segment_ranges.append((start, end))
        for frame_index in range(start, end + 1):
            prior = interpolate_keyframes(segment, frame_index)
            if frame_index in keyframe_lookup and LOCK_KEYFRAMES:
                refined = keyframe_lookup[frame_index]
                score = None
            else:
                if role == "main" and segment_index == 1:
                    search_radius = 4
                elif role == "aux":
                    search_radius = 3
                elif 100 <= frame_index <= 125:
                    search_radius = 10
                else:
                    search_radius = 8
                refined, score = refine_position(
                    frames[frame_index],
                    prior,
                    nearby_templates(frame_index, templates),
                    search_radius,
                )
            dense[frame_index] = {
                "x": float(refined[0]),
                "y": float(refined[1]),
                "r": float(refined[2]),
                "segment": segment_index,
                "score": score,
                "anchor": frame_index in keyframe_lookup,
            }

    # Multi-pass jitter cleanup while keeping every manually calibrated frame fixed.
    for segment_index, (start, end) in enumerate(segment_ranges):
        for _ in range(2):
            original = {f: dense[f].copy() for f in range(start, end + 1) if f in dense}
            for frame_index in range(start + 1, end):
                item = dense[frame_index]
                if item["anchor"]:
                    continue
                neighbors = [
                    (-2, 0.08),
                    (-1, 0.18),
                    (0, 0.48),
                    (1, 0.18),
                    (2, 0.08),
                ]
                weighted = []
                for offset, weight in neighbors:
                    neighbor = original.get(frame_index + offset)
                    if neighbor:
                        weighted.append((neighbor, weight))
                total = sum(weight for _, weight in weighted)
                if total <= 0:
                    continue
                item["x"] = sum(neighbor["x"] * weight for neighbor, weight in weighted) / total
                item["y"] = sum(neighbor["y"] * weight for neighbor, weight in weighted) / total
                item["r"] = sum(neighbor["r"] * weight for neighbor, weight in weighted) / total
    return dense


def draw_label(draw, x, y, text, fill, scale):
    font = ImageFont.load_default()
    bbox = draw.textbbox((0, 0), text, font=font)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    pad = max(2, int(3 * scale))
    y = max(0, y - height - pad * 2)
    draw.rectangle((x, y, x + width + pad * 2, y + height + pad * 2), fill=fill)
    draw.text((x + pad, y + pad), text, fill=(255, 255, 255), font=font)


def draw_annotations(frame, role, frame_index, dense_track):
    image = Image.fromarray(frame, "RGB").convert("RGBA")
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    width, height = image.size
    scale = max(width / 960.0, height / 540.0)

    person_width = max(2, int(round(2 * scale)))
    for track in PERSON_TRACKS[role]:
        if not track.get("draw", True):
            continue
        box = interpolate_box(track["anchors"], frame_index)
        if box is None:
            continue
        x1, y1, x2, y2 = box
        if role == "aux":
            y_shift = 10 * scale
            y1 -= y_shift
            y2 -= y_shift
        x1 = max(0, min(width - 1, x1))
        y1 = max(0, min(height - 1, y1))
        x2 = max(0, min(width - 1, x2))
        y2 = max(0, min(height - 1, y2))
        person_label = track.get("label", track.get("id", "person")).replace("_", " ")
        outline, label_fill = PERSON_STYLES.get(person_label, DEFAULT_PERSON_STYLE)
        for offset in range(person_width):
            draw.rectangle((x1 - offset, y1 - offset, x2 + offset, y2 + offset), outline=outline)
        draw_label(draw, int(x1), int(y1), person_label, label_fill, scale)

    current = dense_track.get(frame_index)
    if current is not None:
        segment_id = current["segment"]
        trail = [
            (idx, item)
            for idx, item in dense_track.items()
            if item["segment"] == segment_id and frame_index - 35 <= idx <= frame_index
        ]
        trail.sort(key=lambda item: item[0])
        if len(trail) > 1:
            for idx in range(1, len(trail)):
                age_factor = idx / max(1, len(trail) - 1)
                alpha = int(70 + 165 * age_factor)
                p1 = trail[idx - 1][1]
                p2 = trail[idx][1]
                draw.line(
                    (p1["x"], p1["y"], p2["x"], p2["y"]),
                    fill=(255, 164, 23, int(alpha * 0.82)),
                    width=max(2, int(round(2 * scale))),
                )

        x = current["x"]
        y = current["y"]
        radius = current["r"]
        opening_foot_contact = role == "main" and current["segment"] == 0 and frame_index <= 190
        draw_radius = radius + (6 * scale if opening_foot_contact else 0)
        if opening_foot_contact:
            halo = draw_radius + 5 * scale
            draw.ellipse(
                (x - halo, y - halo, x + halo, y + halo),
                fill=(255, 214, 31, 42),
                outline=(255, 214, 31, 120),
            )
        marker_width = max(2, int(round(3 * scale)))
        for offset in range(marker_width):
            draw.ellipse(
                (x - draw_radius - offset, y - draw_radius - offset, x + draw_radius + offset, y + draw_radius + offset),
                outline=(255, 214, 31, 245),
            )
        dot = max(2, int(round(2 * scale)))
        draw.ellipse((x - dot, y - dot, x + dot, y + dot), fill=(255, 214, 31, 245))
        tick = max(6 if opening_foot_contact else 5, int(round((6 if opening_foot_contact else 5) * scale)))
        draw.line((x - tick, y, x + tick, y), fill=(255, 214, 31, 230), width=max(1, int(round(1.5 * scale))))
        draw.line((x, y - tick, x, y + tick), fill=(255, 214, 31, 230), width=max(1, int(round(1.5 * scale))))
        label_x = int(x + draw_radius + 8 * scale)
        if label_x > width - int(52 * scale):
            label_x = int(x - draw_radius - 42 * scale)
        label_x = max(0, min(width - 1, label_x))
        label_y = int(y - draw_radius) if opening_foot_contact else int(y + draw_radius + 8 * scale)
        if opening_foot_contact:
            draw.line((x + draw_radius * 0.7, y - draw_radius * 0.7, label_x, label_y), fill=(255, 214, 31, 210), width=max(1, int(round(1.5 * scale))))
        draw_label(draw, label_x, label_y, "ball", (145, 103, 11, 230), scale)

    return np.array(Image.alpha_composite(image, overlay).convert("RGB"))


def encode_video(frames, video_path, output_path, meta, role, dense_track, samples):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.Popen(
        [
            FFMPEG,
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            f"{meta['width']}x{meta['height']}",
            "-r",
            f"{meta['fps']:.6f}",
            "-i",
            "-",
            "-i",
            str(video_path),
            "-map",
            "0:v:0",
            "-map",
            "1:a?",
            "-vf",
            "scale=1920:1080:flags=lanczos,unsharp=5:5:0.42:3:3:0.16",
            "-c:v",
            "h264_mf",
            "-b:v",
            "16M",
            "-pix_fmt",
            "yuv420p",
            "-c:a",
            "copy",
            "-movflags",
            "+faststart",
            str(output_path),
        ],
        stdin=subprocess.PIPE,
    )
    assert proc.stdin is not None
    for frame_index, frame in enumerate(frames):
        annotated = draw_annotations(frame, role, frame_index, dense_track)
        if frame_index in samples:
            samples[frame_index] = annotated.copy()
        proc.stdin.write(annotated.tobytes())
    proc.stdin.close()
    code = proc.wait()
    if code != 0:
        raise RuntimeError(f"ffmpeg failed with exit code {code}")


def make_contact_sheet(samples, output_path, title_prefix):
    ordered = sorted(samples.items(), key=lambda item: item[0])
    if not ordered:
        return
    thumbs = []
    for frame_index, frame in ordered:
        image = Image.fromarray(frame, "RGB")
        image.thumbnail((420, 236))
        canvas = Image.new("RGB", (420, 264), (18, 18, 18))
        canvas.paste(image, (0, 0))
        draw = ImageDraw.Draw(canvas)
        draw.text((8, 242), f"{title_prefix} frame {frame_index}", fill=(245, 245, 245), font=ImageFont.load_default())
        thumbs.append(canvas)
    cols = 3
    rows = math.ceil(len(thumbs) / cols)
    sheet = Image.new("RGB", (cols * 420, rows * 264), (18, 18, 18))
    for idx, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((idx % cols) * 420, (idx // cols) * 264))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output_path, quality=96)


def write_report(output_path, role, video_path, meta, dense_track, contact_sheet):
    samples = []
    for frame_index in SAMPLE_FRAMES[role]:
        item = dense_track.get(frame_index)
        samples.append(
            {
                "frame": frame_index,
                "timeSec": round(frame_index / meta["fps"], 3),
                "ball": None
                if item is None
                else {
                    "x": round(item["x"], 2),
                    "y": round(item["y"], 2),
                    "r": round(item["r"], 2),
                    "segment": item["segment"],
                    "anchor": item["anchor"],
                    "score": None if item["score"] is None else round(float(item["score"]), 4),
                },
            }
        )
    report = {
        "input": str(video_path),
        "role": role,
        "output": str(output_path),
        "contactSheet": str(contact_sheet),
        "video": {
            "width": meta["width"],
            "height": meta["height"],
            "fps": meta["fps"],
            "durationSec": meta["duration"],
            "frames": meta["frames"],
        },
        "style": ACTIVE_STYLE,
        "method": ACTIVE_METHOD,
        "visibleSegments": [
            {"startFrame": segment[0][0], "endFrame": segment[-1][0], "anchors": segment}
            for segment in BALL_SEGMENTS[role]
        ],
        "framesProcessed": meta["frames"],
        "ballVisibleFrames": len(dense_track),
        "trajectorySamples": samples,
    }
    report_path = output_path.with_name(ACTIVE_REPORT_NAME)
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


def configure_scenario(scenario):
    global BALL_SEGMENTS, PERSON_TRACKS, SAMPLE_FRAMES
    global ACTIVE_STYLE, ACTIVE_METHOD, ACTIVE_REPORT_NAME, LOCK_KEYFRAMES
    config = SCENARIO_CONFIGS[scenario]
    BALL_SEGMENTS = config["ball_segments"]
    PERSON_TRACKS = config["person_tracks"]
    SAMPLE_FRAMES = config["sample_frames"]
    ACTIVE_STYLE = config["style"]
    ACTIVE_METHOD = config["method"]
    ACTIVE_REPORT_NAME = config["report"]
    LOCK_KEYFRAMES = config["lock_keyframes"]
    return config


def process(role, video_path, output_dir, scenario):
    config = configure_scenario(scenario)
    meta = probe(video_path)
    frames = read_frames(video_path, meta)
    dense_track = build_dense_ball_track(frames, role)
    output_path = output_dir / config["tracked_video"]
    contact_sheet = output_dir / config["contact_sheet"]
    samples = {frame_index: None for frame_index in SAMPLE_FRAMES[role] if frame_index < len(frames)}
    encode_video(frames, video_path, output_path, meta, role, dense_track, samples)
    samples = {frame_index: frame for frame_index, frame in samples.items() if frame is not None}
    make_contact_sheet(samples, contact_sheet, role)
    write_report(output_path, role, video_path, meta, dense_track, contact_sheet)
    print(json.dumps({"role": role, "output": str(output_path), "contactSheet": str(contact_sheet)}, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--role", choices=["main", "aux"], required=True)
    parser.add_argument("--video", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--scenario", choices=sorted(SCENARIO_CONFIGS), default="shot")
    args = parser.parse_args()
    process(args.role, Path(args.video), Path(args.output_dir), args.scenario)


if __name__ == "__main__":
    main()
