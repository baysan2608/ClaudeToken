"""Fourfold FX hero meshes (stream `fx`): rock set with baked normal maps, crystals, metal pieces, chips.

    /home/user/tools/bpyenv/bin/python unreal/Tools/vfx/meshes.py [--only rock_0,disc] [--preview DIR] [--quick]

Outputs (committed) to unreal/SourceArt/VFX/Meshes/: SM_FX_<name>.fbx (+ T_FX_rock_<k>_N.png). Deterministic seeds.
Export: the frozen Tools/blender/common/ff_fbx_export settings (metres, FBX_SCALE_ALL, -Z forward / Y up), so Blender
(x, y, z) arrives in Unreal as (x, -y, z) centimetres. Every mesh keeps the contract of its procedural twin in
Source/FourfoldFX/Private/Logic/FxMeshLib.cpp (same size / axes / UV channels), which the C++ draws when an asset is
missing:
  rock_k      unit radius (1 m) rock, 300-1500 tris, smooth shading + baked tangent-space normal map (DirectX green),
              UV0 = normal-map unwrap, UV1 = blob offset (UE X, UE Y), UV2 = (blob offset UE Z, per-vertex random),
              metres of mesh units (the material relaxes the vertex by the offset when the rock melts)
  ice_shard   hexagonal crystal along +Z (-0.5 .. 0.5, radius 0.16), flat shaded, UV0 = (across facet, along),
              UV1 = (crystal random, base height)
  crystal_0/1 clusters (same channels)
  disc        lathe around +Z, radius 1, thickness 0.12, UV0 = (angle, t)
  lance       lathe along +Z (-0.5 .. 0.5), radius 1 (the view scales it thin), UV0 = (angle, t)
  plate       chamfered unit cube (half 0.5), UV0 planar
  spike       faceted cone from z = -0.05 to 1, base radius 1
  caltrop     four spikes of length 1
  chip        20-face icosahedron of radius 1 (debris)
Rock normal maps are baked with Cycles from a sculpt-like high-resolution version (ellipsoid + lumps + planar chips
+ ridged cracks + grain, ~80 k faces) onto the decimated low mesh.
"""
from __future__ import annotations

import argparse
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(UNREAL, "SourceArt", "VFX", "Meshes")
sys.path.insert(0, os.path.join(UNREAL, "Tools", "blender", "common"))

import bpy  # noqa: E402
import bmesh  # noqa: E402

import ff_fbx_export  # noqa: E402  (frozen export settings)

NORMAL_SIZE = 512


# ---------------------------------------------------------------------------------------------- noise (numpy)
def _hash3(ix, iy, iz, seed):
    h = (ix.astype(np.int64) * 73856093) ^ (iy.astype(np.int64) * 19349663) ^ (iz.astype(np.int64) * 83492791) ^ (seed * 2654435761)
    h = (h ^ (h >> 13)) * 1274126177
    h = h ^ (h >> 16)
    return ((h & 0xFFFFFF).astype(np.float64)) / float(0xFFFFFF)


def value_noise3(p, seed):
    """Smooth value noise in [0, 1] at points p (N, 3)."""
    i = np.floor(p).astype(np.int64)
    f = p - i
    u = f * f * f * (f * (f * 6 - 15) + 10)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = ((u[:, 0] if dx else 1 - u[:, 0]) * (u[:, 1] if dy else 1 - u[:, 1]) * (u[:, 2] if dz else 1 - u[:, 2]))
                out = out + w * _hash3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz, seed)
    return out


def fbm3(p, seed, octaves=4, gain=0.5):
    s, a, n = 0.0, 1.0, 0.0
    for o in range(octaves):
        s = s + a * value_noise3(p * (2.03 ** o), seed + o * 17)
        n += a
        a *= gain
    return s / n


def ridged3(p, seed, octaves=3):
    s, a, n = 0.0, 1.0, 0.0
    for o in range(octaves):
        r = 1.0 - np.abs(value_noise3(p * (2.1 ** o), seed + 31 * o) * 2.0 - 1.0)
        s = s + a * r * r
        n += a
        a *= 0.5
    return s / n


# ---------------------------------------------------------------------------------------------- blender helpers
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scn = bpy.context.scene
    scn.unit_settings.system = "METRIC"
    scn.unit_settings.scale_length = 1.0


