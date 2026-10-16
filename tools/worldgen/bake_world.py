#!/usr/bin/env python3
"""
bake_world.py -- bake the Ashen Peaks (ashen_peaks.py) into game data.

Everything the game knows about the land comes from here, so the map in the
design document, the notebook map in the game and the ground you walk on are
the same map:

    world_height.bin   129 x 129 big-endian f32 heights (4 m grid)
    world_color.bin    2 x 129 x 129 RGBA8 vertex colours: summer, then autumn
    world_surface.bin  128 x 128 u8 ground type per cell (footsteps)
    world_trees.bin    8-byte records: x, z (u16, 1/64 m), kind, height (dm), yaw, tint
    world_tufts.bin    8-byte records: x, z (u16, 1/64 m), kind, size (cm), yaw, shade
    world_data.h       constants, places, trail and creek polylines
    tex_map (RGB565)   the notebook map, lettered in Lookout Hand

build_assets.py calls build(); run this file directly for a summary and a
preview of the placements.
"""

import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "font"))

import ashen_peaks as A  # noqa: E402
import lookout_hand  # noqa: E402

GRID = A.GRID_CELLS                  # 128 cells
VERTS = GRID + 1                     # 129 samples per side
CELL = A.GRID_SPACING                # 4 m
FINE = 2.0                           # metres per sample of the painting/placement field

# Must match surface_t, tree_kind and tuft_kind in src/world/world.h.
SURF_GRASS, SURF_DIRT, SURF_ROCK, SURF_WOOD, SURF_STAIR, SURF_NEEDLES, SURF_ASH, SURF_SCREE = range(8)
TREE_CONIFER0, TREE_ASPEN, TREE_SNAG = 0, 4, 5
(TUFT_GRASS, TUFT_FLOWERS, TUFT_ROCK, TUFT_FERN, TUFT_SAPLING, TUFT_FIREWEED, TUFT_SEDGE) = range(7)

# Places where nothing grows: props, sit spots, doorways (centre, radius).
CLEARINGS = [
    (A.KNOB_CENTRE, 14.0),
    (A.CABIN_BUILDING, 9.0),
    ((258.0, 440.0), 6.0),            # cabin yard / trail junction
    ((250.0, 498.0), 9.0),            # trailhead turnaround
    (A.GATE, 6.0),
    ((150.0, 150.0), 7.0),            # tarn dock
    ((403.0, 119.5), 5.0),            # Augie's bench
    ((398.0, 112.0), 5.0),            # overlook
    ((142.0, 318.0), 4.0),            # drift fence
    ((196.0, 214.0), 4.0),            # cold spring
]


def _pal():
    with open(os.path.join(ROOT, "data", "palette", "time_of_day.json")) as f:
        pal = json.load(f)

    def hexrgb(s):
        s = s.lstrip("#")
        return np.array([int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])
    return {k: hexrgb(v) for k, v in pal["albedo"].items()}


def _mix(c, col, w):
    """Lerp an (..., 3) image toward colour(s) col by weight w (...)."""
    w = np.clip(w, 0.0, 1.0)[..., None]
    return c * (1.0 - w) + np.asarray(col) * w


# ---------------------------------------------------------------------------
# Fields
# ---------------------------------------------------------------------------

class Fields:
    """Heights and masks on the fine (2 m) grid, plus the 4 m game grid."""

    def __init__(self):
        n = int(A.WORLD_SIZE / FINE) + 1
        xs = np.arange(n) * FINE
        self.x, self.z = np.meshgrid(xs, xs)
        self.h = A.height(self.x, self.z)
        self.mk = A.masks(self.x, self.z, self.h)
        self.wl = A.tarn_water_level()
        self.r = np.hypot(self.x - 256.0, self.z - 256.0) / 256.0
        d_ridge, _ = A.dist_to_segment(self.x, self.z, A.RIDGE_A, A.RIDGE_B)
        self.d_ridge = d_ridge
        step = int(CELL / FINE)
        self.step = step
        # The game grid samples the exact same heights.
        self.gh = self.h[::step, ::step].copy()

    def sample(self, field, x, z):
        """Nearest fine-grid sample of a field at world (x, z) arrays."""
        i = np.clip(np.rint(np.asarray(x) / FINE).astype(int), 0, field.shape[1] - 1)
        j = np.clip(np.rint(np.asarray(z) / FINE).astype(int), 0, field.shape[0] - 1)
        return field[j, i]


