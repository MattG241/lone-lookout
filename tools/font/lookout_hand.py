"""
lookout_hand.py -- "Lookout Hand", an original single-stroke hand-lettering font.

Every glyph is drawn from a few pen strokes (lines, elliptical arcs and dots)
on a small unit grid, then rasterised with a pencil-like brush: slight slant,
wobble, pressure taper and paper grain. Each glyph gets two jittered variants
so a line of text never looks typed. Nothing here is derived from an existing
typeface.

Grid units: baseline y=0, x-height 5, cap height 7.5, ascender 8, descender -2.5.
Primitives:  L(x0, y0, x1, y1)            straight stroke
             A(cx, cy, rx, ry, a0, a1)    elliptical arc, degrees, a0 -> a1
                                          (a1 > a0 counter-clockwise, y up)
             D(x, y)                      pencil dot
"""

import math

import numpy as np
from PIL import Image, ImageDraw


def L(x0, y0, x1, y1):
    return ("L", x0, y0, x1, y1)


def A(cx, cy, rx, ry, a0, a1):
    return ("A", cx, cy, rx, ry, a0, a1)


def D(x, y):
    return ("D", x, y)


_S_LOWER = [A(2, 3.75, 1.8, 1.25, 20, 270), A(2, 1.25, 1.8, 1.25, 90, -160)]
_S_UPPER = [A(2.6, 5.6, 2.4, 1.9, 20, 270), A(2.6, 1.9, 2.4, 1.9, 90, -160)]

