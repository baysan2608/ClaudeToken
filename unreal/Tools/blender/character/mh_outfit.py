"""Loose martial-arts training pants for the MetaHuman fighters, generated from their exported body.

    blender -b --python unreal/Tools/blender/character/mh_outfit.py [-- --preview <dir>]

Input:  unreal/SourceArt/Character/MetaHuman/SKM_FF_Body.fbx  (Tools/gasp/export_mh_body.py; Epic sample content)
Output: unreal/SourceArt/Character/MetaHuman/SKM_FF_Pants.fbx + SKM_FF_Sash.fbx (same skeleton, git-ignored: derived
        from that body)

The pants are the body's own leg / hip surface pushed out along its normals by a profile (snug waistband, roomy thighs,
wide shins gathered into ankle cuffs), with horizontal folds where loose cotton bunches, then given thickness. Every
vertex keeps the body's skin weights, so the pants follow the leader pose exactly; setup part `metahuman` imports the
FBX onto metahuman_base_skel and the runtime swaps it in for the cargo pants.
"""
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import ff_fbx_export as FX  # noqa: E402

UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.path.join(UNREAL, "SourceArt", "Character", "MetaHuman", "SKM_FF_Body.fbx")
OUT = os.path.join(UNREAL, "SourceArt", "Character", "MetaHuman", "SKM_FF_Pants.fbx")
OUT_SASH = os.path.join(UNREAL, "SourceArt", "Character", "MetaHuman", "SKM_FF_Sash.fbx")

ARM_WORDS = ("hand", "lowerarm", "upperarm", "index", "middle", "ring", "pinky", "thumb", "clavicle", "spine_03",
             "spine_04", "spine_05", "neck", "head")
THICKNESS = 0.0035                     # m, cloth shell
CLEARANCE = 0.012                      # m, minimum gap cloth -> skin after smoothing
WAIST_ABOVE_PELVIS = 0.075             # m, waistband height above the pelvis joint
CUFF_ABOVE_ANKLE = 0.035               # m, cuff edge above the ankle joint


