// FourfoldFX logic island - CPU mesh buffers and procedural geometry builders (engine-free, sim space).
//
// Winding convention (logic / sim space, right-handed): the front face of a triangle (a, b, c) is the side that
// cross(b - a, c - a) points to; every builder emits outward-facing triangles this way and fills matching normals.
// The Unreal glue mirrors Y/Z (sim -> UE), which flips handedness, and therefore reverses each triangle
// (FxUeConvert.h) so the faces stay outward in Unreal (whose front faces are those cross(b - a, c - a) points to,
// clockwise as seen from the front).
//
// Channel conventions (the HLSL in unreal/Shaders/FX reads them):
//   uv0  ribbons / strips: (across 0..1, distance along in metres); shells / cones: (angle 0..1, along 0..1);
//        particles: corner (0..1, 0..1)
//   uv1  ribbons: (along 0..1, per-strand random); particles: (flipbook frame, extra); rocks: blob offset
//        (sim x, sim z) = UE local (X, Y)
//   uv2  strips: (distance to the front in m, cross-section -1..1); particles: (age 0..1, per-particle random);
//        rocks: (blob offset sim y = UE local Z, per-vertex random). Rock offsets are in mesh units (unit rock: m).
//   col  linear RGBA tint / alpha (particles: colour over life; ribbons: width / life data where documented)
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"

#include <cstdint>
#include <vector>

namespace ffx {

struct MeshData {
	std::vector<Vec3> pos, nrm, tan;
	std::vector<Vec2> uv0, uv1, uv2;
	std::vector<Color> col;
	std::vector<int32_t> idx;

	// Bumped by Commit(): `version` on every commit, `topo` when the index buffer or vertex count changed (the
	// Unreal glue then recreates the section instead of updating vertices in place).
	uint32_t version = 0;
	uint32_t topo = 0;
	// Process-unique id assigned by the first Commit() (the glue keys uploads on (pointer, uid, version)).
	uint32_t uid = 0;

	void Clear();
	int NumVerts() const { return static_cast<int>(pos.size()); }
	int NumTris() const { return static_cast<int>(idx.size() / 3); }
	bool Empty() const { return idx.empty(); }
	int Add(const Vec3& p, const Vec3& n, const Vec2& t0, const Color& c = Color(),
	        const Vec2& t1 = Vec2(), const Vec2& t2 = Vec2(), const Vec3& tg = Vec3(1.0f, 0.0f, 0.0f));
	void Tri(int a, int b, int c);
	void Quad(int a, int b, int c, int d);      // a b c d around the face (front by the winding rule)
	void Append(const MeshData& o);
	void ComputeNormals();                      // smooth, area weighted, from the winding
	void ComputeTangents();                     // from uv0.x direction, orthogonalised against the normal
	void Transform(const Xform& x);             // positions, normals, tangents (normals re-normalised)
	// Pads with degenerate vertices / triangles up to fixed capacities (keeps Unreal's in-place update path).
	void PadTo(int vertCapacity, int idxCapacity);
	// Marks the buffers changed; detects topology changes (index content / vertex count) by hash.
	void Commit();
	bool Valid() const;                         // sizes consistent, indices in range

private:
	uint64_t lastTopoHash_ = 0;
	bool committed_ = false;
};

// ------------------------------------------------------------------------------------------------- builders
// Every builder APPENDS to `m` (call m.Clear() first for a fresh mesh).

// Quad centred at c spanned by half vectors hx (u) and hy (v); front = hx x hy.
void AppendQuad(MeshData& m, const Vec3& c, const Vec3& hx, const Vec3& hy, const Color& col = Color(),
                const Vec2& uvMin = Vec2(0.0f, 0.0f), const Vec2& uvMax = Vec2(1.0f, 1.0f));

// Camera-facing ribbon through `pts` (>= 2) with full widths `w` (size == pts or 1). uv0 = (across, metres along),
// uv1 = (along 0..1, strand), col per point (size == pts or 1, empty = white).
void AppendRibbon(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& w, const Vec3& camPos,
                  const std::vector<Color>& cols = {}, float strand = 0.0f);

// Ribbon lying in the plane whose normal is `up` (ground streaks, trails seen from above).
void AppendFlatRibbon(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& w, const Vec3& up,
                      const Color& col = Color());

// Tube along `pts` with radii `r` (size == pts or 1), `sides` around, rotation-minimising frames, optional
// hemispherical caps (capRings rings each). uv0 = (around 0..1, metres along), uv1 = (along 0..1, 0).
void AppendTube(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& r, int sides, bool capStart,
                bool capEnd, int capRings = 3, const Color& col = Color());

// Ground strip with a rounded cross-section (port of LavaWaveView.set_path): points tail -> front on the ground,
// full widths per point. Heights blend tail -> front with a bulging rounded front cap. uv0 = (metres along, metres
// across), uv1 = (along 0..1, cross -1..1), uv2 = (metres to the front, cos of the cross-section angle).
struct StripParams {
	int cross = 9;
	int frontCap = 4;
	float heightFront = 0.40f;
	float heightTail = 0.20f;
	float frontBulge = 0.28f;
	float widthBulge = 0.10f;
};
void AppendPathStrip(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& widths,
                     const StripParams& sp = StripParams());

// UV sphere / ellipsoid (radii), optionally the upper hemisphere only (domes). uv0 = (around, up 0..1).
void AppendSphere(MeshData& m, const Vec3& c, const Vec3& radii, int rings, int segs, bool hemisphere = false,
                  const Color& col = Color());

// Open surface of revolution along `axis` from `base`: radius profile rad[i] at height t_i = i / (n - 1) * height.
// twist = radians of rotation per unit t (spiral bands). uv0 = (around 0..1, t 0..1). Outward normals.
void AppendLathe(MeshData& m, const Vec3& base, const Vec3& axis, float height, const std::vector<float>& rad, int segs,
                 float twist = 0.0f, const Color& col = Color(), bool inward = false);

// Flat annulus (r0 inner, r1 outer) facing `normal`. uv0 = (around 0..1, radial 0 inner .. 1 outer).
void AppendRing(MeshData& m, const Vec3& c, const Vec3& normal, float r0, float r1, int segs, const Color& col = Color(),
                float arcStart = 0.0f, float arcLen = kFxTau);

// Chamfered (rounded-edge) box: half extents h, chamfer radius r, oriented by `b` (orthonormal). uv0 = planar per
// face in metres. uv2 = (0, rnd) so the rock material can stagger per block.
void AppendChamferBox(MeshData& m, const Vec3& c, const Vec3& h, float r, const Basis& b, float rnd = 0.0f,
                      const Color& col = Color());

// Procedural rock (icosphere 2 subdivisions = 320 triangles, noise displaced, faceted normals) of unit radius at
// the origin; uv1/uv2 carry the relaxed blob position + per-vertex random for the melt (same contract as the
// Blender rocks). Deterministic per seed.
void AppendRock(MeshData& m, uint32_t seed, float radius = 1.0f, const Vec3& c = Vec3());

// Spike (cone with a few facets) from base centre c along `up`, base radius r, height h.
void AppendSpike(MeshData& m, const Vec3& c, const Vec3& up, float r, float h, int facets, float twist,
                 const Color& col = Color());

// Thin crescent blade (wind): an arc of `arcLen` radians and radius R in the plane spanned by fwd / side around
// centre c, thickness in the plane (w), tapered tips. uv0 = (along 0..1, across 0..1).
void AppendCrescent(MeshData& m, const Vec3& c, const Vec3& fwd, const Vec3& normal, float R, float w, float arcLen,
                    int segs, const Color& col = Color());

}  // namespace ffx