GLYPHS = {
    # lowercase
    "a": [A(2, 2.5, 2, 2.5, 0, 360), L(4, 5, 4, 0)],
    "b": [L(0, 8, 0, 0), A(2, 2.5, 2, 2.5, 0, 360)],
    "c": [A(2, 2.5, 2, 2.5, 45, 315)],
    "d": [A(2, 2.5, 2, 2.5, 0, 360), L(4, 8, 4, 0)],
    "e": [L(0.1, 2.5, 4, 2.5), A(2, 2.5, 2, 2.5, 0, 315)],
    "f": [L(1.5, 0, 1.5, 6.2), A(3, 6.2, 1.5, 1.6, 180, 30), L(0, 4.6, 3, 4.6)],
    "g": [A(2, 2.5, 2, 2.5, 0, 360), L(4, 5, 4, -1), A(2, -1, 2, 1.5, 0, -160)],
    "h": [L(0, 8, 0, 0), A(2, 2.8, 2, 2.2, 180, 0), L(4, 2.8, 4, 0)],
    "i": [L(1, 5, 1, 0), D(1, 6.6)],
    "j": [L(2, 5, 2, -1), A(0.6, -1, 1.4, 1.5, 0, -170), D(2, 6.6)],
    "k": [L(0, 8, 0, 0), L(3.6, 5, 0.1, 2.2), L(1.3, 3.1, 3.8, 0)],
    "l": [L(0.8, 8, 0.8, 0.9), A(1.8, 0.9, 1.0, 0.9, 180, 290)],
    "m": [L(0, 5, 0, 0), A(1.6, 3.3, 1.6, 1.7, 180, 0), L(3.2, 3.3, 3.2, 0),
          A(4.8, 3.3, 1.6, 1.7, 180, 0), L(6.4, 3.3, 6.4, 0)],
    "n": [L(0, 5, 0, 0), A(2, 2.8, 2, 2.2, 180, 0), L(4, 2.8, 4, 0)],
    "o": [A(2, 2.5, 2, 2.5, 90, 450)],
    "p": [L(0, 5, 0, -2.5), A(2, 2.5, 2, 2.5, 0, 360)],
    "q": [A(2, 2.5, 2, 2.5, 0, 360), L(4, 5, 4, -2.5), L(4, -2.5, 4.8, -1.8)],
    "r": [L(0, 5, 0, 0), A(2.2, 2.8, 2.2, 2.2, 180, 55)],
    "s": _S_LOWER,
    "t": [L(1.5, 7, 1.5, 0.9), A(2.6, 0.9, 1.1, 0.9, 180, 300), L(0, 5, 3.2, 5)],
    "u": [L(0, 5, 0, 2.2), A(2, 2.2, 2, 2.2, 180, 360), L(4, 5, 4, 0)],
    "v": [L(0, 5, 2, 0), L(2, 0, 4, 5)],
    "w": [L(0, 5, 1.5, 0), L(1.5, 0, 3, 4), L(3, 4, 4.5, 0), L(4.5, 0, 6, 5)],
    "x": [L(0, 5, 4, 0), L(4, 5, 0, 0)],
    "y": [L(0, 5, 2.1, 0.2), L(4, 5, 1.0, -2.5)],
    "z": [L(0, 5, 4, 5), L(4, 5, 0, 0), L(0, 0, 4, 0)],
    # uppercase
    "A": [L(0, 0, 2.6, 7.5), L(2.6, 7.5, 5.2, 0), L(1.0, 3, 4.2, 3)],
    "B": [L(0, 0, 0, 7.5), L(0, 7.5, 2.4, 7.5), A(2.4, 5.75, 1.7, 1.75, 90, -90), L(2.4, 4, 0, 4),
          L(0, 4, 2.7, 4), A(2.7, 2, 2, 2, 90, -90), L(2.7, 0, 0, 0)],
    "C": [A(3, 3.75, 3, 3.75, 45, 315)],
    "D": [L(0, 0, 0, 7.5), L(0, 7.5, 1.6, 7.5), A(1.6, 3.75, 3.2, 3.75, 90, -90), L(1.6, 0, 0, 0)],
    "E": [L(4, 7.5, 0, 7.5), L(0, 7.5, 0, 0), L(0, 0, 4.2, 0), L(0, 3.9, 3.2, 3.9)],
    "F": [L(4, 7.5, 0, 7.5), L(0, 7.5, 0, 0), L(0, 3.9, 3.2, 3.9)],
    "G": [A(3, 3.75, 3, 3.75, 45, 345), L(3.6, 3.2, 5.95, 3.2)],
    "H": [L(0, 0, 0, 7.5), L(5, 0, 5, 7.5), L(0, 3.8, 5, 3.8)],
    "I": [L(1, 0, 1, 7.5), L(0, 7.5, 2, 7.5), L(0, 0, 2, 0)],
    "J": [L(3.6, 7.5, 3.6, 1.8), A(1.8, 1.8, 1.8, 1.8, 0, -180)],
    "K": [L(0, 0, 0, 7.5), L(4.4, 7.5, 0.1, 3.0), L(1.5, 4.4, 4.8, 0)],
    "L": [L(0, 7.5, 0, 0), L(0, 0, 4, 0)],
    "M": [L(0, 0, 0.4, 7.5), L(0.4, 7.5, 3, 2.2), L(3, 2.2, 5.6, 7.5), L(5.6, 7.5, 6, 0)],
    "N": [L(0, 0, 0, 7.5), L(0, 7.5, 5, 0), L(5, 0, 5, 7.5)],
    "O": [A(3, 3.75, 3, 3.75, 90, 450)],
    "P": [L(0, 0, 0, 7.5), L(0, 7.5, 2.4, 7.5), A(2.4, 5.5, 1.9, 2, 90, -90), L(2.4, 3.5, 0, 3.5)],
    "Q": [A(3, 3.75, 3, 3.75, 90, 450), L(3.6, 1.6, 6.2, -0.6)],
    "R": [L(0, 0, 0, 7.5), L(0, 7.5, 2.4, 7.5), A(2.4, 5.5, 1.9, 2, 90, -90), L(2.4, 3.5, 0, 3.5),
          L(2.0, 3.5, 4.8, 0)],
    "S": _S_UPPER,
    "T": [L(0, 7.5, 5.2, 7.5), L(2.6, 7.5, 2.6, 0)],
    "U": [L(0, 7.5, 0, 2.6), A(2.5, 2.6, 2.5, 2.6, 180, 360), L(5, 2.6, 5, 7.5)],
    "V": [L(0, 7.5, 2.6, 0), L(2.6, 0, 5.2, 7.5)],
    "W": [L(0, 7.5, 1.8, 0), L(1.8, 0, 3.5, 6), L(3.5, 6, 5.2, 0), L(5.2, 0, 7, 7.5)],
    "X": [L(0, 7.5, 5, 0), L(5, 7.5, 0, 0)],
    "Y": [L(0, 7.5, 2.5, 3.8), L(5, 7.5, 2.5, 3.8), L(2.5, 3.8, 2.5, 0)],
    "Z": [L(0, 7.5, 5, 7.5), L(5, 7.5, 0, 0), L(0, 0, 5, 0)],
    # digits
    "0": [A(2.2, 3.6, 2.2, 3.6, 90, 450)],
    "1": [L(0.6, 5.8, 2, 7.2), L(2, 7.2, 2, 0)],
    "2": [A(2.1, 5.1, 2, 2.1, 160, -30), L(3.9, 4.1, 0, 0), L(0, 0, 4.2, 0)],
    "3": [A(2.0, 5.3, 1.9, 1.9, 150, -90), A(2.1, 1.9, 2.1, 1.9, 90, -150)],
    "4": [L(3.2, 0, 3.2, 7.2), L(3.2, 7.2, 0, 2.2), L(0, 2.2, 4.6, 2.2)],
    "5": [L(4, 7.2, 0.8, 7.2), L(0.8, 7.2, 0.5, 4.1), A(2.1, 2.2, 2.1, 2.2, 130, -150)],
    "6": [L(3.6, 7.2, 0.6, 3.0), A(2.2, 2.2, 2.2, 2.2, 0, 360)],
    "7": [L(0, 7.2, 4.2, 7.2), L(4.2, 7.2, 1.4, 0)],
    "8": [A(2.1, 5.5, 1.7, 1.7, 0, 360), A(2.1, 1.9, 2.1, 1.9, 0, 360)],
    "9": [A(2.1, 5.0, 2.1, 2.2, 0, 360), L(4.2, 5.0, 3.0, 0)],
    # punctuation
    ".": [D(0.5, 0.3)],
    ",": [L(0.7, 0.6, 0.1, -1.2)],
    "'": [L(0.6, 7.6, 0.4, 5.8)],
    '"': [L(0.5, 7.6, 0.3, 5.8), L(1.6, 7.6, 1.4, 5.8)],
    "!": [L(0.7, 7.5, 0.5, 2.2), D(0.45, 0.3)],
    "?": [A(2, 5.6, 1.9, 1.9, 160, -60), L(2.95, 3.95, 2.0, 2.4), D(2.0, 0.3)],
    "-": [L(0, 2.8, 2.4, 2.8)],
    ":": [D(0.5, 4.4), D(0.5, 0.3)],
    ";": [D(0.6, 4.4), L(0.7, 0.6, 0.1, -1.2)],
    "(": [A(2.4, 3.2, 2.2, 5.2, 120, 240)],
    ")": [A(-0.2, 3.2, 2.2, 5.2, 60, -60)],
    "[": [L(1.6, 8, 0, 8), L(0, 8, 0, -1.6), L(0, -1.6, 1.6, -1.6)],
    "]": [L(0, 8, 1.6, 8), L(1.6, 8, 1.6, -1.6), L(1.6, -1.6, 0, -1.6)],
    "{": [A(2.0, 5.6, 1.0, 2.4, 90, 180), L(1.0, 5.6, 0.2, 3.2), L(0.2, 3.2, 1.0, 0.8),
          A(2.0, 0.8, 1.0, 2.4, 180, 270)],
    "}": [A(0.0, 5.6, 1.0, 2.4, 90, 0), A(0.0, 0.8, 1.0, 2.4, 0, -90), L(1.0, 5.6, 1.8, 3.2), L(1.8, 3.2, 1.0, 0.8)],
    "/": [L(0, -1, 3.5, 8)],
    "\\": [L(0, 8, 3.5, -1)],
    "|": [L(0.5, 8, 0.5, -2)],
    "&": [L(4.6, 0, 1.0, 5.2), A(1.9, 6.2, 1.2, 1.2, 215, -35), L(2.9, 5.5, 0.6, 2.6),
          A(2.1, 1.6, 1.6, 1.6, 150, 330), L(3.6, 1.0, 4.6, 2.8)],
    "+": [L(0, 3, 3.2, 3), L(1.6, 1.4, 1.6, 4.6)],
    "=": [L(0, 2, 3.2, 2), L(0, 3.6, 3.2, 3.6)],
    "*": [L(1.5, 7.5, 1.5, 4.5), L(0.2, 6.8, 2.8, 5.2), L(0.2, 5.2, 2.8, 6.8)],
    "%": [A(1, 6.5, 0.9, 1, 0, 360), L(4, 7.5, 0, 0), A(3.2, 1, 0.9, 1, 0, 360)],
    "#": [L(1.3, 0, 2, 7), L(3.3, 0, 4, 7), L(0.2, 2.4, 4.6, 2.4), L(0.4, 4.8, 4.8, 4.8)],
    "$": _S_UPPER + [L(2.6, 8.4, 2.6, -0.9)],
    "@": [A(3.2, 3.6, 1.2, 1.5, 0, 360), L(4.4, 4.8, 4.4, 2.4), A(3.4, 3.6, 3.4, 3.9, -10, 330)],
    "<": [L(3, 5, 0, 2.6), L(0, 2.6, 3, 0.2)],
    ">": [L(0, 5, 3, 2.6), L(3, 2.6, 0, 0.2)],
    "^": [L(0, 5.8, 1.5, 7.6), L(1.5, 7.6, 3, 5.8)],
    "_": [L(0, -1.2, 4, -1.2)],
    "`": [L(0, 7.6, 0.8, 6.6)],
    "~": [A(1, 3, 1, 0.8, 180, 0), A(3, 3, 1, 0.8, 180, 360)],
}

