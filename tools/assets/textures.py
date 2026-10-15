"""
textures.py -- procedural "Soft Paper Peaks" textures for the game.

Everything is generated from seeded noise and simple shapes; there are no
source images. Each generator returns float arrays in [0,1] that gxtex.py
encodes into GX tiles.
"""

import math

import numpy as np
from PIL import Image, ImageDraw


# ---------------------------------------------------------------------------
# Periodic (tileable) noise
# ---------------------------------------------------------------------------

def _hash(ix, iy, seed):
    h = (ix.astype(np.uint64) * np.uint64(374761393) + iy.astype(np.uint64) * np.uint64(668265263)
         + np.uint64(seed) * np.uint64(2654435761)) & np.uint64(0xFFFFFFFF)
    h = ((h ^ (h >> np.uint64(13))) * np.uint64(1274126177)) & np.uint64(0xFFFFFFFF)
    return (h ^ (h >> np.uint64(16))).astype(np.float64) / 4294967295.0


def tile_noise(w, h, cells_x, cells_y, seed):
    """Smooth value noise that wraps seamlessly at the texture edges."""
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    fx = xs / w * cells_x
    fy = ys / h * cells_y
    x0 = np.floor(fx).astype(np.int64)
    y0 = np.floor(fy).astype(np.int64)
    tx, ty = fx - x0, fy - y0
    u = tx * tx * (3 - 2 * tx)
    v = ty * ty * (3 - 2 * ty)

    def g(ix, iy):
        return _hash(ix % cells_x, iy % cells_y, seed)

    a, b = g(x0, y0), g(x0 + 1, y0)
    c, d = g(x0, y0 + 1), g(x0 + 1, y0 + 1)
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v


def tile_fbm(w, h, cells, octaves, seed, aspect=(1, 1)):
    tot = np.zeros((h, w))
    amp, norm = 1.0, 0.0
    for i in range(octaves):
        cx = max(1, int(cells * aspect[0]) << i)
        cy = max(1, int(cells * aspect[1]) << i)
        tot += amp * tile_noise(w, h, cx, cy, seed + i * 31)
        norm += amp
        amp *= 0.5
    return tot / norm


def wrap_blur(img, r):
    """Box blur with wrap-around (keeps tileability)."""
    out = img
    for _ in range(2):
        acc = np.zeros_like(out)
        for dy in range(-r, r + 1):
            acc += np.roll(out, dy, axis=0)
        out = acc / (2 * r + 1)
        acc = np.zeros_like(out)
        for dx in range(-r, r + 1):
            acc += np.roll(out, dx, axis=1)
        out = acc / (2 * r + 1)
    return out


def normalise(img, mean=0.5, spread=0.12):
    z = (img - img.mean()) / (img.std() + 1e-9)
    return np.clip(mean + z * spread / 2.0, 0, 1)


# ---------------------------------------------------------------------------
# Paper detail (world-space modulation on terrain and props)
# ---------------------------------------------------------------------------

def paper_detail(size=256, seed=11):
    """Paper tooth + dry-brush streaks + colored-pencil hatching, centred on 0.5.

    The TEV multiplies by 2*value, so 0.5 = no change; the spread stays small
    (about +/-9 %) so the texture reads as surface, not pattern.
    """
    tooth = tile_fbm(size, size, 64, 2, seed)                     # fine grain
    blotch = tile_fbm(size, size, 4, 3, seed + 1)                 # soft washes
    streak = tile_fbm(size, size, 3, 3, seed + 2, aspect=(1, 10)) # dry brush (stretched)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    hatch = 0.5 + 0.5 * np.sin((xs + ys) * (2 * math.pi / size) * 48 + tooth * 3.0)
    hatch *= tile_fbm(size, size, 6, 2, seed + 3) > 0.55           # hatching only in patches
    img = 0.35 * tooth + 0.30 * blotch + 0.30 * streak - 0.05 * hatch
    return normalise(img, 0.5, 0.12)


