"""A tiny numpy mesh container used while assembling the fighter (no bpy needed except to_object)."""
import numpy as np

import ch_weights as cw


class Part:
    def __init__(self, name, verts, faces, mats, W=None, uvs=None, src_index=None):
        """verts (n,3); faces: list of vertex-index lists; mats: list (per face) of material slot names;
        W: (n, NB) weights; uvs: optional list (per face) of per-loop (u,v); src_index: optional (n,) origin ids."""
        self.name = name
        self.v = np.asarray(verts, dtype=np.float64)
        self.f = [list(map(int, f)) for f in faces]
        self.m = list(mats) if not isinstance(mats, str) else [mats] * len(self.f)
        self.W = np.zeros((len(self.v), cw.NB)) if W is None else np.asarray(W, dtype=np.float64)
        self.uv = uvs
        self.src = src_index

    # ------------------------------------------------------------------ topology helpers
    def subset(self, face_mask, name=None):
        keep = [i for i, k in enumerate(face_mask) if k]
        used = sorted({v for i in keep for v in self.f[i]})
        remap = -np.ones(len(self.v), dtype=np.int64)
        remap[used] = np.arange(len(used))
        p = Part(name or self.name, self.v[used], [[int(remap[v]) for v in self.f[i]] for i in keep],
                 [self.m[i] for i in keep], self.W[used],
                 [self.uv[i] for i in keep] if self.uv is not None else None,
                 (self.src[used] if self.src is not None else np.array(used)))
        return p

    def face_centers(self):
        return np.array([self.v[f].mean(axis=0) for f in self.f])

    def vertex_normals(self):
        n = np.zeros_like(self.v)
        for f in self.f:
            p = self.v[f]
            # Newell normal
            nn = np.zeros(3)
            for i in range(len(f)):
                a, b = p[i], p[(i + 1) % len(f)]
                nn += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]),
                                (a[0] - b[0]) * (a[1] + b[1])])
            n[f] += nn
        l = np.linalg.norm(n, axis=1, keepdims=True)
        l[l == 0] = 1
        return n / l

    def edges_boundary(self, face_sel=None):
        """Directed boundary edges (a, b) of the selected faces (winding of the selected face)."""
        sel = range(len(self.f)) if face_sel is None else [i for i, k in enumerate(face_sel) if k]
        cnt = {}
        dirs = {}
        for i in sel:
            f = self.f[i]
            for k in range(len(f)):
                a, b = f[k], f[(k + 1) % len(f)]
                key = (min(a, b), max(a, b))
                cnt[key] = cnt.get(key, 0) + 1
                dirs[key] = (a, b)
        return [dirs[k] for k, c in cnt.items() if c == 1]

    def adjacency(self):
        return cw.adjacency(len(self.v), self.f)

    def laplacian_smooth(self, iters=1, alpha=0.5, mask=None):
        A = self.adjacency()
        for _ in range(iters):
            vn = A @ self.v
            if mask is None:
                self.v = (1 - alpha) * self.v + alpha * vn
            else:
                self.v[mask] = (1 - alpha) * self.v[mask] + alpha * vn[mask]

    def tri_count(self):
        return sum(len(f) - 2 for f in self.f)


def subdivide(part, name=None):
    """One step of linear quad subdivision (every n-gon -> n quads through edge midpoints and the centroid).
    Weights and per-loop UVs are interpolated; positions stay on the original facets (smooth afterwards)."""
    V = list(part.v)
    W = list(part.W)
    emid = {}
    faces, mats, uvs = [], [], ([] if part.uv is not None else None)
    for fi, f in enumerate(part.f):
        n = len(f)
        ci = len(V)
        V.append(part.v[f].mean(axis=0))
        W.append(part.W[f].mean(axis=0))
        mids = []
        for k in range(n):
            a, b = f[k], f[(k + 1) % n]
            key = (min(a, b), max(a, b))
            if key not in emid:
                emid[key] = len(V)
                V.append(0.5 * (part.v[a] + part.v[b]))
                W.append(0.5 * (part.W[a] + part.W[b]))
            mids.append(emid[key])
        if uvs is not None:
            u = np.asarray(part.uv[fi], float)
            uc = tuple(u.mean(axis=0))
            um = [tuple(0.5 * (u[k] + u[(k + 1) % n])) for k in range(n)]
        for k in range(n):
            faces.append([f[k], mids[k], ci, mids[k - 1]])
            mats.append(part.m[fi])
            if uvs is not None:
                uvs.append([tuple(u[k]), um[k], uc, um[k - 1]])
    return Part(name or part.name, np.array(V), faces, mats, np.array(W), uvs)


def boundary_distance(part):
    """Euclidean distance of every vertex to the nearest open-boundary vertex (inf if the part is closed)."""
    from scipy.spatial import cKDTree
    b = sorted({v for e in part.edges_boundary() for v in e})
    if not b:
        return np.full(len(part.v), np.inf)
    d, _ = cKDTree(part.v[b]).query(part.v)
    return d