# Code points beyond ASCII that the game's text uses, mapped to atlas slots.
EXTRA = {
    0x2014: ("emdash", [L(0, 2.8, 6, 2.8)]),
    0x2026: ("ellipsis", [D(0.5, 0.3), D(2.3, 0.3), D(4.1, 0.3)]),
    0x00B0: ("degree", [A(1, 6.5, 0.9, 0.9, 0, 360)]),
    0x2019: ("rsquo", [L(0.8, 7.6, 0.2, 5.8)]),
}

FIRST, LAST = 32, 126
EXTRA_SLOTS = sorted(EXTRA)                  # slot 95.. in the table
SPACE_ADVANCE = 3.3
LETTER_GAP = 1.15
SLANT = 0.14
VARIANTS = 2


def glyph_strokes(code):
    if code in EXTRA:
        return EXTRA[code][1]
    ch = chr(code)
    return GLYPHS.get(ch, [])


def slot_codes():
    return list(range(FIRST, LAST + 1)) + EXTRA_SLOTS


def _sample(prim, rng, wobble):
    """Turn a primitive into a list of polylines (unit space)."""
    kind = prim[0]
    if kind == "L":
        _, x0, y0, x1, y1 = prim
        n = max(2, int(math.hypot(x1 - x0, y1 - y0) * 2.5))
        t = np.linspace(0, 1, n)
        xs = x0 + (x1 - x0) * t
        ys = y0 + (y1 - y0) * t
    elif kind == "A":
        _, cx, cy, rx, ry, a0, a1 = prim
        n = max(3, int(abs(a1 - a0) / 7) + 1)
        a = np.radians(np.linspace(a0, a1, n))
        xs = cx + rx * np.cos(a)
        ys = cy + ry * np.sin(a)
    else:
        _, x, y = prim
        xs = np.array([x - 0.16, x + 0.16])
        ys = np.array([y, y + 0.12])
    # A slow wobble along the stroke, so straight lines aren't ruler-straight.
    phase = rng.random() * 6.28
    k = np.linspace(0, 1, len(xs))
    xs = xs + wobble * np.sin(k * 3.1 + phase) + rng.normal(0, wobble * 0.5)
    ys = ys + wobble * np.cos(k * 2.3 + phase) + rng.normal(0, wobble * 0.5)
    return [(xs, ys, kind == "D")]


