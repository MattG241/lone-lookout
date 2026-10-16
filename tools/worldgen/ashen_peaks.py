#!/usr/bin/env python3
"""
ashen_peaks.py -- the single source of truth for the Ashen Peaks layout.

Phase 1 (design): defines the playable map -- terrain features, the six
locations, the trail network and vegetation masks -- and renders a
"found in Augie's notebook" paper map for the design document.

Phase 3 (world building): tools/worldgen/bake_world.py imports this module
and bakes the 129x129 heightmap, vertex colours, surface IDs and foliage
instances into binary blobs linked into the .dol, so the map in the design
document and the map in the game can never drift apart.

Coordinate system (matches the game):
    x = metres east, z = metres south, origin at the NW corner.
    The playable square is 512 m x 512 m. North is -z (top of the map).

Usage:
    python3 tools/worldgen/ashen_peaks.py --map docs/concept/ashen_peaks_map.png
"""

import argparse
import math

import numpy as np

# ---------------------------------------------------------------------------
# World constants
# ---------------------------------------------------------------------------

WORLD_SIZE = 512.0          # metres per side
GRID_CELLS = 128            # heightmap cells per side (in-game)
GRID_SPACING = WORLD_SIZE / GRID_CELLS   # 4 m between height samples
BASE_ELEVATION = 2190.0     # metres above sea level at height 0 (flavour only)

# Main areas. Positions are in world metres; radius is the "you have
# arrived" trigger used for music cues and the notebook.
LOCATIONS = [
    dict(id="tower",  name="Kestrel Lookout",       pos=(256.0, 248.0), radius=28.0),
    dict(id="meadow", name="Larkspur Meadow",       pos=(112.0, 334.0), radius=60.0),
    dict(id="tarn",   name="Mirror Tarn",           pos=(124.0, 136.0), radius=55.0),
    dict(id="ridge",  name="Cinder Ridge Overlook", pos=(398.0, 112.0), radius=26.0),
    dict(id="burn",   name="The '98 Burn",          pos=(392.0, 372.0), radius=65.0),
    dict(id="cabin",  name="Pell's Cabin",          pos=(258.0, 440.0), radius=30.0),
]

# Minor landmarks (not "areas"): sit spots, the trailhead, the supply box.
LANDMARKS = [
    dict(id="trailhead", name="Trailhead & supply box", pos=(250.0, 498.0)),
    dict(id="dock",      name="Tarn dock",              pos=(156.0, 150.0)),
    dict(id="bench",     name="Augie's bench",          pos=(403.0, 119.5)),
    dict(id="fence",     name="Old drift fence",        pos=(142.0, 318.0)),
    dict(id="spring",    name="Cold spring",            pos=(196.0, 214.0)),
]

# Trails as polylines (world metres). Together they form one big loop
# with two spurs, so a player can never get "stuck" down a dead end.
# The climb to the ridge angles across its gentle south-west face.
TRAILS = {
    "tower-meadow": [(256, 258), (238, 284), (204, 298), (170, 312), (140, 326), (112, 334)],
    "meadow-tarn":  [(112, 334), (120, 296), (134, 256), (150, 214), (150, 178)],
    "tarn-shore":   [(150, 178), (158, 162), (160, 146), (160, 128)],
    "tarn-ridge":   [(160, 128), (198, 108), (240, 98), (288, 96), (326, 110), (352, 130),
                     (374, 130), (390, 120), (398, 112)],
    "ridge-tower":  [(398, 112), (372, 140), (352, 168), (322, 196), (290, 222), (264, 242)],
    "tower-cabin":  [(256, 258), (266, 300), (258, 342), (248, 386), (258, 440)],
    "cabin-burn":   [(258, 440), (300, 428), (340, 404), (372, 384), (392, 372)],
    "burn-tower":   [(392, 372), (356, 336), (322, 306), (290, 282), (262, 262)],
    "cabin-gate":   [(258, 440), (252, 470), (250, 498)],
    "spring-spur":  [(204, 298), (200, 260), (196, 214)],
}

# The creek runs from the tarn outlet, through the meadow, out the SW notch.
CREEK = [(158, 160), (166, 186), (156, 214), (162, 240), (146, 262), (138, 286), (120, 300),
         (94, 318), (84, 346), (92, 372), (78, 400), (62, 424), (60, 452), (42, 478), (30, 512)]

