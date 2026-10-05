"""Shared helpers for the tileable PBR texture generator (numpy + scipy only; no Pillow).

Everything here is periodic: fields are made with FFT filtering and warps use grid-wrap, so every
output tiles seamlessly at N x N.  Run via /home/user/tools/bpyenv/bin/python (it has numpy/scipy).
"""
import os
import struct
import zlib

import numpy as np
from scipy import ndimage as ndi

N = 1024


# ----------------------------------------------------------------------------- colour / png
def srgb_to_lin(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 1e-9), 1 / 2.4) - 0.055)


def hexlin(h):
    h = h.lstrip("#")
    return srgb_to_lin(np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]))


def _png_chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, arr):
    """arr: HxWxC uint8 (C = 3 or 4)."""
    arr = np.ascontiguousarray(arr)
    h, w, c = arr.shape
    best = None
    for ftype in (1, 2):   # Sub / Up filters; keep the smaller result
        if ftype == 1:
            f = arr.astype(np.int16)
            f[:, 1:, :] = arr[:, 1:, :].astype(np.int16) - arr[:, :-1, :].astype(np.int16)
        else:
            f = arr.astype(np.int16)
            f[1:, :, :] = arr[1:, :, :].astype(np.int16) - arr[:-1, :, :].astype(np.int16)
        f = (f & 0xFF).astype(np.uint8).reshape(h, w * c)
        raw = np.concatenate([np.full((h, 1), ftype, np.uint8), f], axis=1).tobytes()
        comp = zlib.compress(raw, 9)
        if best is None or len(comp) < len(best):
            best = comp
    ctype = {3: 2, 4: 6}[c]
    png = b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    png += _png_chunk(b"IDAT", best) + _png_chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(png)


def to_u8(x):
    return np.clip(np.round(x * 255.0), 0, 255).astype(np.uint8)


# ----------------------------------------------------------------------------- periodic noise
_KX = np.fft.fftfreq(N)[None, :] * N
_KY = np.fft.fftfreq(N)[:, None] * N


def fbm(seed, beta=2.0, kmin=1.0, kmax=N / 2, ax=1.0, ay=1.0):
    """Periodic power-law noise, zero mean / unit std.  beta: spectral slope (2 = natural, 1 = rough).
    ax/ay stretch the spectrum (ax<1 => features elongated along x)."""
    rng = np.random.default_rng(seed)
    F = np.fft.fft2(rng.standard_normal((N, N)))
    k = np.sqrt((_KX / ax) ** 2 + (_KY / ay) ** 2)
    k[0, 0] = 1.0
    amp = k ** (-beta / 2.0)
    amp[(k < kmin) | (k > kmax)] = 0.0
    amp[0, 0] = 0.0
    n = np.fft.ifft2(F * amp).real
    return n / n.std()


def gnoise(seed, cx, cy):
    """Band-limited gaussian noise: cx/cy = characteristic cycles per tile along x / y."""
    rng = np.random.default_rng(seed)
    F = np.fft.fft2(rng.standard_normal((N, N)))
    amp = np.exp(-((_KX / cx) ** 2 + (_KY / cy) ** 2) * 0.5)
    amp[0, 0] = 0.0
    n = np.fft.ifft2(F * amp).real
    return n / n.std()


def blur(x, sigma):
    return ndi.gaussian_filter(x, sigma, mode="wrap")


def warp(img, dx, dy):
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float64)
    coords = np.array([(yy + dy) % N, (xx + dx) % N])
    return ndi.map_coordinates(img, coords, order=1, mode="grid-wrap")


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def normal_from_height(h_mm, px_mm, strength=1.0):
    """OpenGL-style tangent-space normal (green = image-up).  h in millimetres, px_mm = mm per pixel."""
    dhx = (np.roll(h_mm, -1, 1) - np.roll(h_mm, 1, 1)) / (2.0 * px_mm)
    dhr = (np.roll(h_mm, -1, 0) - np.roll(h_mm, 1, 0)) / (2.0 * px_mm)     # per image row (downwards)
    nx = -dhx * strength
    ny = dhr * strength                                                    # up = -row  =>  +dh/drow
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / ln, ny / ln, nz / ln], axis=-1)


def cavity_ao(h_mm, sigma_px, scale_mm, floor=0.0):
    """Cheap ambient occlusion from a height field: low relative to its blurred surroundings => occluded."""
    d = blur(h_mm, sigma_px) - h_mm
    return np.clip(1.0 - np.clip(d, 0, None) / scale_mm, floor, 1.0)


