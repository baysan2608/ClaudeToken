"""Pure-Python core of the L_Ruins bake (Unreal's Python 3.11 and plain python3 3.9; no numpy): sample grids, the floor rule
of ff::WorldDef (terrain, raised to surface - wade depth under the water layer), walkability (the no-trap check), interior
boxes of raised stone, site picking, and the writers of world.json + heights.f32 + water.f32 in the format
ff::WorldDef::Parse / LoadWaterLayer read (Source/FourfoldCore/Private/App/OpenWorld.cpp). Used by bake_ruins.py; tests:
test_ruins_tools.py."""
import array
import json
import math
import os
import sys
from collections import deque

NAN = float("nan")


def wet(v):
    """True for a water sample; NaN marks a dry one (NaN is the only value not equal to itself)."""
    return v == v


class Grid:
    def __init__(self, nx, nz, x0, z0, cell):
        self.nx, self.nz = int(nx), int(nz)
        self.x0, self.z0, self.cell = float(x0), float(z0), float(cell)

    @classmethod
    def from_spec(cls, g):
        return cls(g["nx"], g["nz"], g["x0"], g["z0"], g["cell"])

    def idx(self, i, k):
        return k * self.nx + i

    def pos(self, i, k):
        return self.x0 + i * self.cell, self.z0 + k * self.cell

    def nearest(self, x, z):
        i = min(self.nx - 1, max(0, int(round((x - self.x0) / self.cell))))
        k = min(self.nz - 1, max(0, int(round((z - self.z0) / self.cell))))
        return i, k

    def inside(self, x, z, margin):
        return (self.x0 + margin < x < self.x0 + (self.nx - 1) * self.cell - margin and
                self.z0 + margin < z < self.z0 + (self.nz - 1) * self.cell - margin)

    def neighbours(self, i, k):
        for di, dk in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            a, b = i + di, k + dk
            if 0 <= a < self.nx and 0 <= b < self.nz:
                yield a, b


def _under(h, w):
    return wet(w) and h < w


def sample_floors(heights, water, wade):
    """Standing floor per sample: the terrain, raised to surface - wade where the sample is under water."""
    return [max(h, w - wade) if _under(h, w) else h for h, w in zip(heights, water)]


def _corners(grid, x, z):
    fx = min(max((x - grid.x0) / grid.cell, 0.0), grid.nx - 1.0)
    fz = min(max((z - grid.z0) / grid.cell, 0.0), grid.nz - 1.0)
    i, k = min(int(fx), grid.nx - 2), min(int(fz), grid.nz - 2)
    tx, tz = fx - i, fz - k
    b = grid.idx(i, k)
    return ((b, (1 - tx) * (1 - tz)), (b + 1, tx * (1 - tz)), (b + grid.nx, (1 - tx) * tz), (b + grid.nx + 1, tx * tz))


def height_at(heights, grid, x, z):
    return sum(heights[j] * wt for j, wt in _corners(grid, x, z))


def water_at(water, grid, x, z):
    """WorldDef::WaterSurface: bilinear over the wet corners (renormalised); NaN when none."""
    s = ws = 0.0
    for j, wt in _corners(grid, x, z):
        if wet(water[j]):
            s += water[j] * wt
            ws += wt
    return s / ws if ws >= 1e-9 else NAN


def floor_at(heights, water, grid, wade, x, z):
    """WorldDef::FloorAt with a water layer."""
    h = height_at(heights, grid, x, z)
    w = water_at(water, grid, x, z)
    if not wet(w) or h >= w:
        return h
    return max(h, w - wade)


def slope(fl, grid, i, k):
    """Steepest rise / run to a 4-neighbour sample."""
    a = fl[grid.idx(i, k)]
    return max(abs(fl[grid.idx(ni, nk)] - a) for ni, nk in grid.neighbours(i, k)) / grid.cell


def _step_ok(fl, a, b, cell, max_slope):
    rise = fl[b] - fl[a]
    return rise <= 0.05 or rise <= cell * max_slope           # ArenaMap::limit_terrain's rule at sample spacing


