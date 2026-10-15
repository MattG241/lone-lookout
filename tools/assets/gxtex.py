"""
gxtex.py -- encode numpy images into GX-native tiled texture data.

The Wii's GPU reads textures in small tiles (4x4, 8x4 or 8x8 texels
depending on format). Writing the tiled layout here means the game can hand
the embedded bytes straight to GX_InitTexObj() with no conversion at boot
and no third-party converters in the build.

All functions take float arrays in [0,1] (H x W, or H x W x C) and return
bytes. Width and height must be multiples of the format's tile size.
"""

import numpy as np

# GX texture format IDs (match GX_TF_* in libogc's gx.h)
GX_TF_I4 = 0x0
GX_TF_I8 = 0x1
GX_TF_IA4 = 0x2
GX_TF_IA8 = 0x3
GX_TF_RGB565 = 0x4
GX_TF_RGB5A3 = 0x5
GX_TF_RGBA8 = 0x6


def _q(a, levels):
    """Quantise [0,1] floats to integers 0..levels-1 with rounding."""
    return np.clip(np.rint(np.asarray(a, dtype=np.float64) * (levels - 1)), 0, levels - 1).astype(np.uint32)


def _tiles(a, tw, th):
    """Reorder an (H, W, ...) array into (tiles_y, tiles_x, th, tw, ...) order."""
    h, w = a.shape[:2]
    assert h % th == 0 and w % tw == 0, f"{w}x{h} not a multiple of {tw}x{th}"
    rest = a.shape[2:]
    t = a.reshape(h // th, th, w // tw, tw, *rest)
    return t.swapaxes(1, 2)


def encode_i4(img):
    v = _q(img, 16)
    t = _tiles(v, 8, 8).reshape(-1, 2)           # pairs of texels, row-major in tile
    return ((t[:, 0] << 4) | t[:, 1]).astype(np.uint8).tobytes()


def encode_i8(img):
    return _tiles(_q(img, 256), 8, 4).astype(np.uint8).tobytes()


def encode_ia4(intensity, alpha):
    v = (_q(alpha, 16) << 4) | _q(intensity, 16)
    return _tiles(v, 8, 4).astype(np.uint8).tobytes()


def encode_ia8(intensity, alpha):
    a = _q(alpha, 256)
    i = _q(intensity, 256)
    v = np.stack([a, i], axis=-1)                # A first, then I
    return _tiles(v, 4, 4).astype(np.uint8).tobytes()


def encode_rgb565(rgb):
    r = _q(rgb[..., 0], 32)
    g = _q(rgb[..., 1], 64)
    b = _q(rgb[..., 2], 32)
    v = (r << 11) | (g << 5) | b
    return _tiles(v, 4, 4).astype(">u2").tobytes()


def encode_rgb5a3(rgba):
    a8 = _q(rgba[..., 3], 256)
    opaque = a8 >= 255
    r5, g5, b5 = (_q(rgba[..., c], 32) for c in range(3))
    r4, g4, b4 = (_q(rgba[..., c], 16) for c in range(3))
    a3 = _q(rgba[..., 3], 8)
    v = np.where(opaque,
                 0x8000 | (r5 << 10) | (g5 << 5) | b5,
                 (a3 << 12) | (r4 << 8) | (g4 << 4) | b4)
    return _tiles(v, 4, 4).astype(">u2").tobytes()


def encode_rgba8(rgba):
    q = _q(rgba, 256)
    t = _tiles(q, 4, 4).reshape(-1, 16, 4)       # (tiles, 16 texels, RGBA)
    ar = np.stack([t[..., 3], t[..., 0]], axis=-1).reshape(-1, 32)
    gb = np.stack([t[..., 1], t[..., 2]], axis=-1).reshape(-1, 32)
    return np.concatenate([ar, gb], axis=1).astype(np.uint8).tobytes()


def decode_i4(data, w, h):
    """Inverse of encode_i4 -- used by the self-test only."""
    b = np.frombuffer(data, dtype=np.uint8)
    v = np.stack([b >> 4, b & 15], axis=-1).reshape(h // 8, w // 8, 8, 8)
    return v.swapaxes(1, 2).reshape(h, w) / 15.0


def _self_test():
    rng = np.random.default_rng(0)
    img = rng.random((16, 16))
    back = decode_i4(encode_i4(img), 16, 16)
    assert np.allclose(back, np.rint(img * 15) / 15)
    assert len(encode_i8(rng.random((8, 16)))) == 128
    assert len(encode_ia8(rng.random((8, 8)), rng.random((8, 8)))) == 128
    assert len(encode_rgba8(rng.random((8, 8, 4)))) == 256
    # RGBA8: first tile's first AR pair must be (A, R) of texel (0,0).
    px = np.zeros((4, 4, 4))
    px[0, 0] = [1.0, 0.5, 0.25, 0.75]
    enc = encode_rgba8(px)
    assert enc[0] == 191 and enc[1] == 255 and enc[32] == 128 and enc[33] == 64
    print("gxtex self-test ok")


if __name__ == "__main__":
    _self_test()