# ---------------------------------------------------------------------------
# Vertex colours (painted on the fine grid, filtered down to 4 m)
# ---------------------------------------------------------------------------

def paint(F, season):
    """Albedo for the whole map. season 0 = late summer, 1 = early autumn."""
    P = _pal()
    x, z, h, mk = F.x, F.z, F.h, F.mk
    wash = A.fbm(x / 45.0, z / 45.0, 3, seed=71)
    fine = A.fbm(x / 9.0, z / 9.0, 3, seed=72)

    # 1. Grass: drifting between green-gold and straw.
    t = np.clip((0.12 + 0.45 * wash) * (1 - season) + (0.70 + 0.35 * wash) * season, 0, 1)
    c = P["grass_summer"] * (1 - t[..., None]) + P["grass_autumn"] * t[..., None]
    damp = (1.0 - A.smoothstep(4.0, 18.0, mk["d_creek"])) * (0.45 - 0.25 * season)
    c = _mix(c, [0.52, 0.60, 0.42], damp)
    # Late summer still has green in the hollows; it drains to gold by autumn.
    green = A.smoothstep(0.42, 0.7, A.fbm(x / 60.0, z / 60.0, 3, seed=77)) * (1.0 - season) * 0.45
    c = _mix(c, [0.50, 0.58, 0.35], green)

    # 2. Needle duff under the forest.
    duff = (P["pine_dark"] * 0.7 + P["trail"] * 0.3) * 0.92
    c = _mix(c, duff * (0.92 + 0.16 * fine[..., None]), mk["forest"] ** 0.6 * 0.9)

    # 3. Meadow: paler, warmer grass with a larkspur haze in summer.
    meadow = np.array([0.80, 0.79, 0.55]) * (1 - season) + np.array([0.86, 0.77, 0.56]) * season
    c = _mix(c, meadow, mk["meadow"] * 0.6)
    lark = mk["meadow"] * A.smoothstep(0.55, 0.75, A.fbm(x / 14.0, z / 14.0, 3, seed=73))
    c = _mix(c, P["larkspur"], lark * (0.16 - 0.12 * season))

    # 4. The burn: grey ash ground, fireweed pink (rust in autumn).
    fire = np.array([0.74, 0.54, 0.58]) * (1 - season) + np.array([0.64, 0.46, 0.38]) * season
    burn_c = _mix(P["burn_ground"] * np.ones_like(c), fire,
                  0.35 * A.smoothstep(0.45, 0.7, A.fbm(x / 12.0, z / 12.0, 3, seed=74)))
    c = _mix(c, burn_c, mk["burn"] * 0.85)

    # 5. Rock on steep ground.
    rock = A.smoothstep(0.62, 0.95, mk["slope"] + (fine - 0.5) * 0.25)
    c = _mix(c, P["rock"] * (0.9 + 0.2 * fine[..., None]), rock)

    # 6. Cinder Ridge scree: pale ash with dark cinder patches; also the high rim.
    scree = A.smoothstep(70.0, 92.0, h) * (1.0 - A.smoothstep(40.0, 90.0, F.d_ridge))
    scree = np.maximum(scree, A.smoothstep(0.93, 1.05, F.r) * A.smoothstep(0.4, 0.8, mk["slope"]))
    c = _mix(c, P["ash_scree"], scree * 0.85)
    cinder = scree * A.smoothstep(0.58, 0.7, A.fbm(x / 8.0, z / 8.0, 3, seed=75))
    c = _mix(c, [0.43, 0.39, 0.38], cinder * 0.7)

    # 7. Mirror Tarn: dark lakebed under the water, a sedge ring on the shore.
    depth = F.wl - h
    near_tarn = A.dist_to_point(x, z, A.TARN_CENTRE) < 95.0
    lakebed = np.where(near_tarn, A.smoothstep(-0.2, 2.5, depth), 0.0)
    c = _mix(c, [0.33, 0.37, 0.33], lakebed)
    sedge = np.where(near_tarn, (1.0 - A.smoothstep(0.3, 2.2, -depth)) * A.smoothstep(-0.6, 0.0, -depth), 0.0)
    sedge_c = np.array([0.50, 0.57, 0.38]) * (1 - season) + np.array([0.64, 0.56, 0.40]) * season
    c = _mix(c, sedge_c, sedge * 0.7)

    # 8. The creek's wet margins.
    c = _mix(c, [0.40, 0.43, 0.36], (1.0 - A.smoothstep(1.5, 5.0, mk["d_creek"])) * 0.55)

    # 9. Wear: trails (the ribbon mesh draws the tread itself) and bare pads.
    c = _mix(c, P["trail"], (1.0 - A.smoothstep(0.5, 3.0, mk["d_trail"])) * 0.22)
    bare = np.zeros_like(h)
    for (cx, cz), r in (((256.0, 248.0), 9.0), ((250.0, 438.0), 10.0), ((250.0, 498.0), 8.0)):
        bare = np.maximum(bare, 1.0 - A.smoothstep(r * 0.5, r, A.dist_to_point(x, z, (cx, cz))))
    c = _mix(c, P["trail"] * 1.04, bare * 0.6)

    # 10. A slow paper wash over everything, like uneven watercolour.
    c *= (0.95 + 0.10 * A.fbm(x / 90.0, z / 90.0, 2, seed=76))[..., None]
    return np.clip(c, 0, 1)


