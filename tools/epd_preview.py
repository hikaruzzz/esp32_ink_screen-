#!/usr/bin/env python3
"""四色墨水屏图片预处理模拟器（主机侧，不上板）。

用途：在电脑上把 JPEG 走一遍与固件相同的处理管线，输出 PNG 预览，
用来调参数、对比算法、复现问题，不必反复烧录和等 15 秒刷屏。

算法 --algo fsgm（手机网页用的是这一条，也是默认目标）：
  1. cover 裁切到 768x552
  2. sRGB 域：饱和度 → 曝光(线性域) → 对比度 → 伽马
  3. 转到 Oklab，把每个像素投影到「四色凸包最合适的边」上（丢弃印不出的色度）
  4. Floyd–Steinberg 误差扩散（蛇形扫描）选四色之一 → 2bpp 索引

其它 --algo 用于对照：edge（板上旧的蓝噪声管线）、fs（线性 RGB + FS）、
fsok（Oklab + FS、不做色域映射）、bayer4（更旧的 4x4 Bayer）。

用法：
  python tools/epd_preview.py 图片.jpg --algo fsgm -o out.png
  python tools/epd_preview.py 图片.jpg --exposure 1.4 --gamma 1.2 --contrast 1.1
  python tools/epd_preview.py 图片.jpg --grid-bug         # 复现 MCU 边界白格栅
  python tools/epd_preview.py 图片.jpg --scale 2          # 放大看观感
  python tools/fb_crosscheck.py 图片.png                  # 网页算法 vs 本实现
"""

from __future__ import annotations

import argparse
import sys

import numpy as np
from PIL import Image

W, H = 768, 552

# 面板四色（sRGB 近似值）。真实墨水屏的白不是 255、黑不是 0，
# 用理想色板会让抖动去追屏幕根本到不了的亮度，累积误差导致发灰发暗。
# 数值来自实拍照采样（docs/calibration.md）：以纸白为参考归一化。
# 必须与固件 image_pipeline.c 的 k_pal_srgb、网页 index.html 的 PAL 一致。
PALETTE = np.array([
    [33, 32, 34],     # EPD_BLACK  (0)
    [204, 204, 198],  # EPD_WHITE  (1)
    [217, 133, 9],    # EPD_YELLOW (2) —— 这块屏的黄偏橙
    [109, 31, 22],    # EPD_RED    (3)
], dtype=np.float64)

BAYER4 = np.array([
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
], dtype=np.float64)


# ---------------------------------------------------------------- 色彩工具

def srgb_to_lin(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.float64)
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x: np.ndarray) -> np.ndarray:
    x = np.clip(np.asarray(x, dtype=np.float64), 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055)


# ---------------------------------------------------------------- 蓝噪声

def _bn_kernel(n: int, sigma: float) -> np.ndarray:
    y, x = np.mgrid[0:n, 0:n]
    dy = np.minimum(y, n - y).astype(np.float64)
    dx = np.minimum(x, n - x).astype(np.float64)
    return np.exp(-(dy ** 2 + dx ** 2) / (2.0 * sigma * sigma))


def _energy(pattern: np.ndarray, kf: np.ndarray) -> np.ndarray:
    n = pattern.shape[0]
    return np.fft.irfft2(np.fft.rfft2(pattern.astype(np.float64)) * kf, s=(n, n))