# ----------------------------------------------------------------------------- layouts
class Layout:
    """Periodic running-bond layout: rows of random height, each row split into blocks of random width.
    The widths of every row sum exactly to the tile width, and rows are shifted so vertical joints of
    neighbouring rows stay apart (hand-laid look)."""

    def __init__(self, rows, wmin, wmax, seed, hjit=0.15, minoff=0.08):
        rng = np.random.default_rng(seed)
        hs = rng.uniform(1 - hjit, 1 + hjit, rows)
        hs = hs / hs.sum() * N
        self.row_edges = np.concatenate([[0.0], np.cumsum(hs)])
        self.rows = rows
        self.row_joints = []
        self.row_base = []
        prev = None
        base = 0
        for r in range(rows):
            for _try in range(200):
                widths = []
                tot = 0.0
                while tot < 1.0:
                    w = rng.uniform(wmin, wmax)
                    widths.append(w)
                    tot += w
                widths = np.array(widths) / tot * N
                shift = rng.uniform(0, N)
                j = np.sort((np.cumsum(widths) - widths[0] + shift) % N)
                if prev is None:
                    break
                dmin = np.min(np.abs(j[:, None] - prev[None, :]))
                if dmin > minoff * N * 0.5 * (wmin + wmax):
                    break
            self.row_joints.append(j)
            self.row_base.append(base)
            base += len(j)
            prev = j
        self.count = base

    def label(self, xw, yw):
        """xw, yw: warped pixel coordinates (arrays in [0, N)).  Returns dict of arrays."""
        row = np.clip(np.searchsorted(self.row_edges, yw, side="right") - 1, 0, self.rows - 1)
        ids = np.zeros(xw.shape, np.int32)
        dx = np.zeros(xw.shape)
        u = np.zeros(xw.shape)
        bw = np.zeros(xw.shape)
        for r in range(self.rows):
            m = row == r
            if not m.any():
                continue
            j = self.row_joints[r]
            xs = (xw[m] - j[0]) % N
            edges = np.concatenate([j - j[0], [N]])
            c = np.clip(np.searchsorted(edges, xs, side="right") - 1, 0, len(j) - 1)
            left = xs - edges[c]
            right = edges[c + 1] - xs
            ids[m] = self.row_base[r] + c
            dx[m] = np.minimum(left, right)
            w = edges[c + 1] - edges[c]
            bw[m] = w
            u[m] = left / w
        y0 = self.row_edges[row]
        y1 = self.row_edges[row + 1]
        dy = np.minimum(yw - y0, y1 - yw)
        bh = y1 - y0
        v = (yw - y0) / bh
        return dict(id=ids, dx=dx, dy=dy, d=np.minimum(dx, dy), u=u, v=v, bw=bw, bh=bh, row=row)


def per_id(arr, ids):
    return np.asarray(arr)[ids]


def write_import(png_path, normal_map=False, mipmaps=True):
    """Hand-written .import so mipmaps + VRAM compression are on (Godot fills uid/path on first import)."""
    res = "res://" + os.path.relpath(png_path, os.path.join(os.path.dirname(__file__), "..", "..", "game")).replace(os.sep, "/")
    text = f'''[remap]

importer="texture"
type="CompressedTexture2D"

[params]

compress/mode=2
compress/high_quality=false
compress/lossy_quality=0.7
compress/uastc_level=0
compress/rdo_quality_loss=0.0
compress/hdr_compression=1
compress/normal_map={1 if normal_map else 0}
compress/channel_pack=0
mipmaps/generate={"true" if mipmaps else "false"}
mipmaps/limit=-1
roughness/mode=0
roughness/src_normal=""
process/channel_remap/red=0
process/channel_remap/green=1
process/channel_remap/blue=2
process/channel_remap/alpha=3
process/fix_alpha_border=false
process/premult_alpha=false
process/normal_map_invert_y=false
process/hdr_as_srgb=false
process/hdr_clamp_exposure=false
process/size_limit=0
detect_3d/compress_to=0
'''
    imp = png_path + ".import"
    if os.path.exists(imp) and "uid=" in open(imp).read():
        # keep Godot's generated uid/path; only refresh params
        old = open(imp).read()
        head = old.split("[params]")[0]
        text = head + "[params]" + text.split("[params]")[1]
    open(imp, "w").write(text)
