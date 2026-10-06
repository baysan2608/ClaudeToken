"""Texel maps: rasterise the final mesh into each material's UV square (position, normal, pattern coordinates,
part id, metres per texel), AO bake with Cycles, padding, height -> tangent-space normal (DirectX green), PNG IO."""
import os

import bpy
import numpy as np
from PIL import Image
from scipy import ndimage


class TexelMaps:
    pass


def mesh_tables(obj):
    """Loop-triangle tables of an evaluated (rest pose) mesh object."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    nt = len(me.loop_triangles)
    tri_loops = np.empty(nt * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("loops", tri_loops)
    tri_verts = np.empty(nt * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("vertices", tri_verts)
    tri_mat = np.empty(nt, dtype=np.int64)
    me.loop_triangles.foreach_get("material_index", tri_mat)
    tri_poly = np.empty(nt, dtype=np.int64)
    me.loop_triangles.foreach_get("polygon_index", tri_poly)
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    vn = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("normal", vn)
    vn = vn.reshape(-1, 3)
    uv = np.empty(len(me.loops) * 2)
    me.uv_layers["UVMap"].data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    puv = np.zeros_like(uv)
    if "PatternUV" in me.uv_layers:
        me.uv_layers["PatternUV"].data.foreach_get("uv", puv.ravel())
        puv = puv.reshape(-1, 2)
    part = np.zeros(len(me.polygons), dtype=np.int64)
    if "part_id" in me.attributes:
        me.attributes["part_id"].data.foreach_get("value", part)
    T = {
        "pos": co[tri_verts].reshape(nt, 3, 3),
        "nrm": vn[tri_verts].reshape(nt, 3, 3),
        "uv": uv[tri_loops].reshape(nt, 3, 2),
        "puv": puv[tri_loops].reshape(nt, 3, 2),
        "mat": tri_mat,
        "part": part[tri_poly],
    }
    ev.to_mesh_clear()
    return T


def rasterize(T, slot_index, S):
    """Barycentric rasterisation of every triangle of one material into S x S maps (row 0 = v 0)."""
    sel = np.nonzero(T["mat"] == slot_index)[0]
    m = TexelMaps()
    m.S = S
    m.mask = np.zeros((S, S), bool)
    m.pos = np.zeros((S, S, 3), np.float32)
    m.nrm = np.zeros((S, S, 3), np.float32)
    m.puv = np.zeros((S, S, 2), np.float32)
    m.part = -np.ones((S, S), np.int32)
    m.mpt = np.zeros((S, S), np.float32)
    for t in sel:
        uv = T["uv"][t] * S - 0.5
        lo = np.floor(uv.min(axis=0)).astype(int)
        hi = np.ceil(uv.max(axis=0)).astype(int) + 1
        lo = np.clip(lo, 0, S - 1)
        hi = np.clip(hi, 0, S)
        if hi[0] <= lo[0] or hi[1] <= lo[1]:
            continue
        xs, ys = np.meshgrid(np.arange(lo[0], hi[0]), np.arange(lo[1], hi[1]))
        a, b, c = uv
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            continue
        w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / den
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / den
        w2 = 1 - w0 - w1
        eps = -1e-4
        ins = (w0 >= eps) & (w1 >= eps) & (w2 >= eps)
        if not ins.any():
            continue
        X, Y = xs[ins], ys[ins]
        W = np.stack([w0[ins], w1[ins], w2[ins]], axis=1)
        m.mask[Y, X] = True
        m.pos[Y, X] = W @ T["pos"][t]
        n = W @ T["nrm"][t]
        m.nrm[Y, X] = n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)
        m.puv[Y, X] = W @ T["puv"][t]
        m.part[Y, X] = T["part"][t]
        p = T["pos"][t]
        a3 = 0.5 * np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
        a2 = 0.5 * abs(den)
        m.mpt[Y, X] = np.sqrt(a3 / max(a2, 1e-12))
    return m


def fill_index(mask):
    """For every texel the index (y, x) of the nearest masked texel (padding / dilation)."""
    _d, ind = ndimage.distance_transform_edt(~mask, return_indices=True)
    return ind


def pad(img, ind):
    return img[ind[0], ind[1]]


def height_to_normal(h, mpt, strength=1.0):
    """Height (metres) -> tangent-space normal, DirectX convention (green = -dh/dv flipped). Returns float rgb."""
    dhdx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dhdy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    s = strength / np.maximum(mpt, 1e-5)
    nx = -dhdx * s
    ny = -dhdy * s            # OpenGL (+V up) component
    nz = np.ones_like(h)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    n = np.stack([nx / l, -ny / l, nz / l], axis=-1)      # DirectX: flip green
    return n * 0.5 + 0.5


def blend_normals(n1, n2):
    """Whiteout blend of two tangent-space normals given as [-1,1] arrays."""
    x = n1[..., 0] + n2[..., 0]
    y = n1[..., 1] + n2[..., 1]
    z = n1[..., 2] * n2[..., 2]
    l = np.sqrt(x * x + y * y + z * z)
    return np.stack([x / l, y / l, z / l], axis=-1)


def to_srgb(lin):
    lin = np.clip(lin, 0, 1)
    return np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055)


def save_png(path, arr, mode):
    """arr in [0,1], row 0 = v 0 (flipped to image space)."""
    a = np.flipud(np.clip(arr, 0, 1))
    img = Image.fromarray((a * 255 + 0.5).astype(np.uint8), mode)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    return path


def bake_ao(obj, sizes, samples=24, distance=0.3, margin=8):
    """Cycles AO bake of every material slot of obj (rest pose) into float images; returns {slot: (S,S) array}."""
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = samples
    scn.render.bake.margin = margin
    scn.render.bake.use_clear = True
    if scn.world is None:
        scn.world = bpy.data.worlds.new("bake_world")
    scn.world.light_settings.distance = distance
    imgs = {}
    for slot in obj.material_slots:
        mat = slot.material
        S = sizes[mat.name]
        img = bpy.data.images.new(f"_ao_{mat.name}", S, S, alpha=False, float_buffer=True)
        img.generated_color = (1, 1, 1, 1)
        mat.use_nodes = True
        nt = mat.node_tree
        node = nt.nodes.new("ShaderNodeTexImage")
        node.name = "_ao_bake"
        node.image = img
        for n in nt.nodes:
            n.select = False
        node.select = True
        nt.nodes.active = node
        imgs[mat.name] = img
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="AO")
    out = {}
    for name, img in imgs.items():
        S = img.size[0]
        px = np.empty(S * S * 4, np.float32)
        img.pixels.foreach_get(px)
        out[name] = px.reshape(S, S, 4)[..., 0].copy()
        mat = bpy.data.materials[name]
        mat.node_tree.nodes.remove(mat.node_tree.nodes["_ao_bake"])
        bpy.data.images.remove(img)
    return out
