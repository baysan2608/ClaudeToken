"""Fourfold VFX - generates the 8x8 flipbook atlases (1024^2, 128 px frames) in unreal/SourceArt/VFX/Flipbooks.

    /home/user/tools/bpyenv/bin/python unreal/Tools/vfx/flipbooks.py [names...] [--preview DIR] [--quick]

Simulated with ff_fluid.Smoke3D (numpy stable fluids, see that module for why not Mantaflow) and rendered by its
volume ray-marcher; splashes / grains are ballistic particles splatted with soft shaded discs. Deterministic (fixed
seeds). Packed format (linear, sRGB OFF in Unreal, TC_Default):
  R = lighting (key + sky, not premultiplied)   G = optical thickness 0..1 (soft edges / dissolve)
  B = emission temperature 0..1 (black-body in the shader; 0 for non-fire)   A = coverage alpha
Frame order: row-major from the top-left, frame f at column f % 8, row f // 8 (FFFlipbookUV in Shaders/Common).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ff_fluid import Smoke3D, render  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "..", "SourceArt", "VFX", "Flipbooks"))
FRAMES = 64
TILE = 128
GRID = 8


def _fit_tile(img, tile=TILE, margin=0.0):
    """Resize a rendered frame (H, W, 4) to the tile, keeping aspect (pad with zero alpha)."""
    h, w = img.shape[:2]
    s = (tile * (1.0 - margin)) / max(h, w)
    out = np.zeros((tile, tile, 4), np.float32)
    nh, nw = max(1, int(round(h * s))), max(1, int(round(w * s)))
    rs = np.stack([np.asarray(Image.fromarray(img[:, :, c]).resize((nw, nh), Image.BICUBIC)) for c in range(4)], -1)
    y0 = (tile - nh) // 2
    x0 = (tile - nw) // 2
    out[y0:y0 + nh, x0:x0 + nw] = rs
    return np.clip(out, 0.0, 1.0)


def _fade_edges(tile_img, px=3):
    """Zero alpha at the tile border so bilinear / mip sampling never bleeds a neighbour frame in."""
    a = tile_img[:, :, 3]
    ramp = np.clip(np.minimum.reduce([np.arange(TILE)[:, None] + 0 * np.arange(TILE)[None, :],
                                      (TILE - 1 - np.arange(TILE))[:, None] + 0 * np.arange(TILE)[None, :],
                                      np.arange(TILE)[None, :] + 0 * np.arange(TILE)[:, None],
                                      (TILE - 1 - np.arange(TILE))[None, :] + 0 * np.arange(TILE)[:, None]]) / px,
                   0.0, 1.0)
    tile_img[:, :, 3] = a * ramp
    tile_img[:, :, 2] *= ramp
    return tile_img


def pack_atlas(frames):
    atlas = np.zeros((TILE * GRID, TILE * GRID, 4), np.float32)
    for i, f in enumerate(frames[:GRID * GRID]):
        r, c = divmod(i, GRID)
        atlas[r * TILE:(r + 1) * TILE, c * TILE:(c + 1) * TILE] = _fade_edges(f.copy())
    return atlas


def save_png(arr, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray((np.clip(arr, 0, 1) * 255.0 + 0.5).astype(np.uint8), "RGBA").save(path, optimize=True)


def blackbody(t):
    t = np.clip(t, 0, 1)[..., None]
    c = np.zeros(t.shape[:-1] + (3,), np.float32)

    def ss(e0, e1, x):
        k = np.clip((x - e0) / (e1 - e0), 0, 1)
        return k * k * (3 - 2 * k)
    c = c + (np.array([0.30, 0.012, 0.0]) - c) * ss(0.0, 0.28, t)
    c = c + (np.array([0.95, 0.15, 0.012]) - c) * ss(0.22, 0.55, t)
    c = c + (np.array([1.0, 0.46, 0.06]) - c) * ss(0.5, 0.8, t)
    c = c + (np.array([1.0, 0.82, 0.38]) - c) * ss(0.78, 1.0, t)
    return c


def preview(atlas, path, lit=(0.92, 0.9, 0.86), shadow=(0.30, 0.31, 0.34), emissive=4.0, bg=(0.16, 0.18, 0.22)):
    """Human-readable composite of a packed atlas over a flat background (what the shader roughly does)."""
    lit = np.array(lit, np.float32)
    shadow = np.array(shadow, np.float32)
    L = atlas[..., 0:1]
    col = shadow + (lit - shadow) * L
    a = atlas[..., 3:4]
    em = blackbody(atlas[..., 2]) * emissive * (atlas[..., 2:3] > 0.02)
    out = np.array(bg, np.float32) * (1 - a) + col * a + em
    out = out / (1.0 + out * 0.25)   # mild tonemap for viewing
    img = (np.clip(out, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(img, "RGB").save(path)


# ---------------------------------------------------------------------------------------------- presets
def _recentre(frames, keep_bottom=False, smooth=0.85):
    """Keeps a puff horizontally centred in its tile (turbulence may drift it a few cells): alpha-weighted centroid,
    smoothed over time. Fire keeps its base where it is (keep_bottom) and only the plume above is not moved."""
    out = []
    cx_s = None
    for img in frames:
        a = img[..., 3]
        w = a.sum(0)
        tot = float(w.sum())
        cx = float((w * np.arange(img.shape[1])).sum() / tot) if tot > 1e-3 else img.shape[1] / 2
        cx_s = cx if cx_s is None else cx_s * smooth + cx * (1 - smooth)
        dx = img.shape[1] / 2 - cx_s
        out.append(ndimage.shift(img, (0, dx, 0), order=1, mode="constant") if not keep_bottom else img)
    return out


def _dissolve(img, t, erode=0.55, tail=3.0):
    """Ends a one-shot by eroding thin parts first, fully transparent at t = 1."""
    k = float(np.clip(1.0 - t, 0.0, 1.0))
    thr = (1.0 - k) * erode
    img[..., 3] = np.clip((img[..., 3] - thr) / max(1.0 - thr, 1e-3), 0, 1) * min(1.0, k * tail)
    img[..., 2] *= min(1.0, k * tail)
    return img


def _run(sim, n_frames, substeps, src, rparams, crop=None, warm=0, upscale=1.5, on_frame=None):
    frames = []
    total = warm + n_frames
    for f in range(total):
        for _ in range(substeps):
            sim.step(1.0 / substeps, (lambda s, f=f: src(s, f)) if src else None)
        if f >= warm:
            img = render(sim, crop=crop, upscale=upscale, **rparams)
            if on_frame:
                img = on_frame(img, f - warm)
            frames.append(img)
    return frames


def smoke_puff(quick=False, kind="smoke"):
    """Impact / release puff: a quick radial expansion that slows (drag), rolls up and rises a little, then
    dissolves. steam rises faster and thins out quicker; mist barely rises."""
    n = 40 if quick else 72
    sim = Smoke3D(n, n, n, seed={"smoke": 11, "steam": 23, "mist": 37}[kind])
    c = ((n - 1) * 0.5, n * 0.36, (n - 1) * 0.5)
    style = {
        "smoke": dict(buoy_t=0.03, vort=0.35, turb=0.8, cool=0.05, dens_decay=0.014, vel_decay=0.09),
        "steam": dict(buoy_t=0.07, vort=0.30, turb=1.0, cool=0.03, dens_decay=0.030, vel_decay=0.04),
        "mist": dict(buoy_t=0.01, vort=0.20, turb=0.6, cool=0.03, dens_decay=0.018, vel_decay=0.06),
    }[kind]
    for k, v in style.items():
        setattr(sim, k, v)
    sim.buoy_d = 0.0
    sim.turb_scale = n / 14.0
    sim.sponge = 0.12

    def src(s, f):
        if f < 2:
            m = s.sphere_mask(c, n * 0.12, soft=2.5)
            s.dens += m * 0.7
            s.temp += m * 0.5
            s.radial_velocity(c, 1.15, m, up=0.15)
    ext = {"smoke": 1.3, "steam": 0.9, "mist": 0.6}[kind]
    rp = dict(extinction=ext, ambient=0.5 if kind != "smoke" else 0.38, key=0.95, shadow_ext=ext * 0.8,
              emit_temp_scale=0.0)
    frames = _run(sim, FRAMES, 1, src, rp, crop=((0.04, 0.96), (0.08, 1.0)),
                  on_frame=lambda img, f: _dissolve(img, f / (FRAMES - 1), erode=0.5))
    return _recentre(frames)


def dust_puff(quick=False, sand=False):
    """Ground burst (footstep, impact, rising wall): dust shoots outward along the floor in a rolling ring and
    lifts a little; sand adds ballistic grains."""
    n = 40 if quick else 72
    ny = int(n * 0.5)
    sim = Smoke3D(n, ny, n, seed=41 if not sand else 43)
    sim.buoy_t = 0.006
    sim.buoy_d = 0.003
    sim.vort = 0.45
    sim.turb = 0.9
    sim.turb_scale = n / 14.0
    sim.cool = 0.08
    sim.dens_decay = 0.012
    sim.vel_decay = 0.07
    sim.sponge = 0.1
    c = ((n - 1) * 0.5, 1.5, (n - 1) * 0.5)

    def src(s, f):
        if f < 3:
            m = s.disc_mask(c, n * 0.10, 2.5, soft=2.0)
            s.dens += m * 0.8
            s.temp += m * 0.1
            # outward from a point below the floor: a ground-hugging ring that rolls up at its rim
            s.radial_velocity((c[0], -n * 0.2, c[2]), 1.9, m, up=0.05)
    rp = dict(extinction=1.4, ambient=0.45, key=0.9, shadow_ext=1.2, emit_temp_scale=0.0)

    grains = None
    if sand:
        rng = np.random.default_rng(7)
        k = 140
        ang = rng.uniform(0, 2 * np.pi, k)
        el = rng.uniform(0.4, 1.25, k)
        sp = rng.uniform(0.55, 1.0, k)
        grains = dict(p0=np.zeros((k, 3)), v=np.stack([np.cos(ang) * np.cos(el), np.sin(el), np.sin(ang) * np.cos(el)], 1)
                      * sp[:, None], size=rng.uniform(0.6, 1.4, k))

    def on_frame(img, f):
        img = _dissolve(img, f / (FRAMES - 1), erode=0.45)
        if grains is not None:
            img = _splat_grains(img, grains, f, n)
        return img
    frames = _run(sim, FRAMES, 1, src, rp, crop=((0.0, 1.0), (0.0, 0.92)), on_frame=on_frame)
    return _recentre(frames)


def _splat_grains(img, g, f, n):
    """Ballistic sand grains (specks with a lit top) splatted over the dust frame."""
    h, w = img.shape[:2]
    t = f * 0.055
    p = g["p0"] + g["v"] * t + np.array([0, -0.5 * 1.7 * t * t, 0])
    out = img.copy()
    fade = float(np.clip(1.25 - f / FRAMES * 1.4, 0, 1))
    for (x, y, z), s in zip(p, g["size"]):
        if y < -0.02:
            continue
        px = (0.5 + x * 0.45) * w
        py = h - (0.03 + y * 0.9) * h
        r = s * 1.2
        x0, x1 = int(max(px - 3, 0)), int(min(px + 4, w))
        y0, y1 = int(max(py - 3, 0)), int(min(py + 4, h))
        if x0 >= x1 or y0 >= y1:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1]
        d2 = ((xx - px) ** 2 + (yy - py) ** 2) / (r * r)
        a = np.clip(1.2 - d2, 0, 1) * fade
        lit = np.clip(0.75 - 0.25 * (yy - py) / r, 0.2, 1.0)
        sl = out[y0:y1, x0:x1]
        sl[..., 0] = sl[..., 0] * (1 - a) + lit * a
        sl[..., 1] = np.maximum(sl[..., 1], a * 0.6)
        sl[..., 3] = sl[..., 3] + (1 - sl[..., 3]) * a
    return out


def fire(quick=False, mode="loop"):
    """loop: a steady burning patch (seamless 64-frame loop); burst: a short upward flare that burns out into
    smoke; explosion: a fuel ball igniting at once, expanding fireball rolling into a dark smoke cloud."""
    n = 40 if quick else 64
    ny = int(n * (1.6 if mode != "explosion" else 1.2))
    sim = Smoke3D(n, ny, n, seed={"loop": 101, "burst": 103, "explosion": 107}[mode])
    sim.buoy_t = 0.08 if mode != "explosion" else 0.05
    sim.buoy_d = 0.0
    sim.vort = 0.55
    sim.turb = 1.4 if mode != "explosion" else 1.1
    sim.turb_scale = n / 16.0
    sim.cool = 0.11 if mode == "loop" else 0.075
    sim.dens_decay = 0.05 if mode == "loop" else 0.014
    sim.vel_decay = 0.0 if mode != "explosion" else 0.03
    sim.ignite = 0.1
    sim.burn_rate = 0.5
    sim.burn_heat = 1.4
    sim.burn_soot = 0.18 if mode == "loop" else 0.45
    sim.sponge = 0.15
    base = ((n - 1) * 0.5, 3.0, (n - 1) * 0.5)
    expl = ((n - 1) * 0.5, ny * 0.35, (n - 1) * 0.5)

    def src(s, f):
        if mode == "loop":
            m = s.disc_mask(base, n * 0.11, 1.5, soft=2.0)
            s.fuel += m * 0.32
            s.temp += m * 0.22
            s.u[1] += m * 0.25
        elif mode == "burst":
            if f < 8:
                m = s.disc_mask(base, n * 0.11, 1.5, soft=2.0)
                s.fuel += m * 0.9
                s.temp += m * 0.5
                s.u[1] += m * 1.5
        else:
            if f < 2:
                m = s.sphere_mask(expl, n * 0.12, soft=2.0)
                s.fuel += m * 1.8
                s.temp += m * 1.2
                s.radial_velocity(expl, 2.0, m, up=0.2)
    warm = 48 if mode == "loop" else 0
    rp = dict(extinction=1.1 if mode != "loop" else 0.7, ambient=0.35, key=0.85, emit_temp_scale=0.9)
    crop = ((0.08, 0.92), (0.0, 0.84)) if mode != "explosion" else ((0.0, 1.0), (0.04, 1.0))
    count = FRAMES + (16 if mode == "loop" else 0)

    def on_frame(img, f):
        if mode != "loop":
            img = _dissolve(img, f / (FRAMES - 1), erode=0.5, tail=2.5)
        return img
    frames = _run(sim, count, 2, src, rp, crop=crop, warm=warm, on_frame=on_frame)
    if mode == "loop":
        # seamless loop: cross-fade the 16 extra frames into the first 16
        for i in range(16):
            w = (i + 0.5) / 16.0
            frames[i] = frames[i] * w + frames[FRAMES + i] * (1 - w)
        frames = frames[:FRAMES]
    if mode == "explosion":
        frames = _recentre(frames)
    return frames


def water_splash(quick=False):
    """Crown splash: a rising ring sheet breaking into droplets, a central jet, falling drops; shaded soft blobs
    (refraction-like rim, specular dot), with a thin spray mist."""
    rng = np.random.default_rng(55)
    k = 220 if not quick else 110
    ang = rng.uniform(0, 2 * np.pi, k)
    kind = rng.uniform(0, 1, k)
    crown = kind < 0.7
    el = np.where(crown, rng.uniform(1.0, 1.32, k), rng.uniform(1.38, 1.55, k))
    sp = np.where(crown, rng.uniform(0.8, 1.05, k), rng.uniform(1.0, 1.3, k))
    v = np.stack([np.cos(ang) * np.cos(el), np.sin(el), np.sin(ang) * np.cos(el)], 1) * sp[:, None]
    size = np.where(crown, rng.uniform(0.9, 1.6, k), rng.uniform(1.2, 2.2, k))
    res = 256
    g = 2.3
    frames = []
    for f in range(FRAMES):
        t = f * 0.026
        life = float(np.clip(1.0 - f / FRAMES, 0, 1))
        p = v * t + np.array([0, -0.5 * g * t * t, 0])
        img = np.zeros((res, res, 4), np.float32)
        # crown sheet: early, a thin ring wall connecting the droplets' roots
        if f < 26:
            sheet_h = 0.28 * np.sin(np.clip(f / 26.0, 0, 1) * np.pi)
            rad = 0.10 + 0.85 * t
            yy, xx = np.mgrid[0:res, 0:res]
            xs = (xx / res - 0.5) / 0.36
            ys = (0.93 - yy / res) / 0.62
            band = np.clip(1.0 - np.abs(np.abs(xs) - rad) / 0.035, 0, 1) * (ys > 0) * (ys < sheet_h) * (np.abs(xs) < rad + 0.04)
            front = np.clip(1.0 - (xs / max(rad, 1e-3)) ** 2, 0, 1) * (ys > 0) * (ys < sheet_h * 0.8) * 0.35
            a = np.clip(band * 0.8 + front, 0, 1) * (1.0 - f / 26.0) ** 0.5
            img[..., 0] = 0.55 + 0.35 * band
            img[..., 1] = a * 0.5
            img[..., 3] = a
        order = np.argsort(-p[:, 2])
        for i in order:
            x, y, z = p[i]
            if y < -0.03:
                continue
            s = size[i] * 1.5 * (res / 128.0) * (1.0 + 0.6 * (1 - life))
            vy = v[i, 1] - g * t
            stretch = 1.0 + 1.8 * min(abs(vy), 1.0) * life
            px = res * (0.5 + x * 0.42)
            py = res * (0.93 - y * 0.75)
            rx, ry = s * 1.7, s * 1.7 * stretch
            x0, x1 = int(max(px - rx - 2, 0)), int(min(px + rx + 3, res))
            y0, y1 = int(max(py - ry - 2, 0)), int(min(py + ry + 3, res))
            if x0 >= x1 or y0 >= y1:
                continue
            yy, xx = np.mgrid[y0:y1, x0:x1]
            dx = (xx - px) / rx
            dy = (yy - py) / ry
            d2 = dx * dx + dy * dy
            a = np.clip((1.0 - d2) * 3.0, 0, 1) * (0.6 + 0.4 * life)
            nz = np.sqrt(np.clip(1 - d2, 0, 1))
            ndl = np.clip(-0.5 * dx - 0.6 * dy + 0.62 * nz, 0, 1)
            spec = np.clip((-0.45 * dx - 0.55 * dy + 0.7 * nz) - 0.86, 0, 1) * 7.0
            rim = (1 - nz) ** 2
            lit = np.clip(0.3 + 0.4 * ndl + 0.4 * rim + spec, 0, 1)
            sl = img[y0:y1, x0:x1]
            sl[..., 0] = sl[..., 0] * (1 - a) + lit * a
            sl[..., 1] = np.maximum(sl[..., 1], a * (0.4 + 0.6 * nz))
            sl[..., 3] = sl[..., 3] + (1 - sl[..., 3]) * a
        mist = ndimage.gaussian_filter(img[..., 3], 7) * 0.45 * life
        img[..., 0] = np.where(img[..., 3] > 0.01, img[..., 0], 0.85)
        img[..., 3] = np.clip(img[..., 3] + mist * (1 - img[..., 3]), 0, 1)
        frames.append(_dissolve(img, f / (FRAMES - 1), erode=0.3, tail=4.0))
    return frames


PRESETS = {
    "smoke_puff": lambda q: smoke_puff(q, "smoke"),
    "steam_puff": lambda q: smoke_puff(q, "steam"),
    "dust_puff": lambda q: dust_puff(q, False),
    "sand_burst": lambda q: dust_puff(q, True),
    "fire_loop": lambda q: fire(q, "loop"),
    "fire_burst": lambda q: fire(q, "burst"),
    "explosion": lambda q: fire(q, "explosion"),
    "water_splash": lambda q: water_splash(q),
}
LOOK = {  # preview colours (lit, shadow) per flipbook: what the materials do with the tint
    "smoke_puff": ((0.55, 0.53, 0.5), (0.12, 0.12, 0.13)),
    "steam_puff": ((0.97, 0.97, 0.98), (0.55, 0.58, 0.62)),
    "dust_puff": ((0.62, 0.55, 0.45), (0.25, 0.22, 0.19)),
    "sand_burst": ((0.88, 0.74, 0.50), (0.42, 0.33, 0.22)),
    "fire_loop": ((0.30, 0.28, 0.27), (0.08, 0.07, 0.07)),
    "fire_burst": ((0.30, 0.28, 0.27), (0.08, 0.07, 0.07)),
    "explosion": ((0.36, 0.34, 0.32), (0.08, 0.07, 0.07)),
    "water_splash": ((0.85, 0.95, 1.0), (0.2, 0.42, 0.55)),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--preview", default=None)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    names = a.names or list(PRESETS)
    report = {}
    for nm in names:
        t0 = time.time()
        frames = PRESETS[nm](a.quick)
        tiles = [_fit_tile(f) for f in frames]
        atlas = pack_atlas(tiles)
        path = os.path.join(a.out, f"T_FX_FB_{nm}.png")
        save_png(atlas, path)
        if a.preview:
            lit, sh = LOOK[nm]
            preview(atlas, os.path.join(a.preview, f"prev_{nm}.png"), lit=lit, shadow=sh)
        report[nm] = {"file": os.path.relpath(path, os.path.join(HERE, "..", "..")), "frames": FRAMES, "grid": GRID,
                      "tile": TILE, "seconds": round(time.time() - t0, 1)}
        print(f"{nm}: {report[nm]['seconds']} s -> {path}", flush=True)
    if not a.names and not a.quick:
        with open(os.path.join(a.out, "flipbooks.json"), "w") as f:
            json.dump({"schema": "fourfold.fx_flipbooks/1", "format": "R light, G thickness, B temperature, A alpha",
                       "flipbooks": report}, f, indent=2)


if __name__ == "__main__":
    main()
