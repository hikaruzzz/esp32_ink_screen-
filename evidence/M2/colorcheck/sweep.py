# -*- coding: utf-8 -*-
"""色卡参数扫描：把 50 个色块拼成一张图，跑完整管线（含抖动），
用「抖动后的块平均」和原色块比 Oklab ΔE00 —— 和眼睛看到的一致。"""
import sys, pathlib, json, itertools
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools'))
import epd_preview as ep

D = pathlib.Path('evidence/M2/colorcheck')
names = json.load(open(D / 'patches_names.json', encoding='utf-8'))
raw = np.load(D / 'patches_raw.npy')

P, GAP = 32, 6
cols, rowsN = 10, 5
Wc = cols * P + (cols + 1) * GAP
Hc = rowsN * P + (rowsN + 1) * GAP
canvas = np.full((Hc, Wc, 3), 242 / 255.0)
pos = []
for i in range(50):
    r, c = divmod(i, cols)
    y0 = GAP + r * (P + GAP); x0 = GAP + c * (P + GAP)
    canvas[y0:y0 + P, x0:x0 + P] = raw[i] / 255.0
    pos.append((y0, y0 + P, x0, x0 + P))

def lab(a): return ep.lin_to_oklab(ep.srgb_to_lin(np.clip(a / 255.0, 0, 1)))
def dE00(a, b):
    d = a - b
    return float(np.sqrt(d[0] ** 2 + 2 * (d[1] ** 2 + d[2] ** 2)))

wg = ep.white_gain(canvas)
print(f"canvas {Wc}x{Hc}, white_gain {wg:.3f}")
res = []
for expo, gamma, con, sat in itertools.product(
        [0.55, 0.60, 0.65, 0.70, 0.75], [0.95, 1.00], [1.00, 1.05], [1.00, 1.15, 1.30]):
    lin = ep.srgb_to_lin(ep.tone_map(canvas, expo * wg, gamma, con, sat))
    idx = ep.algo_fs_gm_lin(lin, 1.0, grain=1)
    out = ep.to_rgb(idx) * 255.0
    ds = []
    for i, (y0, y1, x0, x1) in enumerate(pos):
        m = out[y0:y1, x0:x1].reshape(-1, 3).mean(0)
        ds.append(dE00(lab(raw[i]), lab(m)))
    res.append((float(np.mean(ds)), expo, gamma, con, sat))
    print(f"expo {expo:.2f} g {gamma:.2f} c {con:.2f} s {sat:.2f}  dE00 {np.mean(ds):.4f}", flush=True)

res.sort()
print("\nbest:")
for v, e, g, c, s in res[:8]:
    print(f"  {v:.4f}  expo {e:.2f} gamma {g:.2f} con {c:.2f} sat {s:.2f}")
json.dump(res, open(D / 'sweep.json', 'w'))