TARN_CENTRE = (124.0, 136.0)
KNOB_CENTRE = (256.0, 248.0)
RIDGE_A, RIDGE_B = (318.0, 66.0), (482.0, 176.0)
MEADOW_CENTRE = (112.0, 334.0)
BURN_CENTRE = (392.0, 372.0)
CABIN_CENTRE = (258.0, 440.0)          # the trail junction in the cabin yard
CABIN_BUILDING = (244.0, 434.0)        # the cabin itself, west of the junction
GATE = (250.0, 505.0)                  # road gate at the south edge of the district


# ---------------------------------------------------------------------------
# Small numeric helpers (numpy only -- no scipy dependency)
# ---------------------------------------------------------------------------

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _hash2(ix, iz, seed):
    """Deterministic 2D integer hash -> [0,1). Works on numpy int arrays."""
    h = (ix.astype(np.uint64) * np.uint64(374761393)
         + iz.astype(np.uint64) * np.uint64(668265263)
         + np.uint64(seed) * np.uint64(2654435761)) & np.uint64(0xFFFFFFFF)
    h = ((h ^ (h >> np.uint64(13))) * np.uint64(1274126177)) & np.uint64(0xFFFFFFFF)
    h = h ^ (h >> np.uint64(16))
    return h.astype(np.float64) / 4294967295.0


def value_noise(x, z, seed=0):
    """Smooth 2D value noise in [0,1). x, z are float arrays (noise units)."""
    x0 = np.floor(x).astype(np.int64)
    z0 = np.floor(z).astype(np.int64)
    fx, fz = x - x0, z - z0
    u = fx * fx * (3.0 - 2.0 * fx)
    v = fz * fz * (3.0 - 2.0 * fz)
    # Offset into positive range so the uint64 cast in the hash is well defined.
    x0 = x0 + 100000
    z0 = z0 + 100000
    a = _hash2(x0, z0, seed)
    b = _hash2(x0 + 1, z0, seed)
    c = _hash2(x0, z0 + 1, seed)
    d = _hash2(x0 + 1, z0 + 1, seed)
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v


def fbm(x, z, octaves=5, seed=0, lacunarity=2.0, gain=0.5):
    """Fractal sum of value noise, normalised to roughly [0,1]."""
    total = np.zeros_like(x, dtype=np.float64)
    amp, freq, norm = 1.0, 1.0, 0.0
    for i in range(octaves):
        total += amp * value_noise(x * freq, z * freq, seed + i * 101)
        norm += amp
        amp *= gain
        freq *= lacunarity
    return total / norm


def dist_to_segment(px, pz, a, b):
    ax, az = a
    bx, bz = b
    vx, vz = bx - ax, bz - az
    t = ((px - ax) * vx + (pz - az) * vz) / (vx * vx + vz * vz)
    t = np.clip(t, 0.0, 1.0)
    dx = px - (ax + t * vx)
    dz = pz - (az + t * vz)
    return np.sqrt(dx * dx + dz * dz), t


def dist_to_polyline(px, pz, pts):
    best = np.full(np.shape(px), 1e9)
    for a, b in zip(pts[:-1], pts[1:]):
        d, _ = dist_to_segment(px, pz, a, b)
        best = np.minimum(best, d)
    return best


def dist_to_point(px, pz, p):
    return np.sqrt((px - p[0]) ** 2 + (pz - p[1]) ** 2)


def box_blur(img, r):
    """Separable box blur (edge-clamped). Three passes approximate a Gaussian."""
    if r <= 0:
        return img
    out = img.astype(np.float64)
    for axis in (0, 1):
        pad = [(0, 0)] * out.ndim
        pad[axis] = (r + 1, r)
        p = np.pad(out, pad, mode="edge")
        c = np.cumsum(p, axis=axis)
        hi = np.take(c, np.arange(2 * r + 1, c.shape[axis]), axis=axis)
        lo = np.take(c, np.arange(0, c.shape[axis] - 2 * r - 1), axis=axis)
        out = (hi - lo) / (2 * r + 1)
    return out


def gauss_blur(img, r):
    for _ in range(3):
        img = box_blur(img, r)
    return img


# ---------------------------------------------------------------------------
# Terrain
# ---------------------------------------------------------------------------

