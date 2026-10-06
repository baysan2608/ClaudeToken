// FourfoldFX logic island - the per-frame output of the FX director: what to draw and which lights to light.
// The Unreal glue (FourfoldFxRenderer) binds every DrawItem.key to a pooled component (procedural mesh, static mesh
// or instanced static mesh) with a dynamic material instance of `mat`, uploads geometry only when the mesh version
// changes (recreating the section when the topology changed), pushes changed parameters, and hides / returns
// components whose key did not appear this frame. Keys are never reused. Everything is in SIM space.
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
	MeshAsset fallbackAsset = MeshAsset::None; // unused: the glue falls back to `fallbackMesh` when the asset is missing
	const MeshData* mesh = nullptr;            // ProcMesh geometry (or the fallback for a missing static mesh)
	const std::vector<Xform>* instances = nullptr;   // Instanced: per-instance transforms (component space)
	uint32_t instVersion = 0;
	Xform xform;                               // component transform (sim space)
	ParamBlock params;
	bool castShadow = false;
	int sortPriority = 0;                      // translucency sort priority (higher draws later)
};

struct LightReq {
	uint32_t key = 0;
	Vec3 pos;
	Color color;
	float intensity = 1.0f;   // relative (scaled by fx_config "lights.intensity_scale" in the glue)
	float radius = 3.0f;      // metres
	float priority = 1.0f;    // higher wins the <= N light budget
};

struct DrawList {
	std::vector<DrawItem> items;
	std::vector<LightReq> lights;
	void Clear() {
		items.clear();
		lights.clear();
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