def blue_noise(n: int = 16, sigma: float = 1.5, seed: int = 20261031) -> np.ndarray:
    """Ulichney void-and-cluster，返回 n x n 的 [0,1) 阈值矩阵。"""
    rng = np.random.default_rng(seed)
    kf = np.fft.rfft2(_bn_kernel(n, sigma))
    total = n * n
    p = rng.random((n, n)) < 0.1

    # 1) 反复把「最紧的簇」挪到「最大的空洞」，直到不动
    for _ in range(4000):
        if p.all() or not p.any():
            break
        e = _energy(p, kf)
        c = tuple(np.argwhere(p)[np.argmax(e[p])])
        p[c] = False
        e = _energy(p, kf)
        v = tuple(np.argwhere(~p)[np.argmin(e[~p])])
        if v == c:
            p[c] = True
            break
        p[v] = True

    # 2) 从「最紧的簇」开始逐个拔除，给 1..ones 排名
    rank = np.zeros((n, n), dtype=np.int64)
    ones = int(p.sum())
    for level in range(ones, 0, -1):
        e = _energy(p, kf)
        c = np.unravel_index(np.argmax(np.where(p, e, -np.inf)), (n, n))
        rank[c] = level
        p[c] = False

    # 3) 从空图开始逐个填「最大的空洞」，给 ones+1..total 排名
    #    候选只取还没排名的格子，否则会把第 2 步已排好的格子覆盖掉
    for level in range(ones + 1, total + 1):
        e = _energy(p, kf)
        free = (rank == 0) & ~p
        c = np.unravel_index(np.argmin(np.where(free, e, np.inf)), (n, n))
        rank[c] = level
        p[c] = True

    assert rank.min() >= 1 and rank.max() == total, "void-and-cluster 排名不完整"
    return (rank - 0.5) / float(total)


_BN16 = None


def noise_mask(shape, kind: str) -> np.ndarray:
    global _BN16
    h, w = shape
    if kind == "bayer4":
        return (BAYER4[np.arange(h) % 4][:, np.arange(w) % 4] + 0.5) / 16.0
    if _BN16 is None:
        # 量化成 0..255：与固件 s_bn / 网页 BN 用同一张表，预览才能逐像素对齐
        _BN16 = np.round(blue_noise() * 255) / 255.0
    n = _BN16.shape[0]
    return _BN16[np.arange(h) % n][:, np.arange(w) % n]


# ---------------------------------------------------------------- 管线

def cover_crop(img: Image.Image) -> np.ndarray:
    iw, ih = img.size
    s = max(W / iw, H / ih)
    sw, sh = W / s, H / s
    sx, sy = (iw - sw) / 2, (ih - sh) / 2
    img = img.convert("RGB").resize((W, H), Image.LANCZOS,
                                    box=(sx, sy, sx + sw, sy + sh))
    return np.asarray(img, dtype=np.float64) / 255.0


def tone_map(rgb: np.ndarray, exposure: float, gamma: float,
             contrast: float, sat: float) -> np.ndarray:
    """返回 sRGB 域的调色结果（0..1）。"""
    y = rgb @ np.array([0.299, 0.587, 0.114])
    rgb = np.clip(y[..., None] + (rgb - y[..., None]) * sat, 0.0, 1.0)

    lin = srgb_to_lin(rgb) * exposure                 # 曝光：线性域乘法
    rgb = lin_to_srgb(lin)

    rgb = np.clip(0.5 + (rgb - 0.5) * contrast, 0.0, 1.0)  # 对比度绕中灰
    rgb = np.clip(rgb, 0.0, 1.0) ** (1.0 / gamma)     # gamma>1 提亮中间调
    return rgb


def mix_edges_full(lin: np.ndarray, mask: np.ndarray) -> np.ndarray:
    pal = srgb_to_lin(PALETTE / 255.0)
    h, w, _ = lin.shape
    best_err = np.full((h, w), np.inf)
    best_a = np.zeros((h, w), dtype=np.int64)
    best_b = np.zeros((h, w), dtype=np.int64)
    best_w = np.zeros((h, w))

    for i in range(4):
        for j in range(i + 1, 4):
            d = pal[i] - pal[j]
            den = float(d @ d)
            if den < 1e-9:
                continue
            wgt = np.clip(((lin - pal[j]) @ d) / den, 0.0, 1.0)
            mix = pal[j] + wgt[..., None] * d
            err = ((lin - mix) ** 2).sum(axis=2)
            take = err < best_err
            best_err = np.where(take, err, best_err)
            best_a = np.where(take, i, best_a)
            best_b = np.where(take, j, best_b)
            best_w = np.where(take, wgt, best_w)

    # 这一格用 a，否则用 b
    return np.where(mask < best_w, best_a, best_b)


