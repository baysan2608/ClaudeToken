"""Mesh infrastructure for the Fourfold training fighter: math helpers, MeshBuilder (lofts with explicit skin weights,
per-corner UVs, vertex-colour masks) and the atlas packer.  The character itself is assembled in fighter_character.py
from fighter_body.py (head/hair/hands/torso skin), fighter_garments.py (clothes, wraps, shoes).

Everything is authored in the authoring frame A (x left, y forward, z up) and converted to Blender coordinates on the
way in.  Parts are lofts of cross-section rings; each vertex receives an explicit bone-weight dict.  No modifiers.
"""
import math
from mathutils import Vector

from fighter_skeleton import A, LEFT_REST, SPINE, SIDES, SHOULDER_Z, HIP_Z

MAT_NAMES = ["skin", "cloth_main", "cloth_accent", "wraps", "hair"]
SKIN, MAIN, ACC, WRAPS, HAIR = range(5)
MAT_COLORS = {
    "skin": (0.78, 0.55, 0.40, 1.0),
    "cloth_main": (0.72, 0.74, 0.78, 1.0),
    "cloth_accent": (0.78, 0.22, 0.16, 1.0),
    "wraps": (0.86, 0.80, 0.66, 1.0),
    "hair": (0.09, 0.07, 0.06, 1.0),
}
DOUBLE_SIDED = {"cloth_accent"}


# ---------------------------------------------------------------------------------------
# small math helpers
# ---------------------------------------------------------------------------------------
def clamp01(x):
    return 0.0 if x < 0 else 1.0 if x > 1 else x


def sstep(e0, e1, x):
    """smoothstep from e0 to e1 (works for e0 > e1 as a falling step)."""
    if e0 == e1:
        return 0.0 if x < e0 else 1.0
    t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


class Curve:
    """Monotone cubic (PCHIP-like) interpolation through (x, y) knots, clamped outside."""

    def __init__(self, pts):
        pts = sorted(pts)
        self.x = [p[0] for p in pts]
        self.y = [p[1] for p in pts]
        n = len(pts)
        d = [(self.y[i + 1] - self.y[i]) / (self.x[i + 1] - self.x[i]) for i in range(n - 1)]
        m = [0.0] * n
        m[0], m[-1] = d[0], d[-1]
        for i in range(1, n - 1):
            m[i] = 0.0 if d[i - 1] * d[i] <= 0 else 2 * d[i - 1] * d[i] / (d[i - 1] + d[i])
        self.m = m

    def __call__(self, x):
        xs, ys, m = self.x, self.y, self.m
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = 0
        while xs[i + 1] < x:
            i += 1
        h = xs[i + 1] - xs[i]
        t = (x - xs[i]) / h
        h00 = 2 * t ** 3 - 3 * t ** 2 + 1
        h10 = t ** 3 - 2 * t ** 2 + t
        h01 = -2 * t ** 3 + 3 * t ** 2
        h11 = t ** 3 - t ** 2
        return h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1]


def sgnpow(v, e):
    return math.copysign(abs(v) ** e, v)


def ring_pts(c, u, v, ru, rv, n, p=2.0, phase=0.0):
    out = []
    e = 2.0 / p
    for k in range(n):
        a = phase + 2 * math.pi * k / n
        out.append(c + u * (ru * sgnpow(math.cos(a), e)) + v * (rv * sgnpow(math.sin(a), e)))
    return out


def frame_from_tangent(t, ref):
    t = t.normalized()
    u = ref - t * ref.dot(t)
    if u.length < 1e-6:
        u = Vector((1, 0, 0)) - t * t.x
    u.normalize()
    v = t.cross(u)
    return u, v, t


def norm_weights(w, keep=4):
    items = [(k, v) for k, v in w.items() if v > 0.012]
    items.sort(key=lambda kv: -kv[1])
    items = items[:keep]
    s = sum(v for _, v in items) or 1.0
    return {k: v / s for k, v in items}


def side_name(base, side):
    return f"{base}.{side}"


