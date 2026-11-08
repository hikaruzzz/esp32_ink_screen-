"""Audit the actual webpage renderer; no board, flashing, or new dependencies.

python tools/audit_epd_color.py
Outputs go to evidence/M2/color_audit/. Linear-light area averages are a model,
not measurements of the photographed panel (camera/illumination are unknown).
"""
import json
from pathlib import Path
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageDraw

import epd_preview as ep
from fb_crosscheck import js_style_linear


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/M2/color_audit"
PAL = ep.PALETTE.astype(np.uint8)
PLIN = ep.srgb_to_lin(PAL / 255.0)


def render(src, quality=None):
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        src.tofile(td / "src.rgb")
        args = ["node", str(ROOT / "tools/fb_crosscheck.js"),
                str(td / "src.rgb"), str(td / "frame.2bpp"), "1"]
        if quality is not None:
            (td / "quality.json").write_text(json.dumps(quality), encoding="utf-8")
            args.append(str(td / "quality.json"))
        proc = subprocess.run(args, capture_output=True, text=True, check=True)
        packed = np.frombuffer((td / "frame.2bpp").read_bytes(), dtype=np.uint8)
        assert packed.size == ep.W * ep.H // 4
        idx = np.stack([(packed >> s) & 3 for s in (6, 4, 2, 0)], axis=1)
        return idx.reshape(ep.H, ep.W), proc.stdout.strip()


def srgb8(lin):
    return np.round(ep.lin_to_srgb(lin) * 255).astype(np.uint8).tolist()


def delta_ok(a, b):
    return float(np.linalg.norm(ep.lin_to_oklab(a) - ep.lin_to_oklab(b)))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    neutral = dict(expo=1, gamma=1, con=1, sat=1, temp=0, hue=0)
    colors = [*PAL.tolist(), [100, 100, 100], [150, 150, 150],
              srgb8(PLIN.mean(axis=0)), srgb8((PLIN[0] + PLIN[1]) / 2)]
    src = np.full((ep.H, ep.W, 3), 255, dtype=np.uint8)
    # White reference prevents auto-white from changing the identity exposure.
    for i, color in enumerate(colors):
        src[8:, i * 96:(i + 1) * 96] = color
    idx, log = render(src, neutral)
    lines = ["Actual index.html renderer, neutral tone controls, grain=1", log,
             "Physical mean = encode_sRGB(mean(decode_sRGB(PAL[code])))",
             "delta_ok = Euclidean Oklab distance (NOT CIEDE2000)", ""]
    for i, color in enumerate(colors):
        block = idx[40:-40, i * 96 + 16:(i + 1) * 96 - 16]
        target = ep.srgb_to_lin(np.array(color) / 255.0)
        mean = PLIN[block].mean(axis=(0, 1))
        counts = np.bincount(block.ravel(), minlength=4) / block.size
        lines.append(f"target={color}; physical_mean={srgb8(mean)}; "
                     f"delta_ok={delta_ok(target, mean):.6f}; B/W/Y/R={counts.round(4).tolist()}")
        if i < 4:
            assert np.all(block == i), f"Pure palette color {i} does not round-trip"
        else:
            assert delta_ok(target, mean) < 0.015, f"Area mixing drift: {color}"
    exact = PLIN.mean(axis=0).reshape(1, 1, 3)
    mapped = ep.ok_gamut_map_lin(exact)[0, 0]
    assert np.allclose(ep.physical_gamut_map_lin(exact), exact, atol=1e-6)
    lines += ["", "Equal-area four-ink color is physically realizable:",
              f"sRGB={srgb8(exact[0, 0])}; edge-only projection delta_ok="
              f"{np.linalg.norm(mapped - ep.lin_to_oklab(exact)[0, 0]):.6f}", ""]
    Image.fromarray(src).save(OUT / "synthetic_input.png")
    Image.fromarray(PAL[idx]).save(OUT / "synthetic_render.png")

    original = Image.open(ROOT / "docs/色卡-raw.jpg")
    rgb = ep.cover_crop(original)
    src = np.round(rgb * 255).astype(np.uint8)
    idx, log = render(src)
    toned = js_style_linear(src, 0.60, 1, 1.05, 1.15)
    lines += ["Color card, actual default controls", log,
              f"auto-white gain={ep.white_gain(rgb):.6f}",
              "Patch: source / tone target / model physical mean / delta_ok(tone, output)"]
    # Centers follow this supplied chart, not arbitrary images. Inset avoids JPEG edges.
    scale = max(ep.W / original.width, ep.H / original.height)
    sx = (original.width - ep.W / scale) / 2
    sy = (original.height - ep.H / scale) / 2
    swatches = []
    for row in range(1, 6):
        for col in range(5 if row == 5 else 9):
            cx = (104 + 107.5 * col - sx) * scale
            cy = (62 + 140.5 * row - sy) * scale
            ys = slice(max(0, int(cy - 22)), min(ep.H, int(cy + 22)))
            xs = slice(int(cx - 22), int(cx + 22))
            source = ep.srgb_to_lin(src[ys, xs] / 255.0).mean(axis=(0, 1))
            target = toned[ys, xs].mean(axis=(0, 1))
            mean = PLIN[idx[ys, xs]].mean(axis=(0, 1))
            error = delta_ok(target, mean)
            lines.append(f"r{row + 1}c{col + 1}: {srgb8(source)} / {srgb8(target)} / "
                         f"{srgb8(mean)} / {error:.6f}")
            swatches.append((row + 1, col + 1, source, target, mean))
    comparison = Image.new("RGB", (900, 40 + len(swatches) * 24), "#202020")
    draw = ImageDraw.Draw(comparison)
    draw.text((12, 10), "patch     source (linear mean)       toned target          output (linear area mean)", fill="white")
    for i, (row, col, source, target, mean) in enumerate(swatches):
        y = 36 + i * 24
        draw.text((12, y + 3), f"r{row}c{col}", fill="white")
        for j, lin in enumerate((source, target, mean)):
            draw.rectangle((100 + j * 260, y, 345 + j * 260, y + 20), fill=tuple(srgb8(lin)))
    comparison.save(OUT / "patch_means.png")
    Image.fromarray(PAL[idx]).save(OUT / "card_web_render.png")
    (OUT / "report.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:16]))
    print(f"Evidence: {OUT}")


if __name__ == "__main__":
    main()