def _height_features(x, z):
    """Terrain before flat pads and trail wear (see height())."""
    x = np.asarray(x, dtype=np.float64)
    z = np.asarray(z, dtype=np.float64)

    # Domain warp: bends the otherwise geometric features (round knob,
    # straight ridge, circular rim) into something that reads as land.
    wx = (fbm(x / 110.0, z / 110.0, 3, seed=61) - 0.5) * 48.0
    wz = (fbm(x / 110.0, z / 110.0, 3, seed=62) - 0.5) * 48.0
    # Keep the warp gentle at the places we build on.
    calm = np.ones_like(x)
    for c in (KNOB_CENTRE, CABIN_CENTRE, TARN_CENTRE, MEADOW_CENTRE):
        calm *= smoothstep(20.0, 70.0, dist_to_point(x, z, c)) * 0.75 + 0.25
    x = x + wx * calm
    z = z + wz * calm

    # Bowl and rim -------------------------------------------------------
    dx = (x - 256.0) / 256.0
    dz = (z - 256.0) / 256.0
    r = np.sqrt(dx * dx + dz * dz)
    rim = smoothstep(0.66, 1.08, r) * 120.0
    rim += smoothstep(0.80, 1.10, r) * fbm(x / 38.0, z / 38.0, 4, seed=7) * 45.0

    # Notches in the rim: south (trailhead road) and south-west (creek outlet).
    notch_s = np.exp(-((x - 252.0) / 48.0) ** 2) * smoothstep(400.0, 512.0, z)
    notch_sw = np.exp(-(dist_to_point(x, z, (34.0, 506.0)) / 95.0) ** 2)
    rim *= 1.0 - 0.82 * np.maximum(notch_s, notch_sw)
    # Behind Cinder Ridge the ridge itself is the edge; ease the rim there
    # so the overlook's south-west face stays walkable.
    d_rr, _ = dist_to_segment(x, z, RIDGE_A, RIDGE_B)
    rim *= 1.0 - 0.6 * np.exp(-(d_rr / 70.0) ** 2) * smoothstep(1.02, 0.8, r)

    h = 18.0 + rim

    # Gentle tilt: the whole bowl drains toward the SW notch.
    h += (x / 512.0) * 10.0 - (z / 512.0) * 4.0

    # Kestrel Knob -- the tower's hill, with a long spur running SE
    # (the burn trail follows it) and a shorter one toward the ridge.
    d_knob = dist_to_point(x, z, KNOB_CENTRE)
    h += 34.0 * np.exp(-(d_knob / 80.0) ** 2) + 8.0 * np.exp(-(d_knob / 170.0) ** 2)
    # Spurs are shoulders that leave the summit, not extra height on it.
    d_spur, t_spur = dist_to_segment(x, z, KNOB_CENTRE, (350.0, 330.0))
    h += 12.0 * (1.0 - t_spur) * smoothstep(0.0, 0.35, t_spur) * np.exp(-(d_spur / 40.0) ** 2)
    d_spur2, t_spur2 = dist_to_segment(x, z, KNOB_CENTRE, (330.0, 180.0))
    h += 10.0 * (1.0 - t_spur2) * smoothstep(0.0, 0.35, t_spur2) * np.exp(-(d_spur2 / 36.0) ** 2)

    # Cinder Ridge -- an elongated crest, highest two-thirds of the way along.
    d_ridge, t_ridge = dist_to_segment(x, z, RIDGE_A, RIDGE_B)
    along = 0.55 + 0.45 * np.sin(np.clip(t_ridge, 0, 1) * math.pi * 0.9 + 0.25)
    # Asymmetric: a long, walkable face toward the bowl (south-west), a
    # steep drop on the far side (north-east, outside the playable area).
    vx, vz = RIDGE_B[0] - RIDGE_A[0], RIDGE_B[1] - RIDGE_A[1]
    sw = ((x - RIDGE_A[0]) * vz - (z - RIDGE_A[1]) * vx) < 0.0
    width = np.where(sw, 88.0, 46.0)
    h += 52.0 * along * np.exp(-(d_ridge / width) ** 2)
    h += 6.0 * np.exp(-(d_ridge / 14.0) ** 2)                # sharp spine

    # Mirror Tarn basin.
    h -= 17.0 * np.exp(-(dist_to_point(x, z, TARN_CENTRE) / 52.0) ** 2)

    # The burn sits on a long SE slope that climbs toward the rim.
    d_burn = dist_to_point(x, z, BURN_CENTRE)
    h += 8.0 * np.exp(-(d_burn / 90.0) ** 2) * ((x - BURN_CENTRE[0]) / 90.0 + 1.0)

    # Organic variation everywhere.
    h += (fbm(x / 70.0, z / 70.0, 5, seed=3) - 0.5) * 14.0
    h += (fbm(x / 18.0, z / 18.0, 3, seed=11) - 0.5) * 2.5

    # Larkspur Meadow -- pulled toward a flat, slightly dished floor.
    d_meadow = dist_to_point(x, z, MEADOW_CENTRE)
    m = 1.0 - smoothstep(40.0, 85.0, d_meadow)
    h = h * (1.0 - m) + (24.0 + (d_meadow / 85.0) * 4.0) * m

    # Creek channel.
    h -= 2.6 * np.exp(-(dist_to_polyline(x, z, CREEK) / 5.0) ** 2)
    return h


