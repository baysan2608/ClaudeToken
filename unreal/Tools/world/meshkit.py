"""Tiny pure-numpy polygon mesh kit for the courtyard meshes (no Blender needed to build / measure / test).

Authoring convention: SIM coordinates in metres, +Y up (x, y, z) - the same space as Source/FourfoldCore/Data/sim.json.  The Blender
writer (bpy_io.py) rotates by +90 degrees about X (Blender = (x, -z, y)) and the FBX import in Unreal then lands every vertex at
UE = 100 * (x, z, y), i.e. exactly where the sim says.  UV0 is projected from sim space with a per-material tile size (metres per
repeat), so textures run continuously across neighbouring meshes and across 34 m walls; UV1 is a lightmap atlas (shelf packer).

Faces are convex planar polygons (3..8 points, CCW seen from outside).  Vertices are shared only inside a smooth group (smooth > 0),
so every other edge is hard by construction.
"""
from __future__ import annotations

import math
from contextlib import contextmanager

import numpy as np

Vec = np.ndarray


def v3(x=0.0, y=0.0, z=0.0):
    return np.array([x, y, z], dtype=np.float64)


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def normal_of(P):
    """Newell normal of a polygon (unit)."""
    n = np.zeros(3)
    k = len(P)
    for i in range(k):
        a, b = P[i], P[(i + 1) % k]
        n += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])])
    ln = np.linalg.norm(n)
    return n / ln if ln > 1e-12 else np.array([0.0, 1.0, 0.0])


def uv_box(tile, rot=False, off=(0.0, 0.0)):
    """World box projection: the dominant axis of the face normal picks the plane. rot swaps u / v (grain along the other axis)."""
    def f(P, n):
        ax = int(np.argmax(np.abs(n)))
        if ax == 1:
            u, v = P[:, 0], P[:, 2]
        elif ax == 0:
            u, v = P[:, 2], P[:, 1]
        else:
            u, v = P[:, 0], P[:, 1]
        if rot:
            u, v = v, u
        return np.stack([u / tile + off[0], v / tile + off[1]], -1)
    return f


def uv_planar_xz(tile, off=(0.0, 0.0)):
    return lambda P, n: np.stack([P[:, 0] / tile + off[0], P[:, 2] / tile + off[1]], -1)