def render_glyph(code, scale, variant, seed=1234):
    """Rasterise one glyph. Returns (coverage HxW float, left_px, top_px, advance_px)."""
    rng = np.random.default_rng(seed + code * 7 + variant * 1009)
    prims = glyph_strokes(code)
    ss = 4                                     # supersampling
    pad = 2.0                                  # px around the ink
    strokes = []
    for p in prims:
        strokes += _sample(p, rng, 0.06 + 0.04 * variant)

    if strokes:
        allx = np.concatenate([s[0] + SLANT * s[1] for s in strokes])
        minx, maxx = allx.min(), allx.max()
    else:
        minx, maxx = 0.0, 0.0
    asc, desc = 8.6, -3.0
    left = minx * scale - pad
    width = int(math.ceil((maxx - minx) * scale + 2 * pad))
    height = int(math.ceil((asc - desc) * scale + 2 * pad))
    top = -(asc * scale + pad)                 # cell top relative to baseline (y down)

    img = Image.new("L", (max(1, width) * ss, height * ss), 0)
    d = ImageDraw.Draw(img)
    base_w = 0.62 * scale * ss * (0.92 + 0.16 * rng.random())
    for xs, ys, is_dot in strokes:
        px = ((xs + SLANT * ys) * scale - left) * ss
        py = (-ys * scale - top) * ss
        n = len(px)
        for i in range(n - 1):
            # Pressure: thinner at the very ends of each stroke.
            t = i / max(1, n - 2)
            pr = 0.78 + 0.22 * math.sin(math.pi * min(1.0, max(0.0, t)))
            w = max(1.0, base_w * (1.9 if is_dot else pr))
            d.line([(px[i], py[i]), (px[i + 1], py[i + 1])], fill=255, width=int(round(w)))
            r = w / 2
            d.ellipse([px[i + 1] - r, py[i + 1] - r, px[i + 1] + r, py[i + 1] + r], fill=255)
        r = base_w / 2
        d.ellipse([px[0] - r, py[0] - r, px[0] + r, py[0] + r], fill=255)

    a = np.asarray(img, dtype=np.float64) / 255.0
    # Pencil tooth: graphite skips a little on the paper grain.
    tooth = rng.random(a.shape)
    a = a * (0.78 + 0.22 * tooth)
    cov = a.reshape(height, ss, max(1, width), ss).mean(axis=(1, 3))
    cov = np.clip(cov * 1.25, 0, 1)
    advance = ((maxx - minx) + LETTER_GAP) * scale if strokes else SPACE_ADVANCE * scale
    # The ink starts at the pen and the advance covers the ink plus the gap, so
    # every pair of letters (and every word space) is spaced the same.
    return cov, -pad, top, advance