# Flat building pads: (centre, inner radius, outer radius, extra height).
PADS = [
    (KNOB_CENTRE, 9.0, 16.0, 1.0),     # tower footing on the knob summit
    ((250.0, 437.0), 15.0, 26.0, 0.0), # Pell's Cabin and its yard
    ((250.0, 498.0), 10.0, 20.0, 0.0), # trailhead turnaround
]


def _padded(x, z):
    """Terrain features plus the flat building pads."""
    x = np.asarray(x, dtype=np.float64)
    z = np.asarray(z, dtype=np.float64)
    h = _height_features(x, z)
    # Flat pads, levelled to the terrain height at their centre.
    for centre, r0, r1, lift in PADS:
        target = float(_height_features(np.array([centre[0]]), np.array([centre[1]]))[0]) + lift
        p = 1.0 - smoothstep(r0, r1, dist_to_point(x, z, centre))
        h = h * (1.0 - p) + target * p
    return h


# Trails are graded into the hillside: each one gets a smoothed elevation
# profile along its length, and the ground within a few metres is cut or
# filled to that profile. The bed is level across and free of bumps, so a
# walker never meets a sudden steep step. Order matters: a trail that starts
# on an earlier one is pinned to that trail's profile so junctions agree.
TRAIL_ORDER = ["tower-meadow", "tower-cabin", "burn-tower", "ridge-tower", "cabin-burn", "cabin-gate",
               "meadow-tarn", "tarn-shore", "tarn-ridge", "spring-spur"]
TRAIL_BED = 3.0          # half-width of the level bed (m)
TRAIL_BANK = 12.0        # cut/fill blends back into the hillside by here
_PROFILE_STEP = 1.0
MAX_GRADE = 0.36         # ~20 degrees along a trail
# The last pull up to the overlook is allowed to be a steeper scramble.
TRAIL_MAX_GRADE = {"tarn-ridge": 0.52, "ridge-tower": 0.52}
_profiles = None


def _arc(pts):
    p = np.asarray(pts, dtype=np.float64)
    seg = np.sqrt(((p[1:] - p[:-1]) ** 2).sum(axis=1))
    return p, np.concatenate([[0.0], np.cumsum(seg)])


def _nearest_on(pts, px, pz):
    """Distance and arc length of the nearest point on a polyline."""
    p, s = _arc(pts)
    best = np.full(np.shape(px), 1e9)
    best_s = np.zeros(np.shape(px))
    for i in range(len(p) - 1):
        d, t = dist_to_segment(px, pz, p[i], p[i + 1])
        m = d < best
        best = np.where(m, d, best)
        best_s = np.where(m, s[i] + t * (s[i + 1] - s[i]), best_s)
    return best, best_s


def trail_profiles():
    """name -> (arc lengths, bed heights), computed once from the padded terrain."""
    global _profiles
    if _profiles is not None:
        return _profiles
    prof = {}
    for name in TRAIL_ORDER:
        p, s = _arc(TRAILS[name])
        ss = np.arange(0.0, s[-1] + 1e-6, _PROFILE_STEP)
        if ss[-1] < s[-1]:
            ss = np.append(ss, s[-1])
        xs = np.interp(ss, s, p[:, 0])
        zs = np.interp(ss, s, p[:, 1])
        nat = _padded(xs, zs)
        n = len(ss)
        # Pin the ends: to an earlier trail's bed where they meet one, else
        # to the natural ground (the pads, at the buildings).
        ends = []
        for k in (0, n - 1):
            e = nat[k]
            for other, (os_, oh) in prof.items():
                d, sa = _nearest_on(TRAILS[other], np.array([xs[k]]), np.array([zs[k]]))
                if d[0] < 3.0:
                    e = float(np.interp(sa[0], os_, oh))
                    break
            ends.append(e)
        # Moving average over +/-R metres, with the ends extended flat.
        R = int(18.0 / _PROFILE_STEP)
        ext = np.concatenate([np.full(R, ends[0]), nat, np.full(R, ends[1])])
        c = np.concatenate([[0.0], np.cumsum(ext)])
        out = (c[2 * R + 1:] - c[:-2 * R - 1]) / (2 * R + 1)
        f = ss / ss[-1]
        out += (ends[0] - out[0]) * (1.0 - f) + (ends[1] - out[-1]) * f
        # Grade limiter: no stretch steeper than MAX_GRADE (or the trail's
        # average, if the two ends alone demand more).
        ds = np.diff(ss)
        g = max(TRAIL_MAX_GRADE.get(name, MAX_GRADE), 1.02 * abs(ends[1] - ends[0]) / ss[-1])
        for _ in range(6):
            out[0] = ends[0]
            for i in range(1, n):
                out[i] = min(max(out[i], out[i - 1] - g * ds[i - 1]), out[i - 1] + g * ds[i - 1])
            out[-1] = ends[1]
            for i in range(n - 2, -1, -1):
                out[i] = min(max(out[i], out[i + 1] - g * ds[i]), out[i + 1] + g * ds[i])
        prof[name] = (ss, out)
    _profiles = prof
    return prof


