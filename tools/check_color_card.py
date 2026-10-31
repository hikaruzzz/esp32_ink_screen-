#!/usr/bin/env python3
"""对「实屏色卡照片」取样：四个色块相对纸白的中位数比值 vs 固件色板。

用法：
    python tools/check_color_card.py 色卡照片.jpg [-o 对照图.png]

步骤：
  1. 自动试 4 个旋转方向，找出「黑 黄 红 自左向右」且大致等高（或自下而上）
     的那个方向（手机竖拍横屏时经常是转过的）
  2. 在 1/4 缩图上用阈值找出黑/黄/红三个大色块，白块取黑块与黄块之间
  3. 取每个色块内部的中位数，除以同一张照片里的「白块中位数」
  4. 与 image_pipeline.c 的 k_pal_srgb 做同样的归一化后比较

方法沿用 docs/calibration.md：以「相对纸白」为准，抵消曝光与白平衡。
注意：夜间/混合光下拍的照片会把黑块抬高、把饱和色压灰（本工具会把这两项
的变化量打出来），所以判断色板是否需要改，请用白天自然光、正对屏幕的照片。
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw

import epd_preview  # 同一份 PALETTE，避免再抄一份色板

FRAME_W, FRAME_H = 768, 552
PALETTE = epd_preview.PALETTE
NAMES = ["黑", "白", "黄", "红"]
PATCH_W, PATCH_H, PATCH_Y, PATCH_X0, PATCH_DX = 150, 180, 110, 48, 174
PATCH_C = [(PATCH_X0 + i * PATCH_DX + PATCH_W / 2, PATCH_Y + PATCH_H / 2)
           for i in range(4)]     # frame 坐标下的四个色块中心


# ---------------------------------------------------------------- 检测

def blobs(mask: np.ndarray, min_area: int = 150):
    """返回所有连通块：[(cx, cy, area, w, h, fill)]。输入应是缩小过的图。"""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    out = []
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys, xs):
        if seen[y0, x0]:
            continue
        stack = [(y0, x0)]
        seen[y0, x0] = True
        n = 0
        sx = sy = 0.0
        x1 = x2 = x0
        y1 = y2 = y0
        while stack:
            y, x = stack.pop()
            n += 1
            sx += x
            sy += y
            x1, x2 = min(x1, x), max(x2, x)
            y1, y2 = min(y1, y), max(y2, y)
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True
                    stack.append((ny, nx))
        if n >= min_area:
            bw, bh = x2 - x1 + 1, y2 - y1 + 1
            out.append((sx / n, sy / n, n, bw, bh, n / float(bw * bh)))
    return out


def square_blob(mask: np.ndarray):
    """色块是实心、近似 150:180 的矩形；取满足这个形状的最大连通块。

    这样能自动跳过书上的黑字、粉色导线、PCB 阴影这些干扰。"""
    best = (0.0, 0.0, 0)
    for cx, cy, n, bw, bh, fill in blobs(mask):
        ratio = bw / float(bh)
        if 0.55 < ratio < 1.8 and fill > 0.65 and n > best[2]:
            best = (cx, cy, n)
    return best


def detect(img: Image.Image):
    """在 1/4 缩图上找黑/黄/红三块，返回各自 (x, y, 面积)。"""
    small = img.resize((img.width // 4, img.height // 4), Image.BOX)
    a = np.asarray(small).astype(np.float64)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    gray = a.mean(axis=2)

    masks = {
        "黑": gray < 70,
        "黄": (r > 140) & (r - b > 55) & (g > 80) & (g < 210),
        "红": (r - g > 25) & (r < 190) & (g < 120) & (b < 120),
    }
    return {k: square_blob(m) for k, m in masks.items()}


def order_ok(p: dict) -> bool:
    """黑 黄 红 沿同一方向排列，且相邻间距接近（色卡是等距的）。"""
    pts = [p["黑"], p["黄"], p["红"]]
    d1 = np.subtract(pts[1][:2], pts[0][:2])
    d2 = np.subtract(pts[2][:2], pts[1][:2])
    n1, n2 = np.linalg.norm(d1), np.linalg.norm(d2)
    if n1 < 20 or n2 < 20:
        return False
    cosang = float(d1 @ d2 / (n1 * n2))
    # 色卡顺序是 黑 白 黄 红：黑→黄 跨两格，黄→红 跨一格，所以比值应接近 2
    return cosang > 0.95 and 1.4 < n1 / n2 < 2.6


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("photo")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--rotate", type=int, default=None,
                    help="强制旋转角度（0/90/180/270，逆时针），默认自动")
    args = ap.parse_args()

    src = Image.open(args.photo).convert("RGB")
    angles = [args.rotate] if args.rotate is not None else [0, 90, 180, 270]
    chosen = None
    for ang in angles:
        img = src.rotate(ang, expand=True) if ang else src
        p = detect(img)
        if all(p[k][2] > 500 for k in ("黑", "黄", "红")) and order_ok(p):
            chosen = (ang, img, p)
            break
    if chosen is None:
        print("没能定位三个色块：请正对屏幕、让四个色块都拍全（可加 --rotate 90）")
        return 1
    ang, img, p = chosen
    scale = 4  # 检测是在 1/4 缩图上做的

    # 白块 = 黑块与黄块连线的中点
    white = ((p["黑"][0] + p["黄"][0]) / 2, (p["黑"][1] + p["黄"][1]) / 2)
    centers = {"黑": p["黑"][:2], "白": white, "黄": p["黄"][:2], "红": p["红"][:2]}

    a = np.asarray(img).astype(np.float64)
    box = int(PATCH_W * scale * 0.22)          # 取样半径：色块的 ~22%
    vals = {}
    for k, (cx, cy) in centers.items():
        x, y = int(cx * scale), int(cy * scale)
        vals[k] = np.median(a[y - box:y + box, x - box:x + box].reshape(-1, 3), axis=0)

    w = vals["白"]
    print(f"旋转 {ang}°，取样半径 {box}px")
    print("\n颜料  照片中位数        相对纸白            色板相对纸白")
    pal_rel = {n: PALETTE[i] / PALETTE[1] for i, n in enumerate(NAMES)}
    for k in NAMES:
        rel = vals[k] / w
        pr = pal_rel[k]
        print(f"{k}   {vals[k].round(0)!s:18s} "
              f"{rel[0]:.2f}/{rel[1]:.2f}/{rel[2]:.2f}   "
              f"{pr[0]:.2f}/{pr[1]:.2f}/{pr[2]:.2f}")

    black_lift = vals["黑"].mean() / w.mean()
    print(f"\n黑块/纸白 = {black_lift:.2f}（色板 0.16；>0.22 说明照片暗部被抬高，"
          f"彩度会被压低，建议白天重拍）")

    if args.out:
        left = img.copy()
        d = ImageDraw.Draw(left)
        for k, (cx, cy) in centers.items():
            x, y = cx * scale, cy * scale
            d.rectangle([x - box, y - box, x + box, y + box], outline=(0, 255, 0), width=4)
        card = Image.open(pathlib.Path(__file__).parent.parent /
                          "evidence/M2/color_card.png")
        left = left.resize((FRAME_W, int(left.height * FRAME_W / left.width)))
        out = Image.new("RGB", (FRAME_W * 2 + 16, max(left.height, card.height) + 28),
                        "white")
        out.paste(left, (0, 28))
        out.paste(card, (FRAME_W + 16, 28))
        ImageDraw.Draw(out).text((8, 8), "photo", fill=(0, 0, 0))
        ImageDraw.Draw(out).text((FRAME_W + 24, 8), "model", fill=(0, 0, 0))
        out.save(args.out)
        print("对照图:", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
