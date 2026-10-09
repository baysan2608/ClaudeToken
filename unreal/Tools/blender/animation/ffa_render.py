"""Fourfold animation toolkit - fast software renderer for previews (numpy + PIL, no GPU, no bpy).

Flat-shaded painter's-algorithm rasteriser of the proxy mannequin (ffa_proxy) with a floor grid, a soft sun shadow,
planted-foot markers and labels.  ~40 ms per view, so every clip can be looked at frame by frame while iterating.

Views (A-frame, the fighter at the origin facing +y):
  game  - the gameplay camera: behind and above (7 m back, 3.2 m up), framed on the fighter
  side  - from the character's right, chest height (profile)
  front - front-right three-quarter
  top   - straight down (footwork)
"""
import math
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import ffa_proxy as proxy
import ffa_rig as rig
from ffa_math import A, norm

LIGHT = norm(A(0.35, 0.55, -1.0))          # direction the sunlight travels
VIEWS = {
    "game": dict(pos=(0.25, -7.0, 3.2), at=(0.0, 0.25, 0.92), fov=17.0),
    "side": dict(pos=(-6.0, 0.35, 1.05), at=(0.0, 0.35, 0.92), fov=20.0),
    "front": dict(pos=(-3.6, 4.2, 1.7), at=(0.0, 0.25, 0.92), fov=24.0),
    "top": dict(pos=(0.0, 0.3, 7.0), at=(0.0, 0.3, 0.0), fov=22.0, up=(0, 1, 0)),
    "hand": dict(pos=(-0.25, 1.3, 1.0), at=(0.0, 0.3, 0.95), fov=30.0),
}
_FONT = None


def font(size=13):
    global _FONT
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


class Camera:
    def __init__(self, pos, at, fov, w, h, up=(0, 0, 1), ortho=None):
        self.c = A(*pos)
        t = A(*at)
        upv = A(*up) if up != (0, 0, 1) else np.array([0.0, 0.0, 1.0])
        self.fwd = norm(t - self.c)
        self.right = norm(np.cross(self.fwd, upv))
        self.up = np.cross(self.right, self.fwd)
        self.w, self.h = w, h
        self.fov = fov
        self.ortho = ortho

    @property
    def f(self):
        return (self.h / 2.0) / math.tan(math.radians(self.fov) / 2.0)

    def project(self, P):
        d = P - self.c
        x = d @ self.right
        y = d @ self.up
        z = d @ self.fwd
        if self.ortho:
            s = self.h / self.ortho
            return np.stack([self.w / 2 + x * s, self.h / 2 - y * s], -1), z
        zc = np.maximum(z, 1e-3)
        return np.stack([self.w / 2 + self.f * x / zc, self.h / 2 - self.f * y / zc], -1), z


def _floor(draw, cam, ss, follow=(0.0, 0.0)):
    ext = 4.0
    step = 0.5
    cx, cy = follow
    for i in np.arange(-ext, ext + 1e-6, step):
        for a, b in (((cx + i, cy - ext, 0.0), (cx + i, cy + ext, 0.0)), ((cx - ext, cy + i, 0.0), (cx + ext, cy + i, 0.0))):
            P = np.array([A(*a), A(*b)])
            # clip to the camera front
            pts = []
            for u in np.linspace(0, 1, 9):
                p = P[0] * (1 - u) + P[1] * u
                s, z = cam.project(p[None, :])
                if z[0] > 0.2:
                    pts.append(tuple(s[0] * ss))
            if len(pts) > 1:
                major = abs(i) < 1e-6
                draw.line(pts, fill=(150, 150, 146) if major else (182, 182, 176), width=max(1, int(ss * (1.6 if major else 1))))