def algo_fs(srgb: np.ndarray, strength: float = 1.0) -> np.ndarray:
    """误差扩散（Floyd–Steinberg，蛇形扫描），线性光下最近色。

    手机浏览器会跑同一套算法后再上传，所以这里的输出 = 实屏效果。
    """
    pal = srgb_to_lin(PALETTE / 255.0)
    buf = srgb_to_lin(srgb).copy()
    h, w, _ = buf.shape
    idx = np.zeros((h, w), dtype=np.uint8)

    for y in range(h):
        step = 1 if y % 2 == 0 else -1
        for x in (range(w) if step > 0 else range(w - 1, -1, -1)):
            c = np.clip(buf[y, x], 0.0, 1.0)
            pal_d = ((pal - c) ** 2).sum(axis=1)
            k = int(pal_d.argmin())
            idx[y, x] = k
            err = (c - pal[k]) * strength
            if 0 <= x + step < w:
                buf[y, x + step] += err * (7 / 16)
            if y + 1 < h:
                if 0 <= x - step < w:
                    buf[y + 1, x - step] += err * (3 / 16)
                buf[y + 1, x] += err * (5 / 16)
                if 0 <= x + step < w:
                    buf[y + 1, x + step] += err * (1 / 16)
    return idx


def lin_to_oklab(lin: np.ndarray) -> np.ndarray:
    r, g, b = lin[..., 0], lin[..., 1], lin[..., 2]
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l_, m_, s_ = np.cbrt(l), np.cbrt(m), np.cbrt(s)
    L = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_
    A = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_
    B = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    return np.stack([L, A, B], axis=-1)


def algo_fs_ok(srgb: np.ndarray, strength: float = 1.0,
               clamp_err: float = 0.25) -> np.ndarray:
    """Oklab 里最近色 + 误差扩散；误差夹紧，避免不可达色（蓝/绿）漂移。"""
    pal = lin_to_oklab(srgb_to_lin(PALETTE / 255.0))
    buf = lin_to_oklab(srgb_to_lin(srgb)).copy()
    h, w, _ = buf.shape
    idx = np.zeros((h, w), dtype=np.uint8)

    for y in range(h):
        step = 1 if y % 2 == 0 else -1
        for x in (range(w) if step > 0 else range(w - 1, -1, -1)):
            c = buf[y, x]
            k = int(((pal - c) ** 2).sum(axis=1).argmin())
            idx[y, x] = k
            err = np.clip((c - pal[k]) * strength, -clamp_err, clamp_err)
            if 0 <= x + step < w:
                buf[y, x + step] += err * (7 / 16)
            if y + 1 < h:
                if 0 <= x - step < w:
                    buf[y + 1, x - step] += err * (3 / 16)
                buf[y + 1, x] += err * (5 / 16)
                if 0 <= x + step < w:
                    buf[y + 1, x + step] += err * (1 / 16)
    return idx


def ok_adjust(lab: np.ndarray, hue_deg: float = 0.0,
              temp: float = 0.0) -> np.ndarray:
    """色相 = 在 Oklab 色度平面上旋转；色温 = 沿 b 轴平移（正=黄暖，负=蓝冷）。

    与网页的 quality() → renderFinal() 完全对应；temp 的 1.0 对应 b +0.06。
    """
    if hue_deg == 0 and temp == 0:
        return lab
    r = np.deg2rad(hue_deg)
    c, s = np.cos(r), np.sin(r)
    a, b = lab[..., 1], lab[..., 2]
    out = lab.copy()
    out[..., 1] = a * c - b * s
    out[..., 2] = a * s + b * c + temp * 0.06
    return out