def vertex_colours(F, season):
    """Filter the painting down to the 4 m grid and apply cavity AO."""
    c = paint(F, season)
    k = np.array([1.0, 2.0, 1.0])
    k2 = np.outer(k, k) / 16.0
    p = np.pad(c, ((1, 1), (1, 1), (0, 0)), mode="edge")
    blur = sum(k2[a, b] * p[a:a + c.shape[0], b:b + c.shape[1]] for a in range(3) for b in range(3))
    v = blur[::F.step, ::F.step]
    gh = F.gh
    hp = np.pad(gh, 1, mode="edge")
    lap = hp[:-2, 1:-1] + hp[2:, 1:-1] + hp[1:-1, :-2] + hp[1:-1, 2:] - 4.0 * gh
    ao = np.clip(1.0 - lap * 0.045, 0.80, 1.06)
    v = np.clip(v * ao[..., None], 0, 1)
    rgba = np.concatenate([v, np.ones(v.shape[:2] + (1,))], axis=2)
    return np.rint(rgba * 255).astype(np.uint8)


def surfaces(F):
    """Ground type per 4 m cell, sampled at cell centres (trails are added in game)."""
    mk = F.mk
    s = F.step
    c = slice(s // 2, None, s)
    scree = A.smoothstep(70.0, 92.0, F.h) * (1.0 - A.smoothstep(40.0, 90.0, F.d_ridge))
    out = np.full((GRID, GRID), SURF_GRASS, dtype=np.uint8)
    take = lambda a: a[c, c][:GRID, :GRID]  # noqa: E731
    out[take(mk["forest"]) > 0.45] = SURF_NEEDLES
    out[take(mk["burn"]) > 0.5] = SURF_ASH
    out[take(mk["slope"]) > 0.8] = SURF_ROCK
    out[take(scree) > 0.5] = SURF_SCREE
    for (cx, cz), r in (((256.0, 248.0), 8.0), ((250.0, 438.0), 9.0), ((250.0, 498.0), 8.0)):
        out[take(A.dist_to_point(F.x, F.z, (cx, cz))) < r] = SURF_DIRT
    return out


# ---------------------------------------------------------------------------
# Trees and tufts
# ---------------------------------------------------------------------------

def _clear(x, z):
    ok = np.ones(np.shape(x), dtype=bool)
    for c, r in CLEARINGS:
        ok &= A.dist_to_point(x, z, c) > r
    return ok


def _jitter_grid(rng, spacing):
    n = int(A.WORLD_SIZE / spacing)
    g = (np.arange(n) + 0.5) * spacing
    x, z = np.meshgrid(g, g)
    x = x.ravel() + rng.uniform(-0.45, 0.45, x.size) * spacing
    z = z.ravel() + rng.uniform(-0.45, 0.45, z.size) * spacing
    keep = (x > 1) & (x < A.WORLD_SIZE - 1) & (z > 1) & (z < A.WORLD_SIZE - 1)
    return x[keep], z[keep]


def _space_out(x, z, min_d):
    """Greedy Poisson-disk thinning with a hash grid."""
    cell = min_d
    grid = {}
    keep = np.zeros(len(x), dtype=bool)
    for i in range(len(x)):
        gx, gz = int(x[i] // cell), int(z[i] // cell)
        ok = True
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for j in grid.get((gx + dx, gz + dz), ()):
                    if (x[i] - x[j]) ** 2 + (z[i] - z[j]) ** 2 < min_d * min_d:
                        ok = False
                        break
                if not ok:
                    break
            if not ok:
                break
        if ok:
            keep[i] = True
            grid.setdefault((gx, gz), []).append(i)
    return keep


def place_trees(F, seed=1998):
    rng = np.random.default_rng(seed)
    x, z = _jitter_grid(rng, 5.0)
    mk = {k: F.sample(v, x, z) for k, v in F.mk.items()}
    h = F.sample(F.h, x, z)
    u = rng.random(len(x))
    kind = np.full(len(x), -1)

    p_con = mk["forest"] ** 1.4 * 0.62
    p_asp = mk["aspen"] * 0.9
    p_snag = mk["burn"] ** 1.5 * 0.40
    ridge = (1.0 - A.smoothstep(30.0, 80.0, F.sample(F.d_ridge, x, z))) * A.smoothstep(60.0, 80.0, h)
    p_white = ridge * (1.0 - A.smoothstep(0.6, 0.9, mk["slope"])) * 0.10

    # The rim: sparse, unreachable stands on the steep inner walls of the bowl,
    # so the skyline reads as forested mountainside rather than bare slope.
    hh = F.sample(F.h, x, z)
    rr = F.sample(F.r, x, z)
    rim = A.smoothstep(0.72, 0.9, rr) * (1.0 - A.smoothstep(1.3, 2.2, mk["slope"]))
    rim *= 1.0 - A.smoothstep(150.0, 185.0, hh)                     # treeline
    rim *= A.smoothstep(0.35, 0.65, A.fbm(x / 40.0, z / 40.0, 3, seed=42))
    p_con = np.maximum(p_con, rim * 0.5 * (mk["slope"] > 0.9))

    kind = np.where(u < p_con, 0, kind)
    kind = np.where((kind < 0) & (u < p_con + p_asp), TREE_ASPEN, kind)
    kind = np.where((kind < 0) & (u < p_con + p_asp + p_snag), TREE_SNAG, kind)
    kind = np.where((kind < 0) & (u < p_con + p_asp + p_snag + p_white), 10, kind)   # whitebark

    ok = (kind >= 0) & _clear(x, z) & (mk["d_trail"] > 5.0) & (mk["d_creek"] > 3.0) & ~mk["water"]
    ok &= (h > F.wl + 0.8) | (A.dist_to_point(x, z, A.TARN_CENTRE) > 95.0)
    ok &= mk["slope"] < 2.2
    x, z, kind, h = x[ok], z[ok], kind[ok], h[ok]
    order = rng.permutation(len(x))
    x, z, kind = x[order], z[order], kind[order]
    keep = _space_out(x, z, 3.4)
    x, z, kind = x[keep], z[keep], kind[keep]

    n = len(x)
    height = np.empty(n)
    conifer = kind == 0
    kind = np.where(conifer, rng.integers(0, 4, n), kind)
    height[conifer] = rng.uniform(9.0, 18.0, conifer.sum())
    asp = kind == TREE_ASPEN
    height[asp] = rng.uniform(7.0, 11.5, asp.sum())
    snag = kind == TREE_SNAG
    height[snag] = rng.uniform(6.0, 13.0, snag.sum())
    white = kind == 10
    kind = np.where(white, rng.integers(0, 4, n), kind)
    height[white] = rng.uniform(4.5, 7.5, white.sum())
    yaw = rng.integers(0, 256, n)
    tint = rng.integers(0, 256, n)
    return x, z, kind.astype(int), height, yaw, tint


def place_tufts(F, seed=2024):
    rng = np.random.default_rng(seed)
    x, z = _jitter_grid(rng, 2.1)
    mk = {k: F.sample(v, x, z) for k, v in F.mk.items()}
    h = F.sample(F.h, x, z)
    u = rng.random(len(x))
    depth = F.wl - h
    near_tarn = A.dist_to_point(x, z, A.TARN_CENTRE) < 95.0
    open_ground = (1.0 - mk["forest"]) * (1.0 - mk["meadow"]) * (1.0 - mk["burn"])
    open_ground *= 1.0 - A.smoothstep(0.5, 0.8, mk["slope"])
    trail_edge = A.smoothstep(1.4, 2.0, mk["d_trail"]) * (1.0 - A.smoothstep(3.0, 5.5, mk["d_trail"]))
    shore = np.where(near_tarn, A.smoothstep(-0.2, 0.2, -depth) * (1.0 - A.smoothstep(0.8, 2.5, -depth)), 0.0)
    creek = 1.0 - A.smoothstep(1.4, 4.0, mk["d_creek"])

    probs = [
        (TUFT_GRASS, mk["meadow"] * 0.42 + trail_edge * 0.30 + open_ground * 0.10 + creek * 0.20
         + mk["burn"] * 0.05 + mk["forest"] * 0.03),
        (TUFT_FLOWERS, mk["meadow"] * 0.22 + trail_edge * 0.03),
        (TUFT_FERN, mk["forest"] * 0.07 + creek * 0.10),
        (TUFT_SAPLING, mk["burn"] ** 1.3 * 0.20),
        (TUFT_FIREWEED, mk["burn"] ** 1.3 * 0.20),
        (TUFT_SEDGE, shore * 0.6 + creek * 0.25),
        (TUFT_ROCK, A.smoothstep(0.45, 0.8, mk["slope"]) * 0.05 + mk["forest"] * 0.012),
    ]
    kind = np.full(len(x), -1)
    acc = np.zeros(len(x))
    for k, p in probs:
        hit = (kind < 0) & (u >= acc) & (u < acc + p)
        kind = np.where(hit, k, kind)
        acc = acc + p
    ok = (kind >= 0) & _clear(x, z) & (mk["d_trail"] > 1.4) & (mk["d_creek"] > 1.0) & (depth < -0.05)
    ok &= F.sample(F.r, x, z) < 0.97
    x, z, kind = x[ok], z[ok], kind[ok]
    n = len(x)
    size = np.empty(n)
    for k, lo, hi in ((TUFT_GRASS, 0.45, 0.95), (TUFT_FLOWERS, 0.5, 0.85), (TUFT_FERN, 0.5, 0.9),
                      (TUFT_SAPLING, 0.6, 1.3), (TUFT_FIREWEED, 0.7, 1.15), (TUFT_SEDGE, 0.4, 0.8),
                      (TUFT_ROCK, 0.5, 1.4)):
        m = kind == k
        size[m] = rng.uniform(lo, hi, m.sum())
    yaw = rng.integers(0, 256, n)
    shade = rng.integers(0, 256, n)
    return x, z, kind, size, yaw, shade


# ---------------------------------------------------------------------------
# Notebook map
# ---------------------------------------------------------------------------

def _hand_text(img, d, xy, text, kind, fill, scale=1.0):
    """Letter a map label in Lookout Hand straight onto the PIL image."""
    gscale = {"title": 4.6, "label": 3.3, "small": 2.5}[kind] * scale
    pen, base = xy[0], xy[1] + 8.6 * gscale
    arr = np.asarray(img, dtype=np.float64)
    col = np.array(fill[:3], dtype=np.float64)
    alpha = (fill[3] / 255.0) if len(fill) > 3 else 1.0
    for i, ch in enumerate(text):
        cov, left, top, adv = lookout_hand.render_glyph(ord(ch), gscale, i % 2)
        x0, y0 = int(round(pen + left)), int(round(base + top))
        gh, gw = cov.shape
        xa, ya = max(0, x0), max(0, y0)
        xb, yb = min(arr.shape[1], x0 + gw), min(arr.shape[0], y0 + gh)
        if xb > xa and yb > ya:
            a = cov[ya - y0:yb - y0, xa - x0:xb - x0][..., None] * alpha
            arr[ya:yb, xa:xb, :3] = arr[ya:yb, xa:xb, :3] * (1 - a) + col * a
        pen += adv
    from PIL import Image
    img.paste(Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), img.mode))