def render(W, Hd, cam, size=(320, 320), ss=2, label=None, plants=None, flags=None, bg=(214, 214, 208), sky=(232, 234, 236),
           ghost=None):
    """One view of one posed frame.  W / Hd from ffa_rig.fk.  plants = (left_planted, right_planted) booleans."""
    w, h = size
    cam.w, cam.h = w * ss, h * ss
    img = Image.new("RGB", (w * ss, h * ss), bg)
    dr = ImageDraw.Draw(img)
    # sky gradient above the horizon
    hz = cam.project(cam.c[None, :] + np.array([[cam.fwd[0] * 100, cam.fwd[1] * 100, -cam.c[2]]]))[0][0][1]
    if hz > 0 and abs(cam.fwd[2]) < 0.7:
        dr.rectangle([0, 0, w * ss, min(hz, h * ss)], fill=sky)
    _floor(dr, cam, 1.0)
    V, F, B0, B1, Wb, names, C = proxy.mesh()
    P = proxy.posed_vertices(W, Hd)
    tri = P[F]                                              # (F,3,3)
    # ---- shadow on the floor (z = 0)
    t = -tri[..., 2] / LIGHT[2]
    shp = tri + t[..., None] * LIGHT[None, None, :]
    s2, z2 = cam.project(shp.reshape(-1, 3))
    s2 = s2.reshape(-1, 3, 2)
    mask = Image.new("L", img.size, 0)
    md = ImageDraw.Draw(mask)
    for k in range(len(F)):
        md.polygon([tuple(x) for x in s2[k]], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(radius=1.2 * ss))
    arr = np.asarray(img).astype(np.float32)
    m = np.asarray(mask).astype(np.float32)[..., None] / 255.0
    arr = arr * (1.0 - 0.32 * m)
    img = Image.fromarray(arr.clip(0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(img)
    # ---- plant markers
    if plants is not None:
        for side, on in zip(rig.SIDES, plants):
            p = Hd["foot_" + side].copy()
            p[2] = 0.0
            s, _ = cam.project(np.array([p]))
            x, y = s[0]
            r = 4 * ss
            col = (40, 170, 60) if on else (200, 200, 200)
            dr.ellipse([x - r, y - r * 0.6, x + r, y + r * 0.6], outline=col, width=ss)
    # ---- body
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nn = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    cen = tri.mean(axis=1)
    if cam.ortho:
        vis = (nn @ cam.fwd) < 0
    else:
        vis = np.einsum("ij,ij->i", nn, cen - cam.c) < 0
    s3, z3 = cam.project(tri.reshape(-1, 3))
    s3 = s3.reshape(-1, 3, 2)
    zc = z3.reshape(-1, 3).mean(axis=1)
    lam = np.clip(nn @ (-LIGHT), 0.0, 1.0)
    rim = np.clip(1.0 - np.abs(nn @ cam.fwd), 0.0, 1.0) ** 3
    shade = 0.42 + 0.62 * lam + 0.12 * rim
    col = np.clip(C * shade[:, None], 0, 1) ** (1 / 1.25) * 255
    order = np.argsort(-zc)
    for k in order:
        if not vis[k]:
            continue
        dr.polygon([tuple(x) for x in s3[k]], fill=tuple(int(c) for c in col[k]))
    if ghost is not None:
        # ghost = list of 3D polylines (A-frame) to draw on top, e.g. hand trails
        for pts, colr in ghost:
            sp, zz = cam.project(np.array(pts))
            if len(sp) > 1:
                dr.line([tuple(x) for x in sp], fill=colr, width=ss)
    img = img.resize((w, h), Image.LANCZOS)
    d2 = ImageDraw.Draw(img)
    if flags:
        if "contact" in flags:
            d2.rectangle([0, 0, w - 1, h - 1], outline=(210, 40, 30), width=3)
    if label:
        d2.text((5, 3), label, fill=(30, 30, 30), font=font(12))
    return img


def make_camera(view, w, h, shift=(0.0, 0.0)):
    v = VIEWS[view]
    pos = (v["pos"][0] + shift[0], v["pos"][1] + shift[1], v["pos"][2])
    at = (v["at"][0] + shift[0], v["at"][1] + shift[1], v["at"][2])
    return Camera(pos, at, v["fov"], w, h, up=v.get("up", (0, 0, 1)), ortho=v.get("ortho"))


def clip_shift(res):
    """Camera shift (A-frame x, y) for clips whose body travels far from the origin (falls, lying): the mean pelvis
    ground position, applied only when it is more than 15 cm away."""
    ps = [rig.fk(p)[1]["pelvis"] for p in res.poses]
    mx = float(np.mean([p[0] for p in ps]))
    my = float(np.mean([-p[1] for p in ps]))
    return (mx, my) if math.hypot(mx, my) > 0.15 else (0.0, 0.0)


def frame_image(pose, view, size=(320, 320), label=None, plants=None, flags=None, ghost=None, shift=(0.0, 0.0)):
    W, Hd, _ = rig.fk(pose)
    cam = make_camera(view, *size, shift=shift)
    return render(W, Hd, cam, size, label=label, plants=plants, flags=flags, ghost=ghost)


def contact_sheet(res, frames, path, views=("game", "side"), panel=(240, 270), title=None):
    """frames = list of (frame, caption).  One row per view."""
    pw, ph = panel
    head = 26
    sheet = Image.new("RGB", (pw * len(frames), ph * len(views) + head), (246, 246, 244))
    d = ImageDraw.Draw(sheet)
    d.text((6, 5), title or res.name, fill=(20, 20, 20), font=font(14))
    sh = clip_shift(res)
    for r, view in enumerate(views):
        for c, (f, cap) in enumerate(frames):
            pl = _planted_at(res, f)
            fl = ["contact"] if f in (res.clip.contacts or []) else None
            im = frame_image(res.poses[f], view, (pw, ph), label=f"{cap} f{f}", plants=pl, flags=fl, shift=sh)
            sheet.paste(im, (c * pw, head + r * ph))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if path.endswith(".jpg"):
        sheet.save(path, quality=84, optimize=True)
    else:
        sheet.save(path, optimize=True)
    return path


def _planted_at(res, f):
    out = []
    for s in rig.SIDES:
        out.append(any(a <= f < b or (f == b and b == res.frames) for a, b in res.plants.get(s, [])))
    return tuple(out)


def video(res, path, views=("game", "front"), size=(640, 360), loops=1, slow=1, tmpdir=None):
    """MP4 (H.264, yuv420p) of the clip: views side by side; non-loop clips get a 12-frame hold at the end."""
    import shutil
    import tempfile
    pw = size[0] // len(views)
    ph = size[1]
    tmp = tmpdir or tempfile.mkdtemp(prefix="ffa_vid_")
    frames = list(range(res.frames + (0 if res.loop else 1)))
    seq = frames * loops if res.loop else frames + [res.frames] * 12
    k = 0
    cache = {}
    sh = clip_shift(res)
    for f in seq:
        if f not in cache:
            img = Image.new("RGB", size, (240, 240, 240))
            pl = _planted_at(res, f)
            fl = ["contact"] if f in (res.clip.contacts or []) else None
            for i, view in enumerate(views):
                im = frame_image(res.poses[f], view, (pw, ph), label=(f"{res.name}  f{f}/{res.frames}" if i == 0 else view),
                                 plants=pl, flags=fl, shift=sh)
                img.paste(im, (i * pw, 0))
            p = os.path.join(tmp, f"c{f:04d}.png")
            img.save(p)
            cache[f] = p
        for _ in range(slow):
            shutil.copy(cache[f], os.path.join(tmp, f"f{k:05d}.png"))
            k += 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    from ffa_compare import ffmpeg
    cmd = [ffmpeg(), "-y", "-loglevel", "error", "-framerate", "60", "-i", os.path.join(tmp, "f%05d.png"),
           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "30", "-preset", "slow", "-movflags", "+faststart", path]
    subprocess.run(cmd, check=True)
    if tmpdir is None:
        shutil.rmtree(tmp, ignore_errors=True)
    return path