def ok_gamut_map(srgb: np.ndarray, hue: float = 0.0,
                 temp: float = 0.0) -> np.ndarray:
    """在 Oklab 里把每个像素投影到四色凸包最合适的一条边上。

    凸包外的颜色（蓝、绿、青）会被压到最近的可表示颜色 —— 丢掉的是
    根本印不出来的色度，亮度关系保留，所以蓝天不会变成一片白。
    """
    return ok_gamut_map_lin(srgb_to_lin(srgb), hue, temp)


def ok_gamut_map_lin(lin: np.ndarray, hue: float = 0.0,
                     temp: float = 0.0) -> np.ndarray:
    """同 ok_gamut_map，但输入直接是线性 RGB（0..1）。"""
    return ok_gamut_map_lab(ok_adjust(lin_to_oklab(lin), hue, temp))


def ok_gamut_map_lab(lab: np.ndarray) -> np.ndarray:
    """同 ok_gamut_map，但输入已经是 Oklab 坐标。"""
    pal = lin_to_oklab(srgb_to_lin(PALETTE / 255.0))

    best_err = np.full(lab.shape[:2], np.inf)
    best = lab.copy()
    for i in range(4):
        for j in range(i + 1, 4):
            d = pal[i] - pal[j]
            den = float(d @ d)
            if den < 1e-9:
                continue
            w = np.clip(((lab - pal[j]) @ d) / den, 0.0, 1.0)
            mix = pal[j] + w[..., None] * d
            err = ((lab - mix) ** 2).sum(axis=2)
            take = err < best_err
            best_err = np.where(take, err, best_err)
            best = np.where(take[..., None], mix, best)
    return best


def algo_fs_gm(srgb: np.ndarray, strength: float = 1.0,
               clamp_err: float = 0.25, jitter: float = 0.0,
               hue: float = 0.0, temp: float = 0.0) -> np.ndarray:
    """先色域映射到凸包，再做误差扩散 —— 抖动只负责混色，不再背着
    印不出来的颜色漂移。手机端用的是同一个算法。

    jitter：给误差加一点确定性的小扰动，打散平坦区域的横向条纹。
    hue/temp：色相（度）/ 色温（-1..1），在色域映射之前调整。
    """
    return algo_fs_gm_lin(srgb_to_lin(srgb), strength, clamp_err, jitter,
                          hue, temp)


def algo_fs_gm_lin(lin: np.ndarray, strength: float = 1.0,
                   clamp_err: float = 0.25, jitter: float = 0.0,
                   hue: float = 0.0, temp: float = 0.0) -> np.ndarray:
    """同 algo_fs_gm，但输入直接是线性 RGB（0..1）。"""
    pal = lin_to_oklab(srgb_to_lin(PALETTE / 255.0))
    buf = ok_gamut_map_lin(lin, hue, temp)
    h, w, _ = buf.shape
    idx = np.zeros((h, w), dtype=np.uint8)
    rng = np.random.default_rng(20261031)

    for y in range(h):
        step = 1 if y % 2 == 0 else -1
        for x in (range(w) if step > 0 else range(w - 1, -1, -1)):
            c = buf[y, x]
            k = int(((pal - c) ** 2).sum(axis=1).argmin())
            idx[y, x] = k
            err = (c - pal[k]) * strength
            if jitter > 0:
                err = err + (rng.random(3) - 0.5) * jitter
            err = np.clip(err, -clamp_err, clamp_err)
            if 0 <= x + step < w:
                buf[y, x + step] += err * (7 / 16)
            if y + 1 < h:
                if 0 <= x - step < w:
                    buf[y + 1, x - step] += err * (3 / 16)
                buf[y + 1, x] += err * (5 / 16)
                if 0 <= x + step < w:
                    buf[y + 1, x + step] += err * (1 / 16)
    return idx