def mesh_from_numpy(name, verts, faces):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def icosphere_arrays(subdiv):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    bm.verts.ensure_lookup_table()
    v = np.array([vv.co[:] for vv in bm.verts], np.float64)
    f = [[vv.index for vv in face.verts] for face in bm.faces]
    bm.free()
    return v, f


def set_active(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob


def shade_smooth(ob, flat=False):
    for p in ob.data.polygons:
        p.use_smooth = not flat


def add_uv(ob, name, values_per_vertex):
    """UV map whose per-loop values come from per-vertex (u, v) pairs."""
    me = ob.data
    uv = me.uv_layers.new(name=name)
    vi = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", vi)
    data = values_per_vertex[vi].astype(np.float32).ravel()
    uv.data.foreach_set("uv", data)
    return uv


def export_static(ob, path, mat_name):
    """Static mesh FBX through the frozen export settings (single material slot)."""
    if not ob.data.materials:
        ob.data.materials.append(bpy.data.materials.get(mat_name) or bpy.data.materials.new(mat_name))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    set_active(ob)
    kw = ff_fbx_export._common()
    kw.update(object_types={"MESH"}, use_mesh_modifiers=True, mesh_smooth_type="FACE", use_tspace=True,
              use_triangles=True, bake_anim=False)
    bpy.ops.export_scene.fbx(filepath=path, **kw)
    return path


def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


# ---------------------------------------------------------------------------------------------- rocks
def rock_shape(dirs, k, rng, detail=True):
    """Sculpt-like rock: ellipsoid proportions, lumps, planar chips (flattened facets), ridged cracks, grain."""
    sc = np.array([1.0, rng.uniform(0.62, 0.95), rng.uniform(0.48, 0.78)])
    if rng.uniform() < 0.5:
        sc[0], sc[1] = sc[1], sc[0]
    p = dirs * sc
    seed = 1000 + 97 * k
    lump = fbm3(dirs * 1.4 + k * 3.1, seed, 3)
    p = p * (1.0 + 0.22 * (lump[:, None] - 0.5))
    # planar chips: everything past a random plane is pushed back onto it (a fractured face)
    for i in range(int(rng.integers(5, 10))):
        n = rng.normal(size=3)
        n /= np.linalg.norm(n)
        ext = np.abs(p @ n).max()
        d = ext * rng.uniform(0.62, 0.86)
        h = p @ n
        over = np.clip(h - d, 0.0, None)
        p = p - n[None, :] * (over * rng.uniform(0.85, 1.0))[:, None]
    if detail:
        crack = ridged3(dirs * 3.2 + k * 1.7, seed + 7, 3)
        p = p - dirs * (0.035 * np.clip((crack - 0.62) / 0.38, 0.0, 1.0) ** 2)[:, None]
        grain = fbm3(dirs * 18.0 + k, seed + 11, 2)
        p = p + dirs * (0.012 * (grain - 0.5))[:, None]
        pits = fbm3(dirs * 7.0 + k * 0.3, seed + 13, 2)
        p = p - dirs * (0.02 * np.clip((pits - 0.62) / 0.38, 0.0, 1.0))[:, None]
    # unit radius
    p = p / np.linalg.norm(p, axis=1).max()
    return p


def blob_offsets(verts, k):
    """Blob = ellipsoid with the rock's proportions (x 0.9, vertical x 0.92 more); offset = blob - vertex (metres).
    Returned in UE axes: (x, -y, z) of Blender."""
    ext = np.abs(verts).max(0)
    e = 0.9 * np.array([ext[0], ext[1], ext[2] * 0.92])
    d = verts / np.maximum(np.linalg.norm(verts, axis=1, keepdims=True), 1e-6)
    s = 1.0 / np.sqrt(((d / e) ** 2).sum(1))
    blob = d * s[:, None]
    off = blob - verts
    rng = np.random.default_rng(5000 + k)
    rnd = rng.uniform(0.0, 1.0, len(verts))
    return np.stack([off[:, 0], -off[:, 1]], 1), np.stack([off[:, 2], rnd], 1)


def make_rock(k, quick=False, preview_dir=None):
    reset_scene()
    rng = np.random.default_rng(700 + k)
    dirs_hi, faces_hi = icosphere_arrays(4 if quick else 6)
    state = rng.bit_generator.state
    hi = mesh_from_numpy(f"rock_{k}_hi", rock_shape(dirs_hi, k, rng, True), faces_hi)
    shade_smooth(hi)
    # low: same rock, decimated from a medium version (keeps the chipped silhouette)
    rng.bit_generator.state = state
    dirs_lo, faces_lo = icosphere_arrays(4)
    lo = mesh_from_numpy(f"SM_FX_rock_{k}", rock_shape(dirs_lo, k, rng, False), faces_lo)
    set_active(lo)
    target = 900 + 70 * (k % 4)
    dec = lo.modifiers.new("dec", "DECIMATE")
    dec.ratio = target / max(tri_count(lo), 1)
    bpy.ops.object.modifier_apply(modifier="dec")
    shade_smooth(lo)
    # UV0: unwrap for the normal map
    set_active(lo)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    lo.data.uv_layers[0].name = "UVMap"
    # bake the high detail into a tangent-space normal map on the low mesh
    img = bpy.data.images.new(f"T_FX_rock_{k}_N", NORMAL_SIZE, NORMAL_SIZE, alpha=False, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    mat = bpy.data.materials.new("MI_FX_Rock")
    mat.use_nodes = True
    nt = mat.node_tree
    node = nt.nodes.new("ShaderNodeTexImage")
    node.image = img
    nt.nodes.active = node
    lo.data.materials.append(mat)
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = 1
    bake = scn.render.bake
    bake.use_selected_to_active = True
    bake.cage_extrusion = 0.04
    bake.max_ray_distance = 0.12
    bake.margin = 8
    bake.normal_space = "TANGENT"
    bpy.ops.object.select_all(action="DESELECT")
    hi.select_set(True)
    lo.select_set(True)
    bpy.context.view_layer.objects.active = lo
    bpy.ops.object.bake(type="NORMAL")
    px = np.empty(NORMAL_SIZE * NORMAL_SIZE * 4, np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(NORMAL_SIZE, NORMAL_SIZE, 4)
    px[..., 1] = 1.0 - px[..., 1]          # OpenGL (Blender) -> DirectX (Unreal) green
    save_png_rgb(px[::-1, :, :3], os.path.join(OUT, f"T_FX_rock_{k}_N.png"))
    nt.nodes.remove(node)
    bpy.data.objects.remove(hi, do_unlink=True)
    # UV1 / UV2: blob offsets for the melt (after decimation: per final vertex)
    v = np.array([vv.co[:] for vv in lo.data.vertices], np.float64)
    uv1, uv2 = blob_offsets(v, k)
    add_uv(lo, "BlobXY", uv1)
    add_uv(lo, "BlobZRnd", uv2)
    path = export_static(lo, os.path.join(OUT, f"SM_FX_rock_{k}.fbx"), "MI_FX_Rock")
    info = {"tris": tri_count(lo), "verts": len(lo.data.vertices), "uv": [u.name for u in lo.data.uv_layers]}
    if preview_dir:
        info["preview_mesh"] = save_obj_for_preview(lo, os.path.join(preview_dir, f"rock_{k}.npz"))
    return path, info


def save_png_rgb(rgb01, path):
    from PIL import Image
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray((np.clip(rgb01, 0, 1) * 255.0 + 0.5).astype(np.uint8), "RGB").save(path, optimize=True)


def save_obj_for_preview(ob, path):
    """Mesh arrays (triangulated loops) for the review render."""
    me = ob.data
    me.calc_loop_triangles()
    tris = np.array([t.vertices[:] for t in me.loop_triangles], np.int64)
    v = np.array([vv.co[:] for vv in me.vertices], np.float64)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    np.savez(path, v=v, t=tris)
    return path


# ---------------------------------------------------------------------------------------------- crystals
def crystal_one(verts, faces, uv0, uv1, base, axis, length, radius, rnd, sides=6, tip=0.28):
    """One hexagonal prism with a pointed tip; flat shaded (unshared vertices per face)."""
    axis = axis / np.linalg.norm(axis)
    a = np.cross(axis, [0.0, 0.0, 1.0] if abs(axis[2]) < 0.9 else [1.0, 0.0, 0.0])
    a /= np.linalg.norm(a)
    b = np.cross(axis, a)
    ring0, ring1 = [], []
    for s in range(sides):
        ang = 2 * math.pi * s / sides + rnd * 2.0
        r = radius * (0.85 + 0.3 * ((s * 7919 + int(rnd * 1000)) % 13) / 13.0)
        d = a * math.cos(ang) + b * math.sin(ang)
        ring0.append(base + d * r * 0.9)
        ring1.append(base + axis * (length * (1 - tip)) + d * r)
    apex = base + axis * length

    def quad(p0, p1, p2, p3, u0, u1):
        i = len(verts)
        verts.extend([p0, p1, p2, p3])
        uv0.extend([(0.0, u0), (1.0, u0), (1.0, u1), (0.0, u1)])
        uv1.extend([(rnd, 0.0)] * 4)
        faces.append([i, i + 1, i + 2, i + 3])

    def tri(p0, p1, p2, u0, u1):
        i = len(verts)
        verts.extend([p0, p1, p2])
        uv0.extend([(0.0, u0), (1.0, u0), (0.5, u1)])
        uv1.extend([(rnd, 0.0)] * 3)
        faces.append([i, i + 1, i + 2])

    for s in range(sides):
        t = (s + 1) % sides
        quad(ring0[s], ring0[t], ring1[t], ring1[s], 0.0, 1.0 - tip)
        tri(ring1[s], ring1[t], apex, 1.0 - tip, 1.0)
    # base cap (hidden in the ground mostly)
    i = len(verts)
    verts.extend(ring0[::-1])
    uv0.extend([(0.5, 0.0)] * sides)
    uv1.extend([(rnd, 0.0)] * sides)
    faces.append(list(range(i, i + sides)))


def make_crystal(name, mode, seed):
    reset_scene()
    rng = np.random.default_rng(seed)
    verts, faces, uv0, uv1 = [], [], [], []
    if mode == "shard":
        crystal_one(verts, faces, uv0, uv1, np.array([0.0, 0.0, -0.5]), np.array([0.0, 0.0, 1.0]), 1.0, 0.16, rng.uniform())
    else:
        n = int(rng.integers(5, 8))
        for i in range(n):
            ang = 2 * math.pi * i / n + rng.uniform(-0.3, 0.3)
            tilt = rng.uniform(0.15, 0.55) if i else 0.05
            axis = np.array([math.cos(ang) * math.sin(tilt), math.sin(ang) * math.sin(tilt), math.cos(tilt)])
            base = np.array([math.cos(ang), math.sin(ang), 0.0]) * (rng.uniform(0.0, 0.25) if i else 0.0) - [0, 0, 0.15]
            ln = rng.uniform(0.55, 1.0) if i else 1.15
            crystal_one(verts, faces, uv0, uv1, base, axis, ln, ln * rng.uniform(0.12, 0.18), rng.uniform())
    ob = mesh_from_numpy(f"SM_FX_{name}", np.array(verts), faces)
    shade_smooth(ob, flat=True)
    me = ob.data
    # per-loop UVs (vertices are unshared, loops map 1:1 onto them)
    add_uv(ob, "UVMap", np.array(uv0))
    add_uv(ob, "Crystal", np.array(uv1))
    return export_static(ob, os.path.join(OUT, f"SM_FX_{name}.fbx"), "MI_FX_Crystal"), {"tris": tri_count(ob)}


# ---------------------------------------------------------------------------------------------- metal / misc
def lathe(name, radius, z, sides, smooth=True, noise=0.0, seed=0):
    """Surface of revolution around +Z from (radius, z) profile; UV0 = (angle 0..1, t 0..1)."""
    rng = np.random.default_rng(seed)
    n = len(radius)
    verts, uvs, faces = [], [], []
    for i in range(n):
        for s in range(sides + 1):
            a = 2 * math.pi * s / sides
            r = radius[i] * (1.0 + (rng.uniform(-noise, noise) if noise and s < sides else 0.0))
            verts.append((math.cos(a) * r, math.sin(a) * r, z[i]))
            uvs.append((s / sides, i / (n - 1)))
    for i in range(n - 1):
        for s in range(sides):
            a0 = i * (sides + 1) + s
            b0 = a0 + sides + 1
            faces.append([a0, a0 + 1, b0 + 1, b0])
    ob = mesh_from_numpy(name, np.array(verts), faces)
    bpy.context.view_layer.objects.active = ob
    set_active(ob)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=1e-5)
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    shade_smooth(ob, flat=not smooth)
    # UVs from the original vertex grid (after merging, recompute per loop from positions)
    me = ob.data
    uv = me.uv_layers.new(name="UVMap")
    co = np.array([v.co[:] for v in me.vertices])
    vi = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", vi)
    ang = (np.arctan2(co[:, 1], co[:, 0]) / (2 * math.pi)) % 1.0
    t = (co[:, 2] - min(z)) / max(max(z) - min(z), 1e-6)
    luv = np.stack([ang[vi], t[vi]], 1).astype(np.float32)
    # fix the seam per face: loops of one face that wrap across u = 0 / 1
    for p in me.polygons:
        ids = list(range(p.loop_start, p.loop_start + p.loop_total))
        us = luv[ids, 0]
        if us.max() - us.min() > 0.5:
            for li in ids:
                if luv[li, 0] < 0.5:
                    luv[li, 0] += 1.0
    uv.data.foreach_set("uv", luv.ravel())
    return ob


def bevel(ob, width, segments):
    set_active(ob)
    m = ob.modifiers.new("bev", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    bpy.ops.object.modifier_apply(modifier="bev")


def make_metal(name):
    reset_scene()
    if name == "disc":
        ob = lathe("SM_FX_disc", [0.0, 0.18, 0.55, 0.92, 1.0, 0.92, 0.55, 0.18, 0.0],
                   [-0.045, -0.05, -0.06, -0.035, 0.0, 0.035, 0.06, 0.05, 0.045], 48)
    elif name == "lance":
        ob = lathe("SM_FX_lance", [0.0, 0.55, 0.8, 1.0, 1.0, 0.95, 0.8, 0.45, 0.0],
                   [-0.5, -0.495, -0.48, -0.4, 0.2, 0.25, 0.32, 0.42, 0.5], 12)
    elif name == "plate":
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        ob = bpy.context.active_object
        ob.name = "SM_FX_plate"
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.cube_project(cube_size=1.0)
        bpy.ops.object.mode_set(mode="OBJECT")
        bevel(ob, 0.04, 2)
        shade_smooth(ob)
    elif name == "spike":
        ob = lathe("SM_FX_spike", [0.0, 1.0, 0.55, 0.18, 0.0], [-0.05, 0.0, 0.45, 0.85, 1.0], 7, smooth=False, noise=0.08,
                   seed=3)
        return export_static(ob, os.path.join(OUT, "SM_FX_spike.fbx"), "MI_FX_Rock"), {"tris": tri_count(ob)}
    elif name == "caltrop":
        verts, faces = [], []
        dirs = [(0, 0, 1), (0.943, 0, -0.333), (-0.471, 0.816, -0.333), (-0.471, -0.816, -0.333)]
        for d in dirs:
            d = np.array(d, float)
            s = np.cross(d, [0, 1, 0] if abs(d[1]) < 0.9 else [1, 0, 0])
            s /= np.linalg.norm(s)
            t = np.cross(d, s)
            base = len(verts)
            for kk in range(6):
                a = 2 * math.pi * kk / 6
                verts.append(tuple((s * math.cos(a) + t * math.sin(a)) * 0.13))
            verts.append(tuple(d))
            for kk in range(6):
                faces.append([base + kk, base + (kk + 1) % 6, base + 6])
        ob = mesh_from_numpy("SM_FX_caltrop", np.array(verts), faces)
        set_active(ob)
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.uv.smart_project()
        bpy.ops.object.mode_set(mode="OBJECT")
        shade_smooth(ob, flat=True)
    elif name == "chip":
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=1.0)
        ob = bpy.context.active_object
        ob.name = "SM_FX_chip"
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project()
        bpy.ops.object.mode_set(mode="OBJECT")
        shade_smooth(ob, flat=True)
        return export_static(ob, os.path.join(OUT, "SM_FX_chip.fbx"), "MI_FX_Rock"), {"tris": tri_count(ob)}
    else:
        raise ValueError(name)
    return export_static(ob, os.path.join(OUT, f"SM_FX_{name}.fbx"), "MI_FX_Metal"), {"tris": tri_count(ob)}


# ---------------------------------------------------------------------------------------------- review render
def render_preview(out_png, quick=False):
    """Imports every exported FBX back (round trip check) and renders a lit contact sheet with Cycles."""
    reset_scene()
    names = sorted(f for f in os.listdir(OUT) if f.endswith(".fbx"))
    x = 0.0
    for i, fn in enumerate(names):
        bpy.ops.import_scene.fbx(filepath=os.path.join(OUT, fn))
        obs = [o for o in bpy.context.selected_objects if o.type == "MESH"]
        for ob in obs:
            col, row = i % 6, i // 6
            ob.location = (col * 2.4 - 6.0, 0.0, -row * 2.4 + 2.4)
            ob.rotation_euler = (0.35, 0.0, 0.6)
            dims = max(ob.dimensions)
            s = 1.8 / max(dims, 1e-3)
            ob.scale = (s, s, s)
            mat = bpy.data.materials.new("prev")
            mat.use_nodes = True
            bsdf = mat.node_tree.nodes["Principled BSDF"]
            stem = fn[len("SM_FX_"):-4]
            if stem.startswith("rock"):
                bsdf.inputs["Base Color"].default_value = (0.11, 0.10, 0.09, 1)
                bsdf.inputs["Roughness"].default_value = 0.85
                npath = os.path.join(OUT, f"T_FX_{stem}_N.png")
                if os.path.exists(npath):
                    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
                    tex.image = bpy.data.images.load(npath)
                    tex.image.colorspace_settings.name = "Non-Color"
                    # back to OpenGL for Blender's normal map node
                    sep = mat.node_tree.nodes.new("ShaderNodeSeparateColor")
                    comb = mat.node_tree.nodes.new("ShaderNodeCombineColor")
                    inv = mat.node_tree.nodes.new("ShaderNodeMath")
                    inv.operation = "SUBTRACT"
                    inv.inputs[0].default_value = 1.0
                    nm = mat.node_tree.nodes.new("ShaderNodeNormalMap")
                    nm.uv_map = "UVMap"
                    L = mat.node_tree.links
                    L.new(tex.outputs["Color"], sep.inputs["Color"])
                    L.new(sep.outputs["Red"], comb.inputs["Red"])
                    L.new(sep.outputs["Green"], inv.inputs[1])
                    L.new(inv.outputs["Value"], comb.inputs["Green"])
                    L.new(sep.outputs["Blue"], comb.inputs["Blue"])
                    L.new(comb.outputs["Color"], nm.inputs["Color"])
                    L.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
            elif stem in ("ice_shard", "crystal_0", "crystal_1"):
                bsdf.inputs["Base Color"].default_value = (0.6, 0.85, 0.95, 1)
                bsdf.inputs["Roughness"].default_value = 0.15
            else:
                bsdf.inputs["Base Color"].default_value = (0.56, 0.62, 0.70, 1)
                bsdf.inputs["Metallic"].default_value = 1.0
                bsdf.inputs["Roughness"].default_value = 0.3
            ob.data.materials.clear()
            ob.data.materials.append(mat)
    scn = bpy.context.scene
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 15.0
    cam = bpy.data.objects.new("cam", cam_data)
    scn.collection.objects.link(cam)
    cam.location = (0.0, -20.0, 0.6)
    cam.rotation_euler = (math.radians(90), 0.0, 0.0)
    scn.camera = cam
    sun_d = bpy.data.lights.new("sun", "SUN")
    sun_d.energy = 4.0
    sun = bpy.data.objects.new("sun", sun_d)
    sun.rotation_euler = (math.radians(50), math.radians(-20), math.radians(-30))
    scn.collection.objects.link(sun)
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.35, 0.4, 0.48, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
    scn.world = world
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = 8 if quick else 24
    scn.render.resolution_x = 1440
    scn.render.resolution_y = 720
    scn.render.filepath = out_png
    bpy.ops.render.render(write_still=True)
    return out_png


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--preview", default=None)
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
    only = [s for s in a.only.split(",") if s]
    jobs = [(f"rock_{k}", lambda k=k: make_rock(k, a.quick, a.preview)) for k in range(8)]
    jobs += [("ice_shard", lambda: make_crystal("ice_shard", "shard", 11)),
             ("crystal_0", lambda: make_crystal("crystal_0", "cluster", 21)),
             ("crystal_1", lambda: make_crystal("crystal_1", "cluster", 22))]
    jobs += [(n, lambda n=n: make_metal(n)) for n in ("disc", "lance", "plate", "spike", "caltrop", "chip")]
    for name, fn in jobs:
        if only and name not in only:
            continue
        path, info = fn()
        print(f"{name}: {info} -> {os.path.relpath(path, UNREAL)}", flush=True)
    if a.preview:
        print("preview:", render_preview(os.path.join(a.preview, "meshes.png"), a.quick))


if __name__ == "__main__":
    main()
