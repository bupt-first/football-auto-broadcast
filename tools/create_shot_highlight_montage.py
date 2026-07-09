import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter


FFMPEG = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffmpeg.exe"
)

ROOT = Path(r"D:\football-auto-broadcast\football-auto-broadcast")
MAIN_VIDEO = ROOT / r"build\bin\output video\射门主视角\highlight_tracked_precise_v6.mp4"
AUX_VIDEO = ROOT / r"build\bin\output video\射门辅视角\highlight_tracked_precise_v6.mp4"
OUTPUT = ROOT / r"build\bin\output video\shot_highlight_montage_hq.mp4"
CONTACT_SHEET = ROOT / r"build\bin\output video\shot_highlight_montage_hq_contact_sheet.jpg"

FPS = 30
SRC_W = 1280
SRC_H = 720
OUT_W = 1920
OUT_H = 1080

MAIN_BALL = [
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

AUX_BALL = [
    (0, 353.5, 263.0, 15.0),
    (15, 383.0, 247.0, 15.0),
    (30, 424.0, 248.5, 16.0),
    (45, 449.0, 256.5, 16.0),
    (60, 478.0, 257.5, 16.0),
    (75, 484.5, 261.0, 16.0),
    (90, 481.0, 258.0, 16.0),
    (170, 662.0, 90.0, 34.0),
    (175, 704.0, 86.0, 39.0),
    (180, 724.0, 34.0, 42.0),
    (195, 724.0, 34.0, 42.0),
    (200, 724.0, 34.0, 42.0),
    (204, 724.0, 34.0, 42.0),
]


def font(size, bold=False):
    candidates = [
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\arialbd.ttf" if bold else r"C:\Windows\Fonts\arial.ttf",
    ]
    for item in candidates:
        path = Path(item)
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


FONT_TITLE = font(78, True)
FONT_BIG = font(46, True)
FONT_MED = font(34, True)
FONT_SMALL = font(24, False)


def read_scaled_frames(path):
    proc = subprocess.Popen(
        [
            FFMPEG,
            "-v",
            "error",
            "-i",
            str(path),
            "-vf",
            f"scale={SRC_W}:{SRC_H}:flags=lanczos",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-",
        ],
        stdout=subprocess.PIPE,
    )
    frame_size = SRC_W * SRC_H * 3
    frames = []
    assert proc.stdout is not None
    while True:
        raw = proc.stdout.read(frame_size)
        if len(raw) < frame_size:
            break
        frames.append(np.frombuffer(raw, dtype=np.uint8).reshape((SRC_H, SRC_W, 3)).copy())
    proc.wait()
    return frames


def interp(points, frame_index):
    points = sorted(points, key=lambda item: item[0])
    if frame_index <= points[0][0]:
        return points[0][1:]
    if frame_index >= points[-1][0]:
        return points[-1][1:]
    for idx in range(len(points) - 1):
        start = points[idx]
        end = points[idx + 1]
        if start[0] <= frame_index <= end[0]:
            t = (frame_index - start[0]) / max(1, end[0] - start[0])
            return tuple(start[col] + (end[col] - start[col]) * t for col in range(1, 4))
    return points[-1][1:]


def clamp(value, low, high):
    return max(low, min(high, value))


def crop_zoom(frame, center_x, center_y, zoom):
    crop_w = int(SRC_W / zoom)
    crop_h = int(crop_w * OUT_H / OUT_W)
    if crop_h > SRC_H:
        crop_h = int(SRC_H / zoom)
        crop_w = int(crop_h * OUT_W / OUT_H)
    x0 = int(round(clamp(center_x - crop_w / 2, 0, SRC_W - crop_w)))
    y0 = int(round(clamp(center_y - crop_h / 2, 0, SRC_H - crop_h)))
    crop = Image.fromarray(frame[y0 : y0 + crop_h, x0 : x0 + crop_w], "RGB")
    return crop.resize((OUT_W, OUT_H), Image.Resampling.LANCZOS)


def place_cover(frame, box):
    x, y, w, h = box
    image = Image.fromarray(frame, "RGB")
    scale = max(w / image.width, h / image.height)
    resized = image.resize((int(image.width * scale), int(image.height * scale)), Image.Resampling.LANCZOS)
    left = (resized.width - w) // 2
    top = (resized.height - h) // 2
    return resized.crop((left, top, left + w, top + h))


def draw_text_box(draw, xy, text, text_font, fill=(255, 255, 255), accent=(255, 177, 35)):
    x, y = xy
    bbox = draw.textbbox((0, 0), text, font=text_font)
    pad_x = 18
    pad_y = 10
    draw.rounded_rectangle(
        (x, y, x + bbox[2] + pad_x * 2, y + bbox[3] + pad_y * 2),
        radius=8,
        fill=(0, 0, 0, 155),
        outline=accent,
        width=2,
    )
    draw.text((x + pad_x, y + pad_y), text, font=text_font, fill=fill)


def add_overlay(image, title=None, subtitle=None, tag=None, progress=None):
    image = image.convert("RGBA")
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    if tag:
        draw_text_box(draw, (54, 46), tag, FONT_SMALL)
    if title:
        bbox = draw.textbbox((0, 0), title, font=FONT_BIG)
        x = 54
        y = OUT_H - 142
        draw.rounded_rectangle((x - 18, y - 16, x + bbox[2] + 22, y + bbox[3] + 20), radius=10, fill=(0, 0, 0, 135))
        draw.text((x, y), title, font=FONT_BIG, fill=(255, 255, 255))
    if subtitle:
        draw.text((58, OUT_H - 82), subtitle, font=FONT_SMALL, fill=(235, 235, 235))
    if progress is not None:
        bar_w = int(OUT_W * 0.34)
        bar_h = 8
        x = OUT_W - bar_w - 54
        y = OUT_H - 58
        draw.rounded_rectangle((x, y, x + bar_w, y + bar_h), radius=4, fill=(255, 255, 255, 70))
        draw.rounded_rectangle((x, y, x + int(bar_w * progress), y + bar_h), radius=4, fill=(255, 177, 35, 210))
    return Image.alpha_composite(image, overlay).convert("RGB")


def fade(image, amount):
    if amount <= 0:
        return image
    black = Image.new("RGB", image.size, (0, 0, 0))
    return Image.blend(image, black, amount)


def clip_frame_indices(start, end, playback=1.0):
    source_count = max(1, end - start + 1)
    output_count = max(1, int(round(source_count / playback)))
    frames = []
    for out_idx in range(output_count):
        source_idx = int(round(start + out_idx * playback))
        frames.append(min(end, source_idx))
    return frames


def title_sequence(main_frames, duration=36):
    base = Image.fromarray(main_frames[105], "RGB").resize((OUT_W, OUT_H), Image.Resampling.LANCZOS)
    base = base.filter(ImageFilter.GaussianBlur(5))
    frames = []
    for i in range(duration):
        img = base.copy().convert("RGBA")
        dark = Image.new("RGBA", img.size, (0, 0, 0, 112))
        img = Image.alpha_composite(img, dark)
        draw = ImageDraw.Draw(img)
        title = "射门高光锦集"
        sub = "主视角 + 辅视角 | 人物放大 | 轨迹复盘"
        tw = draw.textbbox((0, 0), title, font=FONT_TITLE)[2]
        sw = draw.textbbox((0, 0), sub, font=FONT_MED)[2]
        draw.text(((OUT_W - tw) // 2, 410), title, font=FONT_TITLE, fill=(255, 255, 255))
        draw.text(((OUT_W - sw) // 2, 505), sub, font=FONT_MED, fill=(255, 207, 96))
        frames.append(fade(img.convert("RGB"), max(0, 0.45 - i / duration * 0.45)))
    return frames


def main_shooter_zoom(main_frames):
    out = []
    frame_ids = clip_frame_indices(72, 105, playback=0.75)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(MAIN_BALL, f)
        # Keep the shooter and ball in the same zoomed frame before release.
        center_x = clamp(ball_x + 95, 420, 920)
        center_y = clamp(ball_y - 72, 300, 530)
        zoom = 1.55 + 0.25 * (idx / max(1, len(frame_ids) - 1))
        image = crop_zoom(main_frames[f], center_x, center_y, zoom)
        image = add_overlay(
            image,
            "人物放大：起脚射门",
            "主视角慢放，聚焦起脚与球的离脚瞬间",
            "主视角 / SHOOTER ZOOM",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def main_ball_flight(main_frames):
    out = []
    frame_ids = clip_frame_indices(95, 145, playback=0.55)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(MAIN_BALL, f)
        center_x = clamp(ball_x + 26, 320, 900)
        center_y = clamp(ball_y - 28, 260, 560)
        zoom = 1.8
        image = crop_zoom(main_frames[f], center_x, center_y, zoom)
        image = add_overlay(
            image,
            "射门瞬间慢放",
            "加密球点后，球框贴住高速飞行轨迹",
            "主视角 / BALL FLIGHT",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def main_goal_finish(main_frames):
    out = []
    frame_ids = clip_frame_indices(130, 180, playback=0.8)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(MAIN_BALL, f)
        image = crop_zoom(main_frames[f], clamp(ball_x + 15, 280, 470), clamp(ball_y - 35, 300, 500), 1.72)
        image = add_overlay(
            image,
            "门前处理",
            "球进入门前区域，保留人物与球轨迹关系",
            "主视角 / FINISH",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def aux_ground_replay(aux_frames):
    out = []
    frame_ids = clip_frame_indices(35, 90, playback=0.7)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(AUX_BALL, f)
        image = crop_zoom(aux_frames[f], clamp(ball_x + 52, 330, 590), clamp(ball_y - 50, 190, 330), 1.75)
        image = add_overlay(
            image,
            "辅视角回放：球与人物关系",
            "低机位展示球贴地运动与人物站位",
            "辅视角 / LOW ANGLE",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def aux_close_ball(aux_frames):
    out = []
    frame_ids = clip_frame_indices(170, 204, playback=0.7)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(AUX_BALL, f)
        image = crop_zoom(aux_frames[f], clamp(ball_x - 35, 520, 780), clamp(ball_y + 45, 70, 190), 1.62)
        image = add_overlay(
            image,
            "辅视角补标段",
            "170 帧后球重新露出，补入高光回放",
            "辅视角 / CLOSE BALL",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def split_screen(main_frames, aux_frames):
    out = []
    count = 72
    for idx in range(count):
        main_f = min(145, 90 + int(idx * 55 / max(1, count - 1)))
        aux_f = min(90, 35 + int(idx * 55 / max(1, count - 1)))
        left = place_cover(main_frames[main_f], (0, 0, OUT_W // 2, OUT_H))
        right = place_cover(aux_frames[aux_f], (0, 0, OUT_W // 2, OUT_H))
        canvas = Image.new("RGB", (OUT_W, OUT_H), (0, 0, 0))
        canvas.paste(left, (0, 0))
        canvas.paste(right, (OUT_W // 2, 0))
        overlay = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)
        draw.rectangle((OUT_W // 2 - 2, 0, OUT_W // 2 + 2, OUT_H), fill=(255, 177, 35, 220))
        draw_text_box(draw, (52, 42), "主视角：射门轨迹", FONT_SMALL)
        draw_text_box(draw, (OUT_W // 2 + 52, 42), "辅视角：低位回放", FONT_SMALL)
        draw_text_box(draw, (52, OUT_H - 104), "双视角复盘", FONT_MED)
        canvas = Image.alpha_composite(canvas.convert("RGBA"), overlay).convert("RGB")
        out.append(apply_clip_fade(canvas, idx, count))
    return out


def ending(main_frames):
    base = crop_zoom(main_frames[110], 560, 485, 1.65)
    frames = []
    for i in range(42):
        image = base.copy().convert("RGBA")
        overlay = Image.new("RGBA", image.size, (0, 0, 0, 90))
        image = Image.alpha_composite(image, overlay)
        draw = ImageDraw.Draw(image)
        text = "高光完成"
        sub = "人物射门、球路、双视角回放已合成"
        tw = draw.textbbox((0, 0), text, font=FONT_TITLE)[2]
        sw = draw.textbbox((0, 0), sub, font=FONT_MED)[2]
        draw.text(((OUT_W - tw) // 2, 430), text, font=FONT_TITLE, fill=(255, 255, 255))
        draw.text(((OUT_W - sw) // 2, 520), sub, font=FONT_MED, fill=(255, 207, 96))
        frames.append(fade(image.convert("RGB"), i / 42 * 0.15))
    return frames


def apply_clip_fade(image, idx, count):
    fade_len = min(8, count // 4)
    amount = 0.0
    if fade_len > 0 and idx < fade_len:
        amount = max(amount, (fade_len - idx) / fade_len * 0.18)
    if fade_len > 0 and idx >= count - fade_len:
        amount = max(amount, (idx - (count - fade_len)) / fade_len * 0.18)
    return fade(image, amount)


def open_encoder():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    return subprocess.Popen(
        [
            FFMPEG,
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            f"{OUT_W}x{OUT_H}",
            "-r",
            str(FPS),
            "-i",
            "-",
            "-c:v",
            "h264_mf",
            "-b:v",
            "20M",
            "-pix_fmt",
            "yuv420p",
            "-movflags",
            "+faststart",
            str(OUTPUT),
        ],
        stdin=subprocess.PIPE,
    )


def prepare_output_frame(image):
    if image.mode != "RGB":
        image = image.convert("RGB")
    if image.size != (OUT_W, OUT_H):
        image = image.resize((OUT_W, OUT_H), Image.Resampling.LANCZOS)
    return image.filter(ImageFilter.UnsharpMask(radius=1.0, percent=115, threshold=3))


def write_encoded_frame(proc, image):
    assert proc.stdin is not None
    if image.mode != "RGB" or image.size != (OUT_W, OUT_H):
        raise ValueError(f"encoded frame must be RGB {OUT_W}x{OUT_H}, got {image.mode} {image.size}")
    proc.stdin.write(np.array(image, dtype=np.uint8).tobytes())


def close_encoder(proc):
    assert proc.stdin is not None
    proc.stdin.close()
    code = proc.wait()
    if code != 0:
        raise RuntimeError(f"ffmpeg failed with exit code {code}")


def make_contact_sheet(samples):
    thumbs = []
    for frame_index, image in samples[:9]:
        image = image.copy()
        image.thumbnail((420, 236))
        canvas = Image.new("RGB", (420, 264), (18, 18, 18))
        canvas.paste(image, (0, 0))
        draw = ImageDraw.Draw(canvas)
        draw.text((8, 242), f"montage frame {frame_index}", font=ImageFont.load_default(), fill=(245, 245, 245))
        thumbs.append(canvas)
    sheet = Image.new("RGB", (1260, 792), (18, 18, 18))
    for idx, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((idx % 3) * 420, (idx // 3) * 264))
    sheet.save(CONTACT_SHEET, quality=96)


def main():
    main_frames = read_scaled_frames(MAIN_VIDEO)
    aux_frames = read_scaled_frames(AUX_VIDEO)
    proc = open_encoder()
    samples = []
    last_frame = None
    frame_index = 0
    sections = [
        title_sequence,
        main_shooter_zoom,
        main_ball_flight,
        main_goal_finish,
        aux_ground_replay,
        aux_close_ball,
        split_screen,
        ending,
    ]
    try:
        for section in sections:
            if section in (aux_ground_replay, aux_close_ball):
                sequence = section(aux_frames)
            elif section is split_screen:
                sequence = section(main_frames, aux_frames)
            else:
                sequence = section(main_frames)
            for image in sequence:
                image = prepare_output_frame(image)
                if frame_index == 0 or frame_index % 60 == 0:
                    samples.append((frame_index, image.copy()))
                last_frame = image.copy()
                write_encoded_frame(proc, image)
                frame_index += 1
            del sequence
        if last_frame is not None:
            samples.append((frame_index - 1, last_frame))
        close_encoder(proc)
    except Exception:
        if proc.stdin:
            proc.stdin.close()
        proc.kill()
        raise
    make_contact_sheet(samples)
    print(OUTPUT)
    print(CONTACT_SHEET)


if __name__ == "__main__":
    main()