def reach(fl, grid, start, margin, max_slope=1.0, reverse=False, blocked=None):
    """bytearray: 1 for samples a fighter can walk to from `start` (reverse: samples that can walk to `start`), inside the
    edge band only; `blocked` (blocked_samples) marks samples a wall closes."""
    seen = bytearray(grid.nx * grid.nz)
    seen[grid.idx(*start)] = 1
    q = deque([start])
    while q:
        i, k = q.popleft()
        a = grid.idx(i, k)
        for ni, nk in grid.neighbours(i, k):
            b = grid.idx(ni, nk)
            if seen[b] or (blocked is not None and blocked[b]) or not grid.inside(*grid.pos(ni, nk), margin):
                continue
            ok = _step_ok(fl, b, a, grid.cell, max_slope) if reverse else _step_ok(fl, a, b, grid.cell, max_slope)
            if ok:
                seen[b] = 1
                q.append((ni, nk))
    return seen


def traps(fl, grid, start, margin, max_slope=1.0, blocked=None):
    """Samples reachable from `start` that cannot walk back to it (pits, gorges), row-major order."""
    fwd = reach(fl, grid, start, margin, max_slope, blocked=blocked)
    back = reach(fl, grid, start, margin, max_slope, reverse=True, blocked=blocked)
    return [j for j in range(len(fwd)) if fwd[j] and not back[j]]


RIM_WALL_HEIGHT = 5.0   # m above the rim: a standing leap (~2.2 m) plus a cloud step (~1.8 m) does not clear it
STEP_HEIGHT = 0.4       # Sim::STEP_HEIGHT: a solid whose top is at most this far above the floor is stepped onto
ACTOR_HEIGHT = 1.8


def rim_walls(fl, grid, start, margin, max_slope=1.0, height=RIM_WALL_HEIGHT):
    """Invisible walls on the trap samples next to walkable ground (the river gorge's cliffs, sheer pits): a fighter can no
    longer step or fall into ground they could not climb back out of. One box per such sample (the cell around it), from
    1 m below its floor to `height` above its highest walkable neighbour; row-major order, names rim_<n>."""
    fwd = reach(fl, grid, start, margin, max_slope)
    back = reach(fl, grid, start, margin, max_slope, reverse=True)
    out = []
    for k in range(grid.nz):
        for i in range(grid.nx):
            j = grid.idx(i, k)
            if not fwd[j] or back[j]:
                continue
            rims = [fl[grid.idx(a, b)] for a, b in grid.neighbours(i, k) if fwd[grid.idx(a, b)] and back[grid.idx(a, b)]]
            if rims:
                out.append(_box(grid, i, i, k, k, max(rims) + height, fl[j] - 1.0, f"rim_{len(out)}", 0.0))
    return out


def blocked_samples(solids, fl, grid, step=STEP_HEIGHT, body=ACTOR_HEIGHT):
    """bytearray: 1 for samples whose standing space a box fills (its top above floor + step, its bottom below the head).
    Boxes sunk below the floor (the pyramid's interior boxes) block nothing."""
    out = bytearray(grid.nx * grid.nz)
    for b in solids:
        (x0, y0, z0), (x1, y1, z1) = b["min"], b["max"]
        i0 = max(0, int(math.floor((x0 - grid.x0) / grid.cell)))
        i1 = min(grid.nx - 1, int(math.ceil((x1 - grid.x0) / grid.cell)))
        k0 = max(0, int(math.floor((z0 - grid.z0) / grid.cell)))
        k1 = min(grid.nz - 1, int(math.ceil((z1 - grid.z0) / grid.cell)))
        for k in range(k0, k1 + 1):
            for i in range(i0, i1 + 1):
                x, z = grid.pos(i, k)
                j = grid.idx(i, k)
                if x0 < x < x1 and z0 < z < z1 and y1 > fl[j] + step and y0 < fl[j] + body:
                    out[j] = 1
    return out


def _box(grid, i0, i1, k0, k1, top, bottom, name, inset):
    half = grid.cell * 0.5 - min(inset, 0.4 * grid.cell)       # a fine grid keeps a footprint: never a zero-width box
    xa, za = grid.pos(i0, k0)
    xb, zb = grid.pos(i1, k1)
    return {"min": [round(xa - half, 3), round(bottom, 3), round(za - half, 3)],
            "max": [round(xb + half, 3), round(top, 3), round(zb + half, 3)],
            "kind": "wall", "surface": "stone", "name": name}