class Mesh:
    def __init__(self, name, slots):
        """slots: list of (slot_name, tile_m) - tile_m is the default UV repeat in metres for that material."""
        self.name = name
        self.slots = [s if isinstance(s, tuple) else (s, 1.0) for s in slots]
        self.faces = []          # dict(P, mat, uv, col, smooth)
        self._stack = [(np.eye(3), np.zeros(3))]

    # ------------------------------------------------------------------ transform stack
    @contextmanager
    def xf(self, R=None, t=None):
        R0, t0 = self._stack[-1]
        R = np.eye(3) if R is None else np.asarray(R, float)
        t = np.zeros(3) if t is None else np.asarray(t, float)
        self._stack.append((R0 @ R, R0 @ t + t0))
        try:
            yield self
        finally:
            self._stack.pop()

    def T(self, p):
        R, t = self._stack[-1]
        return np.asarray(p, float) @ R.T + t

    def slot(self, name):
        for i, (n, _) in enumerate(self.slots):
            if n == name:
                return i
        raise KeyError(name)

    # ------------------------------------------------------------------ faces
    def face(self, pts, mat, uv=None, col=None, smooth=0, rot=False, tile=None, off=(0.0, 0.0)):
        """pts: local points (CCW seen from outside).  uv: None (box projection) | callable(P_world, n_world) | array (n,2).
        col: None | (r,g,b,a) | callable(P_world)->(n,4) | array."""
        P = self.T(np.asarray(pts, float))
        if len(P) < 3:
            return
        n = normal_of(P)
        if np.linalg.norm(np.cross(P[1] - P[0], P[2] - P[0])) < 1e-12 and len(P) == 3:
            return                                   # degenerate
        mi = self.slot(mat) if isinstance(mat, str) else int(mat)
        if uv is None:
            uvv = uv_box(tile or self.slots[mi][1], rot, off)(P, n)
        elif callable(uv):
            uvv = uv(P, n)
        else:
            uvv = np.asarray(uv, float)
        if col is None:
            cc = np.tile(np.array([1.0, 0.0, 0.0, 1.0]), (len(P), 1))     # R = 1 (unused / hang 0..), G = grime 0
        elif callable(col):
            cc = np.asarray(col(P), float)
        else:
            c = np.asarray(col, float)
            cc = np.tile(c, (len(P), 1)) if c.ndim == 1 else c
            if cc.shape[1] == 3:
                cc = np.concatenate([cc, np.ones((len(cc), 1))], 1)
        self.faces.append(dict(P=P, mat=mi, uv=uvv, col=cc, smooth=int(smooth), n=n))

    def quad(self, a, b, c, d, mat, **kw):
        self.face([a, b, c, d], mat, **kw)

    def tri(self, a, b, c, mat, **kw):
        self.face([a, b, c], mat, **kw)

    # ------------------------------------------------------------------ solids
    def box(self, mn, mx, mat, mats=None, skip=(), uvrot=None, col=None, tile=None):
        """Axis-aligned box in local space. mats: dict face -> slot (faces: top bottom px nx pz nz); skip: faces to omit."""
        x0, y0, z0 = mn
        x1, y1, z1 = mx
        mats = mats or {}
        uvrot = uvrot or {}
        def m(k):
            return mats.get(k, mat)
        def add(k, pts):
            if k not in skip:
                self.face(pts, m(k), rot=uvrot.get(k, False), col=col, tile=tile)
        add("top", [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)])
        add("bottom", [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)])
        add("px", [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)])
        add("nx", [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)])
        add("pz", [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])
        add("nz", [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)])

    def chamfer_box(self, mn, mx, c, mat, mats=None, skip=(), top_mat=None, chamfer_mat=None, col=None, tile=None):
        """Box with all vertical edges and the top edges chamfered by c (c <= half the smallest size).  The chamfer faces share
        the smooth group id of the box (so they shade softly with the sides); bottom is flat."""
        x0, y0, z0 = mn
        x1, y1, z1 = mx
        mats = mats or {}
        tm = top_mat or mats.get("top", mat)
        cm = chamfer_mat or mat
        def side(k):
            return mats.get(k, mat)
        yt = y1 - c
        # top
        if "top" not in skip:
            self.face([(x0 + c, y1, z1 - c), (x1 - c, y1, z1 - c), (x1 - c, y1, z0 + c), (x0 + c, y1, z0 + c)], tm, col=col, tile=tile)
        if "bottom" not in skip:
            self.face([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], side("bottom"), col=col, tile=tile)
        # sides (vertical, inset by c at both ends so the vertical chamfers fit)
        if "px" not in skip:
            self.face([(x1, y0, z1 - c), (x1, y0, z0 + c), (x1, yt, z0 + c), (x1, yt, z1 - c)], side("px"), col=col, tile=tile)
        if "nx" not in skip:
            self.face([(x0, y0, z0 + c), (x0, y0, z1 - c), (x0, yt, z1 - c), (x0, yt, z0 + c)], side("nx"), col=col, tile=tile)
        if "pz" not in skip:
            self.face([(x0 + c, y0, z1), (x1 - c, y0, z1), (x1 - c, yt, z1), (x0 + c, yt, z1)], side("pz"), col=col, tile=tile)
        if "nz" not in skip:
            self.face([(x1 - c, y0, z0), (x0 + c, y0, z0), (x0 + c, yt, z0), (x1 - c, yt, z0)], side("nz"), col=col, tile=tile)
        # vertical corner chamfers
        for (cx, cz, sx, sz) in ((x1, z1, 1, 1), (x1, z0, 1, -1), (x0, z0, -1, -1), (x0, z1, -1, 1)):
            a = (cx, y0, cz - sz * c)
            b = (cx - sx * c, y0, cz)
            if sx * sz > 0:
                pts = [(cx, y0, cz - sz * c), (cx - sx * c, y0, cz), (cx - sx * c, yt, cz), (cx, yt, cz - sz * c)]
            else:
                pts = [(cx - sx * c, y0, cz), (cx, y0, cz - sz * c), (cx, yt, cz - sz * c), (cx - sx * c, yt, cz)]
            self.face(pts[::-1], cm, col=col, tile=tile)
        # top edge chamfers (4 slopes) + 4 corner triangles
        self.face([(x0 + c, y1, z1 - c), (x0 + c, yt, z1), (x1 - c, yt, z1), (x1 - c, y1, z1 - c)], cm, col=col, tile=tile)
        self.face([(x1 - c, y1, z0 + c), (x1 - c, yt, z0), (x0 + c, yt, z0), (x0 + c, y1, z0 + c)], cm, col=col, tile=tile)
        self.face([(x1 - c, y1, z1 - c), (x1, yt, z1 - c), (x1, yt, z0 + c), (x1 - c, y1, z0 + c)], cm, col=col, tile=tile)
        self.face([(x0 + c, y1, z0 + c), (x0, yt, z0 + c), (x0, yt, z1 - c), (x0 + c, y1, z1 - c)], cm, col=col, tile=tile)
        for (cx, cz, sx, sz) in ((x1, z1, 1, 1), (x1, z0, 1, -1), (x0, z0, -1, -1), (x0, z1, -1, 1)):
            ix, iz = cx - sx * c, cz - sz * c
            if sx * sz > 0:
                self.face([(ix, y1, iz), (ix, yt, cz), (cx, yt, iz)], cm, col=col, tile=tile)
            else:
                self.face([(ix, y1, iz), (cx, yt, iz), (ix, yt, cz)], cm, col=col, tile=tile)

    def lathe(self, profile, center, mat, segs=16, smooth=1, start=0.0, closed_axis=True, col=None, tile=None, uvfn=None):
        """Surface of revolution about the local Y axis through `center`.  profile: [(radius, y), ...] ordered bottom to top; the
        faces point outward when the profile goes upward (radius > 0)."""
        cx, cy, cz = center
        pts = np.array(profile, float)
        for i in range(len(pts) - 1):
            r0, y0 = pts[i]
            r1, y1 = pts[i + 1]
            for k in range(segs):
                a0 = start + 2 * math.pi * k / segs
                a1 = start + 2 * math.pi * (k + 1) / segs
                p = lambda r, y, a: (cx + r * math.cos(a), cy + y, cz + r * math.sin(a))
                A, B, C, D = p(r0, y0, a0), p(r0, y0, a1), p(r1, y1, a1), p(r1, y1, a0)
                # outward for increasing angle in the x-z plane seen from +y is clockwise: order A, D, C, B
                quad = [A, D, C, B]
                if r0 < 1e-9:
                    quad = [A, D, C]
                elif r1 < 1e-9:
                    quad = [A, D, B]
                self.face(quad, mat, smooth=smooth, col=col, tile=tile, uv=uvfn)

    def disc(self, center, radius, mat, y_up=True, segs=16, col=None, tile=None):
        cx, cy, cz = center
        pts = [(cx + radius * math.cos(2 * math.pi * k / segs), cy, cz + radius * math.sin(2 * math.pi * k / segs)) for k in range(segs)]
        self.face(pts[::-1] if y_up else pts, mat, col=col, tile=tile)

    def prism(self, poly_xz, y0, y1, mat, mats=None, col=None, tile=None, skip=()):
        """Extrude a convex polygon in the x-z plane between y0 and y1 (either orientation; faces point outward)."""
        mats = mats or {}
        poly_xz = [tuple(p) for p in poly_xz]
        if normal_of([(x, 0.0, z) for x, z in poly_xz])[1] < 0:      # make the top face point up
            poly_xz = poly_xz[::-1]
        n = len(poly_xz)
        if "top" not in skip:
            self.face([(x, y1, z) for x, z in poly_xz], mats.get("top", mat), col=col, tile=tile)
        if "bottom" not in skip:
            self.face([(x, y0, z) for x, z in poly_xz][::-1], mats.get("bottom", mat), col=col, tile=tile)
        for i in range(n):
            (xa, za), (xb, zb) = poly_xz[i], poly_xz[(i + 1) % n]
            self.face([(xa, y0, za), (xb, y0, zb), (xb, y1, zb), (xa, y1, za)], mats.get("side", mat), col=col, tile=tile)

    def sphere(self, center, radii, mat, segs=8, rings=5, smooth=1, col=None, tile=None, uvfn=None, seed=None, jitter=0.0):
        cx, cy, cz = center
        rx, ry, rz = radii
        rng = np.random.default_rng(seed) if seed is not None else None
        prof = []
        for j in range(rings + 1):
            t = -math.pi / 2 + math.pi * j / rings
            prof.append((math.cos(t), math.sin(t)))
        verts = {}
        def pt(j, k):
            key = (j, k % segs)
            if key not in verts:
                r, s = prof[j]
                a = 2 * math.pi * (k % segs) / segs
                jit = 1.0 + (rng.uniform(-jitter, jitter) if (rng is not None and 0 < j < rings) else 0.0)
                verts[key] = (cx + rx * r * math.cos(a) * jit, cy + ry * s * jit, cz + rz * r * math.sin(a) * jit)
            return verts[key]
        for j in range(rings):
            for k in range(segs):
                A, B, C, D = pt(j, k), pt(j, k + 1), pt(j + 1, k + 1), pt(j + 1, k)
                if j == 0:
                    self.face([A, D, C], mat, smooth=smooth, col=col, tile=tile, uv=uvfn)
                elif j == rings - 1:
                    self.face([A, D, B], mat, smooth=smooth, col=col, tile=tile, uv=uvfn)
                else:
                    self.face([A, D, C, B], mat, smooth=smooth, col=col, tile=tile, uv=uvfn)

    # ------------------------------------------------------------------ queries
    def bounds(self):
        P = np.concatenate([f["P"] for f in self.faces]) if self.faces else np.zeros((1, 3))
        return P.min(0), P.max(0)

    def tri_count(self):
        return int(sum(len(f["P"]) - 2 for f in self.faces))

    def stats(self):
        mn, mx = self.bounds()
        per = {}
        for f in self.faces:
            per[self.slots[f["mat"]][0]] = per.get(self.slots[f["mat"]][0], 0) + len(f["P"]) - 2
        return dict(name=self.name, tris=self.tri_count(), min=mn.tolist(), max=mx.tolist(), slots=per, faces=len(self.faces))

    def recenter(self, pivot):
        """Moves the pivot to `pivot` (sim coords): geometry stays at the same place relative to the pivot; UVs are NOT changed."""
        p = np.asarray(pivot, float)
        for f in self.faces:
            f["P"] = f["P"] - p
        return self

    # ------------------------------------------------------------------ lightmap UV1
    def lightmap_uvs(self, texel_m=0.12, res=512, pad=3.0):
        """Per-face charts (planar projection of each face) packed with a shelf packer into [0,1]^2.  Shrinks the scale until it fits.
        Returns the effective texel size (m) and stores f['uv1']."""
        charts = []
        for f in self.faces:
            P, n = f["P"], f["n"]
            a = np.array([0.0, 1.0, 0.0]) if abs(n[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
            t = np.cross(a, n)
            t /= np.linalg.norm(t) + 1e-12
            b = np.cross(n, t)
            L = np.stack([P @ t, P @ b], -1)
            mn = L.min(0)
            L = L - mn
            charts.append((L, L.max(0)))
        scale = 1.0 / texel_m
        for _ in range(24):
            rects = [(max(2.0, c[1][0] * scale) + pad, max(2.0, c[1][1] * scale) + pad) for c in charts]
            pos = _shelf_pack(rects, res)
            if pos is not None:
                break
            scale *= 0.88
        else:
            raise RuntimeError(f"lightmap packing failed for {self.name}")
        for f, (L, ext), (px, py), (rw, rh) in zip(self.faces, charts, pos, rects):
            uv = (L * scale + np.array([px + pad * 0.5, py + pad * 0.5])) / res
            f["uv1"] = uv
        return 1.0 / scale

    # ------------------------------------------------------------------ finalize
    def finalize(self):
        """-> dict(verts (N,3), polys list of index lists, mat (M,), uv (corner arrays per poly), col, uv1, smooth flags)."""
        verts = []
        lookup = {}
        polys, mats, uvs, cols, uv1s, smooth = [], [], [], [], [], []
        for f in self.faces:
            idx = []
            for k, p in enumerate(f["P"]):
                if f["smooth"] > 0:
                    key = (round(p[0], 5), round(p[1], 5), round(p[2], 5), f["smooth"])
                    if key not in lookup:
                        lookup[key] = len(verts)
                        verts.append(p)
                    idx.append(lookup[key])
                else:
                    idx.append(len(verts))
                    verts.append(p)
            polys.append(idx)
            mats.append(f["mat"])
            uvs.append(f["uv"])
            cols.append(f["col"])
            uv1s.append(f.get("uv1"))
            smooth.append(f["smooth"] > 0)
        return dict(verts=np.array(verts), polys=polys, mat=np.array(mats), uv=uvs, col=cols, uv1=uv1s, smooth=np.array(smooth))


def _shelf_pack(rects, res):
    """rects: list of (w, h) in texels.  Returns top-left positions or None when they do not fit in res x res."""
    order = sorted(range(len(rects)), key=lambda i: -rects[i][1])
    pos = [None] * len(rects)
    x = y = 0.0
    row_h = 0.0
    for i in order:
        w, h = rects[i]
        if w > res or h > res:
            return None
        if x + w > res:
            x = 0.0
            y += row_h
            row_h = 0.0
        if y + h > res:
            return None
        pos[i] = (x, y)
        x += w
        row_h = max(row_h, h)
    return pos