def merge(parts, name):
    vs, fs, ms, Ws, uvs = [], [], [], [], []
    off = 0
    has_uv = all(p.uv is not None for p in parts)
    for p in parts:
        vs.append(p.v)
        fs += [[v + off for v in f] for f in p.f]
        ms += p.m
        Ws.append(p.W)
        if has_uv:
            uvs += p.uv
        off += len(p.v)
    return Part(name, np.vstack(vs), fs, ms, np.vstack(Ws), uvs if has_uv else None)


def split_region_with_lip(part, face_sel, offset, lip=True, normals=None):
    """Detach the selected faces (own vertices along the region border), push them out along the normals by
    `offset` (falling to `offset` at the border too), and bridge the border with a lip strip so the edge reads as a
    layer of cloth on top. Returns the new part (same material assignment; lip faces take the region's material)."""
    nrm = part.vertex_normals() if normals is None else normals
    sel_idx = [i for i, k in enumerate(face_sel) if k]
    bnd = part.edges_boundary(face_sel)
    bverts = sorted({a for a, b in bnd} | {b for a, b in bnd})
    # vertices used by selected faces
    reg = sorted({v for i in sel_idx for v in part.f[i]})
    # vertices used by non-selected faces
    other = {v for i, k in enumerate(face_sel) if not k for v in part.f[i]}
    dup = {}
    v = list(part.v)
    W = list(part.W)
    for vi in reg:
        if vi in other:              # shared with the rest: duplicate
            dup[vi] = len(v)
            v.append(part.v[vi].copy())
            W.append(part.W[vi].copy())
    v = np.array(v)
    W = np.array(W)
    regmap = {vi: dup.get(vi, vi) for vi in reg}
    for vi in reg:
        v[regmap[vi]] = part.v[vi] + nrm[vi] * offset
    faces = [list(f) for f in part.f]
    uvs = [list(u) for u in part.uv] if part.uv is not None else None
    for i in sel_idx:
        faces[i] = [regmap[x] for x in faces[i]]
    mats = list(part.m)
    if lip:
        # UVs of the border edge as seen from the selected face (the lip reuses them: it joins that island)
        edge_uv = {}
        if uvs is not None:
            for i in sel_idx:
                f = part.f[i]
                for k in range(len(f)):
                    a, b = f[k], f[(k + 1) % len(f)]
                    edge_uv[(a, b)] = (part.uv[i][k], part.uv[i][(k + 1) % len(f)], mats[i])
        for a, b in bnd:
            if a in dup and b in dup:
                # selected face winding a->b; the lip goes from the lower (original) border up to the raised one
                faces.append([a, b, dup[b], dup[a]])
                info = edge_uv.get((a, b))
                mats.append(info[2] if info else mats[sel_idx[0]])
                if uvs is not None:
                    ua, ub = (info[0], info[1]) if info else ((0, 0), (0, 0))
                    du = (np.array(ub) - np.array(ua)) * 0.0
                    uvs.append([tuple(ua), tuple(ub), tuple(np.array(ub) + du), tuple(np.array(ua) + du)])
    out = Part(part.name, v, faces, mats, W, uvs)
    out.lip_count = len(faces) - len(part.f)
    return out


def clip(part, fvals, keep_negative=True, eps=1e-9):
    """Cut the mesh along the zero level of a per-vertex scalar field and keep the side where f < 0 (or > 0).
    New vertices are created on crossing edges (shared between neighbouring faces) with interpolated position,
    weights and per-loop UVs; faces become the clipped polygons. Returns a new Part (face order roughly kept)."""
    f = np.asarray(fvals, dtype=np.float64).copy()
    if not keep_negative:
        f = -f
    f[np.abs(f) < eps] = eps          # never exactly zero: no degenerate slivers
    v = list(part.v)
    W = list(part.W)
    edge_new = {}
    out_f, out_m, out_uv = [], [], [] if part.uv is not None else None

    def cut(a, b):
        key = (min(a, b), max(a, b))
        if key in edge_new:
            return edge_new[key]
        t = f[a] / (f[a] - f[b])
        v.append(part.v[a] + t * (part.v[b] - part.v[a]))
        W.append(part.W[a] + t * (part.W[b] - part.W[a]))
        edge_new[key] = (len(v) - 1, a, b, t)
        return edge_new[key]

    for fi, face in enumerate(part.f):
        neg = [f[i] < 0 for i in face]
        if all(neg):
            out_f.append(list(face))
            out_m.append(part.m[fi])
            if out_uv is not None:
                out_uv.append(list(part.uv[fi]))
            continue
        if not any(neg):
            continue
        poly, puv = [], []
        n = len(face)
        for k in range(n):
            a, b = face[k], face[(k + 1) % n]
            if f[a] < 0:
                poly.append(a)
                if out_uv is not None:
                    puv.append(part.uv[fi][k])
            if (f[a] < 0) != (f[b] < 0):
                idx, ea, eb, t = cut(a, b)
                poly.append(idx)
                if out_uv is not None:
                    ua, ub = np.array(part.uv[fi][k]), np.array(part.uv[fi][(k + 1) % n])
                    tt = f[a] / (f[a] - f[b])
                    puv.append(tuple(ua + tt * (ub - ua)))
        if len(poly) >= 3:
            out_f.append(poly)
            out_m.append(part.m[fi])
            if out_uv is not None:
                out_uv.append(puv)
    newp = Part(part.name, np.array(v), out_f, out_m, np.array(W), out_uv)
    return compact(newp)