def _stone_boxes_at(h_all, h_ter, grid, mask, min_rise, sink, quant, inset, foot, min_height):
    """One merge pass at a fixed quantum -> (boxes, dropped columns). A column whose quantised top would sit less than
    min_height above its own box floor (bare terrain - foot) is dropped: a box that thin or inverted stops nothing."""
    tops, dropped = {}, 0
    for k in range(grid.nz):
        for i in range(grid.nx):
            j = grid.idx(i, k)
            if mask[j] and h_all[j] - h_ter[j] >= min_rise:
                top = math.floor((h_all[j] - sink) / quant) * quant
                if top < h_ter[j] - foot + min_height:
                    dropped += 1
                else:
                    tops[(i, k)] = top
    runs = {}
    for k in range(grid.nz):
        row, i = [], 0
        while i < grid.nx:
            t = tops.get((i, k))
            if t is None:
                i += 1
                continue
            i1 = i
            while tops.get((i1 + 1, k)) == t:
                i1 += 1
            row.append((i, i1, t))
            i = i1 + 1
        runs[k] = row
    boxes, open_runs = [], {}
    for k in range(grid.nz + 1):
        cur = set(runs.get(k, []))
        for key in sorted(open_runs):
            if key not in cur:
                i0, i1, t = key
                k0 = open_runs.pop(key)
                bottom = min(h_ter[grid.idx(i, kk)] for kk in range(k0, k) for i in range(i0, i1 + 1)) - foot
                boxes.append(_box(grid, i0, i1, k0, k - 1, t, bottom, f"stone_{len(boxes)}", inset))
        for key in runs.get(k, []):
            open_runs.setdefault(key, k)
    return boxes, dropped


def _largest(boxes, count):
    """The `count` boxes with the biggest footprint (ties: earlier first), in their original order, renumbered."""
    def area(n):
        return (boxes[n]["max"][0] - boxes[n]["min"][0]) * (boxes[n]["max"][2] - boxes[n]["min"][2])

    keep_ids = sorted(sorted(range(len(boxes)), key=lambda n: (-area(n), n))[:count])
    out = []
    for n in keep_ids:
        out.append(dict(boxes[n], name=f"stone_{len(out)}"))
    return out


def stone_boxes(h_all, h_ter, grid, mask, min_rise=1.0, sink=0.3, quant=0.5, inset=0.5, max_boxes=400, foot=1.0,
                min_height=0.25, max_quant=8.0):
    """Interior boxes of raised stone: samples in `mask` standing min_rise above the bare terrain become columns whose top
    sits at least `sink` below the walkable surface (quantised down to `quant`) and whose floor is `foot` below the bare
    terrain, merged into rectangles (equal runs of a row, then equal runs of consecutive rows). They stop shots and line of
    sight inside the stone without changing footing: ArenaMap::ground_height and push_out ignore box tops below the feet +
    step. Never more than max_boxes, and every box is at least min_height tall: while over the cap the quantum doubles
    (up to max_quant) to merge uneven tops, but stops as soon as a coarser quantum would drop columns (their quantised top
    falls below their own floor); then, or at max_quant, the max_boxes largest footprints are kept."""
    boxes, dropped = _stone_boxes_at(h_all, h_ter, grid, mask, min_rise, sink, quant, inset, foot, min_height)
    while len(boxes) > max_boxes and quant * 2.0 <= max_quant:
        coarser, lost = _stone_boxes_at(h_all, h_ter, grid, mask, min_rise, sink, quant * 2.0, inset, foot, min_height)
        if lost > dropped:
            break
        quant, boxes, dropped = quant * 2.0, coarser, lost
    return boxes if len(boxes) <= max_boxes else _largest(boxes, max_boxes)


