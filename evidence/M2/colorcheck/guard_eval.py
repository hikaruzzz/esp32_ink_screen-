# -*- coding: utf-8 -*-
"""绿色保护评估（修正版：调色输出必须先转线性再进 Oklab / 抖动）。"""
import sys, pathlib, json
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools'))
import epd_preview as ep

D = pathlib.Path('evidence/M2/colorcheck')
names = json.load(open(D / 'patches_names.json', encoding='utf-8'))
raw = np.load(D / 'patches_raw.npy')

pal = ep.lin_to_oklab(ep.srgb_to_lin(ep.PALETTE / 255.0))
BW0, BW1 = pal[0], pal[1]
d = BW1 - BW0
DEN = float(d @ d)

def hue(l): return np.degrees(np.arctan2(l[..., 2], l[..., 1])) % 360.0
def chroma(l): return np.hypot(l[..., 1], l[..., 2])

def make_guard(lo, hi, dh_min, c_min, cm_min):
    _orig = ep.ok_gamut_map_lab
    def guard(lab):
        m = _orig(lab)
        ct, cm = chroma(lab), chroma(m)
        dh = np.abs((hue(lab) - hue(m) + 180) % 360 - 180)
        t = np.clip((lab - BW0) @ d / DEN, 0.0, 1.0)
        neutral = BW0 + t[..., None] * d
        use = (ct > c_min) & (cm > cm_min) & (dh > dh_min) & (hue(lab) > lo) & (hue(lab) < hi)
        return np.where(use[..., None], neutral, m)
    guard._orig = _orig
    return guard

def render(guard=None):
    if guard is None:
        ep.ok_gamut_map_lab = make_guard(0, 0, 1e9, 9, 9)._orig
    else:
        ep.ok_gamut_map_lab = guard
    rgb = ep.cover_crop(Image.open('docs/色卡-raw.jpg'))
    wg = ep.white_gain(rgb)
    lin = ep.srgb_to_lin(ep.tone_map(rgb, 0.60 * wg, 1.00, 1.05, 1.15))
    idx = ep.algo_fs_gm_lin(lin, 1.0, grain=1)
    ep.ok_gamut_map_lab = ep.ok_gamut_map_lab._orig if hasattr(ep.ok_gamut_map_lab, '_orig') else ep.ok_gamut_map_lab
    return (ep.to_rgb(idx) * 255).round().astype(np.uint8)

s = max(768 / 1080, 552 / 869)
sx, sy = (1080 - 768 / s) / 2, (869 - 552 / s) / 2
def crd(rx, ry): return (rx - sx) * s, (ry - sy) * s

def lab(a): return ep.lin_to_oklab(ep.srgb_to_lin(np.clip(a / 255.0, 0, 1)))
def dE00(a, b):
    q = a - b
    return float(np.sqrt(q[0] ** 2 + 2 * (q[1] ** 2 + q[2] ** 2)))
def dE(a, b): return float(np.linalg.norm(a - b))

rows = [(r, c) for r in range(5) for c in range(9)] + [(5, c) for c in range(5)]
def score(img):
    ds = []
    for i, (r, c) in enumerate(rows):
        if r == 0: continue
        cx, cy = crd(104 + 107.5 * c, 62 + 140.5 * r)
        y0, y1 = max(0, int(cy - 32)), min(552, int(cy + 32))
        x0, x1 = int(cx - 32), int(cx + 32)
        m = img[y0:y1, x0:x1].reshape(-1, 3).mean(0)
        ds.append(dE00(lab(raw[i]), lab(m)))
    return float(np.mean(ds)), ds

# 原始真值：调色后的色相/色度
toned = ep.tone_map(np.clip(raw / 255.0, 0, 1), 0.60, 1.00, 1.05, 1.15)
lab_t = ep.lin_to_oklab(ep.srgb_to_lin(toned))
tmp = ep.ok_gamut_map_lab(lab_t[None, :, :])[0]     # 基线投影（无保护时）
dh = np.abs((hue(lab_t) - hue(tmp) + 180) % 360 - 180)
tbl = []
for i, n in enumerate(names):
    tbl.append(f"{n:<8}h={hue(lab_t[i]):6.1f} dh={dh[i]:6.1f} C={chroma(lab_t[i]):.3f} Cm={chroma(tmp[i]):.3f}")
(D / 'hue_table.txt').write_text("\n".join(tbl), encoding='utf-8')

base = render()
Image.fromarray(base).save(D / 'card_render.png')
b_mean, _ = score(base)
print(f"基线（无保护）  ΔE00 {b_mean:.4f}")

for tag, args in [('v1 75/45', (75, 210, 45, 0.030, 0.020)),
                  ('v2 110/30', (110, 210, 30, 0.030, 0.020)),
                  ('v3 110/45', (110, 210, 45, 0.030, 0.020))]:
    g = make_guard(*args)
    img = render(g)
    Image.fromarray(img).save(D / f'card_render_{tag.split()[0]}.png')
    m, _ = score(img)
    print(f"{tag:<12} ΔE00 {m:.4f}  (Δ {m - b_mean:+.4f})")