def trail_distance(x, z):
    """Distance to the nearest trail centre line."""
    x = np.asarray(x, dtype=np.float64)
    z = np.asarray(z, dtype=np.float64)
    best = np.full(np.shape(x), 1e9)
    for pts in TRAILS.values():
        best = np.minimum(best, dist_to_polyline(x, z, pts))
    return best


def height(x, z):
    """Terrain height in metres above the map datum at world (x, z).

    Built from a handful of readable features rather than raw noise so the
    level designer (and the radio script) can talk about real places:
    a bowl with a high rim, Kestrel Knob in the middle, Cinder Ridge to the
    NE, the Mirror Tarn basin to the NW, the flat Larkspur Meadow to the SW,
    and two notches in the rim (the creek outlet and the trailhead road).
    """
    x = np.asarray(x, dtype=np.float64)
    z = np.asarray(z, dtype=np.float64)
    h = _padded(x, z)
    # Blend toward the bed of every nearby trail (the nearest point on each).
    # Trails meet at matching bed heights, so weighting them keeps the ground
    # continuous at junctions.
    w_max = np.zeros(np.shape(x))
    w_sum = np.zeros(np.shape(x))
    bed_sum = np.zeros(np.shape(x))
    d_trail = np.full(np.shape(x), 1e9)
    for name, (ss, hh) in trail_profiles().items():
        d, sa = _nearest_on(TRAILS[name], x, z)
        w = 1.0 - smoothstep(TRAIL_BED, TRAIL_BANK, d)
        w2 = w * w
        w_max = np.maximum(w_max, w)
        w_sum += w2
        bed_sum += w2 * np.interp(sa, ss, hh)
        d_trail = np.minimum(d_trail, d)
    bed = np.where(w_sum > 0, bed_sum / np.maximum(w_sum, 1e-12), h)
    h = h * (1.0 - w_max) + bed * w_max
    # Trails are trodden in a little.
    h -= 0.30 * np.exp(-(d_trail / 1.8) ** 2)
    return h


def tarn_water_level():
    """Water surface height of Mirror Tarn (metres)."""
    return float(height(np.array([TARN_CENTRE[0]]), np.array([TARN_CENTRE[1]]))[0]) + 8.5


def masks(x, z, h):
    """Vegetation / surface masks used by both the map and (later) the game."""
    wl = tarn_water_level()
    water = (h < wl) & (dist_to_point(x, z, TARN_CENTRE) < 90.0)

    # Slope from finite differences (metres per metre).
    gz, gx = np.gradient(h, z[:, 0], x[0, :])
    slope = np.sqrt(gx * gx + gz * gz)

    d_trail = np.full(x.shape, 1e9)
    for pts in TRAILS.values():
        d_trail = np.minimum(d_trail, dist_to_polyline(x, z, pts))
    d_creek = dist_to_polyline(x, z, CREEK)

    meadow = 1.0 - smoothstep(48.0, 78.0, dist_to_point(x, z, MEADOW_CENTRE))
    meadow *= 0.7 + 0.3 * fbm(x / 20.0, z / 20.0, 3, seed=21)
    burn = 1.0 - smoothstep(48.0, 82.0, dist_to_point(x, z, BURN_CENTRE)
                            + (fbm(x / 25.0, z / 25.0, 3, seed=31) - 0.5) * 40.0)

    forest = np.ones_like(h)
    forest *= 1.0 - smoothstep(0.55, 0.95, slope)                 # bare on steep scree
    forest *= 1.0 - smoothstep(0.25, 0.55, meadow)
    forest *= 1.0 - smoothstep(0.30, 0.60, burn)
    forest *= smoothstep(5.0, 9.0, d_trail)                         # trails stay open
    forest *= 1.0 - (1.0 - smoothstep(26.0, 34.0, dist_to_point(x, z, KNOB_CENTRE)))
    forest *= 1.0 - (1.0 - smoothstep(12.0, 20.0, dist_to_point(x, z, CABIN_CENTRE)))
    forest *= np.where(water, 0.0, 1.0)
    near_tarn = dist_to_point(x, z, TARN_CENTRE) < 95.0
    forest *= np.where(near_tarn, smoothstep(1.5, 5.0, h - wl), 1.0)  # open shoreline
    hi = h > (np.percentile(h, 93))
    forest *= np.where(hi, 0.25, 1.0)                               # thin on the tops
    forest *= 0.45 + 0.55 * fbm(x / 30.0, z / 30.0, 4, seed=41)     # patchiness

    aspen = (smoothstep(50.0, 70.0, dist_to_point(x, z, MEADOW_CENTRE))
             * (1.0 - smoothstep(70.0, 110.0, dist_to_point(x, z, MEADOW_CENTRE))))
    aspen = np.maximum(aspen, 1.0 - smoothstep(6.0, 22.0, d_creek))
    aspen *= smoothstep(0.55, 0.7, fbm(x / 22.0, z / 22.0, 3, seed=51))
    aspen *= np.where(water, 0.0, 1.0)

    return dict(water=water, slope=slope, forest=np.clip(forest, 0, 1),
                meadow=np.clip(meadow, 0, 1), burn=np.clip(burn, 0, 1),
                aspen=np.clip(aspen, 0, 1), d_trail=d_trail, d_creek=d_creek)


