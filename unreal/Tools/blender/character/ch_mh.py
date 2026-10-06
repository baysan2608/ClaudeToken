"""Readers for the CC0 MakeHuman base-mesh data we start the fighter from (no bpy needed; numpy only).

Source: MakeHuman 1.x assets (base mesh hm08, targets, default skeleton + weights, eye meshes), released as CC0 1.0 by
the MakeHuman project (see unreal/SourceArt/Character/LICENSES.md). Only DATA files are used; no MakeHuman program code is
copied - the macro-target blending below is our own straightforward implementation of "base + sum(weight * delta)".

MakeHuman space: decimetres, +Y up, +Z forward (face), +X = character left.
Fourfold / Blender space: metres, +Z up, character faces -Y, character left = +X.   to_ff(): (x, -z, y) * 0.1
"""
import json
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MH_DIR = os.path.join(UNREAL, "SourceArt", "Character", "third_party", "makehuman")


class ObjMesh:
    """Exact-order OBJ reader (vertex indices must match the target files)."""

    def __init__(self, path):
        v, vt, faces, fuv, groups = [], [], [], [], []
        group = None
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("v "):
                    v.append([float(x) for x in line.split()[1:4]])
                elif line.startswith("vt "):
                    vt.append([float(x) for x in line.split()[1:3]])
                elif line.startswith("g "):
                    group = line.split(None, 1)[1].strip()
                elif line.startswith("f "):
                    fv, ft = [], []
                    for tok in line.split()[1:]:
                        parts = tok.split("/")
                        fv.append(int(parts[0]) - 1)
                        ft.append(int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1)
                    faces.append(fv)
                    fuv.append(ft)
                    groups.append(group)
        self.v = np.array(v, dtype=np.float64)
        self.vt = np.array(vt, dtype=np.float64) if vt else np.zeros((0, 2))
        self.faces = faces
        self.fuv = fuv
        self.groups = groups

    def group_names(self):
        return sorted(set(self.groups))

    def group_faces(self, name_pred):
        return [i for i, g in enumerate(self.groups) if name_pred(g)]

    def group_verts(self, name_pred):
        s = set()
        for i in self.group_faces(name_pred):
            s.update(self.faces[i])
        return np.array(sorted(s), dtype=np.int64)


def read_target(path, nverts):
    """A .target file: 'index dx dy dz' lines. Returns a dense (n,3) delta array."""
    d = np.zeros((nverts, 3), dtype=np.float64)
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line or line[0] == "#" or not line.strip():
                continue
            p = line.split()
            d[int(p[0])] = (float(p[1]), float(p[2]), float(p[3]))
    return d


def _tri(value):
    """MakeHuman-style 3-point slider weights for value in [0,1]: (min, average, max)."""
    v = float(np.clip(value, 0.0, 1.0))
    if v < 0.5:
        return (1.0 - v / 0.5, v / 0.5, 0.0)
    return (0.0, 1.0 - (v - 0.5) / 0.5, (v - 0.5) / 0.5)


def macro_target_weights(recipe):
    """Our blend of the male / young macro targets. recipe keys: muscle, weight, proportions (0..1, 0.5 = neutral),
    races {name: w}. Returns {relative target path: weight}."""
    out = {}
    mus = dict(zip(("minmuscle", "averagemuscle", "maxmuscle"), _tri(recipe["muscle"])))
    wgt = dict(zip(("minweight", "averageweight", "maxweight"), _tri(recipe["weight"])))
    prop = float(recipe.get("proportions", 0.5))
    ideal = max(0.0, (prop - 0.5) * 2.0)
    for m, mw in mus.items():
        for w, ww in wgt.items():
            k = mw * ww
            if k <= 1e-6:
                continue
            out[f"macrodetails/universal-male-young-{m}-{w}.target"] = k
            if ideal > 0:
                out[f"macrodetails/proportions/male-young-{m}-{w}-idealproportions.target"] = k * ideal
    for race, rw in recipe["races"].items():
        if rw > 0:
            out[f"macrodetails/{race}-male-young.target"] = float(rw)
    return out


def apply_targets(base_v, weights, root=None):
    root = root or os.path.join(MH_DIR, "targets")
    v = base_v.copy()
    for rel, w in sorted(weights.items()):
        if abs(w) < 1e-9:
            continue
        v += w * read_target(os.path.join(root, rel), len(base_v))
    return v


def to_ff(v):
    """MakeHuman decimetres (x, y up, z fwd) -> Fourfold metres (x, -z, y)."""
    v = np.asarray(v, dtype=np.float64)
    return np.stack([v[..., 0], -v[..., 2], v[..., 1]], axis=-1) * 0.1


def load_skeleton(path=None):
    path = path or os.path.join(MH_DIR, "rigs", "default.mhskel")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def joint_positions(skel, verts):
    """{joint name: centroid of its vertex list} on the given (morphed) vertex array."""
    return {k: verts[np.array(ix)].mean(axis=0) for k, ix in skel["joints"].items()}


def bone_ends(skel, verts):
    j = joint_positions(skel, verts)
    return {b: (j[d["head"]], j[d["tail"]]) for b, d in skel["bones"].items()}


def load_weights(path=None):
    """{mh bone: (indices, weights)}"""
    path = path or os.path.join(MH_DIR, "rigs", "default_weights.mhw")
    with open(path, encoding="utf-8") as f:
        w = json.load(f)["weights"]
    out = {}
    for b, pairs in w.items():
        a = np.array(pairs, dtype=np.float64).reshape(-1, 2)
        out[b] = (a[:, 0].astype(np.int64), a[:, 1])
    return out