def algo_bayer4(rgb: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """旧算法：±40 抖动 + 理想色板最近色（对照用）。"""
    ideal = np.array([[0, 0, 0], [255, 255, 255], [255, 200, 0], [190, 25, 40]],
                     dtype=np.float64) / 255.0
    off = (mask - 0.5) * 80.0 / 255.0
    v = np.clip(rgb + off[..., None], 0.0, 1.0)
    dist = ((v[..., None, :] - ideal[None, None, :, :]) ** 2).sum(axis=3)
    return dist.argmin(axis=2)


def apply_grid_bug(idx: np.ndarray) -> np.ndarray:
    """复现 image_decode.c 的 MCU 边界 bug：每个 16x16 块丢掉最后一行/列，
    帧缓冲预填白 → 全屏 16 像素周期的白色格栅。"""
    out = idx.copy()
    out[15::16, :] = 1
    out[:, 15::16] = 1
    return out


# ---------------------------------------------------------------- 输出

def to_rgb(idx: np.ndarray) -> np.ndarray:
    return np.clip(PALETTE[idx.astype(np.int64)] / 255.0, 0.0, 1.0)


def save(arr: np.ndarray, path: str, scale: int) -> None:
    im = Image.fromarray((np.clip(arr, 0, 1) * 255).round().astype(np.uint8))
    if scale > 1:
        im = im.resize((W * scale, H * scale), Image.NEAREST)
    im.save(path)


def main() -> int:
    ap = argparse.ArgumentParser(description="四色墨水屏图片预处理预览")
    ap.add_argument("image")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--algo", choices=["edge", "bayer4", "fs", "fsok", "fsgm"],
                    default="edge")
    ap.add_argument("--fs-strength", type=float, default=1.0,
                    help="误差扩散强度，0=不下传误差，1=标准 FS")
    ap.add_argument("--fs-jitter", type=float, default=0.0,
                    help="误差扰动幅度（Oklab 单位），用于打散条纹")
    ap.add_argument("--exposure", type=float, default=1.35)
    ap.add_argument("--gamma", type=float, default=1.10)
    ap.add_argument("--contrast", type=float, default=1.05)
    ap.add_argument("--sat", type=float, default=1.25)
    ap.add_argument("--temp", type=float, default=0.0,
                    help="色温 -1..1（正=偏暖黄，负=偏冷蓝），仅 fsgm")
    ap.add_argument("--hue", type=float, default=0.0,
                    help="色相旋转角度（度），仅 fsgm")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--grid-bug", action="store_true",
                    help="复现旧固件的 MCU 边界白格栅")
    ap.add_argument("--raw", action="store_true", help="同时输出 1:1 原尺寸")
    args = ap.parse_args()

    img = Image.open(args.image)
    rgb = cover_crop(img)

    if args.algo == "bayer4":
        idx = algo_bayer4(rgb, noise_mask(rgb.shape[:2], "bayer4"))
    elif args.algo == "fs":
        toned = tone_map(rgb, args.exposure, args.gamma, args.contrast, args.sat)
        idx = algo_fs(toned, args.fs_strength)
    elif args.algo == "fsok":
        toned = tone_map(rgb, args.exposure, args.gamma, args.contrast, args.sat)
        idx = algo_fs_ok(toned, args.fs_strength)
    elif args.algo == "fsgm":
        toned = tone_map(rgb, args.exposure, args.gamma, args.contrast, args.sat)
        idx = algo_fs_gm(toned, args.fs_strength, jitter=args.fs_jitter,
                         hue=args.hue, temp=args.temp)
    else:
        toned = tone_map(rgb, args.exposure, args.gamma, args.contrast, args.sat)
        lin = srgb_to_lin(toned)
        idx = mix_edges_full(lin, noise_mask(rgb.shape[:2], "blue"))

    if args.grid_bug:
        idx = apply_grid_bug(idx)

    hist = np.bincount(idx.ravel(), minlength=4)
    names = ["黑", "白", "黄", "红"]
    share = "  ".join(f"{names[i]} {hist[i] / idx.size * 100:5.1f}%"
                      for i in range(4))
    print(share)

    out = args.out or (args.image.rsplit(".", 1)[0] + f".{args.algo}.png")
    save(to_rgb(idx), out, args.scale)
    print("预览:", out)
    if args.raw:
        save(to_rgb(idx), out.replace(".png", ".1x.png"), 1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
