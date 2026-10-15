#!/usr/bin/env python3
"""
make_icon.py -- the 128x48 Homebrew Channel icon, drawn from scratch:
a dusk gradient, layered paper ridges, the tower on its knob, and the title
in the game's own Lookout Hand font.

Usage: python3 tools/assets/make_icon.py out.png
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "font"))
import lookout_hand  # noqa: E402

W, H, SS = 128, 48, 4


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.png"
    w, h = W * SS, H * SS
    ys = np.linspace(0, 1, h)[:, None]
    top, hor = np.array([0.55, 0.60, 0.73]), np.array([0.95, 0.77, 0.56])
    img = (top * (1 - ys[..., None] ** 0.8) + hor * ys[..., None] ** 0.8) * np.ones((h, w, 3))

    pil = Image.fromarray((img * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(pil)
    layers = [(0.62, 0.10, (205, 170, 150)), (0.72, 0.07, (150, 125, 120)), (0.84, 0.05, (92, 98, 82))]
    for li, (base, amp, col) in enumerate(layers):
        pts = [(0, h)]
        for x in range(0, w + 1, 4):
            t = x / w
            y = base - amp * (0.5 + 0.5 * math.sin(t * (7 + li * 3) + li * 2.1)) * (0.6 + 0.4 * math.sin(t * 23 + li))
            if li == 2:
                y -= 0.10 * math.exp(-((t - 0.90) / 0.08) ** 2)      # Kestrel Knob
            pts.append((x, y * h))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    # Tower on the knob
    cx, top_y = int(0.90 * w), int(0.50 * h)
    ink = (60, 46, 40)
    for s in (-1, 1):
        d.line([(cx + s * 7 * SS, top_y + 22 * SS), (cx + s * 4 * SS, top_y + 8 * SS)], fill=ink, width=SS)
    d.rectangle([cx - 6 * SS, top_y + 1 * SS, cx + 6 * SS, top_y + 8 * SS], fill=ink)
    d.rectangle([cx - 4 * SS, top_y + 3 * SS, cx + 4 * SS, top_y + 6 * SS], fill=(242, 181, 107))
    d.polygon([(cx - 8 * SS, top_y + 1 * SS), (cx, top_y - 4 * SS), (cx + 8 * SS, top_y + 1 * SS)], fill=ink)
    pil = pil.resize((W, H), Image.LANCZOS)

    # Title in Lookout Hand
    canvas = np.asarray(pil, dtype=np.float64) / 255.0
    pen, base = 6.0, 22.0
    for i, ch in enumerate("Lone Lookout"):
        cov, left, top, adv = lookout_hand.render_glyph(ord(ch), 1.75, i % 2)
        x0, y0 = int(round(pen + left)), int(round(base + top))
        gh, gw = cov.shape
        x1, y1 = min(W, x0 + gw), min(H, y0 + gh)
        a = cov[: y1 - y0, : x1 - x0][..., None]
        canvas[y0:y1, x0:x1] = canvas[y0:y1, x0:x1] * (1 - a) + np.array([0.98, 0.95, 0.88]) * a
        pen += adv
    Image.fromarray((np.clip(canvas, 0, 1) * 255).astype(np.uint8), "RGB").save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