def load():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=SRC)
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    body = next(o for o in bpy.data.objects if o.type == "MESH")
    # the importer hangs everything under a 0.01 empty (cm file); bake it so the scene works in metres
    for o in (arm, body):
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mw
    for o in [o for o in bpy.data.objects if o.type == "EMPTY"]:
        bpy.data.objects.remove(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in (arm, body):
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    body.parent = arm
    body.matrix_parent_inverse.identity()
    arm.name = "root"
    return arm, body


def bone_z(arm, name):
    return arm.data.bones[name].head_local.z


def region_mask(body, waist_z, cuff_z):
    """Vertices the pants cover: between the cuffs and the waistband, not driven by arms / upper torso."""
    names = {g.index: g.name for g in body.vertex_groups}
    upper = np.zeros(len(body.data.vertices))
    for v in body.data.vertices:
        upper[v.index] = sum(g.weight for g in v.groups if any(w in names.get(g.group, "") for w in ARM_WORDS))
    z = np.array([v.co.z for v in body.data.vertices])
    return (z >= cuff_z) & (z <= waist_z) & (upper < 0.05)


def islands(bm):
    seen, out = set(), []
    for v in bm.verts:
        if v.index in seen:
            continue
        comp, stack = [], [v]
        seen.add(v.index)
        while stack:
            a = stack.pop()
            comp.append(a.index)
            for e in a.link_edges:
                b = e.other_vert(a)
                if b.index not in seen:
                    seen.add(b.index)
                    stack.append(b)
        out.append(comp)
    return out


def adjacency(bm):
    return [[e.other_vert(v).index for e in v.link_edges] for v in bm.verts]


def smooth_field(f, adj, iters, pin=None):
    f = f.copy()
    for _ in range(iters):
        g = f.copy()
        for i, nb in enumerate(adj):
            if nb and (pin is None or not pin[i]):
                g[i] = 0.5 * f[i] + 0.5 * sum(f[j] for j in nb) / len(nb)
        f = g
    return f


def profile(z, waist_z, hip_z, knee_z, cuff_z):
    """Outward offset (m) of the cloth from the skin by height: snug waist, roomy seat / thighs, widest at the shin,
    gathered at the cuff."""
    pts = [(cuff_z, 0.010), (cuff_z + 0.045, 0.038), (knee_z - 0.12, 0.055), (knee_z, 0.045),
           (hip_z - 0.05, 0.034), (hip_z + 0.04, 0.022), (waist_z - 0.035, 0.012), (waist_z, 0.008)]
    if z <= pts[0][0]:
        return pts[0][1]
    for (z0, d0), (z1, d1) in zip(pts, pts[1:]):
        if z <= z1:
            u = (z - z0) / max(z1 - z0, 1e-6)
            u = u * u * (3 - 2 * u)
            return d0 + (d1 - d0) * u
    return pts[-1][1]


def noise3(x, y, z):
    return (math.sin(x * 37.1 + y * 11.7) + math.sin(y * 29.3 - z * 13.1) + math.sin(z * 23.7 + x * 17.9)) / 3.0


def build_pants(arm, body):
    pelvis_z = bone_z(arm, "pelvis")
    knee_z = 0.5 * (bone_z(arm, "calf_l") + bone_z(arm, "calf_r"))
    ankle_z = 0.5 * (bone_z(arm, "foot_l") + bone_z(arm, "foot_r"))
    waist_z = pelvis_z + WAIST_ABOVE_PELVIS
    cuff_z = ankle_z + CUFF_ABOVE_ANKLE
    hip_z = pelvis_z - 0.06
    mask = region_mask(body, waist_z, cuff_z)

    pants = body.copy()
    pants.data = body.data.copy()
    pants.name = "SKM_FF_Pants"
    bpy.context.collection.objects.link(pants)
    for m in list(pants.modifiers):
        if m.type != "ARMATURE":
            pants.modifiers.remove(m)
    bm = bmesh.new()
    bm.from_mesh(pants.data)
    bm.verts.ensure_lookup_table()
    drop = [f for f in bm.faces if not all(mask[v.index] for v in f.verts)]
    bmesh.ops.delete(bm, geom=drop, context="FACES_ONLY")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    # the sample body is built from separate islands (duplicate vertices along their seams): weld them, or every seam
    # opens into a gap once the surface is pushed out; then drop crumbs that are not part of the pants surface
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0002)
    bm.verts.ensure_lookup_table()
    for isl in islands(bm):
        if len(isl) < 150:
            bmesh.ops.delete(bm, geom=[bm.verts[i] for i in isl], context="VERTS")
    bm.verts.ensure_lookup_table()
    bm.normal_update()

    n = len(bm.verts)
    co = np.array([v.co[:] for v in bm.verts])
    adj = adjacency(bm)
    # smoothed normals: no spikes from creases / folds of the skin mesh
    nrm = np.array([v.normal[:] for v in bm.verts])
    for _ in range(4):
        nrm = np.array([nrm[i] + sum(nrm[j] for j in nb) for i, nb in enumerate(adj)])
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    off = np.array([profile(c[2], waist_z, hip_z, knee_z, cuff_z) for c in co])
    # inner legs: less room where the two legs face each other (no cloth through cloth at the crotch / knees)
    inner = np.array([max(0.0, -np.sign(c[0]) * nn[0]) if abs(c[0]) < 0.14 else 0.0 for c, nn in zip(co, nrm)])
    off *= 1.0 - 0.6 * np.clip(inner, 0.0, 1.0)
    off = smooth_field(off, adj, 12)
    # folds: horizontal ripples where the wide shin bunches above the cuff and behind the knee
    fold = np.zeros(n)
    for i, c in enumerate(co):
        shin = max(0.0, 1.0 - abs(c[2] - (cuff_z + 0.10)) / 0.12)
        knee_back = max(0.0, 1.0 - abs(c[2] - knee_z) / 0.07) * max(0.0, nrm[i][1])   # +Y = behind (character faces -Y)
        seat = max(0.0, 1.0 - abs(c[2] - (hip_z + 0.02)) / 0.08) * 0.5
        amp = 0.006 * shin + 0.004 * knee_back + 0.002 * seat
        fold[i] = amp * (0.6 * math.sin(c[2] * 2 * math.pi / 0.042 + 2.5 * noise3(*c)) + 0.4 * noise3(c[0] * 3, c[1] * 3, c[2] * 3))
    fold = smooth_field(fold, adj, 2)
    pos = co + nrm * (off + fold)[:, None]
    # cloth does not follow anatomy: a heavily smoothed, roomy seat / crotch (traditional low-crotch training pants), the
    # rest smoothed lightly (no knee-cap or calf bumps); Taubin steps keep the volume
    crotch = np.array([max(0.0, 1.0 - abs(c[0]) / 0.12) * max(0.0, 1.0 - abs(c[2] - (pelvis_z - 0.08)) / 0.16) for c in co])
    edge0 = np.array([v.is_boundary for v in bm.verts])
    for it in range(6):                                    # global Taubin: knee-cap / calf bumps out, volume kept
        lam = 0.5 if it % 2 == 0 else -0.53
        avg = np.array([pos[nb].mean(axis=0) if nb else pos[i] for i, nb in enumerate(adj)])
        step = (avg - pos) * lam
        step[edge0] = 0.0
        pos = pos + step
    cw = np.clip(crotch * 1.6, 0.0, 1.0) * 0.6
    for it in range(40):                                   # crotch / seat: plain Laplacian, fabric bridges the gap
        avg = np.array([pos[nb].mean(axis=0) if nb else pos[i] for i, nb in enumerate(adj)])
        step = (avg - pos) * cw[:, None]
        step[edge0] = 0.0
        pos = pos + step
    # the crotch hangs a little lower and looser
    pos[:, 2] -= 0.025 * crotch
    # clearance: smoothing must never pull cloth into the body (push out along the skin normal to >= CLEARANCE)
    bb = bmesh.new()
    bb.from_mesh(body.data)
    bvh = BVHTree.FromBMesh(bb)
    pushed = 0
    for _ in range(3):
        for i in range(n):
            loc, nn, _fi, _d = bvh.find_nearest(Vector(pos[i]), 0.3)
            if loc is None:
                continue
            d = (Vector(pos[i]) - loc).dot(nn)
            if d < CLEARANCE:
                pos[i] = np.array(loc + nn * CLEARANCE)
                pushed += 1
    bb.free()
    print(f"[FFOUTFIT] clearance: {pushed} pushes")
    for i, v in enumerate(bm.verts):
        v.co = Vector(pos[i])
    # keep the cuff and waist edges round (the profile already gathers them)
    edge = np.array([v.is_boundary for v in bm.verts])
    co2 = np.array([v.co[:] for v in bm.verts])
    for _ in range(3):
        nxt = co2.copy()
        for i, nb in enumerate(adj):
            if edge[i]:
                ring = [j for j in nb if edge[j]]
                if len(ring) == 2:
                    nxt[i] = 0.5 * co2[i] + 0.25 * (co2[ring[0]] + co2[ring[1]])
        co2 = nxt
    for i, v in enumerate(bm.verts):
        v.co = Vector(co2[i])
    bm.to_mesh(pants.data)
    bm.free()
    pants.data.update()

    # thickness (closed waist and cuff rims), smooth shading, fabric UVs, one material slot
    sol = pants.modifiers.new("Shell", "SOLIDIFY")
    sol.thickness = THICKNESS
    sol.offset = -1.0
    sol.use_rim = True
    sol.use_even_offset = True
    bpy.context.view_layer.objects.active = pants
    bpy.ops.object.select_all(action="DESELECT")
    pants.select_set(True)
    # move the solidify before the armature modifier, then apply it
    while pants.modifiers.find("Shell") > 0:
        bpy.ops.object.modifier_move_up(modifier="Shell")
    bpy.ops.object.modifier_apply(modifier="Shell")
    for p in pants.data.polygons:
        p.use_smooth = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.01)
    bpy.ops.object.mode_set(mode="OBJECT")
    # UVs in metres (1 UV unit = 1 m of cloth): the fabric material tiles by its scanned size
    me = pants.data
    uv = me.uv_layers.active.data
    a3 = auv = 0.0
    for p in me.polygons:
        a3 += p.area
        pts = [uv[li].uv for li in p.loop_indices]
        auv += 0.5 * abs(sum(pts[k].x * pts[k - 1].y - pts[k - 1].x * pts[k].y for k in range(len(pts))))
    k = math.sqrt(a3 / max(auv, 1e-9))
    for d in uv:
        d.uv = d.uv * k
    pants.data.materials.clear()
    mat = bpy.data.materials.get("MI_FF_Pants") or bpy.data.materials.new("MI_FF_Pants")
    pants.data.materials.append(mat)
    # drop empty vertex groups (fewer influences to import)
    used = {pants.vertex_groups[g.group].name for v in pants.data.vertices for g in v.groups if g.weight > 1e-4}
    for name in [g.name for g in pants.vertex_groups if g.name not in used]:   # by name: removing re-indexes the rest
        pants.vertex_groups.remove(pants.vertex_groups[name])
    print(f"[FFOUTFIT] pants: {len(pants.data.vertices)} verts, {len(pants.data.polygons)} faces, waist {waist_z:.3f} m, "
          f"cuff {cuff_z:.3f} m, knee {knee_z:.3f} m, {len(pants.vertex_groups)} groups")
    return pants