# ---------------------------------------------------------------------------
# Paper-map renderer (design document only)
# ---------------------------------------------------------------------------

PAPER = np.array([0.93, 0.89, 0.81])
GRAPHITE = np.array([0.30, 0.28, 0.27])
SEPIA = np.array([0.45, 0.31, 0.20])
WASH_FOREST = np.array([0.55, 0.64, 0.52])
WASH_ASPEN = np.array([0.84, 0.76, 0.45])
WASH_MEADOW = np.array([0.86, 0.84, 0.62])
WASH_BURN = np.array([0.66, 0.60, 0.55])
WASH_WATER = np.array([0.55, 0.68, 0.74])
INK_WATER = np.array([0.27, 0.40, 0.50])


def _wash(img, mask, colour, strength=1.0, edge=0.35, rng=None, grain=None):
    """Watercolour-style wash: soft fill, darker pooled edges, paper grain."""
    soft = gauss_blur(mask, 3)
    inner = gauss_blur(mask, 9)
    pooled = np.clip(soft - inner, 0, 1) * edge * 2.5            # pigment pools at edges
    a = np.clip(soft * strength + pooled, 0, 1)
    if grain is not None:
        a *= 0.82 + 0.18 * grain
    a = a[..., None]
    tinted = img * (1 - a) + (img * colour[None, None, :] * 1.08) * a
    return tinted


def _liberation_text():
    """Default label renderer for the design-document map (system serif)."""
    from PIL import ImageFont
    try:
        base = "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf"
        fonts = {k: ImageFont.truetype(base, sz) for k, sz in (("title", 34), ("label", 21), ("small", 15))}
    except OSError:
        fonts = {k: ImageFont.load_default() for k in ("title", "label", "small")}

    def put(img, d, xy, text, kind, fill, scale=1.0):
        d.text(xy, text, font=fonts[kind], fill=fill)
    return put


