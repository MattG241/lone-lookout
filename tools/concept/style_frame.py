#!/usr/bin/env python3
"""
style_frame.py -- "Soft Paper Peaks" style-target renderer (design phase).

This is NOT the game renderer and these are NOT Dolphin screenshots. It is a
2.5D painter's-algorithm mock-up that shows the look the GX renderer must hit,
using the same colour script (data/palette/time_of_day.json) the game will
use. Every technique here has a planned GX equivalent, noted inline:

    layered ridge silhouettes + valley haze  ->  hardware fog + backdrop rings
    two-tone lit/shadow flanks                ->  N.L ramp texture via normal texgen
    cut-paper drop shadows                    ->  baked darkening on the layer behind
    paper grain / brush streaks               ->  world-projected detail texture (TEV)
    softness, lift, tint, grain, vignette     ->  EFB copy filter + 3 fullscreen quads

Frames are rendered at 2x and box-downsampled to the Wii's 640x480, then
(optionally) upscaled 2x so they are easy to look at on a modern display.

Usage:
    python3 tools/concept/style_frame.py --out docs/concept
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "worldgen"))
from ashen_peaks import fbm, gauss_blur, smoothstep  # noqa: E402

PALETTE_PATH = os.path.join(ROOT, "data", "palette", "time_of_day.json")
FONT_PATH = "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf"

W, H = 640, 480     # Wii EFB resolution
SS = 2              # supersampling factor


def hexc(s):
    s = s.lstrip("#")
    return np.array([int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])


def lerp(a, b, t):
    return a + (b - a) * t


def load_palette():
    with open(PALETTE_PATH) as f:
        raw = json.load(f)
    keys = []
    for k in raw["keyframes"]:
        kk = dict(k)
        for c in ("sky_top", "sky_horizon", "fog", "sun", "shadow", "ambient"):
            kk[c] = hexc(k[c])
        keys.append(kk)
    albedo = {n: hexc(v) for n, v in raw["albedo"].items()}
    ui = {n: hexc(v) for n, v in raw["ui"].items()}
    grade = dict(raw["grade"])
    grade["lift"] = hexc(grade["lift"])
    grade["tint"] = hexc(grade["tint"])
    return keys, albedo, ui, grade


# ---------------------------------------------------------------------------
# 1D noise for ridgelines
# ---------------------------------------------------------------------------

def noise1(x, seed):
    xi = np.floor(x).astype(np.int64)
    f = x - xi
    rng = np.random.default_rng(seed)
    table = rng.random(4096)
    a = table[xi % 4096]
    b = table[(xi + 1) % 4096]
    u = f * f * (3 - 2 * f)
    return a * (1 - u) + b * u


def fbm1(x, seed, octaves=5):
    tot, amp, norm, fr = 0.0, 1.0, 0.0, 1.0
    for i in range(octaves):
        tot = tot + amp * noise1(x * fr, seed + 17 * i)
        norm += amp
        amp *= 0.5
        fr *= 2.0
    return tot / norm


# ---------------------------------------------------------------------------
# Scene description
# ---------------------------------------------------------------------------

def ridge_profile(xn, layer, seed):
    """Ridgeline y (fraction of H) for normalised x in [0,1]."""
    n = fbm1(xn * layer["freq"] + seed * 3.1, seed, 5)
    if layer.get("peaky"):
        r = 1.0 - np.abs(n * 2.0 - 1.0)       # ridged noise -> sharp summits
        y = layer["base"] - layer["amp"] * r ** 2.2
    else:
        y = layer["base"] - layer["amp"] * n
    y = y + layer.get("tilt", 0.0) * (xn - 0.5)
    if layer.get("knob"):
        kx, ky, kw = layer["knob"]
        bump = np.exp(-((xn - kx) / kw) ** 2)
        flat = np.exp(-((xn - kx) / (kw * 0.22)) ** 2)
        y = np.minimum(y, lerp(y, ky, bump))
        y = lerp(y, ky - 0.004, flat * 0.9)
    return y


LAYERS = [
    # far "Ashen Peaks" range beyond the district -- the source of the name
    dict(base=0.42, amp=0.17, freq=3.2, peaky=True, depth=0.00, albedo="ash_scree", trees=0,   streaks=True),
    dict(base=0.47, amp=0.06, freq=4.0, depth=0.22, albedo="rock",  trees=260, tree_h=(3, 6),   tree_band=0.05),
    dict(base=0.535, amp=0.05, freq=3.0, depth=0.42, albedo="pine", trees=220, tree_h=(5, 9),   tree_band=0.06, tilt=-0.03),
    dict(base=0.64, amp=0.04, freq=2.2, depth=0.62, albedo="pine", trees=170, tree_h=(8, 14),  tree_band=0.09,
         knob=(0.37, 0.455, 0.16), tower=0.37),
    dict(base=0.745, amp=0.05, freq=2.0, depth=0.82, albedo="pine_dark", trees=70, tree_h=(18, 34), tree_band=0.08, tilt=0.05),
    dict(base=0.865, amp=0.025, freq=1.6, depth=1.00, albedo="grass_autumn", trees=0, tilt=0.07, foreground=True),
]


def conifer_outline(cx, by, height, width, tiers, rng):
    """Cut-paper conifer: stacked jagged tiers. Returns (left_half, right_half)."""
    apex = (cx, by - height)
    left, right = [], []
    for t in range(tiers):
        f0 = (t + 1) / tiers
        y = by - height * (1.0 - f0) - height * 0.06
        w = width * (0.30 + 0.70 * f0) * (0.75 + 0.5 * rng.random())
        notch = height / tiers * 0.45
        droop = notch * 0.35 * rng.random()
        wl = w * (0.8 + 0.4 * rng.random())          # branches are never symmetric
        wr = w * (0.8 + 0.4 * rng.random())
        left += [(cx - wl * 0.45, y - notch * 0.7), (cx - wl, y + droop)]
        right += [(cx + wr * 0.45, y - notch * 0.7), (cx + wr, y + droop * 0.6)]
    trunk = (cx, by)
    left_half = [apex] + left + [(cx - width * 0.08, by), trunk]
    right_half = [apex, trunk, (cx + width * 0.08, by)] + right[::-1]
    return left_half, right_half


def pine_outline(cx, by, height, width, tiers, rng):
    """Large foreground pine: irregular branch clumps instead of even teeth."""
    apex = (cx + rng.normal(0, width * 0.03), by - height)
    sides = {-1: [], 1: []}
    for sgn in (-1, 1):
        pts = sides[sgn]
        for t in range(tiers):
            f0 = (t + 1) / tiers
            y = by - height * (1.0 - f0) - height * 0.05
            span = width * (0.25 + 0.75 * f0 ** 0.8) * (0.7 + 0.6 * rng.random())
            notch = height / tiers * (0.25 + 0.35 * rng.random())
            droop = notch * (0.2 + 0.5 * rng.random())
            inner = span * (0.25 + 0.2 * rng.random())
            pts.append((cx + sgn * inner, y - notch))
            # A clump: a few wobbly points out to a drooping tip.
            for k in range(1, 4):
                fk = k / 4.0
                pts.append((cx + sgn * (inner + (span - inner) * fk) + rng.normal(0, width * 0.02),
                            y - notch * (1 - fk) * 0.9 + rng.normal(0, notch * 0.12)))
            pts.append((cx + sgn * span, y + droop))
            pts.append((cx + sgn * span * 0.8, y + droop * 0.4))
    trunk = (cx, by)
    left_half = [apex] + sides[-1] + [(cx - width * 0.06, by), trunk]
    right_half = [apex, trunk, (cx + width * 0.06, by)] + sides[1][::-1]
    return left_half, right_half


def draw_tower(d, cx, top_y, s, fill, window, glow):
    """Kestrel Lookout: four splayed legs, cross bracing, a glazed cab."""
    leg_h, cab_w, cab_h = 30 * s, 18 * s, 11 * s
    base_y = top_y + leg_h + cab_h + 3 * s
    cab_top = top_y + 3 * s
    cab_bot = cab_top + cab_h
    lw = max(1, int(1.6 * s))
    for sx in (-1, 1):
        d.line([(cx + sx * 9 * s, base_y), (cx + sx * 6 * s, cab_bot)], fill=fill, width=lw)
        d.line([(cx + sx * 4 * s, base_y), (cx + sx * 3 * s, cab_bot)], fill=fill, width=max(1, lw - 1))
    for k in range(3):
        y0 = cab_bot + leg_h * k / 3.0
        y1 = cab_bot + leg_h * (k + 1) / 3.0
        w0 = 6 * s + 3 * s * k / 3.0
        w1 = 6 * s + 3 * s * (k + 1) / 3.0
        d.line([(cx - w0, y0), (cx + w1, y1)], fill=fill, width=max(1, lw - 1))
        d.line([(cx + w0, y0), (cx - w1, y1)], fill=fill, width=max(1, lw - 1))
    # catwalk + cab + roof
    d.rectangle([cx - cab_w * 0.68, cab_bot - 1 * s, cx + cab_w * 0.68, cab_bot + 1 * s], fill=fill)
    d.rectangle([cx - cab_w / 2, cab_top, cx + cab_w / 2, cab_bot], fill=fill)
    d.polygon([(cx - cab_w * 0.66, cab_top), (cx, cab_top - 6 * s), (cx + cab_w * 0.66, cab_top)], fill=fill)
    # windows: sky-reflective by day, warm lamp light at night
    for i in range(3):
        wx0 = cx - cab_w / 2 + 2 * s + i * (cab_w - 4 * s) / 3.0
        d.rectangle([wx0 + 0.6 * s, cab_top + 2.5 * s, wx0 + (cab_w - 4 * s) / 3.0 - 0.6 * s, cab_top + 7 * s],
                    fill=window)
    return (cx, cab_top + 5 * s), glow


def render(key, albedo, ui, grade, seed=4, with_ui=False, subtitle=None):
    rng = np.random.default_rng(seed)
    w, h = W * SS, H * SS
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    xn, yn = xs / w, ys / h

    # --- Sky ---------------------------------------------------------------
    t = np.clip(yn / 0.47, 0, 1) ** 0.85
    img = lerp(key["sky_top"][None, None, :], key["sky_horizon"][None, None, :], t[..., None])

    is_night = key["name"] == "Night"
    sun_pos = {
        "Dawn": (0.12, 0.50), "Morning": (-0.4, 0.0), "Midday": (0.3, -0.6),
        "Golden hour": (0.82, 0.30), "Dusk": (0.86, 0.47), "Night": (0.22, 0.17),
    }[key["name"]]
    d_sun = np.sqrt(((xn - sun_pos[0]) * 4 / 3) ** 2 + (yn - sun_pos[1]) ** 2)
    glow_r = 0.08 if is_night else 0.24
    glow = np.exp(-(d_sun / glow_r) ** 2) * (0.25 if is_night else 0.55)
    img = img + key["sun"][None, None, :] * glow[..., None] * (1.0 - 0.6 * t[..., None] * is_night)
    disc_r = 0.018 if is_night else 0.03
    disc = smoothstep(disc_r, disc_r * 0.75, d_sun)
    disc_col = lerp(key["sun"], np.ones(3), 0.7)
    img = np.maximum(img, lerp(img, disc_col[None, None, :] * 1.15, disc[..., None]))

    if is_night:
        star_y = rng.random(420) * 0.42 * h
        star_x = rng.random(420) * w
        b = rng.random(420) ** 3
        for sx, sy, sb in zip(star_x.astype(int), star_y.astype(int), b):
            img[sy:sy + SS, sx:sx + SS] += 0.15 + 0.55 * sb

    # Painted cloud strokes (dry brush): long soft streaks near the horizon.
    for cx, cy, L, T in ((0.22, 0.20, 0.20, 0.012), (0.55, 0.15, 0.26, 0.010),
                         (0.70, 0.25, 0.18, 0.009), (0.40, 0.29, 0.14, 0.007)):
        streak = fbm(xs / (w * 0.012), ys / (h * 0.0025), 3, seed=int(cx * 100))
        a = np.exp(-((xn - cx) / L) ** 2 * 3 - ((yn - cy) / T) ** 2) * smoothstep(0.35, 0.7, streak)
        ccol = lerp(key["sky_horizon"], key["sun"], 0.35) * (0.75 if is_night else 1.06)
        img = lerp(img, ccol[None, None, :], (a * 0.55)[..., None])

    # --- Ridge layers, far to near ------------------------------------------
    xcol = np.arange(w) / w
    tower_glow = None
    for li, layer in enumerate(LAYERS):
        ridge = ridge_profile(xcol, layer, seed * 13 + li) * h        # px, per column
        top = ridge[None, :]
        mask = smoothstep(top - 0.8, top + 0.8, ys)                     # antialiased fill

        depth = layer["depth"]
        alb = albedo[layer["albedo"]]
        fog_amt = key["fog_density"] * (1.0 - depth) ** 0.8 * 0.95

        # Two-tone flank lighting from the ridge slope (GX: N.L ramp texture).
        slope = np.gradient(ridge) / SS
        facing = np.tanh(slope * 4.0 * key["key_side"])                # -1 shadow .. +1 lit
        below = np.clip((ys - top) / (0.16 * h), 0, 1)
        lit_amt = lerp(0.5 + 0.5 * facing[None, :], 0.55, below)
        lit = alb * (key["ambient"] + key["sun"] * key["key_strength"])
        shd = lerp(alb * key["ambient"], key["shadow"], 0.35)
        col = lerp(shd[None, None, :], lit[None, None, :], lit_amt[..., None])

        # Brush streaks + paper tooth inside the cut-out.
        streak = fbm(xs / (w * 0.03), ys / (h * 0.004), 3, seed=200 + li)
        tooth = fbm(xs / 2.2, ys / 2.2, 2, seed=300 + li)
        col = col * (0.93 + 0.12 * streak[..., None]) * (0.97 + 0.05 * tooth[..., None])

        if layer.get("streaks"):
            # Pale ash gullies on the far peaks.
            gully = fbm(xs / (w * 0.006), ys / (h * 0.05), 3, seed=77)
            ga = smoothstep(0.62, 0.8, gully) * np.clip((ys - top) / (0.12 * h), 0, 1) * \
                (1 - np.clip((ys - top) / (0.25 * h), 0, 1))
            col = lerp(col, (alb * 1.25)[None, None, :], ga[..., None] * 0.6)

        # Valley haze: each layer fades into fog toward its base.
        haze = smoothstep(top, top + 0.11 * h, ys) * (0.35 + 0.65 * (1 - depth)) * key["fog_density"]
        col = lerp(col, key["fog"][None, None, :], np.clip(fog_amt + haze * 0.55, 0, 0.92)[..., None])

        # Trees for this layer (cut-paper, lit half / shadow half).
        tmask_lit = Image.new("L", (w, h), 0)
        tmask_shd = Image.new("L", (w, h), 0)
        td_l, td_s = ImageDraw.Draw(tmask_lit), ImageDraw.Draw(tmask_shd)
        placed = 0
        while placed < layer.get("trees", 0):
            tx = rng.random() * w
            # Forest stands, not a uniform sprinkle.
            stand = noise1(np.array([tx / w * 9.0 + li * 5.0]), 900 + li)[0]
            if rng.random() > smoothstep(0.30, 0.65, stand):
                continue
            placed += 1
            if layer.get("knob") and abs(tx / w - layer["knob"][0]) < 0.035:
                continue                                                # bare summit for the tower
            col_i = int(np.clip(tx, 0, w - 1))
            ty = ridge[col_i] + rng.random() ** 1.8 * layer["tree_band"] * h + 1.5 * SS
            th = (layer["tree_h"][0] + rng.random() * (layer["tree_h"][1] - layer["tree_h"][0])) * SS
            lh, rh = conifer_outline(tx, ty, th, th * 0.24, 3 + int(th / (7 * SS)), rng)
            lit_side, shd_side = (rh, lh) if key["key_side"] >= 0 else (lh, rh)
            td_l.polygon(lit_side, fill=255)
            td_s.polygon(shd_side, fill=255)

        tower_mask = None
        if layer.get("tower") is not None:
            tower_mask = Image.new("L", (w, h), 0)
            tcx = int(layer["tower"] * w)
            top_y = ridge[tcx] - (30 + 11 + 3) * SS
            tdraw = ImageDraw.Draw(tower_mask)
            _, _ = draw_tower(tdraw, tcx, top_y, SS, 255, 0, 0)
            # Windows drawn separately so they can glow.
            win_mask = Image.new("L", (w, h), 0)
            wd = ImageDraw.Draw(win_mask)
            draw_tower(wd, tcx, top_y, SS, 0, 255, 0)
            tower_glow = (tcx, top_y + 8 * SS, np.asarray(win_mask, dtype=np.float64) / 255.0, depth, fog_amt)

        tl = np.asarray(tmask_lit, dtype=np.float64) / 255.0
        ts = np.asarray(tmask_shd, dtype=np.float64) / 255.0
        full = np.clip(mask + tl + ts, 0, 1)
        if tower_mask is not None:
            full = np.clip(full + np.asarray(tower_mask, dtype=np.float64) / 255.0, 0, 1)

        # Cut-paper drop shadow onto everything behind (light from key side).
        off_x = int(-3 * SS * np.sign(key["key_side"] or 1))
        off_y = 3 * SS
        sh = shift(full, off_y, off_x)
        sh = gauss_blur(sh, 2 * SS) * (0.10 + 0.20 * depth) * (1 - fog_amt)
        img = img * (1 - sh[..., None])

        # Tree colours: darker than the ground, same fog treatment.
        tree_alb = albedo["pine_dark"] if depth > 0.3 else albedo["rock"] * 0.85
        t_lit = tree_alb * (key["ambient"] + key["sun"] * key["key_strength"] * 0.8)
        t_shd = lerp(tree_alb * key["ambient"] * 0.85, key["shadow"], 0.3)
        tfog = np.clip(fog_amt + 0.12 * (1 - depth), 0, 0.9)
        t_lit = lerp(t_lit, key["fog"], tfog)
        t_shd = lerp(t_shd, key["fog"], tfog)

        img = lerp(img, col, mask[..., None])
        img = lerp(img, t_shd[None, None, :], ts[..., None])
        img = lerp(img, t_lit[None, None, :], tl[..., None])

        if tower_mask is not None:
            tm = np.asarray(tower_mask, dtype=np.float64) / 255.0
            wood = albedo["tower_wood"] * (key["ambient"] * 0.9 + key["sun"] * key["key_strength"] * 0.3)
            wood = lerp(wood, key["fog"], fog_amt * 0.8)
            img = lerp(img, wood[None, None, :], tm[..., None])
            wm = tower_glow[2]
            day_win = lerp(key["sky_horizon"], key["fog"], 0.3) * 0.9
            win_col = lerp(day_win, albedo["window_light"] * 1.15, key["window_glow"])
            img = lerp(img, win_col[None, None, :], wm[..., None])
            if key["window_glow"] > 0.05:
                halo = gauss_blur(wm, 6 * SS) * 2.2 * key["window_glow"]
                img = img + (albedo["window_light"] * 0.5)[None, None, :] * halo[..., None]

        # Paper edge: a thin light line where each cut-out meets the sky/layer behind.
        edge = np.clip(full - shift(full, SS, 0), 0, 1)
        rim = lerp(np.ones(3) * 0.95, key["sun"], 0.6)
        img = lerp(img, rim[None, None, :], edge[..., None] * (0.22 + 0.25 * depth) * (1 - fog_amt))

        if layer.get("foreground"):
            img = foreground_details(img, ridge, key, albedo, rng, w, h)

    # --- Post: copy-filter softness, lift, tint, paper, grain, vignette -----
    img = downsample(img, SS)
    img = lerp(img, gauss_blur_rgb(img, 1), grade["softness"])
    img = lerp(img, grade["lift"][None, None, :], grade["lift_amount"])
    img = img * grade["tint"][None, None, :]
    py, px = np.mgrid[0:H, 0:W].astype(np.float64)
    paper = fbm(px / 1.5, py / 1.5, 2, seed=901) * 0.7 + fbm(px / 40.0, py / 40.0, 3, seed=902) * 0.3
    img = img * (0.955 + 0.06 * paper)[..., None]
    img = img + np.random.default_rng(seed + 1).normal(0, grade["grain"], (H, W))[..., None]
    r = np.sqrt(((px / W - 0.5) * 1.25) ** 2 + (py / H - 0.5) ** 2) / 0.72
    img = img * (1 - grade["vignette"] * smoothstep(0.45, 1.05, r))[..., None]
    out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")

    if with_ui:
        out = draw_ui(out, ui, subtitle)
    return out


def foreground_details(img, ridge, key, albedo, rng, w, h):
    """Ridge-top grass tufts, a few scree rocks, and a framing whitebark pine."""
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(pil, "RGBA")
    grass = albedo["grass_autumn"]
    lit = np.clip(grass * (key["ambient"] + key["sun"] * key["key_strength"]) * 1.1, 0, 1)
    shd = np.clip(lerp(grass * key["ambient"] * 0.8, key["shadow"], 0.3), 0, 1)

    def c255(c, a):
        return tuple(int(v * 255) for v in c) + (a,)

    # Rocks: angular two-tone scree.
    for _ in range(9):
        rx = rng.random() * w
        ry = ridge[int(rx) % w] + (0.02 + rng.random() * 0.08) * h
        rs = (6 + rng.random() * 12) * SS
        pts = [(rx + math.cos(a) * rs * (0.7 + 0.5 * rng.random()),
                ry + math.sin(a) * rs * 0.55 * (0.7 + 0.5 * rng.random()))
               for a in np.linspace(math.pi, 2 * math.pi, 6)]
        pts.append((rx + rs, ry + 2 * SS))
        pts.append((rx - rs, ry + 2 * SS))
        rock = albedo["rock"]
        d.polygon(pts, fill=c255(np.clip(lerp(rock * key["ambient"], key["shadow"], 0.3), 0, 1), 255))
        half = [p for p in pts if (p[0] - rx) * key["key_side"] >= 0] + [(rx, ry - rs * 0.4)]
        if len(half) >= 3:
            d.polygon(half, fill=c255(np.clip(rock * (key["ambient"] + key["sun"] * key["key_strength"]), 0, 1), 255))

    # Grass tufts: short curved pencil strokes, denser near the camera.
    for _ in range(1600):
        gx = rng.random() * w
        base = ridge[int(gx) % w]
        gy = base - 2 * SS + rng.random() ** 0.7 * (h - base)
        near = (gy - base) / max(1.0, (h - base))
        ln = (3 + near * 14 + rng.random() * 4) * SS
        lean = rng.normal(0.25 * key["key_side"], 0.35) * ln
        colr = lit if rng.random() < 0.55 else shd
        d.line([(gx, gy), (gx + lean * 0.5, gy - ln * 0.6), (gx + lean, gy - ln)],
               fill=c255(colr, int(150 + 90 * rng.random())), width=max(1, int(SS * (0.6 + near))))
    # Larkspur and yarrow flecks.
    for _ in range(140):
        fx = rng.random() * w
        base = ridge[int(fx) % w]
        fy = base + rng.random() * (h - base)
        c = albedo["larkspur"] if rng.random() < 0.5 else albedo["yarrow"]
        rr = SS * (1 + rng.random() * 1.5)
        d.ellipse([fx - rr, fy - rr, fx + rr, fy + rr], fill=c255(np.clip(c * (key["ambient"] + 0.4 * key["sun"]), 0, 1), 220))

    # Framing whitebark pine, cut off by the top of the frame.
    cx, by = 0.05 * w, h * 1.02
    lh, rh = pine_outline(cx, by, h * 1.08, w * 0.10, 8, rng)
    pine = albedo["pine_dark"] * 0.8
    pl = np.clip(pine * (key["ambient"] + key["sun"] * key["key_strength"] * 0.6), 0, 1)
    ps = np.clip(lerp(pine * key["ambient"] * 0.7, key["shadow"], 0.25), 0, 1)
    lit_side, shd_side = (rh, lh) if key["key_side"] >= 0 else (lh, rh)
    shadow_off = [(x - 4 * SS * np.sign(key["key_side"] or 1), y + 4 * SS) for x, y in lit_side + shd_side]
    d.polygon(shadow_off, fill=(0, 0, 0, 40))
    d.polygon(shd_side, fill=c255(ps, 255))
    d.polygon(lit_side, fill=c255(pl, 255))
    return np.asarray(pil, dtype=np.float64) / 255.0


def shift(a, dy, dx):
    """Translate a 2D mask without wrap-around (zero fill)."""
    out = np.zeros_like(a)
    hh, ww = a.shape
    ys0, ys1 = max(0, dy), min(hh, hh + dy)
    xs0, xs1 = max(0, dx), min(ww, ww + dx)
    out[ys0:ys1, xs0:xs1] = a[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
    return out


def downsample(img, f):
    hh, ww = img.shape[0] // f, img.shape[1] // f
    return img[:hh * f, :ww * f].reshape(hh, f, ww, f, 3).mean(axis=(1, 3))


def gauss_blur_rgb(img, r):
    return np.dstack([gauss_blur(img[..., c], r) for c in range(3)])


def draw_ui(img, ui, subtitle):
    """Minimal UI: a small hand-lettered subtitle with a soft shadow.

    The in-game font is the original 'Lookout Hand' stroke font (Phase 2);
    a system italic stands in for it in these concept frames only.
    """
    speaker, line = subtitle
    font = ImageFont.truetype(FONT_PATH, 15)
    font_b = font
    x0, y0 = 70, 418
    shadow = Image.new("L", img.size, 0)
    sd = ImageDraw.Draw(shadow)
    label = speaker + "  "
    sd.text((x0 + 1, y0 + 1), label + line, font=font, fill=200)
    shadow_np = gauss_blur(np.asarray(shadow, dtype=np.float64) / 255.0, 2) * 0.75
    base = np.asarray(img, dtype=np.float64) / 255.0
    base = base * (1 - shadow_np[..., None] * 0.8)
    img = Image.fromarray((np.clip(base, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img, "RGBA")
    spk_col = tuple(int(v * 255) for v in (ui["speaker_theo"] if speaker.startswith("Theo") else ui["speaker_dot"]))
    txt_col = tuple(int(v * 255) for v in ui["subtitle"])
    d.text((x0, y0), label, font=font_b, fill=spk_col + (255,))
    lw = d.textlength(label, font=font_b)
    d.text((x0 + lw, y0), line, font=font, fill=txt_col + (245,))
    # Tiny radio glyph: body, antenna, two "signal" arcs.
    rx, ry = x0 - 26, y0 + 2
    d.rounded_rectangle([rx, ry + 3, rx + 9, ry + 16], radius=2, outline=txt_col + (230,), width=1)
    d.line([(rx + 7, ry + 3), (rx + 7, ry - 4)], fill=txt_col + (230,), width=1)
    d.arc([rx + 9, ry + 1, rx + 17, ry + 11], -50, 50, fill=spk_col + (220,), width=1)
    d.arc([rx + 10, ry - 2, rx + 22, ry + 14], -45, 45, fill=spk_col + (160,), width=1)
    return img


def strip(keys, albedo, ui, grade, path):
    """Same view at six times of day, with each keyframe's swatches."""
    tw, th = 320, 240
    pad, lab = 14, 58
    cols, rows = 3, 2
    sheet = Image.new("RGB", (cols * tw + (cols + 1) * pad, rows * (th + lab) + (rows + 1) * pad + 40),
                      tuple(int(v * 255) for v in ui["paper"]))
    d = ImageDraw.Draw(sheet)
    f_t = ImageFont.truetype(FONT_PATH, 22)
    f_l = ImageFont.truetype(FONT_PATH, 16)
    f_s = ImageFont.truetype(FONT_PATH, 12)
    ink = tuple(int(v * 255) for v in ui["ink"])
    d.text((pad, 12), "Soft Paper Peaks - colour script (data/palette/time_of_day.json)", font=f_t, fill=ink)
    for i, k in enumerate(keys):
        frame = render(k, albedo, ui, grade, seed=4).resize((tw, th), Image.LANCZOS)
        cx = pad + (i % cols) * (tw + pad)
        cy = 40 + pad + (i // cols) * (th + lab + pad)
        sheet.paste(frame, (cx, cy))
        hh = int(k["time"])
        mm = int(round((k["time"] - hh) * 60))
        d.text((cx, cy + th + 6), f"{hh:02d}:{mm:02d}  {k['name']}", font=f_l, fill=ink)
        for j, (nm, c) in enumerate((("sky", k["sky_top"]), ("horizon", k["sky_horizon"]), ("fog", k["fog"]),
                                     ("key", k["sun"]), ("shadow", k["shadow"]))):
            sx = cx + j * 62
            d.rectangle([sx, cy + th + 30, sx + 16, cy + th + 46], fill=tuple(int(v * 255) for v in c), outline=ink)
            d.text((sx + 20, cy + th + 32), nm, font=f_s, fill=ink)
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "concept"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    keys, albedo, ui, grade = load_palette()
    golden = next(k for k in keys if k["name"] == "Golden hour")

    frame = render(golden, albedo, ui, grade, seed=4, with_ui=True,
                   subtitle=("Dot:", "Kestrel, Basin. You still with me up there?"))
    frame.save(os.path.join(args.out, "style_target_golden_hour_640x480.png"))
    frame.resize((W * 2, H * 2), Image.BILINEAR).save(os.path.join(args.out, "style_target_golden_hour.png"))

    night = next(k for k in keys if k["name"] == "Night")
    nframe = render(night, albedo, ui, grade, seed=4, with_ui=True,
                    subtitle=("Theo:", "Basin, Kestrel. Lamp's on. Going quiet for the night."))
    nframe.resize((W * 2, H * 2), Image.BILINEAR).save(os.path.join(args.out, "style_target_night.png"))

    strip(keys, albedo, ui, grade, os.path.join(args.out, "colour_script_time_of_day.png"))
    print("wrote concept frames to", args.out)


if __name__ == "__main__":
    main()