def trunk_boxes(props, heights, water, grid, wade, margin, height=6.0):
    """A box around every big trunk (props with trunk_r >= 0.25 inside the edge band, not in water)."""
    out = []
    for p in props:
        r = p.get("trunk_r", 0.0)
        x, _, z = p["pos"]
        if r < 0.25 or not grid.inside(x, z, margin) or wet(water_at(water, grid, x, z)):
            continue
        r = min(0.9, max(0.3, r))
        f = floor_at(heights, water, grid, wade, x, z)
        out.append({"min": [round(x - r, 3), round(f - 0.5, 3), round(z - r, 3)],
                    "max": [round(x + r, 3), round(f + height, 3), round(z + r, 3)],
                    "kind": "pillar", "surface": "stone", "name": f"trunk_{len(out)}"})
    return out


def pick_flat(fl, grid, ok, x, z, radius, max_slope=0.25):
    """Flattest allowed sample within radius of (x, z); ties: nearer, then lower index. (i, k) or None."""
    best = None
    for k in range(grid.nz):
        for i in range(grid.nx):
            j = grid.idx(i, k)
            if not ok[j]:
                continue
            px, pz = grid.pos(i, k)
            d = math.hypot(px - x, pz - z)
            if d > radius:
                continue
            s = slope(fl, grid, i, k)
            if s > max_slope:
                continue
            key = (round(s, 4), round(d, 4), j)
            if best is None or key < best[0]:
                best = (key, i, k)
    return None if best is None else (best[1], best[2])


def pick_high(fl, grid, ok, x, z, radius, max_slope=0.25):
    """Highest allowed flat sample within radius of (x, z); ties: nearer, then lower index. (i, k) or None."""
    best = None
    for k in range(grid.nz):
        for i in range(grid.nx):
            j = grid.idx(i, k)
            px, pz = grid.pos(i, k)
            d = math.hypot(px - x, pz - z)
            if not ok[j] or d > radius or slope(fl, grid, i, k) > max_slope:
                continue
            key = (-round(fl[j], 4), round(d, 4), j)
            if best is None or key < best[0]:
                best = (key, i, k)
    return None if best is None else (best[1], best[2])