# ---------------------------------------------------------------------------
# Film grain + paper fibres for the post pass
# ---------------------------------------------------------------------------

def grain(size=128, seed=21):
    rng = np.random.default_rng(seed)
    g = rng.normal(0, 1, (size, size))
    g = 0.7 * g + 0.3 * wrap_blur(g, 1) * 2.0
    fib = np.zeros((size, size))
    for _ in range(14):                                             # a few paper fibres
        x, y = rng.random(2) * size
        ang = rng.random() * math.pi
        ln = 6 + rng.random() * 16
        for t in np.linspace(0, ln, int(ln * 2)):
            fib[int(y + math.sin(ang) * t) % size, int(x + math.cos(ang) * t) % size] += 1.0
    img = g * 0.8 - fib * 0.35
    return normalise(img, 0.5, 0.055)


# ---------------------------------------------------------------------------
# Sun / lamp glow sprite
# ---------------------------------------------------------------------------

def glow(size=64):
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    c = (size - 1) / 2
    r = np.sqrt((xs - c) ** 2 + (ys - c) ** 2) / c
    core = np.clip(1 - r / 0.32, 0, 1) ** 0.6
    halo = np.exp(-(r / 0.55) ** 2) * 0.55
    img = np.clip(core + halo, 0, 1) * (r < 1)
    return img


# ---------------------------------------------------------------------------
# Cut-paper foliage atlas (IA8: intensity = painted detail, alpha = cut-out)
# ---------------------------------------------------------------------------

ATLAS_W, ATLAS_H = 512, 512
# name -> (x, y, w, h) in atlas pixels; the game reads these via foliage_atlas.h
ATLAS_CELLS = {
    "conifer0": (0, 0, 128, 256),
    "conifer1": (128, 0, 128, 256),
    "conifer2": (256, 0, 128, 256),
    "conifer3": (384, 0, 128, 256),
    "aspen":    (0, 256, 128, 256),
    "snag":     (128, 256, 128, 256),
    "grass":    (256, 256, 128, 128),
    "flowers":  (384, 256, 128, 128),
    "rock":     (256, 384, 128, 128),
    "fern":     (384, 384, 128, 128),
}


def _conifer(d, w, h, rng, ss):
    """Stacked, irregular, slightly drooping tiers -- a paper-cut pine."""
    cx = w / 2
    tiers = 7 + int(rng.random() * 3)
    top = h * 0.03
    base = h * 0.93
    trunk_w = w * 0.06
    d.rectangle([cx - trunk_w / 2, base - h * 0.08, cx + trunk_w / 2, h * 0.995], fill=255)
    for t in range(tiers):
        f0 = t / tiers
        f1 = (t + 1) / tiers
        y_top = top + (base - top) * f0 * 0.92
        y_bot = top + (base - top) * f1
        span = w * (0.10 + 0.40 * f1 ** 0.85) * (0.85 + 0.25 * rng.random())
        droop = (y_bot - y_top) * (0.15 + 0.25 * rng.random())
        pts = [(cx + rng.normal(0, w * 0.01), y_top)]
        for side in (-1, 1):
            k = 4
            side_pts = []
            for i in range(1, k + 1):
                fi = i / k
                side_pts.append((cx + side * span * fi * (0.9 + 0.2 * rng.random()),
                                 y_top + (y_bot - y_top) * fi * 0.85 + droop * fi ** 3 + rng.normal(0, h * 0.004)))
            if side == -1:
                pts += side_pts
                pts.append((cx - span * 0.35, y_bot - (y_bot - y_top) * 0.15))
                pts.append((cx, y_bot - (y_bot - y_top) * 0.05))
            else:
                pts += [(cx + span * 0.35, y_bot - (y_bot - y_top) * 0.15)] + side_pts[::-1]
        d.polygon(pts, fill=255)


