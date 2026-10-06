"""UV layout: analytic maps for wraps / hair (pattern direction matters), Blender unwrap with rule-based seams for the
garments, grain alignment (world up -> +V), texel-density levelling and per-material packing (no rotation)."""
import numpy as np

import ch_part as P


def _angle_unwrap(a, ref):
    """Bring angles a near the reference angle (no 2*pi jumps inside one face)."""
    return ref + (a - ref + np.pi) % (2 * np.pi) - np.pi


def cylindrical_uv(part, face_mask, p0, p1, seam_dir):
    """Per-loop UVs (metres) for the selected faces: u = arc around the p0->p1 axis (seam along seam_dir),
    v = distance along the axis."""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    ax = (p1 - p0) / np.linalg.norm(p1 - p0)
    e1 = np.asarray(seam_dir, float)
    e1 = e1 - ax * (e1 @ ax)
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(ax, e1)
    rel = part.v - p0
    along = rel @ ax
    radial = rel - np.outer(along, ax)
    ang = np.arctan2(radial @ e2, radial @ e1)                 # (-pi, pi], seam at +-pi (opposite seam_dir)
    r = np.linalg.norm(radial, axis=1)
    sel_v = sorted({v for i, m in enumerate(face_mask) if m for v in part.f[i]})
    rmean = float(np.median(r[sel_v])) if sel_v else 0.03
    if part.uv is None:
        part.uv = [[(0.0, 0.0)] * len(f) for f in part.f]
    for i, m in enumerate(face_mask):
        if not m:
            continue
        f = part.f[i]
        a = ang[f]
        a = _angle_unwrap(a, a[0])
        # keep each face on one side of the seam (all angles in (-pi, pi] after shifting by the mean)
        mean = np.mean(a)
        if mean > np.pi:
            a -= 2 * np.pi
        elif mean <= -np.pi:
            a += 2 * np.pi
        part.uv[i] = [(float(a[k] * rmean), float(along[f[k]])) for k in range(len(f))]
    return part


def polar_uv(part, face_mask, center, axis, seam_dir, radius):
    """Map around a pole (the top knot): v = angular distance from the pole * radius (strands run along v),
    u = azimuth around the pole axis * radius (seam along seam_dir)."""
    c = np.asarray(center, float)
    ax = np.asarray(axis, float) / np.linalg.norm(axis)
    e1 = np.asarray(seam_dir, float)
    e1 = e1 - ax * (e1 @ ax)
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(ax, e1)
    rel = part.v - c
    rel /= np.linalg.norm(rel, axis=1, keepdims=True) + 1e-12
    pol = np.arccos(np.clip(rel @ ax, -1, 1))
    az = np.arctan2(rel @ e2, rel @ e1)
    if part.uv is None:
        part.uv = [[(0.0, 0.0)] * len(f) for f in part.f]
    for i, m in enumerate(face_mask):
        if not m:
            continue
        f = part.f[i]
        a = _angle_unwrap(az[f], az[f][0])
        mean = np.mean(a)
        if mean > np.pi:
            a -= 2 * np.pi
        elif mean <= -np.pi:
            a += 2 * np.pi
        # equal-area-ish: arc length around the pole shrinks with sin(pol)
        part.uv[i] = [(float(a[k] * radius * np.sin(max(pol[f[k]], 0.25))), float(pol[f[k]] * radius))
                      for k in range(len(f))]
    return part


def front_back_classes(part, cy=0.0):
    """Per-face class (0 front, 1 back) by face centroid y (seams fall on the sides / under the arms)."""
    fc = part.face_centers()
    return (fc[:, 1] > cy).astype(int)


# ------------------------------------------------------------------------------------------------ bpy side
def seam_edges_from_classes(part, classes):
    """Edges (a, b) shared by faces of different classes."""
    owner = {}
    seams = set()
    for i, f in enumerate(part.f):
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            key = (min(a, b), max(a, b))
            if key in owner and owner[key] != classes[i]:
                seams.add(key)
            owner.setdefault(key, classes[i])
    return seams


def blender_unwrap(part, classes, extra_seams=()):
    """Unwrap a part in Blender with seams between face classes (+ its open borders). Returns per-loop UVs."""
    import bpy
    import bmesh
    import ch_bl
    obj = ch_bl.mesh_from_arrays("_uv_tmp", part.v, part.f, None)
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.edges.ensure_lookup_table()
    seams = seam_edges_from_classes(part, classes) | set(extra_seams)
    for e in bm.edges:
        a, b = e.verts[0].index, e.verts[1].index
        if (min(a, b), max(a, b)) in seams:
            e.seam = True
    bm.to_mesh(me)
    bm.free()
    me.uv_layers.new(name="UVMap")
    ch_bl.select_only([obj])
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.unwrap(method="ANGLE_BASED", margin=0.001)
    bpy.ops.object.mode_set(mode="OBJECT")
    uvl = me.uv_layers[0].data
    out = []
    li = 0
    for poly in me.polygons:
        out.append([tuple(uvl[poly.loop_start + k].uv) for k in range(poly.loop_total)])
        li += poly.loop_total
    bpy.data.objects.remove(obj)
    bpy.data.meshes.remove(me)
    part.uv = out
    return part


