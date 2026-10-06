// FourfoldFX logic island - shared unit meshes, built once and cached (sim space, unit sized; the draw item's
// transform places and scales them). Pooled Unreal components upload a shared mesh once and keep it.
//
// Mesh contracts (the HLSL in unreal/Shaders/FX reads them; UE local = (sim x, sim z, sim y) x 100):
//   GroundQuad   XZ square -1..1, normal +Y, uv0 0..1 (rings, decals, spin blur)
//   FaceQuad     XY square -1..1, normal +Z, uv0 0..1 (camera-facing rings: the item basis faces the camera)
//   Sphere(d)    unit UV sphere, uv0 = (around 0..1, up 0..1 bottom -> top), smooth normals
//   Flame        open unit cylinder along +Y (0..1), 14 sides x 14 rings, uv0 = (angle 0..1, y 0..1). The flame
//                material reshapes it in the vertex shader from the UVs alone (profile, lobes, sway, burst travel)
//   Cone         open cone, apex radius 0.06 at y = 0, radius 1 at y = 1, 20 x 10, uv0 = (angle, y)
//   Beam         flat strip along +Z (0..1), x = -1 / +1, 2 x 17 vertices, uv0 = (side 0/1, along 0..1)
//   Crescent     arc blade in XZ bulging to +Z (leading radius 1), uv0 = (along the arc, leading 0 .. trailing 1)
//   Sheet        vertical sheet X -1..1, Y 0..1, bowed toward +Z by 0.15, uv0 = (along, 1 - height)
//   Disc / Lance / Rod / Spike / Caltrop   metal pieces (lathes), uv0 = (angle, t)
//   Crystal      hexagonal crystals (shard / cluster / wall / ridge), flat shaded, uv0 = (across facet, along),
//                uv1 = (crystal random, crystal base height)
//   Wall         five chamfered stone blocks, x -1..1, y 0..~1, z -0.27..0.27; col.r = rise delay (0 centre .. 1
//                outer), col.g = block random
//   Rock(seed)   unit radius rock (AppendRock contract: uv1 / uv2 = blob offset + random)
//   Chip         20-triangle icosahedron (debris)
// Owner: stream `fx`.
#pragma once

#include "FxMesh.h"

#include <cstdint>

namespace ffx {

enum class CrystalMode : uint8_t { Shard, Cluster, Wall, Ridge };

namespace meshlib {

const MeshData& GroundQuad();
const MeshData& FaceQuad();
const MeshData& Sphere(int detail);   // 0: 8 x 12, 1: 12 x 18, 2: 16 x 24
const MeshData& Chip();
const MeshData& Flame();
const MeshData& Cone();
const MeshData& Beam();
const MeshData& Crescent();
const MeshData& Sheet();
const MeshData& Disc();
const MeshData& Lance();
const MeshData& Rod();
const MeshData& Spike();
const MeshData& Caltrop();
const MeshData& Crystal(uint32_t seed, CrystalMode mode);   // cached (16 seeds per mode)
const MeshData& Wall(uint32_t seed);                         // cached (16 seeds)
const MeshData& Rock(uint32_t seed);                         // cached (seed % 64)

// Builds every cached mesh once (rocks, walls, crystals, unit meshes) so no gameplay frame pays for it.
void Prewarm();

// Uncached builders (views that own their geometry).
void BuildCrystal(MeshData& m, uint32_t seed, CrystalMode mode);
void BuildWall(MeshData& m, uint32_t seed);
// Lathe around +Y from a (radius, y) profile, `sides` around; smooth or faceted normals. uv0 = (angle, t).
void BuildLathe(MeshData& m, const float* radius, const float* y, int n, int sides, bool smooth);

}  // namespace meshlib
}  // namespace ffx