def _weights_from(src, bvh_src, pt):
    """Skin weights at a point: the nearest source face's vertices, weighted by distance."""
    loc, _n, fi, _d = bvh_src.find_nearest(Vector(pt), 0.5)
    poly = src.data.polygons[fi]
    acc = {}
    ws = []
    for vi in poly.vertices:
        d = (src.data.vertices[vi].co - loc).length
        ws.append((vi, 1.0 / max(d, 1e-4)))
    tot = sum(w for _vi, w in ws)
    for vi, w in ws:
        for g in src.data.vertices[vi].groups:
            name = src.vertex_groups[g.group].name
            acc[name] = acc.get(name, 0.0) + g.weight * w / tot
    s = sum(acc.values()) or 1.0
    return {k: v / s for k, v in acc.items() if v / s > 1e-3}


def build_sash(arm, pants):
    """A wrapped waist sash (role colour) with a knot and two hanging tails at the left hip, sitting on the pants."""
    pelvis = arm.data.bones["pelvis"].head_local
    waist_z = pelvis.z + WAIST_ABOVE_PELVIS
    zs = (waist_z - 0.055, waist_z - 0.02, waist_z + 0.012)       # band rows (bottom, middle, top)
    n_ang = 64
    cx, cy = pelvis.x, pelvis.y

    # radius of the pants around the body axis per angle, from their upper part (robust where the waist edge dips)
    nb = 72
    rad = np.zeros(nb)
    for v in pants.data.vertices:
        if waist_z - 0.07 <= v.co.z <= waist_z + 0.01:
            dx, dy = v.co.x - cx, v.co.y - cy
            a = math.atan2(dx, -dy) % (2 * math.pi)
            k = int(a / (2 * math.pi) * nb) % nb
            rad[k] = max(rad[k], math.hypot(dx, dy))
    for k in range(nb):                                            # fill empty bins from their neighbours
        if rad[k] <= 0.0:
            rad[k] = max(rad[(k - 1) % nb], rad[(k + 1) % nb], 0.15)
    for _ in range(3):
        rad = (np.roll(rad, 1) + rad * 2 + np.roll(rad, -1)) / 4.0

    def surf(ang, z, extra):
        """Point on the waist at angle / height, `extra` outside the pants (ang 0 = front; the character faces -Y)."""
        a = ang % (2 * math.pi)
        f = a / (2 * math.pi) * nb
        k = int(f) % nb
        r = rad[k] + (rad[(k + 1) % nb] - rad[k]) * (f - int(f))
        return Vector((cx + math.sin(a) * (r + extra), cy - math.cos(a) * (r + extra), z))

    verts, faces = [], []
    rows = []
    for zi, z in enumerate(zs):
        bulge = 0.010 if zi == 1 else 0.007                        # wrapped cloth is fuller in the middle
        rows.append([len(verts) + k for k in range(n_ang)])
        for k in range(n_ang):
            verts.append(surf(2 * math.pi * k / n_ang, z, bulge))
    for r0, r1 in zip(rows, rows[1:]):
        for k in range(n_ang):
            faces.append((r0[k], r0[(k + 1) % n_ang], r1[(k + 1) % n_ang], r1[k]))
    # knot + tails at the left hip front (angle ~ +40 deg toward the character's left = +X)
    knot_ang = math.radians(38.0)
    kc = surf(knot_ang, zs[1], 0.022)
    ring = []
    for k in range(10):
        a = 2 * math.pi * k / 10
        t = Vector((math.cos(knot_ang), math.sin(knot_ang), 0.0))
        ring.append(len(verts))
        verts.append(kc + t * (0.025 * math.cos(a)) + Vector((0, 0, 0.022 * math.sin(a))))
    c0 = len(verts)
    verts.append(kc + Vector((math.sin(knot_ang), -math.cos(knot_ang), 0.0)) * 0.012)
    for k in range(10):
        faces.append((ring[k], ring[(k + 1) % 10], c0))
    for side, (dang, length) in enumerate(((math.radians(-7.0), 0.30), (math.radians(9.0), 0.24))):
        prev = None
        segs = 8
        for j in range(segs + 1):
            u = j / segs
            z = zs[0] + 0.01 - u * length
            ang = knot_ang + dang + 0.05 * math.sin(u * 3.0 + side)
            w = 0.045 + 0.012 * u                                   # tails flare a little
            c = surf(ang, z, 0.020 + 0.012 * u)
            t = Vector((math.cos(ang), math.sin(ang), 0.0))
            a, b = len(verts), len(verts) + 1
            verts.append(c - t * (w * 0.5))
            verts.append(c + t * (w * 0.5))
            if prev is not None:
                faces.append((prev[0], prev[1], b, a))
            prev = (a, b)
    me = bpy.data.meshes.new("SKM_FF_Sash")
    me.from_pydata([v[:] for v in verts], [], faces)
    me.update()
    sash = bpy.data.objects.new("SKM_FF_Sash", me)
    bpy.context.collection.objects.link(sash)
    sash.parent = arm
    mod = sash.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    sol = sash.modifiers.new("Shell", "SOLIDIFY")
    sol.thickness = 0.003
    sol.offset = 1.0
    sol.use_rim = True
    bpy.context.view_layer.objects.active = sash
    bpy.ops.object.select_all(action="DESELECT")
    sash.select_set(True)
    while sash.modifiers.find("Shell") > 0:
        bpy.ops.object.modifier_move_up(modifier="Shell")
    bpy.ops.object.modifier_apply(modifier="Shell")
    # skin weights from the pants under each vertex
    bvh_p = BVHTree.FromObject(pants, bpy.context.evaluated_depsgraph_get(), deform=False)
    for v in sash.data.vertices:
        for name, w in _weights_from(pants, bvh_p, v.co).items():
            g = sash.vertex_groups.get(name) or sash.vertex_groups.new(name=name)
            g.add([v.index], w, "REPLACE")
    for p in sash.data.polygons:
        p.use_smooth = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.01)
    bpy.ops.object.mode_set(mode="OBJECT")
    mat = bpy.data.materials.get("MI_FF_Sash") or bpy.data.materials.new("MI_FF_Sash")
    sash.data.materials.append(mat)
    print(f"[FFOUTFIT] sash: {len(sash.data.vertices)} verts, {len(sash.vertex_groups)} groups")
    return sash