def _aspen(d, w, h, rng, ss):
    cx = w / 2
    d.polygon([(cx - w * 0.035, h * 0.99), (cx - w * 0.02, h * 0.35), (cx + w * 0.02, h * 0.35),
               (cx + w * 0.035, h * 0.99)], fill=255)
    for _ in range(26):                                             # leaf clumps
        a = rng.random() * 2 * math.pi
        r = rng.random() ** 0.6
        px = cx + math.cos(a) * r * w * 0.34
        py = h * 0.36 + math.sin(a) * r * h * 0.24 - h * 0.04
        rr = w * (0.07 + 0.08 * rng.random())
        d.ellipse([px - rr, py - rr * 1.2, px + rr, py + rr * 1.2], fill=255)


def _snag(d, w, h, rng, ss):
    cx = w / 2
    lean = rng.normal(0, w * 0.03)
    d.polygon([(cx - w * 0.05, h * 0.995), (cx - w * 0.025 + lean, h * 0.12), (cx + lean, h * 0.08),
               (cx + w * 0.03 + lean, h * 0.14), (cx + w * 0.05, h * 0.995)], fill=255)
    for _ in range(9):                                              # broken branch stubs
        y = h * (0.2 + 0.6 * rng.random())
        side = 1 if rng.random() < 0.5 else -1
        ln = w * (0.08 + 0.18 * rng.random())
        x0 = cx + lean * (1 - y / h)
        d.line([(x0, y), (x0 + side * ln, y - ln * 0.5)], fill=255, width=max(1, int(w * 0.018)))


def _grass(d, w, h, rng, ss, flowers=False):
    for _ in range(70):
        x0 = w * (0.1 + 0.8 * rng.random())
        ln = h * (0.35 + 0.6 * rng.random())
        lean = rng.normal(0, w * 0.12)
        pts = [(x0 - w * 0.012, h), (x0 + lean * 0.5, h - ln * 0.6), (x0 + lean, h - ln),
               (x0 + lean * 0.55 + w * 0.006, h - ln * 0.6), (x0 + w * 0.012, h)]
        d.polygon(pts, fill=255)
    if flowers:
        for _ in range(16):
            x = w * (0.15 + 0.7 * rng.random())
            y = h * (0.1 + 0.45 * rng.random())
            r = w * (0.025 + 0.02 * rng.random())
            d.ellipse([x - r, y - r, x + r, y + r], fill=255)


def _rock(d, w, h, rng, ss):
    n = 9
    pts = []
    for i in range(n):
        a = math.pi + math.pi * i / (n - 1)
        r = 0.42 + 0.12 * rng.random()
        pts.append((w / 2 + math.cos(a) * w * r, h * 0.98 + math.sin(a) * h * r * 1.3))
    d.polygon(pts, fill=255)


def _fern(d, w, h, rng, ss):
    for k in range(7):
        ang = math.radians(-160 + 140 * k / 6)
        ln = h * (0.55 + 0.35 * rng.random())
        x0, y0 = w / 2, h * 0.98
        for t in np.linspace(0.1, 1, 9):
            x = x0 + math.cos(ang) * ln * t
            y = y0 + math.sin(ang) * ln * t
            lw = w * 0.05 * (1 - t) + 2
            d.ellipse([x - lw, y - lw * 0.6, x + lw, y + lw * 0.6], fill=255)


SHAPES = {
    "conifer0": _conifer, "conifer1": _conifer, "conifer2": _conifer, "conifer3": _conifer,
    "aspen": _aspen, "snag": _snag,
    "grass": lambda d, w, h, rng, ss: _grass(d, w, h, rng, ss, False),
    "flowers": lambda d, w, h, rng, ss: _grass(d, w, h, rng, ss, True),
    "rock": _rock, "fern": _fern,
}