def slice_assign(part, fvals, mat_neg=None, mat_pos=None, eps=1e-9):
    """Cut every face crossing the zero level of the field into its negative and positive pieces (shared new
    vertices) and optionally re-assign materials by side. Returns (new part, side per face: -1 / +1)."""
    f = np.asarray(fvals, dtype=np.float64).copy()
    f[np.abs(f) < eps] = eps
    v = list(part.v)
    W = list(part.W)
    edge_new = {}
    out_f, out_m, out_side = [], [], []
    out_uv = [] if part.uv is not None else None

    def cut(a, b):
        key = (min(a, b), max(a, b))
        if key not in edge_new:
            t = f[a] / (f[a] - f[b])
            v.append(part.v[a] + t * (part.v[b] - part.v[a]))
            W.append(part.W[a] + t * (part.W[b] - part.W[a]))
            edge_new[key] = len(v) - 1
        return edge_new[key]

    for fi, face in enumerate(part.f):
        neg = [f[i] < 0 for i in face]
        n = len(face)
        if all(neg) or not any(neg):
            out_f.append(list(face))
            out_side.append(-1 if neg[0] else 1)
            out_m.append(part.m[fi])
            if out_uv is not None:
                out_uv.append(list(part.uv[fi]))
            continue
        for want in (True, False):
            poly, puv = [], []
            for k in range(n):
                a, b = face[k], face[(k + 1) % n]
                if (f[a] < 0) == want:
                    poly.append(a)
                    if out_uv is not None:
                        puv.append(part.uv[fi][k])
                if (f[a] < 0) != (f[b] < 0):
                    poly.append(cut(a, b))
                    if out_uv is not None:
                        ua, ub = np.array(part.uv[fi][k]), np.array(part.uv[fi][(k + 1) % n])
                        tt = f[a] / (f[a] - f[b])
                        puv.append(tuple(ua + tt * (ub - ua)))
            if len(poly) >= 3:
                out_f.append(poly)
                out_side.append(-1 if want else 1)
                out_m.append(part.m[fi])
                if out_uv is not None:
                    out_uv.append(puv)
    if mat_neg is not None or mat_pos is not None:
        out_m = [(mat_neg if s < 0 else mat_pos) or m for m, s in zip(out_m, out_side)]
    return Part(part.name, np.array(v), out_f, out_m, np.array(W), out_uv), np.array(out_side)


def compact(part):
    """Drop unused vertices."""
    used = sorted({v for f in part.f for v in f})
    if len(used) == len(part.v):
        return part
    remap = -np.ones(len(part.v), dtype=np.int64)
    remap[used] = np.arange(len(used))
    return Part(part.name, part.v[used], [[int(remap[v]) for v in f] for f in part.f], part.m, part.W[used],
                part.uv, part.src[used] if part.src is not None and len(part.src) == len(part.v) else None)


def boundary_loops(part, face_sel=None):
    """Ordered boundary loops (lists of vertex ids following the face winding)."""
    edges = part.edges_boundary(face_sel)
    nxt = {}
    for a, b in edges:
        nxt.setdefault(a, []).append(b)
    loops, seen = [], set()
    for a, b in edges:
        if (a, b) in seen:
            continue
        loop = [a]
        cur, prev = b, a
        seen.add((a, b))
        guard = 0
        while cur != a and guard < 100000:
            guard += 1
            loop.append(cur)
            cands = [c for c in nxt.get(cur, []) if (cur, c) not in seen]
            if not cands:
                break
            seen.add((cur, cands[0]))
            prev, cur = cur, cands[0]
        loops.append(loop)
    return loops


def to_object(part, materials, collection=None, smooth=True):
    """Create a Blender mesh object: material slots in the order of `materials` (list of slot names)."""
    import bpy
    import ch_bl
    obj = ch_bl.mesh_from_arrays(part.name, part.v, part.f, part.uv, collection, smooth)
    for mname in materials:
        mat = bpy.data.materials.get(mname) or bpy.data.materials.new(mname)
        obj.data.materials.append(mat)
    idx = np.array([materials.index(m) for m in part.m], dtype=np.int32)
    obj.data.polygons.foreach_set("material_index", idx)
    obj.data.update()
    return obj