# ------------------------------------------------------------------------------------------------ islands
def uv_islands(part, face_sel=None):
    """Connected face sets sharing UV coordinates on shared vertices. Returns list of face index lists."""
    faces = range(len(part.f)) if face_sel is None else [i for i in range(len(part.f)) if face_sel[i]]
    parent = {}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    key_owner = {}
    for i in faces:
        parent[i] = i
        f, uv = part.f[i], part.uv[i]
        for k in range(len(f)):
            a, b = f[k], f[(k + 1) % len(f)]
            ua, ub = uv[k], uv[(k + 1) % len(f)]
            key = (min(a, b), max(a, b))
            sig = (key, tuple(np.round(ua if a < b else ub, 6)), tuple(np.round(ub if a < b else ua, 6)))
            if sig in key_owner:
                union(i, key_owner[sig])
            else:
                key_owner[sig] = i
    groups = {}
    for i in faces:
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def _tri_area3(p):
    return 0.5 * np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))


def island_scale_and_orient(part, island, up=(0, 0, 1), orient=True, density=1.0):
    """Scale an island to metres (3D area / UV area) * density and rotate it so `up` maps to +V."""
    a3 = 0.0
    a2 = 0.0
    g = np.zeros(2)
    upv = np.asarray(up, float)
    for i in island:
        f, uv = part.f[i], np.array(part.uv[i], float)
        p = part.v[f]
        for k in range(1, len(f) - 1):
            P3 = p[[0, k, k + 1]]
            U = uv[[0, k, k + 1]]
            a3 += _tri_area3(P3)
            d2 = 0.5 * abs((U[1, 0] - U[0, 0]) * (U[2, 1] - U[0, 1]) - (U[2, 0] - U[0, 0]) * (U[1, 1] - U[0, 1]))
            a2 += d2
            if orient:
                # gradient of the world 'up' coordinate in UV space
                M = np.array([U[1] - U[0], U[2] - U[0]])
                h = np.array([(P3[1] - P3[0]) @ upv, (P3[2] - P3[0]) @ upv])
                det = np.linalg.det(M)
                if abs(det) > 1e-14:
                    g += np.linalg.solve(M, h) * _tri_area3(P3)
    s = np.sqrt(a3 / a2) * density if a2 > 1e-14 else 1.0
    rot = 0.0
    if orient and np.linalg.norm(g) > 1e-12:
        rot = np.pi / 2 - np.arctan2(g[1], g[0])
    c, sn = np.cos(rot), np.sin(rot)
    for i in island:
        uv = np.array(part.uv[i], float) * s
        uv = uv @ np.array([[c, sn], [-sn, c]])
        part.uv[i] = [tuple(x) for x in uv]
    return s


def _skyline_try(dims, scale, margin, cells=512, allow_rot=False):
    """Bottom-left skyline packing of rectangles (w, h) * scale into the unit square. Returns positions + flags."""
    sky = np.zeros(cells)
    order = sorted(range(len(dims)), key=lambda k: -max(dims[k][0], dims[k][1]) if allow_rot else -dims[k][1])
    out = {}
    for k in order:
        best = None
        for rot in ((False, True) if allow_rot else (False,)):
            w, h = dims[k][::-1] if rot else dims[k]
            w, h = w * scale + margin, h * scale + margin
            wc = int(np.ceil(w * cells))
            if wc > cells:
                continue
            # sliding window max over the skyline
            from numpy.lib.stride_tricks import sliding_window_view
            win = sliding_window_view(sky, wc).max(axis=1)
            x = int(np.argmin(win))
            y = win[x]
            if y + h <= 1.0 and (best is None or y + h < best[2] + best[4]):
                best = (x, rot, y, wc, h)
        if best is None:
            return None
        x, rot, y, wc, h = best
        sky[x:x + wc] = y + h
        out[k] = (x / cells + margin * 0.5, y + margin * 0.5, rot)
    return out


def pack(entries, margin_px=6, size=2048, allow_rot=False):
    """Pack islands of several parts into one 0-1 square (uniform scale; optional 90 deg rotation).
    entries: list of (part, island face list). UVs are in metres on input. Returns UV units per metre."""
    boxes = []
    for pi, (part, isl) in enumerate(entries):
        uv = np.vstack([np.array(part.uv[i], float) for i in isl])
        lo, hi = uv.min(axis=0), uv.max(axis=0)
        boxes.append((pi, lo, hi - lo))
    dims = [b[2] for b in boxes]
    margin = margin_px / size
    lo_s, hi_s = 1e-3, None
    total = sum(d[0] * d[1] for d in dims)
    s = 1.0 / np.sqrt(max(total, 1e-12)) * 0.7
    best = None
    for _ in range(36):
        pos = _skyline_try(dims, s, margin, allow_rot=allow_rot)
        if pos is None:
            hi_s = s
        else:
            best = (s, pos)
            lo_s = s
        s = np.sqrt(lo_s * hi_s) if hi_s is not None else s * 1.4
    s, pos = best
    for k, (pi, lo, wh) in enumerate(boxes):
        part, isl = entries[pi]
        ox, oy, rot = pos[k]
        for i in isl:
            uv = (np.array(part.uv[i], float) - lo) * s
            if rot:
                uv = np.stack([uv[:, 1], wh[0] * s - uv[:, 0]], axis=1)
            uv = uv + np.array([ox, oy])
            part.uv[i] = [tuple(x) for x in uv]
    return s       # UV units per metre