# ---------------------------------------------------------------------------------------
# mesh container
# ---------------------------------------------------------------------------------------
class MeshBuilder:
    """Vertices (with weights + vertex colour), faces (with per-corner UVs in metres, packed into atlases at the end).

    Vertex colour channels (used by the texture bake only): R = surface variant (0 default, 1 = special, see
    fighter_textures.py), G = wear / dirt mask, B = spare.  Parts: each begin() starts a UV island group; `density`
    scales its texel density, `group` makes several parts share one UV rectangle (hair locks)."""

    def __init__(self):
        self.verts = []
        self.weights = []
        self.cols = []
        self.faces = []
        self.fmat = []
        self.fuv = []
        self.fpart = []
        self.parts = []
        self._part = None

    # -- parts -------------------------------------------------------------------------
    def begin(self, name, mat, density=1.0, group=None):
        self._part = dict(name=name, mat=mat, density=density, group=group, faces=[])
        self.parts.append(self._part)
        return self._part

    def vert(self, p, w, col=(0.0, 0.0, 0.0)):
        self.verts.append(Vector(p))
        self.weights.append(norm_weights(w))
        self.cols.append(tuple(col))
        return len(self.verts) - 1

    def face(self, idx, mat=None, uv=None):
        part = self._part
        m = part["mat"] if mat is None else mat
        if uv is None:
            uv = planar_uv([self.verts[k] for k in idx])
        self.faces.append(tuple(idx))
        self.fmat.append(m)
        self.fuv.append([tuple(c) for c in uv])
        part["faces"].append(len(self.faces) - 1)
        return len(self.faces) - 1

    # -- loft ------------------------------------------------------------------------
    def loft(self, rings, wfn, mat=None, cap_start=None, cap_end=None, mat_seg=None, close=True,
             colfn=None, ucoord=None, vcoord=None, flip=False):
        """rings: list of equal-length lists of Vectors.  wfn(i, j, p) -> weight dict.  colfn(i, j, p) -> (r, g, b).
        cap_start / cap_end: None, or (apex_point, weight_dict) -> triangle fan to a pole.
        Winding is auto-corrected so faces point away from the loft axis.  UVs: u = fraction around the ring times the
        mean ring circumference (or ucoord(j)), v = distance along the loft (or vcoord(i))."""
        n = len(rings[0])
        nr = len(rings)
        mat = self._part["mat"] if mat is None else mat
        ids = [[self.vert(p, wfn(i, j, p), colfn(i, j, p) if colfn else (0.0, 0.0, 0.0)) for j, p in enumerate(r)]
               for i, r in enumerate(rings)]
        # ring metrics
        cent = [sum(r, Vector()) / n for r in rings]
        circ = [sum((r[(j + 1) % n] - r[j]).length for j in range(n if close else n - 1)) for r in rings]
        cavg = sum(circ) / nr
        if ucoord is None:
            cum = [0.0]
            for j in range(1, n + (1 if close else 0)):
                cum.append(cum[-1] + sum((r[j % n] - r[j - 1]).length for r in rings) / nr)
            tot = cum[-1] or 1.0
            ucoord = (lambda jj: cum[jj] * (cavg / tot)) if close else (lambda jj: cum[jj])
        if vcoord is None:
            vs = [0.0]
            for i in range(1, nr):
                vs.append(vs[-1] + (cent[i] - cent[i - 1]).length)
            vcoord = lambda i: vs[i]
        new_faces = []
        for i in range(nr - 1):
            m = mat if mat_seg is None else mat_seg(i)
            if m is None:
                continue
            for j in range(n):
                j2 = (j + 1) % n
                if not close and j2 == 0:
                    continue
                uv = [(ucoord(j), vcoord(i)), (ucoord(j + 1), vcoord(i)), (ucoord(j + 1), vcoord(i + 1)), (ucoord(j), vcoord(i + 1))]
                new_faces.append(([ids[i][j], ids[i][j2], ids[i + 1][j2], ids[i + 1][j]], m, uv))
        if cap_start is not None:
            ap, wd = cap_start
            a = self.vert(ap, wd)
            capv = (Vector(ap) - cent[0]).length
            for j in range(n):
                j2 = (j + 1) % n
                um = 0.5 * (ucoord(j) + ucoord(j + 1))
                new_faces.append(([ids[0][j2], ids[0][j], a], mat if mat_seg is None else mat_seg(0),
                                  [(ucoord(j + 1), vcoord(0)), (ucoord(j), vcoord(0)), (um, vcoord(0) - capv)]))
        if cap_end is not None:
            ap, wd = cap_end
            a = self.vert(ap, wd)
            last = nr - 1
            capv = (Vector(ap) - cent[last]).length
            for j in range(n):
                j2 = (j + 1) % n
                um = 0.5 * (ucoord(j) + ucoord(j + 1))
                new_faces.append(([ids[last][j], ids[last][j2], a], mat if mat_seg is None else mat_seg(last - 1),
                                  [(ucoord(j), vcoord(last)), (ucoord(j + 1), vcoord(last)), (um, vcoord(last) + capv)]))
        # winding check on the first quad (faces must point away from the loft axis; `flip` forces the opposite)
        if new_faces:
            fi = new_faces[0][0]
            pts = [self.verts[k] for k in fi]
            nrm = (pts[1] - pts[0]).cross(pts[2] - pts[1])
            cen = cent[0] * 0.5 + cent[min(1, nr - 1)] * 0.5
            fc = sum(pts, Vector()) / len(pts)
            bad = nrm.dot(fc - cen) < 0
            if bad != flip:
                new_faces = [(list(reversed(f)), m, list(reversed(uv))) for f, m, uv in new_faces]
        for f, m, uv in new_faces:
            self.face(f, m, uv)
        return ids

    def tri_fan_ring(self, ring_ids, apex_id, mat, flip=False):
        n = len(ring_ids)
        for j in range(n):
            a, b = ring_ids[j], ring_ids[(j + 1) % n]
            self.face([b, a, apex_id] if flip else [a, b, apex_id], mat)

    # -- atlas packing ----------------------------------------------------------------------------
    def pack_uvs(self, size=1024, margin_px=7, base_scale=None, log=None):
        """Shelf-pack the UV islands of every material into the unit square.  Returns {mat: texel density px/m}."""
        out = {}
        for m in range(len(MAT_NAMES)):
            rects = []                           # (key, w, h, density)
            groups = {}
            for pi, part in enumerate(self.parts):
                if part["mat"] != m and not any(self.fmat[f] == m for f in part["faces"]):
                    continue
                fl = [f for f in part["faces"] if self.fmat[f] == m]
                if not fl:
                    continue
                us = [c[0] for f in fl for c in self.fuv[f]]
                vs = [c[1] for f in fl for c in self.fuv[f]]
                u0, v0, u1, v1 = min(us), min(vs), max(us), max(vs)
                key = (part["group"], m) if part["group"] else (pi, m)
                g = groups.setdefault(key, dict(w=0.0, h=0.0, d=part["density"], items=[]))
                g["w"] = max(g["w"], u1 - u0)
                g["h"] = max(g["h"], v1 - v0)
                g["items"].append((fl, u0, v0))
            if not groups:
                continue
            keys = sorted(groups, key=lambda k: -groups[k]["h"] * groups[k]["d"])
            lo, hi = 50.0, 6000.0

            def attempt(s):
                x = y = shelf_h = 0
                pos = {}
                for k in keys:
                    g = groups[k]
                    pw = int(math.ceil(g["w"] * g["d"] * s)) + 2 * margin_px
                    ph = int(math.ceil(g["h"] * g["d"] * s)) + 2 * margin_px
                    if pw > size or ph > size:
                        return None
                    if x + pw > size:
                        x, y, shelf_h = 0, y + shelf_h, 0
                    if y + ph > size:
                        return None
                    pos[k] = (x, y)
                    x += pw
                    shelf_h = max(shelf_h, ph)
                return pos
            for _ in range(22):
                mid = 0.5 * (lo + hi)
                if attempt(mid) is not None:
                    lo = mid
                else:
                    hi = mid
            s = lo if base_scale is None else min(lo, base_scale)
            pos = attempt(s)
            used = 0
            for k, g in groups.items():
                x0, y0 = pos[k]
                for fl, u0, v0 in g["items"]:
                    for f in fl:
                        self.fuv[f] = [((x0 + margin_px + (u - u0) * g["d"] * s) / size,
                                        (y0 + margin_px + (v - v0) * g["d"] * s) / size) for (u, v) in self.fuv[f]]
                used += (g["w"] * g["d"] * s) * (g["h"] * g["d"] * s)
            out[m] = s
            if log:
                log(f"[uv] {MAT_NAMES[m]}: {len(groups)} islands, {s:.0f} px/m, fill {used / size / size * 100:.0f}%")
        return out


