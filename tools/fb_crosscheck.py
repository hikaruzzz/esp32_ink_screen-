"""跨端一致性检查：网页脚本（Node）vs Python 参考实现，逐像素比 2bpp 帧。

用法：python tools/fb_crosscheck.py <图片> [--algo fsgm]

开发板现在收的是手机算好的 2bpp 帧，所以「网页里的算法」和
「预览里的算法」必须是同一份结果；两边出现大分歧时这个检查会报出来。
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import epd_preview as ep  # noqa: E402

W, H = ep.W, ep.H
ROW_BYTES = W // 4


def pack_fb(idx: np.ndarray) -> np.ndarray:
    """2bpp 打包：每字节 4 像素，左侧像素在高位（与固件一致）。"""
    out = np.zeros((H, ROW_BYTES), dtype=np.uint8)
    for k in range(4):
        out |= (idx[:, k::4].astype(np.uint16) << (6 - 2 * k)).astype(np.uint8)
    return out


def js_style_linear(src: np.ndarray, expo: float, gamma: float,
                    con: float, sat: float) -> np.ndarray:
    """完全照 index.html 的整数路径算：8 位调色表 + 8 位饱和度，再线性化。

    网页里调色是先量化到 8 位再线性化的，和 epd_preview.tone_map 的纯浮点
    路径差一点点；误差扩散对这点差异很敏感，所以这里必须逐步对齐。
    """
    i = np.arange(256) / 255.0
    s = ep.lin_to_srgb(ep.srgb_to_lin(i) * expo)
    s = np.clip(0.5 + (s - 0.5) * con, 0.0, 1.0) ** (1.0 / gamma)
    tone = np.round(s * 255).astype(np.uint8)
    tl = ep.srgb_to_lin(tone / 255.0)

    r = src[..., 0].astype(np.int64)
    g = src[..., 1].astype(np.int64)
    b = src[..., 2].astype(np.int64)
    luma = (299 * r + 587 * g + 114 * b + 500) // 1000          # | 0
    ri = np.clip(np.floor(luma + (r - luma) * sat + 0.5), 0, 255).astype(np.int64)
    gi = np.clip(np.floor(luma + (g - luma) * sat + 0.5), 0, 255).astype(np.int64)
    bi = np.clip(np.floor(luma + (b - luma) * sat + 0.5), 0, 255).astype(np.int64)
    return np.stack([tl[ri], tl[gi], tl[bi]], axis=-1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    args = ap.parse_args()

    rgb = ep.cover_crop(Image.open(args.image))          # 0..1 sRGB, 768x552
    src = (np.clip(rgb, 0, 1) * 255).round().astype(np.uint8)

    lin = js_style_linear(src, 1.35, 1.10, 1.05, 1.25)
    idx_py = ep.algo_fs_gm_lin(lin, 1.0)
    fb_py = pack_fb(idx_py)

    with tempfile.TemporaryDirectory() as td:
        src_path = pathlib.Path(td) / "src.rgb"
        out_path = pathlib.Path(td) / "js.2bpp"
        src.tofile(src_path)
        proc = subprocess.run(
            ["node", str(pathlib.Path(__file__).parent / "fb_crosscheck.js"),
             str(src_path), str(out_path)],
            capture_output=True, text=True)
        if proc.returncode != 0:
            print(proc.stdout, proc.stderr)
            return 1
        print(proc.stdout.strip())
        fb_js = np.frombuffer(out_path.read_bytes(), dtype=np.uint8)

    if fb_js.size != fb_py.size:
        print(f"长度不一致：py={fb_py.size} js={fb_js.size}")
        return 1

    a = fb_py.reshape(-1)
    b = fb_js.reshape(-1)
    same_byte = float((a == b).mean())
    # 按像素比（一字节 4 像素）
    pa = np.stack([(a >> 6) & 3, (a >> 4) & 3, (a >> 2) & 3, a & 3], axis=1)
    pb = np.stack([(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3], axis=1)
    same_px = float((pa == pb).mean())
    print(f"字节一致 {same_byte * 100:.2f}%   像素一致 {same_px * 100:.2f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