def notebook_map(size=512):
    """The map Augie drew, as it appears in the notebook: (size, size, 3) floats."""
    from PIL import Image
    img = A.render_map(None, 1024, put_text=_hand_text, title=False)
    img = img.convert("RGB").resize((size, size), Image.LANCZOS)
    return np.asarray(img, dtype=np.float64) / 255.0


# ---------------------------------------------------------------------------
# C header
# ---------------------------------------------------------------------------

def _f(v):
    return f"{v:.3f}f"


def header(F, n_trees, n_tufts):
    def hpt(p):
        return float(A.height(np.array([p[0]]), np.array([p[1]]))[0])

    lines = ["/* Generated by tools/worldgen/bake_world.py from ashen_peaks.py -- do not edit. */",
             "#ifndef LL_WORLD_DATA_H", "#define LL_WORLD_DATA_H", "",
             f"#define WORLD_SIZE {_f(A.WORLD_SIZE)}",
             f"#define WORLD_GRID {GRID}",
             f"#define WORLD_VERTS {VERTS}",
             f"#define WORLD_CELL {_f(CELL)}",
             f"#define WORLD_NUM_TREES {n_trees}",
             f"#define WORLD_NUM_TUFTS {n_tufts}",
             "",
             f"#define TARN_X {_f(A.TARN_CENTRE[0])}",
             f"#define TARN_Z {_f(A.TARN_CENTRE[1])}",
             f"#define TARN_WATER_Y {_f(F.wl)}",
             f"#define TARN_RADIUS 95.0f",
             "",
             f"#define TOWER_X {_f(A.KNOB_CENTRE[0])}",
             f"#define TOWER_Z {_f(A.KNOB_CENTRE[1])}",
             f"#define TOWER_PAD_Y {_f(hpt(A.KNOB_CENTRE))}",
             f"#define CABIN_X {_f(A.CABIN_BUILDING[0])}",
             f"#define CABIN_Z {_f(A.CABIN_BUILDING[1])}",
             f"#define CABIN_PAD_Y {_f(hpt(A.CABIN_BUILDING))}",
             f"#define GATE_X {_f(A.GATE[0])}",
             f"#define GATE_Z {_f(A.GATE[1])}",
             ""]
    lines.append("/* Areas: id, name, x, z, radius (enum world_area in world.h has the same order). */")
    lines.append("#define WORLD_AREAS_INIT { \\")
    for loc in A.LOCATIONS:
        lines.append(f'\t{{ "{loc["id"]}", "{loc["name"]}", {_f(loc["pos"][0])}, {_f(loc["pos"][1])}, '
                     f'{_f(loc["radius"])} }}, \\')
    lines.append("}")
    lines.append("#define WORLD_LANDMARKS_INIT { \\")
    for lm in A.LANDMARKS:
        lines.append(f'\t{{ "{lm["id"]}", "{lm["name"]}", {_f(lm["pos"][0])}, {_f(lm["pos"][1])}, 0.0f }}, \\')
    lines.append("}")
    lines.append("")
    # Trails: one flat point list, with (first, count) per trail.
    pts, spans = [], []
    for name in A.TRAIL_ORDER:
        p = A.TRAILS[name]
        spans.append((name, len(pts), len(p)))
        pts += p
    lines.append(f"#define WORLD_NUM_TRAILS {len(spans)}")
    lines.append(f"#define WORLD_TRAIL_POINTS {len(pts)}")
    lines.append("#define WORLD_TRAIL_PTS_INIT { " + ", ".join(f"{{ {x:.1f}f, {z:.1f}f }}" for x, z in pts) + " }")
    lines.append("#define WORLD_TRAILS_INIT { " + ", ".join(f'{{ "{n}", {a}, {c} }}' for n, a, c in spans) + " }")
    lines.append(f"#define WORLD_CREEK_POINTS {len(A.CREEK)}")
    lines.append("#define WORLD_CREEK_INIT { " + ", ".join(f"{{ {x:.1f}f, {z:.1f}f }}" for x, z in A.CREEK) + " }")
    # Where trails cross the creek (for footbridges).
    cross = []
    for name in A.TRAIL_ORDER:
        tp = A.TRAILS[name]
        for a, b in zip(tp[:-1], tp[1:]):
            for c0, c1 in zip(A.CREEK[:-1], A.CREEK[1:]):
                hit = _seg_intersect(a, b, c0, c1)
                if hit is not None:
                    dx, dz = b[0] - a[0], b[1] - a[1]
                    cross.append((hit[0], hit[1], math.atan2(dx, -dz)))
    lines.append(f"#define WORLD_NUM_BRIDGES {len(cross)}")
    lines.append("#define WORLD_BRIDGES_INIT { " + ", ".join(f"{{ {x:.2f}f, {z:.2f}f, {yaw:.4f}f }}"
                                                             for x, z, yaw in cross) + " }")
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def _seg_intersect(a, b, c, d):
    ax, az = a
    bx, bz = b
    cx, cz = c
    dx, dz = d
    den = (bx - ax) * (dz - cz) - (bz - az) * (dx - cx)
    if abs(den) < 1e-9:
        return None
    t = ((cx - ax) * (dz - cz) - (cz - az) * (dx - cx)) / den
    u = ((cx - ax) * (bz - az) - (cz - az) * (bx - ax)) / den
    if 0 <= t <= 1 and 0 <= u <= 1:
        return (ax + t * (bx - ax), az + t * (bz - az))
    return None