def planar_uv(pts):
    """Fallback UVs for a face: project onto its own plane (metres)."""
    if len(pts) < 3:
        return [(0.0, 0.0)] * len(pts)
    n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    if n.length < 1e-12:
        return [(p.x, p.z) for p in pts]
    n.normalize()
    ex = (pts[1] - pts[0])
    if ex.length < 1e-9:
        ex = Vector((1, 0, 0))
    ex = (ex - n * ex.dot(n)).normalized()
    ey = n.cross(ex)
    return [((p - pts[0]).dot(ex), (p - pts[0]).dot(ey)) for p in pts]


# ---------------------------------------------------------------------------------------
# weights helpers
# ---------------------------------------------------------------------------------------
# axial chain along the spine: (joint z, half width of blend zone, bone below, bone above)
_AXIAL = [
    (1.02, 0.065, "hips", "spine"),
    (1.21, 0.065, "spine", "chest"),
    (1.43, 0.045, "chest", "neck"),
    (1.575, 0.04, "neck", "head"),
]


def axial_weights(z, hw_scale=1.0):
    """Weights across hips/spine/chest/neck/head for a point at height z (sequential smooth blends)."""
    bones = ["hips", "spine", "chest", "neck", "head"]
    w = {}
    rem = 1.0
    for k, (zj, hw, _, _) in enumerate(_AXIAL):
        t = sstep(zj - hw * hw_scale, zj + hw * hw_scale, z)
        share = rem * (1 - t)
        w[bones[k]] = share
        rem -= share
    w[bones[-1]] = rem
    return w


def torso_weights(p_a):
    """p_a: point in A frame.  Spine chain + shoulder share near the shoulder tips."""
    x, y, z = p_a
    w = axial_weights(z)
    sh = 0.55 * sstep(0.10, 0.17, abs(x)) * sstep(1.30, 1.38, z) * (1 - sstep(1.43, 1.47, z))
    if sh > 0:
        side = "L" if x > 0 else "R"
        w["chest"] = w.get("chest", 0.0) * (1 - sh)
        w["spine"] = w.get("spine", 0.0) * (1 - sh * 0.5)
        w[f"shoulder.{side}"] = sh
    return w


def to_a(p_blender):
    return (p_blender.x, -p_blender.y, p_blender.z)


