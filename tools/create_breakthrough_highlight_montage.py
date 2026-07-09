import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


FFMPEG = (
    r"D:\WeGameApps\rail_apps\wgprojectm(2002291)\ShadowTrackerExtra\Plugins"
    r"\ICreate\Source\ThirdParty\ICreatreLibrary\bin\recorder-release\ffmpeg.exe"
)

ROOT = Path(r"D:\football-auto-broadcast\football-auto-broadcast")
MAIN_VIDEO = ROOT / r"build\bin\output video\突破射门主视角\highlight_tracked_breakthrough_v12.mp4"
AUX_VIDEO = ROOT / r"build\bin\output video\突破射门辅视角\highlight_tracked_breakthrough_v12.mp4"
OUTPUT = ROOT / r"build\bin\output video\breakthrough_shot_highlight_montage_hq_v12.mp4"
CONTACT_SHEET = ROOT / r"build\bin\output video\breakthrough_shot_highlight_montage_hq_v12_contact_sheet.jpg"

FPS = 30
SRC_W = 1280
SRC_H = 720
OUT_W = 1920
OUT_H = 1080

MAIN_BALL = [
    (0, 262.0, 512.0, 17.0),
    (40, 306.0, 510.0, 16.0),
    (80, 288.0, 510.0, 16.0),
    (120, 262.0, 510.0, 16.0),
    (140, 252.0, 510.0, 16.0),
    (150, 258.0, 515.0, 16.0),
    (160, 238.0, 523.0, 16.0),
    (170, 220.0, 524.0, 16.0),
    (180, 218.0, 520.0, 16.0),
    (190, 225.0, 522.0, 16.0),
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

AUX_BALL = [
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
    (125, 580.0, 184.0, 13.0),
    (130, 580.0, 185.0, 14.0),
    (135, 614.0, 201.0, 18.0),
    (138, 676.0, 226.0, 23.0),
    (140, 740.0, 260.0, 29.0),
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


def apply_clip_fade(image, idx, count):
    fade_len = min(8, count // 4)
    amount = 0.0
    if fade_len > 0 and idx < fade_len:
        amount = max(amount, (fade_len - idx) / fade_len * 0.18)
    if fade_len > 0 and idx >= count - fade_len:
        amount = max(amount, (idx - (count - fade_len)) / fade_len * 0.18)
    return fade(image, amount)


def clip_frame_indices(start, end, playback=1.0):
    source_count = max(1, end - start + 1)
    output_count = max(1, int(round(source_count / playback)))
    return [min(end, int(round(start + idx * playback))) for idx in range(output_count)]


def title_sequence(main_frames, duration=36):
    base = Image.fromarray(main_frames[210], "RGB").resize((OUT_W, OUT_H), Image.Resampling.LANCZOS)
    base = base.filter(ImageFilter.GaussianBlur(5))
    frames = []
    for i in range(duration):
        img = base.copy().convert("RGBA")
        img = Image.alpha_composite(img, Image.new("RGBA", img.size, (0, 0, 0, 116)))
        draw = ImageDraw.Draw(img)
        title = "突破射门高光"
        sub = "主视角 + 辅视角 | 带球突破 | 射门瞬间 | 轨迹复盘"
        tw = draw.textbbox((0, 0), title, font=FONT_TITLE)[2]
        sw = draw.textbbox((0, 0), sub, font=FONT_MED)[2]
        draw.text(((OUT_W - tw) // 2, 410), title, font=FONT_TITLE, fill=(255, 255, 255))
        draw.text(((OUT_W - sw) // 2, 505), sub, font=FONT_MED, fill=(255, 207, 96))
        frames.append(fade(img.convert("RGB"), max(0, 0.45 - i / duration * 0.45)))
    return frames


def main_breakthrough_zoom(main_frames):
    out = []
    frame_ids = clip_frame_indices(140, 210, playback=0.72)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(MAIN_BALL, f)
        center_x = clamp(ball_x + 45, 180, 620)
        center_y = clamp(ball_y - 80, 300, 560)
        zoom = 1.55 + 0.25 * idx / max(1, len(frame_ids) - 1)
        image = crop_zoom(main_frames[f], center_x, center_y, zoom)
        image = add_overlay(
            image,
            "带球突破放大",
            "保留人与球的相对位置，突出过人前后的控球变化",
            "主视角 / BREAKTHROUGH",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def main_finish_zoom(main_frames):
    out = []
    frame_ids = clip_frame_indices(220, 280, playback=0.72)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(MAIN_BALL, f)
        center_x = clamp(ball_x + 80, 520, 1000)
        center_y = clamp(ball_y - 55, 300, 560)
        image = crop_zoom(main_frames[f], center_x, center_y, 1.7)
        image = add_overlay(
            image,
            "射门推进与门前处理",
            "跟随球路进入门前区域，保留防守与守门员位置关系",
            "主视角 / FINISH",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def aux_low_replay(aux_frames):
    out = []
    frame_ids = clip_frame_indices(60, 95, playback=0.65)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(AUX_BALL, min(f, 90))
        image = crop_zoom(aux_frames[f], clamp(ball_x + 35, 430, 730), clamp(ball_y + 8, 160, 320), 1.68)
        image = add_overlay(
            image,
            "辅视角低机位回放",
            "从低角度看突破前球与防守人的距离变化",
            "辅视角 / LOW ANGLE",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def aux_close_ball(aux_frames):
    out = []
    frame_ids = clip_frame_indices(132, 145, playback=0.45)
    for idx, f in enumerate(frame_ids):
        ball_x, ball_y, _ = interp(AUX_BALL, 140)
        image = crop_zoom(aux_frames[f], clamp(ball_x + 12, 520, 780), clamp(ball_y + 15, 220, 390), 1.55)
        image = add_overlay(
            image,
            "近端球点特写",
            "只保留清晰可见的近距离球点，避免遮挡段误标",
            "辅视角 / CLOSE BALL",
            idx / max(1, len(frame_ids) - 1),
        )
        out.append(apply_clip_fade(image, idx, len(frame_ids)))
    return out


def split_screen(main_frames, aux_frames):
    out = []
    count = 78
    for idx in range(count):
        main_f = min(300, 180 + int(idx * 120 / max(1, count - 1)))
        aux_f = min(140, 60 + int(idx * 80 / max(1, count - 1)))
        left = place_cover(main_frames[main_f], (0, 0, OUT_W // 2, OUT_H))
        right = place_cover(aux_frames[aux_f], (0, 0, OUT_W // 2, OUT_H))
        canvas = Image.new("RGB", (OUT_W, OUT_H), (0, 0, 0))
        canvas.paste(left, (0, 0))
        canvas.paste(right, (OUT_W // 2, 0))
        overlay = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)
        draw.rectangle((OUT_W // 2 - 2, 0, OUT_W // 2 + 2, OUT_H), fill=(255, 177, 35, 220))
        draw_text_box(draw, (52, 42), "主视角：突破到射门", FONT_SMALL)
        draw_text_box(draw, (OUT_W // 2 + 52, 42), "辅视角：低机位复盘", FONT_SMALL)
        draw_text_box(draw, (52, OUT_H - 104), "双视角复盘", FONT_MED)
        out.append(apply_clip_fade(Image.alpha_composite(canvas.convert("RGBA"), overlay).convert("RGB"), idx, count))
    return out


def ending(main_frames):
    base = crop_zoom(main_frames[260], 800, 500, 1.6)
    frames = []
    for i in range(42):
        image = base.copy().convert("RGBA")
        image = Image.alpha_composite(image, Image.new("RGBA", image.size, (0, 0, 0, 92)))
        draw = ImageDraw.Draw(image)
        text = "高光完成"
        sub = "突破、射门、球路与双视角回放已合成"
        tw = draw.textbbox((0, 0), text, font=FONT_TITLE)[2]
        sw = draw.textbbox((0, 0), sub, font=FONT_MED)[2]
        draw.text(((OUT_W - tw) // 2, 430), text, font=FONT_TITLE, fill=(255, 255, 255))
        draw.text(((OUT_W - sw) // 2, 520), sub, font=FONT_MED, fill=(255, 207, 96))
        frames.append(fade(image.convert("RGB"), i / 42 * 0.15))
    return frames


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
        draw.text((8, 242), f"breakthrough montage frame {frame_index}", font=ImageFont.load_default(), fill=(245, 245, 245))
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
        main_breakthrough_zoom,
        main_finish_zoom,
        aux_low_replay,
        aux_close_ball,
        split_screen,
        ending,
    ]
    try:
        for section in sections:
            if section in (aux_low_replay, aux_close_ball):
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
