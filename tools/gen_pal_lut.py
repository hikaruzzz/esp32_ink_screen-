#!/usr/bin/env python3
"""生成固件用的四色量化查找表（tools/epd_photo/main/pal_lut.h）。

为什么要 LUT：ESP32-C3 是 RV32IMC，**没有硬件浮点**。逐像素跑「投影到六条边 +
误差比较」在软浮点下要 ~50 秒（实板实测 52 s，还会饿死 IDLE 触发任务看门狗）。
把「调色后的 sRGB(5:5:5) → 选哪两色、混合比例」预先算成表存 flash，
运行时只剩整数查表，单张图解码回到秒级。

输入：toned sRGB 各取高 5 位（bin 中心 8*q+4），索引 q = r<<10 | g<<5 | b。
输出：1 字节 = a(2b) | b(2b)<<2 | code(4b)<<4，code = round(w*15)。
      code > 蓝噪声阈值(0..15) 用 a 端，否则用 b 端。

色板来源：直接解析 image_pipeline.c 里的 k_pal_srgb，避免多处不同步。

用法：
  python tools/gen_pal_lut.py            # 生成 pal_lut.h
  python tools/gen_pal_lut.py --check    # 只做自检（与浮点参考实现对比）
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "epd_photo" / "main" / "image_pipeline.c"
OUT = ROOT / "tools" / "epd_photo" / "main" / "pal_lut.h"


def read_palette(path: Path) -> np.ndarray:
    text = path.read_text(encoding="utf-8")
    m = re.search(r"k_pal_srgb\[4\]\[3\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"在 {path} 里找不到 k_pal_srgb")
    triples = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", m.group(1))
    if len(triples) != 4:
        raise SystemExit(f"色板项数不对：{len(triples)}")
    return np.array([[int(v) for v in t] for t in triples], dtype=np.float64)


def srgb_to_lin(x: np.ndarray) -> np.ndarray:
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def edges(pal_srgb: np.ndarray):
    pal = srgb_to_lin(pal_srgb / 255.0)
    out = []
    for i in range(4):
        for j in range(i + 1, 4):
            d = pal[i] - pal[j]
            den = float(d @ d)
            out.append((pal[j], d, 1.0 / den if den > 1e-9 else 0.0, i, j))
    return out


def quantize(t: np.ndarray, ed: np.ndarray):
    """t: (...,3) 线性光；返回 (a, b, code)。"""
    best_err = np.full(t.shape[:-1], np.inf)
    best_a = np.zeros(t.shape[:-1], np.int64)
    best_b = np.zeros(t.shape[:-1], np.int64)
    best_w = np.zeros(t.shape[:-1])
    for pj, d, inv_den, i, j in ed:
        w = np.clip(((t - pj) @ d) * inv_den, 0.0, 1.0)
        err = ((t - (pj + w[..., None] * d)) ** 2).sum(axis=-1)
        take = err < best_err
        best_err = np.where(take, err, best_err)
        best_a = np.where(take, i, best_a)
        best_b = np.where(take, j, best_b)
        best_w = np.where(take, w, best_w)
    code = np.clip(np.round(best_w * 15.0), 0, 15).astype(np.int64)
    return best_a, best_b, code


def build(pal_srgb: np.ndarray) -> np.ndarray:
    q = np.arange(32)
    rep = np.minimum(q * 8 + 4, 255) / 255.0  # 每个 5 位 bin 的中心
    rr, gg, bb = np.meshgrid(rep, rep, rep, indexing="ij")   # r 高位在前
    t = srgb_to_lin(np.stack([rr, gg, bb], axis=-1))
    a, b, code = quantize(t, edges(pal_srgb))
    return (a | (b << 2) | (code << 4)).astype(np.uint8)


def main() -> int:
    ap = argparse.ArgumentParser(description="生成 pal_lut.h")
    ap.add_argument("--check", action="store_true", help="只自检，不写文件")
    args = ap.parse_args()

    pal = read_palette(SRC)
    lut = build(pal)

    # 自检：随机颜色上，LUT（5 位量化）与全精度浮点结果对比
    rng = np.random.default_rng(20261031)
    rgb255 = rng.integers(0, 256, (20000, 3))
    rgb = rgb255.astype(np.float64) / 255.0
    a_ref, b_ref, code_ref = quantize(srgb_to_lin(rgb), edges(pal))
    idx = ((rgb255[:, 0] >> 3) << 10) | ((rgb255[:, 1] >> 3) << 5) | (rgb255[:, 2] >> 3)
    packed = lut.reshape(-1)[idx]  # 一维展开再取，避免按第一轴高级索引
    a_lut, b_lut, code_lut = packed & 3, (packed >> 2) & 3, packed >> 4
    same_edge = (a_lut == a_ref) & (b_lut == b_ref)
    dc = np.abs(code_lut - code_ref)
    print(f"palette {pal.astype(int).tolist()}")
    print(f"edge agreement {same_edge.mean() * 100:.2f}%   "
          f"code mean |d| {dc.mean():.2f}/15   max {dc.max()}/15")

    if args.check:
        return 0

    lines = [
        "/* 自动生成，勿手改：python tools/gen_pal_lut.py",
        f" * 源色板（image_pipeline.c k_pal_srgb）：{pal.astype(int).tolist()}",
        " * 索引 (r>>3)<<10 | (g>>3)<<5 | (b>>3)；字节 = a | b<<2 | code<<4，",
        " * code=round(w*15)，w 为 a 端覆盖率。 */",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "static const uint8_t k_pal_lut[32768] = {",
    ]
    flat = lut.reshape(-1)
    for i in range(0, flat.size, 16):
        lines.append("    " + ",".join(str(int(v)) for v in flat[i:i + 16]) + ",")
    lines.append("};")
    lines.append("")

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"已写入 {OUT}（{flat.size} 项，{OUT.stat().st_size / 1024:.0f} KB）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