def foliage_atlas(seed=31):
    """Returns (intensity, alpha) arrays for the foliage atlas.

    Intensity is centred near 0.5 (the TEV doubles it), with dry-brush
    streaks and a darker band just inside the cut edge -- the "paper edge".
    """
    rng = np.random.default_rng(seed)
    alpha = np.zeros((ATLAS_H, ATLAS_W))
    inten = np.full((ATLAS_H, ATLAS_W), 0.5)
    ss = 3
    for name, (x, y, w, h) in ATLAS_CELLS.items():
        img = Image.new("L", (w * ss, h * ss), 0)
        d = ImageDraw.Draw(img)
        SHAPES[name](d, w * ss, h * ss, rng, ss)
        a = np.asarray(img, dtype=np.float64) / 255.0
        a = a.reshape(h, ss, w, ss).mean(axis=(1, 3))
        # keep a 2px transparent margin so bilinear filtering never bleeds between cells
        a[:2, :] = 0
        a[-1:, :] *= 1.0
        a[:, :2] = 0
        a[:, -2:] = 0
        cell_i = tile_fbm(w, h, 3, 3, int(rng.integers(1 << 30)), aspect=(1, 4)) * 0.6 + \
            tile_fbm(w, h, 24, 2, int(rng.integers(1 << 30))) * 0.4
        cell_i = normalise(cell_i, 0.52, 0.16)
        # Darker paper edge: where the blurred alpha is below 1 inside the shape.
        soft = wrap_blur(a, 2)
        edge = np.clip((1.0 - soft) * 2.2, 0, 1) * (a > 0.5)
        cell_i = cell_i * (1 - 0.45 * edge)
        # Lighter toward the top (sky light), darker at the base (ground shadow).
        vy = np.linspace(1.06, 0.86, h)[:, None]
        cell_i = np.clip(cell_i * vy, 0, 1)
        alpha[y:y + h, x:x + w] = a
        inten[y:y + h, x:x + w] = cell_i
    return inten, alpha


# ---------------------------------------------------------------------------
# Backdrop ridge silhouettes (IA4: intensity detail, alpha = land)
# ---------------------------------------------------------------------------

def backdrop_layers(w=1024, h=64, layers=3, seed=41):
    """Each layer is a seamless 360-degree ridgeline. Stacked vertically."""
    rng = np.random.default_rng(seed)
    out_i = np.zeros((h * layers, w))
    out_a = np.zeros((h * layers, w))
    xs = np.arange(w) / w
    for li in range(layers):
        # Periodic 1D fbm via sums of sines with integer frequencies (wraps exactly).
        prof = np.zeros(w)
        amp = 1.0
        for k in range(1, 9):
            f = int(rng.integers(2, 4)) * k
            prof += amp * np.sin(2 * math.pi * f * xs + rng.random() * 6.28)
            amp *= 0.55
        prof = (prof - prof.min()) / (prof.max() - prof.min())
        if li == 0:
            prof = 1 - np.abs(prof * 2 - 1)                          # sharp far peaks
            prof = prof ** 1.6
        top = (0.10 + (1 - prof) * (0.55 if li == 0 else 0.45)) * h
        ys = np.arange(h)[:, None]
        a = np.clip((ys - top[None, :]) + 0.5, 0, 1)
        detail = tile_fbm(w, h, 32, 3, seed + li, aspect=(1, 0.25 if li == 0 else 0.5))
        if li == 0:
            # Pale ash gullies: soft vertical streaks running down from the peaks.
            gully = tile_fbm(w, h, 96, 2, seed + 9, aspect=(1, 0.06))
            detail = detail * 0.65 + gully * 0.35
        inten = normalise(detail, 0.5, 0.16)
        # Keep the layer's edge rows clean so bilinear filtering never bleeds
        # one stacked layer into the next (no seam lines along the horizon).
        a[:2, :] = 0.0
        a[-2:, :] = 1.0
        out_i[li * h:(li + 1) * h] = inten
        out_a[li * h:(li + 1) * h] = a
    return out_i, out_a