def render_map(path, size=1024, seed=1, put_text=None, title=True):
    """Paint the notebook map. put_text(img, draw, xy, text, kind, rgba, scale)
    draws labels (kind: title/label/small); the game's baker passes one that
    letters them in Lookout Hand. Returns the PIL image (and saves it if
    path is given)."""
    from PIL import Image, ImageDraw

    rng = np.random.default_rng(seed)
    s = size / WORLD_SIZE                       # pixels per metre
    xs = (np.arange(size) + 0.5) / s
    x, z = np.meshgrid(xs, xs)
    h = height(x, z)
    mk = masks(x, z, h)

    # Paper: cream base, low-frequency blotches and fine fibres.
    grain = fbm(x / 1.2, z / 1.2, 2, seed=99)
    blotch = fbm(x / 60.0, z / 60.0, 3, seed=98)
    img = np.ones((size, size, 3)) * PAPER[None, None, :]
    img *= (0.95 + 0.06 * blotch)[..., None]
    img *= (0.975 + 0.035 * grain)[..., None]

    # Soft warm/cool hillshade, like a light graphite rub.
    gz, gx = np.gradient(h, 1.0 / s)
    light = np.array([-0.6, 0.65, -0.45])        # from the NW, as map convention
    light /= np.linalg.norm(light)
    n = np.dstack([-gx, np.ones_like(h), -gz])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    shade = np.clip((n * light[None, None, :]).sum(axis=2), 0, 1)
    shade = gauss_blur(shade, 2)
    shadow_tint = np.array([0.82, 0.84, 0.92])
    k = np.clip((0.78 - shade) * 1.6, 0, 1)[..., None] * 0.55
    img = img * (1 - k) + img * shadow_tint[None, None, :] * k

    # Washes.
    img = _wash(img, mk["forest"] ** 1.3, WASH_FOREST, 0.85, grain=grain)
    img = _wash(img, mk["aspen"], WASH_ASPEN, 0.45, edge=0.2, grain=grain)
    img = _wash(img, mk["meadow"] * 0.9, WASH_MEADOW, 0.8, grain=grain)
    img = _wash(img, mk["burn"] * 0.9, WASH_BURN, 0.75, grain=grain)
    img = _wash(img, mk["water"].astype(float), WASH_WATER, 1.0, edge=0.6, grain=grain)

    # Contours: every 6 m, index contour every 30 m.
    def contour(interval):
        lv = np.floor(h / interval)
        c = np.zeros_like(h)
        c[:, :-1] = np.maximum(c[:, :-1], lv[:, :-1] != lv[:, 1:])
        c[:-1, :] = np.maximum(c[:-1, :], lv[:-1, :] != lv[1:, :])
        return c

    c_minor = gauss_blur(contour(6.0), 1) * 1.6
    c_major = gauss_blur(contour(30.0), 1) * 2.2
    c = np.clip(c_minor * 0.28 + c_major * 0.55, 0, 0.75)
    c *= 0.65 + 0.35 * grain                                  # pencil skips on the tooth
    c *= np.where(mk["water"], 0.15, 1.0)
    img = img * (1 - c[..., None]) + GRAPHITE[None, None, :] * c[..., None]

    # Creek.
    creek = np.exp(-(mk["d_creek"] * s / 1.4) ** 2)
    creek *= np.where(mk["water"], 0, 1)
    img = img * (1 - creek[..., None] * 0.8) + INK_WATER[None, None, :] * creek[..., None] * 0.8

    # Tarn shoreline in ink.
    wm = mk["water"].astype(float)
    shore = np.clip(gauss_blur(wm, 1) - gauss_blur(wm, 3), 0, 1) * 3.0
    shore = np.clip(shore, 0, 0.8)
    img = img * (1 - shore[..., None]) + INK_WATER[None, None, :] * shore[..., None]

    out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(out, "RGBA")

    def to_px(p):
        return (p[0] * s, p[1] * s)

    # Tree stamps: little hand-drawn conifers where the forest wash is dense.
    fz_idx = rng.integers(0, size, 9000)
    fx_idx = rng.integers(0, size, 9000)
    for py, px in zip(fz_idx, fx_idx):
        f = mk["forest"][py, px]
        if rng.random() < f ** 1.6 * 0.75:
            hgt = 5 + rng.random() * 4
            w = hgt * 0.45
            col = (58, 78, 60, int(120 + 80 * rng.random()))
            d.line([(px - w, py), (px, py - hgt), (px + w, py)], fill=col, width=1)
        elif rng.random() < mk["aspen"][py, px] * 0.35:
            r0 = 2 + rng.random() * 1.5
            d.ellipse([px - r0, py - r0, px + r0, py + r0], outline=(150, 120, 50, 170))
    # Snags in the burn: short vertical ticks.
    for _ in range(2600):
        px, py = rng.integers(0, size, 2)
        if rng.random() < mk["burn"][py, px] * 0.55:
            hgt = 4 + rng.random() * 6
            d.line([(px, py), (px + rng.normal(0, 0.6), py - hgt)], fill=(90, 80, 74, 190), width=1)
    # Meadow flowers.
    for _ in range(3500):
        px, py = rng.integers(0, size, 2)
        if rng.random() < mk["meadow"][py, px] * 0.35:
            c0 = (110, 104, 170, 170) if rng.random() < 0.6 else (230, 220, 190, 200)
            d.point((px, py), fill=c0)

    # Trails: dashed sepia, hand-jittered.
    for pts in TRAILS.values():
        pp = [to_px(p) for p in pts]
        dense = []
        for (ax, ay), (bx, by) in zip(pp[:-1], pp[1:]):
            n_seg = max(2, int(math.hypot(bx - ax, by - ay) / 4))
            for i in range(n_seg):
                t = i / n_seg
                dense.append((ax + (bx - ax) * t + rng.normal(0, 0.5),
                              ay + (by - ay) * t + rng.normal(0, 0.5)))
        dense.append(pp[-1])
        for i in range(0, len(dense) - 1, 2):
            d.line([dense[i], dense[i + 1]], fill=(115, 79, 51, 230), width=2)

    if put_text is None:
        put_text = _liberation_text()
    k = size / 1024.0                            # layout was authored at 1024 px

    ink = (52, 44, 40, 235)

    # Location markers and labels.
    label_offsets = {
        "tower": (16, -34), "meadow": (-70, 40), "tarn": (-58, -86),
        "ridge": (-190, -46), "burn": (-30, 52), "cabin": (18, -6),
    }
    for loc in LOCATIONS:
        px, py = to_px(loc["pos"])
        if loc["id"] == "tower":
            # Tiny tower glyph: legs and cab.
            d.line([(px - 6, py + 8), (px - 3, py - 6)], fill=ink, width=2)
            d.line([(px + 6, py + 8), (px + 3, py - 6)], fill=ink, width=2)
            d.line([(px - 5, py + 3), (px + 5, py - 3)], fill=ink, width=1)
            d.line([(px + 5, py + 3), (px - 5, py - 3)], fill=ink, width=1)
            d.rectangle([px - 6, py - 13, px + 6, py - 6], outline=ink, width=2)
            d.polygon([(px - 8, py - 13), (px, py - 19), (px + 8, py - 13)], outline=ink)
        else:
            d.ellipse([px - 4, py - 4, px + 4, py + 4], outline=ink, width=2)
        ox, oy = label_offsets.get(loc["id"], (10, -10))
        put_text(out, d, (px + ox * k, py + oy * k), loc["name"], "label", ink)

    for lm in LANDMARKS:
        px, py = to_px(lm["pos"])
        d.line([(px - 3, py - 3), (px + 3, py + 3)], fill=ink, width=1)
        d.line([(px - 3, py + 3), (px + 3, py - 3)], fill=ink, width=1)
        offs = {"trailhead": (8, -22), "dock": (8, 2), "bench": (10, -2),
                "fence": (8, -18), "spring": (8, -8)}[lm["id"]]
        put_text(out, d, (px + offs[0] * k, py + offs[1] * k), lm["name"], "small", (70, 60, 55, 220))

    # Title cartouche, compass, scale bar.
    if title:
        put_text(out, d, (34 * k, 26 * k), "The Ashen Peaks", "title", ink)
        put_text(out, d, (36 * k, 66 * k), "District 4 - copied from A. Pell's notebook", "small", (80, 70, 64, 220))

    cx, cy = size - 70 * k, 80 * k
    d.line([(cx, cy + 34 * k), (cx, cy - 34 * k)], fill=ink, width=2)
    d.polygon([(cx, cy - 40 * k), (cx - 7 * k, cy - 24 * k), (cx + 7 * k, cy - 24 * k)], fill=ink)
    d.line([(cx - 20 * k, cy), (cx + 20 * k, cy)], fill=ink, width=1)
    put_text(out, d, (cx - 6 * k, cy - 68 * k), "N", "label", ink)

    bx, by = 40 * k, size - 50 * k
    bar = 100 * s
    d.line([(bx, by), (bx + bar, by)], fill=ink, width=2)
    for i in range(5):
        tx = bx + bar * i / 4
        d.line([(tx, by - 5 * k), (tx, by + 5 * k)], fill=ink, width=1)
    put_text(out, d, (bx + bar + 8 * k, by - 10 * k), "100 m", "small", ink)
    put_text(out, d, (bx, by + 10 * k), "contours 6 m", "small", (80, 70, 64, 200))

    if path:
        out.save(path)
    return out


def summary():
    """Print elevations at key places -- handy when writing the design doc."""
    for loc in LOCATIONS + LANDMARKS:
        px, pz = loc["pos"]
        e = float(height(np.array([px]), np.array([pz]))[0])
        print(f"{loc['name']:<26} x={px:5.0f} z={pz:5.0f}  h={e:6.1f} m  "
              f"(~{BASE_ELEVATION + e:,.0f} m a.s.l.)")
    print(f"Mirror Tarn water level: {tarn_water_level():.1f} m")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", help="write the paper map PNG to this path")
    ap.add_argument("--size", type=int, default=1024)
    ap.add_argument("--summary", action="store_true", help="print key elevations")
    args = ap.parse_args()
    if args.summary:
        summary()
    if args.map:
        render_map(args.map, args.size)
        print(f"wrote {args.map}")


if __name__ == "__main__":
    main()