# ---------------------------------------------------------------------------

def build():
    """Returns (blobs [(name, bytes)], world_data.h text, map RGB floats, stats)."""
    F = Fields()
    heights = F.gh.astype(">f4").tobytes()
    col = np.stack([vertex_colours(F, 0.0), vertex_colours(F, 1.0)])
    surf = surfaces(F)

    tx, tz, tk, th, tyaw, ttint = place_trees(F)
    trees = np.zeros(len(tx), dtype=[("x", ">u2"), ("z", ">u2"), ("kind", "u1"), ("h", "u1"),
                                     ("yaw", "u1"), ("tint", "u1")])
    trees["x"] = np.rint(tx * 64)
    trees["z"] = np.rint(tz * 64)
    trees["kind"] = tk
    trees["h"] = np.clip(np.rint(th * 10), 0, 255)
    trees["yaw"] = tyaw
    trees["tint"] = ttint

    ux, uz, uk, us, uyaw, ushade = place_tufts(F)
    tufts = np.zeros(len(ux), dtype=trees.dtype)
    tufts["x"] = np.rint(ux * 64)
    tufts["z"] = np.rint(uz * 64)
    tufts["kind"] = uk
    tufts["h"] = np.clip(np.rint(us * 100), 0, 255)
    tufts["yaw"] = uyaw
    tufts["tint"] = ushade

    blobs = [("world_height", heights), ("world_color", col.tobytes()), ("world_surface", surf.tobytes()),
             ("world_trees", trees.tobytes()), ("world_tufts", tufts.tobytes())]
    stats = dict(trees=len(tx), tufts=len(ux),
                 tree_kinds=np.bincount(tk, minlength=6).tolist(),
                 tuft_kinds=np.bincount(uk, minlength=7).tolist())
    return blobs, header(F, len(tx), len(ux)), notebook_map(), stats


def main():
    from PIL import Image
    blobs, hdr, mp, stats = build()
    print(stats)
    for name, b in blobs:
        print(f"{name}: {len(b)} bytes")
    out = sys.argv[1] if len(sys.argv) > 1 else "notebook_map.png"
    Image.fromarray((mp * 255).astype(np.uint8)).save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
