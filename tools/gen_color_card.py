#!/usr/bin/env python3
"""生成 768×552 的四色屏色卡原始帧（2bpp），实屏拍照时用来比色。

用法：
    python tools/gen_color_card.py                     # 写 evidence/M2/color_card.{png,2bpp}
    python tools/gen_color_card.py --png a.png --bin a.2bpp

帧格式与固件一致：192 B/行，每字节 4 像素、左侧像素在高位，共 105,984 B。
四个角上的 20×20 黑方块是拍照用的定位标，拍完可以做透视校正，再按下面
打印的色块坐标取样。
"""
import argparse
import pathlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 768, 552
ROW_BYTES = W // 4

# 与 image_pipeline.c 的 k_pal_srgb、网页 PAL 保持同一套实测近似值
PALETTE = np.array([
    [33, 32, 34],      # 0 黑
    [204, 204, 198],   # 1 白
    [217, 133, 9],     # 2 黄
    [109, 31, 22],     # 3 红
], dtype=np.uint8)

BLACK, WHITE, YELLOW, RED = 0, 1, 2, 3

# 色块：名字、颜色码、左上角坐标、宽高（取样取内部，不含 2 px 边框）
PATCH_W, PATCH_H = 150, 180
PATCH_Y = 110
PATCH_X = [48 + i * 174 for i in range(4)]
PATCHES = [
    ("黑", BLACK, PATCH_X[0]),
    ("白", WHITE, PATCH_X[1]),
    ("黄", YELLOW, PATCH_X[2]),
    ("红", RED, PATCH_X[3]),
]


def load_font(size: int) -> ImageFont.FreeTypeFont:
    for name in ("msyh.ttc", "msyhbd.ttc", "simhei.ttf"):
        try:
            return ImageFont.truetype(str(pathlib.Path("C:/Windows/Fonts") / name), size)
        except OSError:
            continue
    return ImageFont.load_default()


def draw_text(idx: np.ndarray, xy, text: str, size: int, code: int = BLACK) -> None:
    """用 PIL 画字，转成单色掩码后写进 2bpp 索引图。"""
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).text(xy, text, font=load_font(size), fill=255, anchor="mm")
    idx[np.array(mask) > 128] = code


def build() -> np.ndarray:
    idx = np.full((H, W), WHITE, dtype=np.uint8)

    # 四角定位标
    for x, y in ((10, 10), (W - 30, 10), (10, H - 30), (W - 30, H - 30)):
        idx[y:y + 20, x:x + 20] = BLACK

    draw_text(idx, (W // 2, 34), "四色墨水屏 色卡", 30)
    draw_text(idx, (W // 2, 64), "EPD 4-color chart", 17)

    for name, code, x in PATCHES:
        y = PATCH_Y
        idx[y + 2:y + PATCH_H - 2, x + 2:x + PATCH_W - 2] = code     # 内部纯色
        idx[y:y + PATCH_H, x:x + PATCH_W] = code                      # 含边框的底色
        idx[y:y + 2, x:x + PATCH_W] = BLACK                           # 上下左右 2 px 黑框
        idx[y + PATCH_H - 2:y + PATCH_H, x:x + PATCH_W] = BLACK
        idx[y:y + PATCH_H, x:x + 2] = BLACK
        idx[y:y + PATCH_H, x + PATCH_W - 2:x + PATCH_W] = BLACK
        draw_text(idx, (x + PATCH_W // 2, PATCH_Y + PATCH_H + 26),
                  f"{name}  #{code}", 24)

    draw_text(idx, (W // 2, 470), "正对屏幕、光线均匀拍照", 20)
    draw_text(idx, (W // 2, 505), "别开美颜 / 滤镜，四角黑标要拍全", 20)
    return idx


def pack(idx: np.ndarray) -> bytes:
    out = np.zeros((H, ROW_BYTES), dtype=np.uint8)
    for k in range(4):
        out |= (idx[:, k::4].astype(np.uint16) << (6 - 2 * k)).astype(np.uint8)
    return out.tobytes()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--png", default="evidence/M2/color_card.png")
    ap.add_argument("--bin", default="evidence/M2/color_card.2bpp")
    args = ap.parse_args()

    idx = build()
    frame = pack(idx)
    assert len(frame) == ROW_BYTES * H, len(frame)

    Image.fromarray(PALETTE[idx]).save(args.png)
    pathlib.Path(args.bin).write_bytes(frame)

    print(f"色卡帧 {len(frame)} B -> {args.bin}")
    print(f"预览 PNG -> {args.png}")
    for name, code, x in PATCHES:
        print(f"  {name} #{code}: x={x + 2}..{x + PATCH_W - 2}, "
              f"y={PATCH_Y + 2}..{PATCH_Y + PATCH_H - 2}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
