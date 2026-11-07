# -*- coding: utf-8 -*-
"""第二轮机位扫描：曝光锚定白点，用 gamma 抬中间调。"""
import sys, pathlib, json, itertools
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / 'tools'))
import epd_preview as ep

D = pathlib.Path('evidence/M2/colorcheck')
raw = np.load(D / 'patches_raw.npy')
P, GAP, cols, rowsN = 32, 6, 10, 5
Wc = cols * P + (cols + 1) * GAP; Hc = rowsN * P + (rowsN + 1) * GAP
canvas = np.full((Hc, Wc, 3), 242 / 255.0)
pos = []
for i in range(50):
    r, c = divmod(i, cols)
    y0 = GAP + r * (P + GAP); x0 = GAP + c * (P + GAP)
    canvas[y0:y0 + P, x0:x0 + P] = raw[i] / 255.0
    pos.append((y0, y0 + P, x0, x0 + P))
wg = ep.white_gain(canvas)

def lab(a): return ep.lin_to_oklab(ep.srgb_to_lin(np.clip(a / 255.0, 0, 1)))
def dE00(a, b):
    q = a - b
    return float(np.sqrt(q[0] ** 2 + 2 * (q[1] ** 2 + q[2] ** 2)))

res = []
for expo, gamma, con in itertools.product([0.55, 0.60, 0.65], [1.00, 1.05, 1.10, 1.15, 1.20], [1.00, 1.05]):
    lin = ep.srgb_to_lin(ep.tone_map(canvas, expo * wg, gamma, con, 1.15))
    idx = ep.algo_fs_gm_lin(lin, 1.0, grain=1)
    out = ep.to_rgb(idx) * 255.0
    ds = [dE00(lab(raw[i]), lab(out[y0:y1, x0:x1].reshape(-1, 3).mean(0)))
          for i, (y0, y1, x0, x1) in enumerate(pos)]
    res.append((float(np.mean(ds)), expo, gamma, con))
    print(f"expo {expo:.2f} gamma {gamma:.2f} con {con:.2f} -> {np.mean(ds):.4f}", flush=True)
res.sort()
print("\nbest:"); [print(f"  {v:.4f}  expo {e:.2f} gamma {g:.2f} con {c:.2f}") for v, e, g, c in res[:6]]
json.dump(res, open(D / 'sweep2.json', 'w'))