def build_atlas(scale, atlas_w, atlas_h):
    """Pack all glyph variants into one atlas. Returns (atlas, table, line metrics)."""
    atlas = np.zeros((atlas_h, atlas_w))
    table = []
    x = y = 0
    row_h = 0
    for code in slot_codes():
        for v in range(VARIANTS):
            cov, left, top, adv = render_glyph(code, scale, v)
            h, w = cov.shape
            if x + w > atlas_w:
                x = 0
                y += row_h + 1
                row_h = 0
            if y + h > atlas_h:
                raise ValueError(f"atlas {atlas_w}x{atlas_h} too small at scale {scale}")
            atlas[y:y + h, x:x + w] = np.maximum(atlas[y:y + h, x:x + w], cov)
            table.append(dict(code=code, variant=v, x=x, y=y, w=w, h=h,
                              xoff=int(round(left)), yoff=int(round(top)),
                              adv8=int(round(adv * 8))))
            x += w + 1
            row_h = max(row_h, h)
    metrics = dict(line_h=int(round(12.5 * scale)), ascent=int(round(7.5 * scale)),
                   xheight=int(round(5 * scale)))
    return atlas, table, metrics


def c_table(name, table, metrics, atlas_w, atlas_h):
    lines = [f"/* Generated by tools/font/lookout_hand.py -- do not edit. */",
             f"#define {name.upper()}_TEX_W {atlas_w}",
             f"#define {name.upper()}_TEX_H {atlas_h}",
             f"#define {name.upper()}_LINE_H {metrics['line_h']}",
             f"#define {name.upper()}_ASCENT {metrics['ascent']}",
             f"#define {name.upper()}_XHEIGHT {metrics['xheight']}",
             f"static const ll_glyph {name}_glyphs[{len(table)}] = {{"]
    for g in table:
        lines.append(f"  {{{g['x']},{g['y']},{g['w']},{g['h']},{g['xoff']},{g['yoff']},{g['adv8']}}},"
                     f" /* U+{g['code']:04X} v{g['variant']} */")
    lines.append("};")
    return "\n".join(lines) + "\n"


def c_slot_map():
    """C helper mapping code points to slots (shared by every size)."""
    out = ["/* Generated by tools/font/lookout_hand.py -- do not edit. */",
           f"#define FONT_FIRST {FIRST}",
           f"#define FONT_LAST {LAST}",
           f"#define FONT_VARIANTS {VARIANTS}",
           f"#define FONT_SLOTS {len(slot_codes())}",
           "static inline int font_slot(unsigned cp) {",
           f"  if (cp >= {FIRST} && cp <= {LAST}) return (int)cp - {FIRST};"]
    for i, cp in enumerate(EXTRA_SLOTS):
        out.append(f"  if (cp == 0x{cp:04X}u) return {LAST - FIRST + 1 + i};  /* {EXTRA[cp][0]} */")
    out += ["  if (cp == 0x2018u) return '\\'' - " + str(FIRST) + ";",
            "  if (cp == 0x201Cu || cp == 0x201Du) return '\"' - " + str(FIRST) + ";",
            "  return '?' - " + str(FIRST) + ";", "}"]
    return "\n".join(out) + "\n"
