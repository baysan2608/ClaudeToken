// FourfoldFX logic island - the per-frame output of the FX director: what to draw and which lights to light.
// The Unreal glue (FourfoldFxRenderer) binds every DrawItem.key to a pooled component (procedural mesh, static mesh
// or instanced static mesh) with a dynamic material instance of `mat`, uploads geometry only when the bound mesh or
// its version changes (recreating the section when the topology changed), pushes changed parameters, and hides /
// returns components whose key did not appear this frame. Keys are never reused. Everything is in SIM space.
//
// Attached items (attachActor >= 0) follow a fighter bone exactly: the glue attaches the component to the fighter's
// body mesh at `attachBone` with absolute rotation / scale; the item's geometry is then relative to the bone position
// in world axes (xform.pos is ignored, xform.basis is the world rotation / scale).
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"
#include "FxMesh.h"
#include "FxTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ffx {

enum class DrawKind : uint8_t { ProcMesh, StaticMesh, Instanced };

struct ParamBlock {
	std::array<float, kNumParams> s{};
	std::array<Color, kNumVParams> v{};
	uint64_t sMask = 0;   // which scalars are set
	uint32_t vMask = 0;
	Flipbook flipbook = Flipbook::None;   // texture parameter "Flipbook" (None = leave the material default)

	void Set(P id, float value) {
		const int i = static_cast<int>(id);
		s[static_cast<size_t>(i)] = value;
		sMask |= (1ULL << i);
	}
	void Set(PV id, const Color& c) {
		const int i = static_cast<int>(id);
		v[static_cast<size_t>(i)] = c;
		vMask |= (1u << i);
	}
	bool Has(P id) const { return (sMask >> static_cast<int>(id)) & 1ULL; }
	bool Has(PV id) const { return (vMask >> static_cast<int>(id)) & 1u; }
	float Get(P id, float def = 0.0f) const { return Has(id) ? s[static_cast<size_t>(id)] : def; }
	Color Get(PV id, const Color& def = Color()) const { return Has(id) ? v[static_cast<size_t>(id)] : def; }
};

struct DrawItem {
	uint32_t key = 0;                          // stable while the item lives; never reused
	DrawKind kind = DrawKind::ProcMesh;
	MatSlot mat = MatSlot::Rock;
	MeshAsset asset = MeshAsset::None;         // StaticMesh / Instanced
	const MeshData* mesh = nullptr;            // ProcMesh geometry (or the fallback for a missing static mesh)
	const std::vector<Xform>* instances = nullptr;   // Instanced: per-instance transforms (component space)
	uint32_t instVersion = 0;
	Xform xform;                               // component transform (sim space)
	ParamBlock params;
	bool castShadow = false;
	int sortPriority = 0;                      // translucency sort priority (higher draws later)
	int attachActor = -1;                      // >= 0: follow this sim actor's bone (see header)
	Bone attachBone = Bone::Pelvis;
};

struct LightReq {
	uint32_t key = 0;
	Vec3 pos;
	Color color;
	float intensity = 1.0f;   // relative (scaled by fx_config "lights.intensity_scale" in the glue)
	float radius = 3.0f;      // metres
	float priority = 1.0f;    // higher wins the <= N light budget
};

// One Niagara system to spawn this frame (fire and forget). The glue spawns the system configured for `cue`
// (fx_config "niagara") and drops the request when the slot has none loaded. Colours are display sRGB; a == 0 keeps
// the system's own colour.
struct SystemReq {
	NCue cue = NCue::Blast;
	Vec3 pos;
	Vec3 dir{0.0f, 1.0f, 0.0f};   // unit: surface normal, jet or strike direction (the system's +Z)
	float scale = 1.0f;           // x the slot's scale
	float intensity = 1.0f;       // strength of the cue (parameter source "intensity")
	Color color{0.0f, 0.0f, 0.0f, 0.0f};
	Color color2{0.0f, 0.0f, 0.0f, 0.0f};
};

struct FracturePiece;   // FxFracture.h

