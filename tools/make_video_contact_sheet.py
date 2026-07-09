import argparse
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


FFMPEG = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffmpeg.exe"
)
FFPROBE = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffprobe.exe"
)


def probe(path):
    result = subprocess.run(
        [
            FFPROBE,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height,r_frame_rate,nb_frames,duration",
            "-of",
            "default=nw=1",
            str(path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    values = {}
    for line in result.stdout.splitlines():
        key, value = line.split("=", 1)
        values[key] = value
    num, den = values["r_frame_rate"].split("/")
    fps = float(num) / float(den)
    frames = int(values.get("nb_frames") or round(float(values["duration"]) * fps))
    return int(values["width"]), int(values["height"]), frames


def read_frames(path, width, height):
    proc = subprocess.Popen(
        [
            FFMPEG,
            "-v",
            "error",
            "-i",
            str(path),
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ],
        stdout=subprocess.PIPE,
    )
    frame_size = width * height * 3
    frames = []
    assert proc.stdout is not None
    while True:
        raw = proc.stdout.read(frame_size)
        if len(raw) < frame_size:
            break
        frames.append(np.frombuffer(raw, dtype=np.uint8).reshape((height, width, 3)).copy())
    proc.wait()
    return frames


def draw_grid(draw, width, height, source_width, source_height, step=100):
    scale_x = width / source_width
    scale_y = height / source_height
    for x in range(step, source_width, step):
        sx = int(round(x * scale_x))
        draw.line((sx, 0, sx, height), fill=(255, 255, 255), width=1)
        draw.text((sx + 2, 2), str(x), fill=(255, 255, 255), font=ImageFont.load_default())
    for y in range(step, source_height, step):
        sy = int(round(y * scale_y))
        draw.line((0, sy, width, sy), fill=(255, 255, 255), width=1)
        draw.text((2, sy + 2), str(y), fill=(255, 255, 255), font=ImageFont.load_default())


def make_sheet(frames, indices, output, label, thumb_size, cols, grid):
    font = ImageFont.load_default()
    thumbs = []
    thumb_w, thumb_h = thumb_size
    for frame_index in indices:
        if frame_index < 0 or frame_index >= len(frames):
            continue
        image = Image.fromarray(frames[frame_index], "RGB")
        source_w, source_h = image.size
        image.thumbnail((thumb_w, thumb_h))
        canvas = Image.new("RGB", (thumb_w, thumb_h + 28), (18, 18, 18))
        canvas.paste(image, (0, 0))
        draw = ImageDraw.Draw(canvas)
        if grid:
            draw_grid(draw, image.width, image.height, source_w, source_h)
        draw.text((8, thumb_h + 6), f"{label} frame {frame_index}", fill=(245, 245, 245), font=font)
        thumbs.append(canvas)
    rows = max(1, math.ceil(len(thumbs) / cols))
    sheet = Image.new("RGB", (cols * thumb_w, rows * (thumb_h + 28)), (18, 18, 18))
    for idx, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((idx % cols) * thumb_w, (idx // cols) * (thumb_h + 28)))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output, quality=96)


def parse_indices(value):
    indices = []
    for part in value.split(","):
        part = part.strip()
        if not part:
            continue
        if ":" in part:
            start, end, step = [int(item) for item in part.split(":")]
            indices.extend(range(start, end + 1, step))
        else:
            indices.append(int(part))
    return indices


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--video", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--indices", required=True)
    parser.add_argument("--label", default="sample")
    parser.add_argument("--thumb-width", type=int, default=420)
    parser.add_argument("--thumb-height", type=int, default=236)
    parser.add_argument("--cols", type=int, default=3)
    parser.add_argument("--grid", action="store_true")
    args = parser.parse_args()
    video = Path(args.video)
    width, height, _ = probe(video)
    frames = read_frames(video, width, height)
    make_sheet(
        frames,
        parse_indices(args.indices),
        Path(args.output),
        args.label,
        (args.thumb_width, args.thumb_height),
        args.cols,
        args.grid,
    )


if __name__ == "__main__":
    main()