def pick_bank(fl, water, heights, grid, ok, min_d=3.0, max_d=10.0, max_slope=0.35):
    """Allowed dry sample min_d..max_d (grid steps x cell) from the water, flat enough, nearest the wet samples' centroid."""
    n = grid.nx * grid.nz
    is_w = [_under(heights[j], water[j]) for j in range(n)]
    wet_js = [j for j in range(n) if is_w[j]]
    if not wet_js:
        return None
    cx = sum(grid.pos(j % grid.nx, j // grid.nx)[0] for j in wet_js) / len(wet_js)
    cz = sum(grid.pos(j % grid.nx, j // grid.nx)[1] for j in wet_js) / len(wet_js)
    dist = [-1] * n
    q = deque()
    for j in wet_js:
        dist[j] = 0
        q.append(j)
    while q:
        j = q.popleft()
        for ni, nk in grid.neighbours(j % grid.nx, j // grid.nx):
            b = grid.idx(ni, nk)
            if dist[b] < 0:
                dist[b] = dist[j] + 1
                q.append(b)
    best = None
    for j in range(n):
        if not ok[j] or is_w[j] or not (min_d <= dist[j] * grid.cell <= max_d):
            continue
        i, k = j % grid.nx, j // grid.nx
        if slope(fl, grid, i, k) > max_slope:
            continue
        x, z = grid.pos(i, k)
        key = (round(math.hypot(x - cx, z - cz), 4), j)
        if best is None or key < best[0]:
            best = (key, i, k)
    return None if best is None else (best[1], best[2])


def make_site(sid, name, region, element, pos, facing, preset, aggro):
    return {"id": sid, "name": name, "region": region, "pos": [round(pos[0], 3), round(pos[1], 3), round(pos[2], 3)],
            "facing": round(facing, 5), "element": int(element), "sub": -1, "preset": preset, "aggro_radius": float(aggro)}


def make_shrine(sid, name, pos):
    return {"id": sid, "name": name, "pos": [round(pos[0], 3), round(pos[1], 3), round(pos[2], 3)]}


def world_dict(grid, spawn, facing, sites, shrines, solids, props, prop_assets, edge_margin, wade_depth, source):
    """world.json for ff::WorldDef::Parse: a static map (terrain.editable false) with a water layer and no global level."""
    return {
        "version": 1,
        "generator": "unreal/Tools/ruins/bake_ruins.py",
        "source": source,
        "terrain": {"nx": grid.nx, "nz": grid.nz, "x0": grid.x0, "z0": grid.z0, "cell": grid.cell, "heights": "heights.f32",
                    "edge_margin": edge_margin, "editable": False},
        "water": {"wade_depth": wade_depth, "layer": "water.f32"},
        "spawn": {"pos": [round(spawn[0], 3), round(spawn[1], 3), round(spawn[2], 3)], "facing": round(facing, 5)},
        "sites": sites,
        "shrines": shrines,
        "solids": solids,
        "metal": [],
        "props": props,
        "prop_assets": prop_assets,
    }


def pack_f32(values):
    a = array.array("f", values)
    if sys.byteorder != "little":
        a.byteswap()
    return a.tobytes()


def unpack_f32(blob):
    a = array.array("f")
    a.frombytes(blob)
    if sys.byteorder != "little":
        a.byteswap()
    return list(a)


def write_world(out_dir, world, heights, water):
    """Writes world.json (strict JSON: no NaN) + the two float32 grids it names; returns {name: path}."""
    os.makedirs(out_dir, exist_ok=True)
    paths = {}
    for name, values in ((world["terrain"]["heights"], heights), (world["water"]["layer"], water)):
        p = os.path.join(out_dir, name)
        with open(p, "wb") as f:
            f.write(pack_f32(values))
        paths[name] = p
    p = os.path.join(out_dir, "world.json")
    with open(p, "w") as f:
        json.dump(world, f, separators=(",", ":"), allow_nan=False)
    paths["world.json"] = p
    return paths


def validate(world, heights, water, grid):
    """Errors that make the map unplayable (empty = fine): grid sizes, NaN heights, no water, the spawn or a site outside the
    edge band / in water / not reachable both ways from the spawn, water you cannot walk out of, too many traps."""
    errs = []
    n = grid.nx * grid.nz
    if len(heights) != n or len(water) != n:
        return [f"grid size mismatch: {len(heights)} heights, {len(water)} water samples, want {n}"]
    if not all(wet(h) for h in heights):
        errs.append("NaN in heights")
    if not any(_under(h, w) for h, w in zip(heights, water)):
        errs.append("no water samples")
    margin = world["terrain"]["edge_margin"]
    fl = sample_floors(heights, water, world["water"]["wade_depth"])
    sx, _, sz = world["spawn"]["pos"]
    if not grid.inside(sx, sz, margin):
        return errs + [f"spawn ({sx:.1f}, {sz:.1f}) outside the edge band"]
    start = grid.nearest(sx, sz)
    blocked = blocked_samples([b for b in world["solids"] if b["kind"] == "wall"], fl, grid)
    fwd = reach(fl, grid, start, margin, blocked=blocked)
    back = reach(fl, grid, start, margin, reverse=True, blocked=blocked)
    trapped = [j for j in range(n) if fwd[j] and not back[j]]
    wet_traps = [j for j in trapped if _under(heights[j], water[j])]
    if wet_traps:
        errs.append(f"{len(wet_traps)} water samples you cannot walk out of")
    reachable = sum(fwd)
    if len(trapped) > 0.01 * reachable:
        errs.append(f"{len(trapped)} trap samples (more than 1 % of {reachable} reachable)")
    points = [("site " + s["id"], s["pos"]) for s in world["sites"]] + [("shrine " + s["id"], s["pos"]) for s in world["shrines"]]
    for what, p in points:
        if not grid.inside(p[0], p[2], margin):
            errs.append(f"{what} outside the edge band")
            continue
        j = grid.idx(*grid.nearest(p[0], p[2]))
        if _under(heights[j], water[j]):
            errs.append(f"{what} is in the water")
        elif not (fwd[j] and back[j]):
            errs.append(f"{what} is not reachable both ways from the spawn")
    if len(world["sites"]) < 2:
        errs.append("fewer than 2 encounter sites")
    for b in world["solids"]:
        if not all(b["min"][a] < b["max"][a] for a in range(3)):
            errs.append(f"degenerate solid {b['name']}")
    return errs
