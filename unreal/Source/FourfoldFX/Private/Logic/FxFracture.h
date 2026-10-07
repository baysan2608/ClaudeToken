// FourfoldFX logic island - fracture pieces for the physics debris of broken stones and walls. Visual only: the sim
// decides when a body breaks (and spawns its own rubble bodies); the Unreal glue simulates these pieces as Chaos rigid
// bodies on a collision copy of the sim arena, lets them settle, then sinks them into the ground.
//
// Pieces are Voronoi cells of a closed mesh: each cell clips the mesh with the bisector planes towards the other sites
// and caps every cut with a flat face, so the pieces fit together exactly. Geometry stays in the source mesh's local
// frame (unit rock / unit wall): drawn with the intact body's transform they rebuild it, and the materials'
// local-space noise continues across the cuts. Owner: stream `fx`.
#pragma once

#include "FxBase.h"
#include "FxMesh.h"

#include <cstdint>
#include <vector>

namespace ffx {

struct FracturePiece {
	MeshData mesh;              // closed, outward wound, source-local (sim metres)
	std::vector<Vec3> hull;     // <= 42 support points of the piece (convex collision), source-local
	Vec3 centre;                // volume centroid
	float volume = 0.0f;
};

// Keeps the part of the closed mesh `in` with dot(n, x) <= d and closes every cut with a flat cap (normal +n, the mean
// colour of the cut edge, uv2 = (0, -1): the rock material shows it as a fresh break). `out` is cleared first. False
// when nothing is left.
bool ClipClosedMesh(const MeshData& in, const Vec3& n, float d, MeshData& out);

// Splits the closed mesh `src` into the Voronoi cells of `sites` (pieces with no volume are dropped).
std::vector<FracturePiece> VoronoiPieces(const MeshData& src, const std::vector<Vec3>& sites);

// n well-spread sites inside the ellipsoid (centre, half extents), deterministic per seed (best-candidate sampling).
std::vector<Vec3> FractureSites(uint32_t seed, int n, const Vec3& centre, const Vec3& halfExtents);

// Signed volume / centroid of a closed mesh (positive for outward winding).
float MeshVolume(const MeshData& m, Vec3* centroid = nullptr);

namespace meshlib {
// Cached piece sets; the references stay valid for the whole run (bounded key spaces, never evicted).
const std::vector<FracturePiece>& RockPieces(uint32_t seed, int n);         // pieces of Rock(seed), n in 2..8
const std::vector<FracturePiece>& WallPieces(uint32_t seed, int perBlock);  // every Wall(seed) block in 1..4 pieces
}  // namespace meshlib

}  // namespace ffx