// Physics debris of a broken stone / wall (FxFracture; visual only). The glue drops every piece (source-local geometry)
// as a rigid body placed with `xform` (the intact body's transform), shrunk by `scale` about its own centre, moving with
// `vel` + `burst` m/s away from `origin` + spin; they settle on a collision copy of the arena and sink after `life` s.
struct FractureReq {
	const std::vector<FracturePiece>* pieces = nullptr;   // meshlib::RockPieces / WallPieces (stable for the run)
	Xform xform;
	MatSlot mat = MatSlot::Rock;
	ParamBlock params;   // look of every piece
	Vec3 vel;
	Vec3 origin;
	float burst = 3.0f;
	float scale = 1.0f;
	float life = 2.5f;
	uint32_t seed = 0;
};

// A persistent Niagara system kept alive by a view (LCue slot, fx_config.json "niagara_loops"): the glue spawns it on
// the key's first frame, then moves / re-binds it every frame (+Z along `dir`), and stops it gently (live particles
// finish) on the first frame the key is missing. Colours as SystemReq (alpha 0 = keep the system's own).
struct LoopReq {
	uint32_t key = 0;
	LCue cue = LCue::Fire;
	Vec3 pos;
	Vec3 dir{0.0f, 1.0f, 0.0f};
	float scale = 1.0f;
	float intensity = 1.0f;
	Color color{0.0f, 0.0f, 0.0f, 0.0f};
	Color color2{0.0f, 0.0f, 0.0f, 0.0f};
};

// A solid the physics debris bounces off (a raised stone wall body): an oriented box, xform.pos = centre and the basis
// columns = half extents along its axes. Only sent while FxFrameIn::physicsDebris is set; keys are stable per body.
struct ColliderReq {
	uint32_t key = 0;
	Xform xform;
};

struct DrawList {
	std::vector<DrawItem> items;
	std::vector<LightReq> lights;
	std::vector<SystemReq> systems;
	std::vector<FractureReq> fractures;
	std::vector<ColliderReq> colliders;
	std::vector<LoopReq> loops;
	void Clear() {
		items.clear();
		lights.clear();
		systems.clear();
		fractures.clear();
		colliders.clear();
		loops.clear();
	}
	LoopReq& Loop(uint32_t key, LCue cue, const Vec3& pos) {
		LoopReq& l = loops.emplace_back();
		l.key = key;
		l.cue = cue;
		l.pos = pos;
		return l;
	}
	FractureReq& Fracture() { return fractures.emplace_back(); }
	SystemReq& System(NCue cue, const Vec3& pos, const Vec3& dir, float scale, float intensity) {
		SystemReq& s = systems.emplace_back();
		s.cue = cue;
		s.pos = pos;
		s.dir = dir;
		s.scale = scale;
		s.intensity = intensity;
		return s;
	}
	DrawItem& Add(uint32_t key, MatSlot mat, const MeshData* mesh, const Xform& x = Xform()) {
		DrawItem& it = items.emplace_back();
		it.key = key;
		it.kind = DrawKind::ProcMesh;
		it.mat = mat;
		it.mesh = mesh;
		it.xform = x;
		return it;
	}
	DrawItem& AddStatic(uint32_t key, MatSlot mat, MeshAsset a, const Xform& x, const MeshData* fallback) {
		DrawItem& it = items.emplace_back();
		it.key = key;
		it.kind = DrawKind::StaticMesh;
		it.mat = mat;
		it.asset = a;
		it.mesh = fallback;
		it.xform = x;
		return it;
	}
	DrawItem& AddInstanced(uint32_t key, MatSlot mat, MeshAsset a, const std::vector<Xform>* inst, uint32_t ver,
	                       const Xform& x, const MeshData* fallbackSingle) {
		DrawItem& it = items.emplace_back();
		it.key = key;
		it.kind = DrawKind::Instanced;
		it.mat = mat;
		it.asset = a;
		it.instances = inst;
		it.instVersion = ver;
		it.mesh = fallbackSingle;
		it.xform = x;
		return it;
	}
	void Light(uint32_t key, const Vec3& p, const Color& c, float intensity, float radius, float priority) {
		if (intensity <= 0.0f) return;
		LightReq& l = lights.emplace_back();
		l.key = key;
		l.pos = p;
		l.color = c;
		l.intensity = intensity;
		l.radius = radius;
		l.priority = priority;
	}
};

// Keeps the strongest `maxLights` requests (priority x intensity, nearer to the camera first on ties), in place.
void SelectLights(std::vector<LightReq>& lights, int maxLights, const Vec3& camPos);

}  // namespace ffx
