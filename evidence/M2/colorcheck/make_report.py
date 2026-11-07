# -*- coding: utf-8 -*-
"""色卡对比报告：原版（cover 裁切，即面板上会看到的那一块）vs 四色管线输出。

度量用「抖动后块平均」——四色屏靠混色出中间调，眼睛看到的就是这个平均值。
第 1 行的色块中心落在裁切线以上，面板只露下半截，不参与统计。
"""
import sys, pathlib, json
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools'))
import epd_preview as ep

D = pathlib.Path('evidence/M2/colorcheck')
names = json.load(open(D / 'patches_names.json', encoding='utf-8'))
raw = np.load(D / 'patches_raw.npy')

rgb = ep.cover_crop(Image.open('docs/色卡-raw.jpg'))
src = (rgb * 255).round().astype(np.uint8)
wg = ep.white_gain(rgb)
lin = ep.srgb_to_lin(ep.tone_map(rgb, 0.60 * wg, 1.00, 1.05, 1.15))
out = (ep.to_rgb(ep.algo_fs_gm_lin(lin, 1.0, grain=1)) * 255).round().astype(np.uint8)
Image.fromarray(out).save(D / 'card_render.png')

s = max(768 / 1080, 552 / 869)
sx, sy = (1080 - 768 / s) / 2, (869 - 552 / s) / 2
def crd(rx, ry): return (rx - sx) * s, (ry - sy) * s

def lab(a): return ep.lin_to_oklab(ep.srgb_to_lin(np.clip(a / 255.0, 0, 1)))
def dE(a, b): return float(np.linalg.norm(a - b))
def dE00(a, b):
    d = a - b
    return float(np.sqrt(d[0] ** 2 + 2 * (d[1] ** 2 + d[2] ** 2)))
def hue(a): return float(np.degrees(np.arctan2(a[2], a[1])) % 360)

rows = [(r, c) for r in range(5) for c in range(9)] + [(5, c) for c in range(5)]
lines = [f"{'色块':<7}{'原色':>15}{'面板':>15}{'Δh':>7}{'ΔE':>7}{'ΔE00':>7}"]
vals = []
for i, (r, c) in enumerate(rows):
    if r == 0:
        continue
    cx, cy = crd(104 + 107.5 * c, 62 + 140.5 * r)
    x0, x1 = int(cx - 32), int(cx + 32)
    y0, y1 = max(0, int(cy - 32)), min(552, int(cy + 32))
    mean = out[y0:y1, x0:x1].reshape(-1, 3).mean(0)
    a, b = lab(raw[i]), lab(mean)
    dh = abs((hue(a) - hue(b) + 180) % 360 - 180)
    lines.append(f"{names[i]:<7}{str(raw[i].astype(int)):>15}{str(mean.round(0).astype(int)):>15}"
                 f"{dh:7.1f}{dE(a, b):7.3f}{dE00(a, b):7.3f}")
    vals.append((dE00(a, b), dE(a, b), dh, names[i]))

vals.sort(reverse=True)
lines.append("")
lines.append(f"平均 ΔE {np.mean([v[1] for v in vals]):.4f}   "
             f"ΔE00 {np.mean([v[0] for v in vals]):.4f}   "
             f"平均色相偏差 {np.mean([v[2] for v in vals]):.1f}°   共 {len(vals)} 块")
lines.append("最差 6 块（ΔE00）：" + "、".join(f"{n} {v:.3f}" for v, _, _, n in vals[:6]))
(D / 'patches_report.txt').write_text("\n".join(lines), encoding='utf-8')

pad = 10
im = Image.new('RGB', (768 * 2 + pad * 3, 552 + pad * 2 + 20), (24, 24, 24))
im.paste(Image.fromarray(src), (pad, pad + 20))
im.paste(Image.fromarray(out), (pad * 2 + 768, pad + 20))
d = ImageDraw.Draw(im)
d.text((pad + 4, 5), '原版色卡（面板可见区域）', fill=(235, 235, 235))
d.text((pad * 2 + 772, 5), '四色管线输出（0.60/1.00/1.05/1.15 颗粒 1）', fill=(235, 235, 235))
im.save(D / 'card_compare.png')
print("ok")