def preview(dirpath, objs):
    """Workbench renders: front, side, back."""
    os.makedirs(dirpath, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x, sc.render.resolution_y = 700, 1000
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "OBJECT"
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 1.25
    for o, col in objs:
        o.color = col
    for name, loc, rot in (("front", (0, -4, 0.62), (math.pi / 2, 0, 0)), ("side", (4, 0, 0.62), (math.pi / 2, 0, math.pi / 2)),
                           ("back", (0, 4, 0.62), (math.pi / 2, 0, math.pi))):
        cam.location = loc
        cam.rotation_euler = rot
        sc.render.filepath = os.path.join(dirpath, f"pants_{name}.png")
        bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    arm, body = load()
    pants = build_pants(arm, body)
    sash = build_sash(arm, pants)
    FX.export_skeletal_mesh_fbx(OUT, arm, [pants])
    FX.export_skeletal_mesh_fbx(OUT_SASH, arm, [sash])
    print(f"[FFOUTFIT] wrote {OUT} and {OUT_SASH}")
    if "--preview" in argv:
        preview(argv[argv.index("--preview") + 1], [(body, (0.55, 0.42, 0.33, 1.0)), (pants, (0.50, 0.47, 0.42, 1.0)),
                                                    (sash, (0.2, 0.35, 0.85, 1.0))])


main()
