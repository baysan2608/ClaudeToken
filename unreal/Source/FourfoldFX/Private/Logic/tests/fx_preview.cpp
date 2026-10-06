// FourfoldFX - offline preview renderer (container only; FF_LOGIC_TESTS). Plays moves through the real engine-free
// sim (FourfoldCore Lab), runs the real FX director, converts every DrawItem exactly like the Unreal glue
// (FxUeConvert.h: sim metres -> UE centimetres, Y/Z swap, item axes) and rasterises it in software, running the very
// material code of Content/Python/fourfold/fx/spec.py (generated C++ via Tools/vfx/shader_check/gen_preview_nodes.py)
// over the shared HLSL (Shaders/Common, Shaders/FX) through hlsl_shim.h. Writes PPM frames for review
// (Tools/vfx/preview_sheet.py makes contact sheets). It approximates the mobile renderer: one sun + sky ambient
// (FFKeyDir), <= 4 point lights from the DrawList, opaque then translucent (priority, then far to near), MSAA-free.
//
//   ffx_preview --noise <T_FX_Noise.rgba8> [--fb <dir with <name>.rgba8 1024^2 atlases>] --out <dir> [--only <filter>]
#if defined(FF_LOGIC_TESTS)

#include "FxConfig.h"
#include "FxDirector.h"
#include "FxLightning.h"
#include "FxMesh.h"
#include "FxMeshLib.h"
#include "ff/FourfoldCore.h"
#include "hlsl_shim.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fxs {
using namespace hlsl;
inline float3 ddx(const float3&) { return float3(0.0f, 0.0f, 0.0f); }
inline float3 ddy(const float3&) { return float3(0.0f, 0.0f, 0.0f); }
#include "fx_nodes.gen.h"
}  // namespace fxs

using ffx::Vec2;
using ffx::Vec3;

// ------------------------------------------------------------------------------------------------ textures
struct TexData {
	int w = 0, h = 0;
	std::vector<uint8_t> px;   // RGBA8
};

namespace hlsl {
float4 Texture2D::Sample(const SamplerState&, float2 uv) const {
	const TexData* t = static_cast<const TexData*>(data);
	if (!t || t->w == 0) return float4(0.5f, 0.5f, 0.5f, 1.0f);
	const float x = uv.x * static_cast<float>(t->w) - 0.5f;
	const float y = uv.y * static_cast<float>(t->h) - 0.5f;
	const float fx = std::floor(x), fy = std::floor(y);
	const float ax = x - fx, ay = y - fy;
	auto texel = [t](int ix, int iy) {
		ix = ((ix % t->w) + t->w) % t->w;
		iy = ((iy % t->h) + t->h) % t->h;
		const uint8_t* p = &t->px[static_cast<size_t>((iy * t->w + ix) * 4)];
		return float4(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f);
	};
	const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
	const float4 a = texel(ix, iy), b = texel(ix + 1, iy), c = texel(ix, iy + 1), d = texel(ix + 1, iy + 1);
	return lerp(lerp(a, b, ax), lerp(c, d, ax), ay);
}
}  // namespace hlsl

namespace {

using hlsl::float2;
using hlsl::float3;
using hlsl::float4;

bool LoadRaw(const std::string& path, int w, int h, TexData& t) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	t.w = w;
	t.h = h;
	t.px.resize(static_cast<size_t>(w * h * 4));
	const size_t n = std::fread(t.px.data(), 1, t.px.size(), f);
	std::fclose(f);
	return n == t.px.size();
}

// Placeholder atlas (until the Blender flipbooks exist): soft round puffs that shrink in alpha over the frames.
void MakePlaceholderAtlas(TexData& t, int grid, bool fire) {
	t.w = t.h = 512;
	t.px.assign(static_cast<size_t>(t.w * t.h * 4), 0);
	const int cell = t.w / grid;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x) {
			const int f = (y / cell) * grid + (x / cell);
			const float life = static_cast<float>(f) / static_cast<float>(grid * grid - 1);
			const float u = (static_cast<float>(x % cell) + 0.5f) / static_cast<float>(cell) * 2.0f - 1.0f;
			const float v = (static_cast<float>(y % cell) + 0.5f) / static_cast<float>(cell) * 2.0f - 1.0f;
			const float r = std::sqrt(u * u + v * v) / (0.55f + 0.35f * life);
			const float cov = ffx::Sat(1.0f - r * r) * (1.0f - 0.6f * life);
			uint8_t* p = &t.px[static_cast<size_t>((y * t.w + x) * 4)];
			p[0] = static_cast<uint8_t>(255.0f * ffx::Sat(0.55f - 0.4f * v));
			p[1] = static_cast<uint8_t>(255.0f * cov);
			p[2] = static_cast<uint8_t>(255.0f * (fire ? ffx::Sat((1.0f - r) * (1.0f - life) * 1.3f) : 0.0f));
			p[3] = static_cast<uint8_t>(255.0f * cov);
		}
}

// ------------------------------------------------------------------------------------------------ UE space helpers
Vec3 Swz(const Vec3& v) { return Vec3(v.x, v.z, v.y); }
float3 F3(const Vec3& v) { return float3(v.x, v.y, v.z); }
Vec3 V(const float3& v) { return Vec3(v.x, v.y, v.z); }

struct Camera {
	Vec3 pos, fwd, right, up;   // UE space (cm)
	float focal = 1.0f;
	int w = 960, h = 540;
};

Camera MakeCamera(const Vec3& simPos, const Vec3& simTarget, int w, int h, float fovYDeg) {
	Camera c;
	c.pos = Swz(simPos) * 100.0f;
	c.fwd = ffx::Norm(Swz(simTarget - simPos));
	c.right = ffx::Norm(Vec3(0.0f, 0.0f, 1.0f).cross(c.fwd));
	c.up = c.fwd.cross(c.right);
	c.w = w;
	c.h = h;
	c.focal = (static_cast<float>(h) * 0.5f) / std::tan(fovYDeg * 0.5f * ffx::kFxPi / 180.0f);
	return c;
}

struct Frame {
	int w = 0, h = 0;
	std::vector<Vec3> col;
	std::vector<float> depth;
	void Reset(int aw, int ah) {
		w = aw;
		h = ah;
		col.assign(static_cast<size_t>(w * h), Vec3(0.0f, 0.0f, 0.0f));
		depth.assign(static_cast<size_t>(w * h), 1e30f);
	}
};

struct Light {
	Vec3 pos;   // UE cm
	Vec3 color;
	float intensity = 0.0f, radius = 300.0f;
};

// A vertex after the vertex stage.
struct VOut {
	Vec3 wpos;        // UE world (cm)
	Vec3 view;        // camera space (x right, y up, z forward)
	Vec2 uv0, uv1, uv2;
	float4 col;
	Vec3 lpos, nrmLocal, nrmWS, tanWS;
};

struct Params final : fxs::ParamSource {
	const ffx::ParamBlock* pb = nullptr;
	const hlsl::Texture2D* noise = nullptr;
	const hlsl::Texture2D* flipbook = nullptr;
	float S(const char* name, float def) const override {
		for (int i = 0; i < ffx::kNumParams; ++i)
			if (ffx::kParamNames[static_cast<size_t>(i)] == name) return pb->Get(static_cast<ffx::P>(i), def);
		return def;
	}
	float3 V(const char* name, float3 def) const override {
		for (int i = 0; i < ffx::kNumVParams; ++i)
			if (ffx::kVParamNames[static_cast<size_t>(i)] == name && pb->Has(static_cast<ffx::PV>(i))) {
				const ffx::Color c = pb->Get(static_cast<ffx::PV>(i));
				return float3(c.r, c.g, c.b);
			}
		return def;
	}
	const hlsl::Texture2D& T(const char* name) const override {
		return std::strcmp(name, "Flipbook") == 0 ? *flipbook : *noise;
	}
};

float3 KeyDir() { return fxs::FFKeyDir(); }

Vec3 Tonemap(Vec3 c) {
	auto aces = [](float x) {
		x = std::max(x, 0.0f) * 0.8f;
		return ffx::Sat((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
	};
	auto srgb = [](float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; };
	return Vec3(srgb(aces(c.x)), srgb(aces(c.y)), srgb(aces(c.z)));
}

// Lit shading of an opaque / translucent-lit pixel (sun + sky ambient + point lights + sky reflection).
Vec3 ShadeLit(const fxs::MatOut& o, const Vec3& n, const Vec3& v, const Vec3& p, const std::vector<Light>& lights) {
	const Vec3 l = V(KeyDir());
	const Vec3 albedo = V(o.base);
	const float metal = ffx::Sat(o.metal);
	const float rough = std::max(o.rough, 0.04f);
	const float ndl = std::max(n.dot(l), 0.0f);
	const float ndv = std::max(n.dot(v), 1e-3f);
	const Vec3 sun(2.3f, 2.2f, 2.0f);
	const float3 skyC = fxs::FFSky(n.z);
	const Vec3 sky = Vec3(skyC.x, skyC.y, skyC.z) * 0.55f;
	const Vec3 diffC = albedo * (1.0f - metal);
	const Vec3 f0 = Vec3(0.08f, 0.08f, 0.08f) * o.spec * (1.0f - metal) + albedo * metal;
	const Vec3 h = ffx::Norm(l + v);
	const float ndh = std::max(n.dot(h), 0.0f);
	const float a2 = rough * rough * rough * rough;
	const float dd = ndh * ndh * (a2 - 1.0f) + 1.0f;
	const float D = a2 / (ffx::kFxPi * dd * dd);
	const float fres = std::pow(1.0f - ffx::Sat(v.dot(h)), 5.0f);
	const Vec3 F = f0 + (Vec3(1.0f, 1.0f, 1.0f) - f0) * fres;
	const float vis = 0.25f / std::max(ndl * ndv, 0.05f) * std::min(ndl * ndv * 4.0f, 1.0f);
	Vec3 c = Vec3(diffC.x * (sky.x + sun.x * ndl), diffC.y * (sky.y + sun.y * ndl), diffC.z * (sky.z + sun.z * ndl));
	c = c + Vec3(F.x * sun.x, F.y * sun.y, F.z * sun.z) * (D * vis * ndl);
	const Vec3 r = n * (2.0f * n.dot(v)) - v;
	const float3 envC = fxs::FFSky(r.z);
	const float fr = std::pow(1.0f - ndv, 5.0f);
	const Vec3 Fe = f0 + (Vec3(1.0f, 1.0f, 1.0f) - f0) * fr;
	c = c + Vec3(envC.x * Fe.x, envC.y * Fe.y, envC.z * Fe.z) * (0.6f * (1.0f - rough));
	for (const Light& L : lights) {
		const Vec3 d = L.pos - p;
		const float dist = d.length();
		if (dist >= L.radius || dist < 1e-3f) continue;
		const float k = (1.0f - (dist / L.radius) * (dist / L.radius));
		const float att = k * k / (1.0f + (dist / 100.0f) * (dist / 100.0f));
		const float ndl2 = std::max(n.dot(d / dist), 0.0f);
		c = c + Vec3(diffC.x * L.color.x, diffC.y * L.color.y, diffC.z * L.color.z) * (L.intensity * att * ndl2 * 2.0f);
	}
	return c + V(o.emissive);
}

class Renderer {
public:
	Camera cam;
	Frame fb;
	float time = 0.0f;
	const hlsl::Texture2D* noise = nullptr;
	std::map<int, const hlsl::Texture2D*> flipbooks;
	std::vector<Light> lights;
	// simple bone guesses for attached items: actor id -> sim feet position + facing
	std::map<int, std::pair<Vec3, float>> actors;

	void Begin() {
		fb.Reset(cam.w, cam.h);
		// sky gradient background
		for (int y = 0; y < fb.h; ++y) {
			const float t = static_cast<float>(y) / static_cast<float>(fb.h);
			const Vec3 c = Vec3(0.55f, 0.68f, 0.85f) * (1.0f - t) + Vec3(0.35f, 0.42f, 0.52f) * t;
			for (int x = 0; x < fb.w; ++x) fb.col[static_cast<size_t>(y * fb.w + x)] = c * 1.1f;
		}
	}

	void DrawGround() {
		// procedural flagstones on a big quad at sim y = 0 (opaque, lit)
		ffx::MeshData m;
		ffx::AppendQuad(m, Vec3(0.0f, 0.0f, 0.0f), Vec3(14.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -14.0f));
		// subdivide by drawing many quads (keeps the rasteriser's near clipping cheap)
		m.Clear();
		for (int i = -14; i < 14; ++i)
			for (int j = -14; j < 14; ++j)
				ffx::AppendQuad(m, Vec3(static_cast<float>(i) + 0.5f, 0.0f, static_cast<float>(j) + 0.5f), Vec3(0.5f, 0.0f, 0.0f),
				                Vec3(0.0f, 0.0f, -0.5f), ffx::Color(), Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f));
		DrawMeshGeneric(m, ffx::Xform(), [](const Vec3& wp, const Vec3&) {
			const float sx = wp.x / 100.0f, sy = wp.y / 100.0f;
			const float jx = std::fabs(sx * 1.6f - std::round(sx * 1.6f));
			const float jy = std::fabs(sy * 1.6f + 0.5f * std::floor(sx * 1.6f) - std::round(sy * 1.6f + 0.5f * std::floor(sx * 1.6f)));
			const float joint = std::min(jx, jy) < 0.03f ? 0.55f : 1.0f;
			const float tone = 0.12f + 0.03f * std::sin(std::floor(sx * 1.6f) * 7.1f + std::floor(sy * 1.6f) * 3.7f);
			return Vec3(tone, tone * 0.96f, tone * 0.9f) * joint;
		});
	}

	void DrawActor(const Vec3& feet) {
		ffx::MeshData m;
		ffx::AppendTube(m, {feet + Vec3(0.0f, 0.25f, 0.0f), feet + Vec3(0.0f, 1.45f, 0.0f)}, {0.2f}, 12, true, true, 3);
		ffx::AppendSphere(m, feet + Vec3(0.0f, 1.62f, 0.0f), Vec3(0.12f, 0.14f, 0.12f), 6, 10);
		DrawMeshGeneric(m, ffx::Xform(), [](const Vec3&, const Vec3&) { return Vec3(0.18f, 0.2f, 0.26f); });
	}

	// Opaque untextured / procedurally coloured mesh (context geometry).
	template <typename ColorFn>
	void DrawMeshGeneric(const ffx::MeshData& m, const ffx::Xform& x, ColorFn colorFn) {
		std::vector<VOut> vs(static_cast<size_t>(m.NumVerts()));
		for (int i = 0; i < m.NumVerts(); ++i) {
			VOut& o = vs[static_cast<size_t>(i)];
			const Vec3 sp = x.Apply(m.pos[static_cast<size_t>(i)]);
			o.wpos = Swz(sp) * 100.0f;
			o.nrmWS = ffx::Norm(Swz(x.basis.Apply(m.nrm[static_cast<size_t>(i)])), Vec3(0.0f, 0.0f, 1.0f));
			ToView(o);
		}
		for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
			const VOut* tri[3] = {&vs[static_cast<size_t>(m.idx[t])], &vs[static_cast<size_t>(m.idx[t + 1])],
			                      &vs[static_cast<size_t>(m.idx[t + 2])]};
			RasterTri(tri, [&](const VOut& p, int px, int py, float z) {
				fxs::MatOut o;
				o.base = F3(colorFn(p.wpos, p.nrmWS));
				o.rough = 0.85f;
				o.spec = 0.3f;
				const Vec3 v = ffx::Norm(cam.pos - p.wpos);
				Vec3 n = ffx::Norm(p.nrmWS);
				if (n.dot(v) < 0.0f) n = n * -1.0f;
				const size_t k = static_cast<size_t>(py * fb.w + px);
				if (z < fb.depth[k]) {
					fb.depth[k] = z;
					fb.col[k] = ShadeLit(o, n, v, p.wpos, lights);
				}
			});
		}
	}

	void SetLights(const ffx::DrawList& dl) {
		lights.clear();
		for (const ffx::LightReq& l : dl.lights) {
			Light L;
			L.pos = Swz(l.pos) * 100.0f;
			L.color = Vec3(l.color.r, l.color.g, l.color.b);
			L.intensity = l.intensity;
			L.radius = l.radius * 100.0f;
			lights.push_back(L);
		}
	}

	void DrawItems(const ffx::DrawList& dl) {
		// opaque first, then translucent sorted by priority, then far to near
		std::vector<const ffx::DrawItem*> opaque, trans;
		for (const ffx::DrawItem& it : dl.items) {
			const int mi = MatIndex(it.mat);
			if (mi < 0 || !it.mesh) continue;
			(fxs::kMatInfo[mi].blend == 0 ? opaque : trans).push_back(&it);
		}
		for (const ffx::DrawItem* it : opaque) DrawOne(*it);
		std::vector<std::pair<std::pair<int, float>, const ffx::DrawItem*>> order;
		for (const ffx::DrawItem* it : trans) {
			const Vec3 c = Swz(ItemOrigin(*it)) * 100.0f;
			order.push_back({{it->sortPriority, -(c - cam.pos).length()}, it});
		}
		std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& o : order) DrawOne(*o.second);
	}

	void WritePPM(const std::string& path) const {
		FILE* f = std::fopen(path.c_str(), "wb");
		if (!f) return;
		std::fprintf(f, "P6\n%d %d\n255\n", fb.w, fb.h);
		std::vector<uint8_t> row(static_cast<size_t>(fb.w * 3));
		for (int y = 0; y < fb.h; ++y) {
			for (int x = 0; x < fb.w; ++x) {
				const Vec3 c = Tonemap(fb.col[static_cast<size_t>(y * fb.w + x)]);
				row[static_cast<size_t>(x * 3)] = static_cast<uint8_t>(255.0f * ffx::Sat(c.x));
				row[static_cast<size_t>(x * 3 + 1)] = static_cast<uint8_t>(255.0f * ffx::Sat(c.y));
				row[static_cast<size_t>(x * 3 + 2)] = static_cast<uint8_t>(255.0f * ffx::Sat(c.z));
			}
			std::fwrite(row.data(), 1, row.size(), f);
		}
		std::fclose(f);
	}

private:
	static int MatIndex(ffx::MatSlot s) {
		const std::string_view n = ffx::MatSlotName(s);
		for (int i = 0; i < fxs::kNumMats; ++i)
			if (n == fxs::kMatInfo[i].key) return i;
		return -1;
	}

	Vec3 BonePos(int actor, ffx::Bone b) const {
		auto it = actors.find(actor);
		if (it == actors.end()) return Vec3();
		const Vec3 feet = it->second.first;
		const float f = it->second.second;
		const Vec3 fwd(std::sin(f), 0.0f, std::cos(f));
		const Vec3 side(fwd.z, 0.0f, -fwd.x);
		switch (b) {
			case ffx::Bone::Pelvis: return feet + Vec3(0.0f, 0.95f, 0.0f);
			case ffx::Bone::Spine: return feet + Vec3(0.0f, 1.15f, 0.0f);
			case ffx::Bone::Chest: return feet + Vec3(0.0f, 1.35f, 0.0f);
			case ffx::Bone::Head: return feet + Vec3(0.0f, 1.62f, 0.0f);
			case ffx::Bone::HandL: return feet + Vec3(0.0f, 1.15f, 0.0f) + fwd * 0.35f + side * 0.25f;
			case ffx::Bone::HandR: return feet + Vec3(0.0f, 1.15f, 0.0f) + fwd * 0.35f - side * 0.25f;
			case ffx::Bone::FootL: return feet + side * 0.14f;
			case ffx::Bone::FootR: return feet - side * 0.14f;
			default: return feet;
		}
	}

	Vec3 ItemOrigin(const ffx::DrawItem& it) const {
		Vec3 o = it.attachActor >= 0 ? BonePos(it.attachActor, it.attachBone) : it.xform.pos;
		if (it.mesh && !it.mesh->pos.empty()) {
			Vec3 c;
			for (const Vec3& p : it.mesh->pos) c = c + p;
			c = c / static_cast<float>(it.mesh->pos.size());
			o = o + it.xform.basis.Apply(c);
		}
		return o;
	}

	void ToView(VOut& o) const {
		const Vec3 d = o.wpos - cam.pos;
		o.view = Vec3(d.dot(cam.right), d.dot(cam.up), d.dot(cam.fwd));
	}

	template <typename PixelFn>
	void RasterTri(const VOut* const tri[3], PixelFn pixel) {
		// near-plane clip (z >= 10 cm) by splitting into at most two triangles
		const float zn = 10.0f;
		VOut in[3] = {*tri[0], *tri[1], *tri[2]};
		VOut poly[4];
		int n = 0;
		for (int i = 0; i < 3; ++i) {
			const VOut& a = in[i];
			const VOut& b = in[(i + 1) % 3];
			const bool ia = a.view.z >= zn, ib = b.view.z >= zn;
			if (ia) poly[n++] = a;
			if (ia != ib && n < 4) {
				const float t = (zn - a.view.z) / (b.view.z - a.view.z);
				poly[n++] = LerpV(a, b, t);
			}
		}
		if (n < 3) return;
		RasterClipped(poly[0], poly[1], poly[2], pixel);
		if (n == 4) RasterClipped(poly[0], poly[2], poly[3], pixel);
	}

	static VOut LerpV(const VOut& a, const VOut& b, float t) {
		VOut o;
		auto l3 = [t](const Vec3& p, const Vec3& q) { return p + (q - p) * t; };
		auto l2 = [t](const Vec2& p, const Vec2& q) { return Vec2(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t); };
		o.wpos = l3(a.wpos, b.wpos);
		o.view = l3(a.view, b.view);
		o.uv0 = l2(a.uv0, b.uv0);
		o.uv1 = l2(a.uv1, b.uv1);
		o.uv2 = l2(a.uv2, b.uv2);
		o.col = hlsl::lerp(a.col, b.col, t);
		o.lpos = l3(a.lpos, b.lpos);
		o.nrmLocal = l3(a.nrmLocal, b.nrmLocal);
		o.nrmWS = l3(a.nrmWS, b.nrmWS);
		o.tanWS = l3(a.tanWS, b.tanWS);
		return o;
	}

	template <typename PixelFn>
	void RasterClipped(const VOut& a, const VOut& b, const VOut& c, PixelFn pixel) {
		const VOut* v[3] = {&a, &b, &c};
		float sx[3], sy[3], iz[3];
		for (int i = 0; i < 3; ++i) {
			iz[i] = 1.0f / v[i]->view.z;
			sx[i] = static_cast<float>(cam.w) * 0.5f + v[i]->view.x * iz[i] * cam.focal;
			sy[i] = static_cast<float>(cam.h) * 0.5f - v[i]->view.y * iz[i] * cam.focal;
		}
		const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
		if (std::fabs(area) < 1e-6f) return;
		const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
		const int x1 = std::min(cam.w - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
		const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
		const int y1 = std::min(cam.h - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
		for (int py = y0; py <= y1; ++py)
			for (int px = x0; px <= x1; ++px) {
				const float fx = static_cast<float>(px) + 0.5f, fy = static_cast<float>(py) + 0.5f;
				float w0 = ((sx[1] - fx) * (sy[2] - fy) - (sx[2] - fx) * (sy[1] - fy)) / area;
				float w1 = ((sx[2] - fx) * (sy[0] - fy) - (sx[0] - fx) * (sy[2] - fy)) / area;
				float w2 = 1.0f - w0 - w1;
				if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
				const float pw0 = w0 * iz[0], pw1 = w1 * iz[1], pw2 = w2 * iz[2];
				const float s = pw0 + pw1 + pw2;
				const float k0 = pw0 / s, k1 = pw1 / s, k2 = pw2 / s;
				VOut p;
				auto i3 = [&](const Vec3& A, const Vec3& B, const Vec3& C) { return A * k0 + B * k1 + C * k2; };
				auto i2 = [&](const Vec2& A, const Vec2& B, const Vec2& C) {
					return Vec2(A.x * k0 + B.x * k1 + C.x * k2, A.y * k0 + B.y * k1 + C.y * k2);
				};
				p.wpos = i3(a.wpos, b.wpos, c.wpos);
				p.view = i3(a.view, b.view, c.view);
				p.uv0 = i2(a.uv0, b.uv0, c.uv0);
				p.uv1 = i2(a.uv1, b.uv1, c.uv1);
				p.uv2 = i2(a.uv2, b.uv2, c.uv2);
				p.col = a.col * k0 + b.col * k1 + c.col * k2;
				p.lpos = i3(a.lpos, b.lpos, c.lpos);
				p.nrmLocal = i3(a.nrmLocal, b.nrmLocal, c.nrmLocal);
				p.nrmWS = i3(a.nrmWS, b.nrmWS, c.nrmWS);
				p.tanWS = i3(a.tanWS, b.tanWS, c.tanWS);
				pixel(p, px, py, p.view.z);
			}
	}

	void DrawOne(const ffx::DrawItem& it) {
		const int mi = MatIndex(it.mat);
		const fxs::MatInfo& info = fxs::kMatInfo[mi];
		const ffx::MeshData& m = *it.mesh;
		Params ps;
		ps.pb = &it.params;
		ps.noise = noise;
		static const hlsl::Texture2D kNone;
		auto fbIt = flipbooks.find(static_cast<int>(it.params.flipbook));
		ps.flipbook = fbIt != flipbooks.end() ? fbIt->second : &kNone;
		// item transform in UE space: axes X = swz(B.x), Y = swz(B.z), Z = swz(B.y); attached items sit on a bone
		const Vec3 origin = it.attachActor >= 0 ? BonePos(it.attachActor, it.attachBone) : it.xform.pos;
		const Vec3 ax = Swz(it.xform.basis.x), ay = Swz(it.xform.basis.z), az = Swz(it.xform.basis.y);
		const Vec3 t = Swz(origin) * 100.0f;
		auto toWorld = [&](const Vec3& l) { return t + ax * l.x + ay * l.y + az * l.z; };
		auto dirWorld = [&](const Vec3& l) { return ax * l.x + ay * l.y + az * l.z; };
		auto nrmWorld = [&](const Vec3& l) {
			return ffx::Norm(ax * (l.x / std::max(ax.length_squared(), 1e-8f)) + ay * (l.y / std::max(ay.length_squared(), 1e-8f)) +
			                     az * (l.z / std::max(az.length_squared(), 1e-8f)),
			                 Vec3(0.0f, 0.0f, 1.0f));
		};
		std::vector<VOut> vs(static_cast<size_t>(m.NumVerts()));
		for (int i = 0; i < m.NumVerts(); ++i) {
			const size_t s = static_cast<size_t>(i);
			VOut& o = vs[s];
			o.lpos = Swz(m.pos[s]) * 100.0f;
			o.nrmLocal = Swz(m.nrm[s]);
			o.uv0 = m.uv0[s];
			o.uv1 = m.uv1[s];
			o.uv2 = m.uv2[s];
			// vertex colours reach the material as 8-bit unorm (FColor, no sRGB conversion)
			auto q = [](float x) { return std::round(ffx::Sat(x) * 255.0f) / 255.0f; };
			o.col = float4(q(m.col[s].r), q(m.col[s].g), q(m.col[s].b), q(m.col[s].a));
			o.nrmWS = nrmWorld(o.nrmLocal);
			o.tanWS = ffx::Norm(dirWorld(Swz(m.tan[s])), Vec3(1.0f, 0.0f, 0.0f));
			Vec3 lp = o.lpos;
			if (info.hasWpo) {
				fxs::NodeIn in;
				in.uv0 = float2(o.uv0.x, o.uv0.y);
				in.uv1 = float2(o.uv1.x, o.uv1.y);
				in.uv2 = float2(o.uv2.x, o.uv2.y);
				in.vc = float3(o.col.x, o.col.y, o.col.z);
				in.vca = o.col.w;
				in.time = time;
				in.lpos = F3(o.lpos);
				in.nrmLocal = F3(o.nrmLocal);
				in.nrmWS = F3(o.nrmWS);
				in.ax = F3(ax);
				in.ay = F3(ay);
				in.az = F3(az);
				lp = lp + V(fxs::VertexStage(mi, in, ps));
			}
			o.wpos = toWorld(lp);
			ToView(o);
		}
		const int blend = info.blend;
		for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
			// UE reverses the winding (FxUeConvert); front = facing the camera by the authored normals
			const VOut* tri[3] = {&vs[static_cast<size_t>(m.idx[k])], &vs[static_cast<size_t>(m.idx[k + 1])],
			                      &vs[static_cast<size_t>(m.idx[k + 2])]};
			if (!info.twoSided) {
				const Vec3 fn = (tri[1]->wpos - tri[0]->wpos).cross(tri[2]->wpos - tri[0]->wpos);
				const Vec3 nAvg = tri[0]->nrmWS + tri[1]->nrmWS + tri[2]->nrmWS;
				const Vec3 toCam = cam.pos - tri[0]->wpos;
				const Vec3 facing = fn.dot(nAvg) >= 0.0f ? fn : fn * -1.0f;
				if (facing.dot(toCam) < 0.0f) continue;
			}
			RasterTri(tri, [&](const VOut& p, int px, int py, float z) {
				const size_t idx = static_cast<size_t>(py * fb.w + px);
				if (z >= fb.depth[idx]) return;
				fxs::NodeIn in;
				in.uv0 = float2(p.uv0.x, p.uv0.y);
				in.uv1 = float2(p.uv1.x, p.uv1.y);
				in.uv2 = float2(p.uv2.x, p.uv2.y);
				in.vc = float3(p.col.x, p.col.y, p.col.z);
				in.vca = p.col.w;
				in.time = time;
				in.lpos = F3(p.lpos);
				in.nrmLocal = F3(p.nrmLocal);
				const Vec3 nv = ffx::Norm(p.nrmWS, Vec3(0.0f, 0.0f, 1.0f));
				in.nrmWS = F3(nv);
				const Vec3 v = ffx::Norm(cam.pos - p.wpos);
				in.cam = F3(v);
				in.wposCR = F3(p.wpos - cam.pos);
				in.ax = F3(ax);
				in.ay = F3(ay);
				in.az = F3(az);
				fxs::MatOut o;
				fxs::PixelStage(mi, in, ps, o);
				if (blend == 0) {
					Vec3 n = ResolveNormal(info, o, nv, p.tanWS);
					fb.depth[idx] = z;
					fb.col[idx] = ShadeLit(o, n, v, p.wpos, lights);
				} else if (blend == 1) {
					const float a = ffx::Sat(o.opacity);
					fb.col[idx] = V(o.emissive) + fb.col[idx] * (1.0f - a);
				} else if (blend == 2) {
					fb.col[idx] = fb.col[idx] + V(o.emissive);
				} else {
					const float a = ffx::Sat(o.opacity);
					const Vec3 n = ResolveNormal(info, o, nv, p.tanWS);
					const Vec3 c = info.lit ? ShadeLit(o, n, v, p.wpos, lights) : V(o.emissive);
					fb.col[idx] = c * a + fb.col[idx] * (1.0f - a);
				}
			});
		}
	}

	static Vec3 ResolveNormal(const fxs::MatInfo& info, const fxs::MatOut& o, const Vec3& nv, const Vec3& tan) {
		if (!info.tangentNormal) return ffx::Norm(V(o.normal), nv);
		const Vec3 tt = ffx::Norm(tan - nv * tan.dot(nv), Vec3(1.0f, 0.0f, 0.0f));
		const Vec3 bt = nv.cross(tt);
		return ffx::Norm(tt * o.normal.x + bt * o.normal.y + nv * o.normal.z, nv);
	}
};


// ------------------------------------------------------------------------------------------------ material gallery
// Hand-built draw lists that put one material family in a row of controlled states (close-up review).
struct Gallery {
	Renderer& r;
	std::string outDir;
	ffx::FxConfig cfg;
	ffx::DrawList dl;
	std::vector<std::unique_ptr<ffx::MeshData>> meshes;
	uint32_t key = 1;

	ffx::MeshData& NewMesh() {
		meshes.push_back(std::make_unique<ffx::MeshData>());
		return *meshes.back();
	}
	ffx::DrawItem& Add(ffx::MatSlot m, const ffx::MeshData* mesh, const Vec3& pos, const ffx::Basis& b = ffx::Basis::Identity()) {
		ffx::Xform x;
		x.pos = pos;
		x.basis = b;
		return dl.Add(key++, m, mesh, x);
	}
	void Shoot(const std::string& name, const Vec3& camPos, const Vec3& target, float time, int w, int h, float fov = 40.0f,
	           bool ground = true) {
		r.cam = MakeCamera(camPos, target, w, h, fov);
		r.time = time;
		r.actors.clear();
		r.Begin();
		r.SetLights(dl);
		if (ground) r.DrawGround();
		r.DrawItems(dl);
		r.WritePPM(outDir + "/" + name + ".ppm");
		dl.Clear();
		meshes.clear();
	}
	static void Flame4(ffx::ParamBlock& pb, const std::array<ffx::Color, 4>& c) {
		pb.Set(ffx::PV::Color, ffx::Linear(c[0]));
		pb.Set(ffx::PV::Color2, ffx::Linear(c[1]));
		pb.Set(ffx::PV::Color3, ffx::Linear(c[2]));
		pb.Set(ffx::PV::Color4, ffx::Linear(c[3]));
	}

	void Run(int w, int h) {
		using ffx::MatSlot;
		using ffx::P;
		using ffx::PV;
		const Vec3 cam(0.0f, 1.3f, 4.2f);
		// 1. rock continuum: cold, hot, glowing cracks, molten blob, crusting, cooled
		const float states[6][3] = {{0, 0, 0}, {0.45f, 0, 0}, {1, 0.25f, 0}, {1, 1, 0}, {0.9f, 1, 0.5f}, {0.2f, 0.3f, 1}};
		for (int i = 0; i < 6; ++i) {
			ffx::DrawItem& it = Add(MatSlot::Rock, &ffx::meshlib::Rock(static_cast<uint32_t>(3 + i)),
			                        Vec3(-2.5f + static_cast<float>(i), 0.45f, 0.0f), ffx::Basis::Identity().Scaled(0.42f));
			it.params.Set(P::Heat, states[i][0]);
			it.params.Set(P::Melt, states[i][1]);
			it.params.Set(P::Crust, states[i][2]);
			it.params.Set(P::Seed, static_cast<float>(i) * 1.7f);
		}
		Shoot("g01_rock_states", cam, Vec3(0.0f, 0.45f, 0.0f), 1.0f, w, h);
		// 2. walls: stone, obsidian, sandstone, damaged + hot
		const ffx::Color tints[4] = {ffx::Color(1, 1, 1), ffx::Color(0.32f, 0.30f, 0.36f), ffx::Color(2.7f, 2.15f, 1.45f), ffx::Color(1, 1, 1)};
		for (int i = 0; i < 4; ++i) {
			ffx::DrawItem& it = Add(MatSlot::Rock, &ffx::meshlib::Wall(static_cast<uint32_t>(i)), Vec3(-2.4f + 1.6f * static_cast<float>(i), 0.0f, -0.5f),
			                        ffx::Basis::Identity().Scaled(0.7f, 1.2f, 0.7f));
			it.params.Set(P::Detail, 2.2f);
			it.params.Set(PV::Tint, tints[i]);
			it.params.Set(P::Glass, i == 1 ? 1.0f : 0.0f);
			it.params.Set(P::Damage, i == 3 ? 0.8f : 0.0f);
			it.params.Set(P::Heat, i == 3 ? 0.6f : 0.0f);
			it.params.Set(P::Seed, static_cast<float>(i));
		}
		Shoot("g02_walls", Vec3(0.0f, 1.6f, 4.5f), Vec3(0.0f, 0.7f, -0.5f), 1.0f, w, h, 45.0f);
		// 3. lava strips: fresh, crusting, cooled
		for (int i = 0; i < 3; ++i) {
			ffx::MeshData& m = NewMesh();
			std::vector<Vec3> pts;
			std::vector<float> wd;
			for (int k = 0; k < 12; ++k) {
				const float t = static_cast<float>(k) / 11.0f;
				pts.push_back(Vec3(-2.6f + 5.2f * t, 0.0f, -1.2f + 1.3f * static_cast<float>(i) + 0.15f * std::sin(t * 5.0f)));
				wd.push_back(0.9f + 0.3f * t);
			}
			ffx::AppendPathStrip(m, pts, wd);
			m.ComputeTangents();
			ffx::DrawItem& it = Add(MatSlot::LavaStrip, &m, Vec3());
			const float melt[3] = {1.0f, 0.8f, 0.15f}, crust[3] = {0.0f, 0.45f, 1.0f};
			it.params.Set(P::Melt, melt[i]);
			it.params.Set(P::Crust, crust[i]);
			it.params.Set(P::Flow, 0.4f);
			it.params.Set(P::Boil, 3.0f);
			it.params.Set(P::Seed, static_cast<float>(i) * 0.7f);
		}
		Shoot("g03_lava_strips", Vec3(0.0f, 2.6f, 3.6f), Vec3(0.0f, 0.0f, -0.2f), 2.0f, w, h, 50.0f);
		// 4. flames: burst ages, held teardrop (loop), core layer, tongue field
		for (int i = 0; i < 3; ++i) {
			for (int layer = 0; layer < 2; ++layer) {
				ffx::Basis b = ffx::Basis::FromUpFwd(Vec3(1.0f, 0.15f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)).Scaled(0.35f, 1.8f, 0.35f);
				ffx::DrawItem& it = Add(MatSlot::Flame, &ffx::meshlib::Flame(), Vec3(-2.4f, 0.6f + 0.75f * static_cast<float>(i), -0.6f), b);
				it.params.Set(P::Style, 0.0f);
				it.params.Set(P::Shape, 0.0f);
				it.params.Set(P::Age, 0.18f + 0.22f * static_cast<float>(i));
				it.params.Set(P::Core, static_cast<float>(layer));
				it.params.Set(P::Cover, layer ? 0.25f : 0.8f);
				it.params.Set(P::Intensity, 1.0f);
				it.params.Set(P::Seed, 0.3f + 0.2f * static_cast<float>(i));
				Flame4(it.params, cfg.flame);
			}
		}
		for (int layer = 0; layer < 2; ++layer) {
			ffx::DrawItem& it = Add(MatSlot::Flame, &ffx::meshlib::Flame(), Vec3(1.0f, 0.0f, -0.4f), ffx::Basis::Identity().Scaled(0.35f, 1.1f, 0.35f));
			it.params.Set(P::Style, 1.0f);
			it.params.Set(P::Shape, 1.0f);
			it.params.Set(P::Scroll, 1.3f);
			it.params.Set(P::Core, static_cast<float>(layer));
			it.params.Set(P::Cover, layer ? 0.25f : 0.8f);
			it.params.Set(P::Intensity, 1.3f);
			it.params.Set(P::Seed, layer ? 0.53f : 0.17f);
			Flame4(it.params, layer ? cfg.flame : cfg.flame);
		}
		{
			// blue held flame
			for (int layer = 0; layer < 2; ++layer) {
				ffx::DrawItem& it = Add(MatSlot::Flame, &ffx::meshlib::Flame(), Vec3(2.3f, 0.0f, -0.4f), ffx::Basis::Identity().Scaled(0.3f, 1.0f, 0.3f));
				it.params.Set(P::Style, 1.0f);
				it.params.Set(P::Shape, 1.0f);
				it.params.Set(P::Scroll, 2.1f);
				it.params.Set(P::Core, static_cast<float>(layer));
				it.params.Set(P::Cover, layer ? 0.25f : 0.6f);
				it.params.Set(P::Intensity, 1.3f);
				Flame4(it.params, cfg.blueFlame);
			}
		}
		Shoot("g04_flames", Vec3(0.0f, 1.4f, 4.6f), Vec3(0.0f, 0.8f, -0.4f), 1.0f, w, h, 50.0f);
		// 5. lightning bolts at three ages + a crackle arc
		for (int i = 0; i < 3; ++i) {
			std::vector<ffx::BoltLine> lines;
			ffx::BoltParams bp;
			ffx::GenerateBolt({Vec3(-2.6f + 1.8f * static_cast<float>(i), 2.2f, -0.5f), Vec3(-2.0f + 1.8f * static_cast<float>(i), 0.0f, -0.5f)},
			                  static_cast<uint32_t>(11 + i), bp, lines);
			ffx::MeshData& m = NewMesh();
			ffx::BuildBoltMesh(m, lines, Vec3(0.0f, 1.3f, 4.2f));
			ffx::DrawItem& it = Add(MatSlot::Lightning, &m, Vec3());
			it.params.Set(P::Age, 0.05f + 0.3f * static_cast<float>(i));
			it.params.Set(P::Intensity, 1.0f);
			it.params.Set(PV::Color, ffx::Linear(cfg.MatColor(ffx::Fam::Lightning)));
		}
		Shoot("g05_lightning", cam, Vec3(0.0f, 1.0f, -0.5f), 1.0f, w, h, 50.0f);
		// 6. water: stream tube, held orb, frozen orb, frozen stream, wave strip
		{
			ffx::MeshData& tube = NewMesh();
			std::vector<Vec3> pts;
			std::vector<float> rad;
			for (int k = 0; k < 16; ++k) {
				const float t = static_cast<float>(k) / 15.0f;
				pts.push_back(Vec3(-2.8f + 2.2f * t, 0.6f + 0.5f * std::sin(t * 3.0f), 0.0f));
				rad.push_back(0.12f * (0.35f + 0.65f * t));
			}
			ffx::AppendTube(tube, pts, rad, 10, true, true, 3);
			ffx::DrawItem& a = Add(MatSlot::Water, &tube, Vec3());
			a.params.Set(P::Shape, 0.0f);
			a.params.Set(P::Flow, 1.0f);
			ffx::DrawItem& b = Add(MatSlot::Water, &ffx::meshlib::Sphere(2), Vec3(0.0f, 0.9f, 0.0f), ffx::Basis::Identity().Scaled(0.32f));
			b.params.Set(P::Shape, 1.0f);
			b.params.Set(P::Detail, 1.0f);
			b.params.Set(P::Flow, 1.0f);
			ffx::DrawItem& c = Add(MatSlot::Water, &ffx::meshlib::Sphere(2), Vec3(1.0f, 0.9f, 0.0f), ffx::Basis::Identity().Scaled(0.32f));
			c.params.Set(P::Shape, 1.0f);
			c.params.Set(P::Frozen, 1.0f);
			ffx::MeshData& strip = NewMesh();
			std::vector<Vec3> sp;
			std::vector<float> sw;
			for (int k = 0; k < 12; ++k) {
				const float t = static_cast<float>(k) / 11.0f;
				sp.push_back(Vec3(1.5f + 1.4f * t, 0.0f, 0.6f - 1.8f * t));
				sw.push_back(0.8f + 0.4f * t);
			}
			ffx::AppendPathStrip(strip, sp, sw);
			strip.ComputeTangents();
			ffx::DrawItem& d = Add(MatSlot::Water, &strip, Vec3());
			d.params.Set(P::Shape, 2.0f);
			d.params.Set(P::Flow, 1.0f);
		}
		Shoot("g06_water", Vec3(0.0f, 1.8f, 4.6f), Vec3(0.0f, 0.5f, -0.2f), 1.0f, w, h, 50.0f);
		// 7. crystals (ice wall, glass cluster), metal (disc, lance, red-hot rod), vine
		{
			ffx::DrawItem& a = Add(MatSlot::Crystal, &ffx::meshlib::Crystal(3, ffx::CrystalMode::Wall), Vec3(-2.2f, 0.0f, -0.5f), ffx::Basis::Identity().Scaled(0.9f, 1.3f, 0.6f));
			a.params.Set(PV::Tint, ffx::Linear(cfg.ice.tint));
			a.params.Set(P::Opacity, cfg.ice.opacity);
			a.params.Set(P::Glow, cfg.ice.edge);
			a.params.Set(P::Frost, cfg.ice.frost);
			ffx::DrawItem& g = Add(MatSlot::Crystal, &ffx::meshlib::Crystal(5, ffx::CrystalMode::Cluster), Vec3(-0.6f, 0.0f, -0.3f), ffx::Basis::Identity().Scaled(0.7f));
			g.params.Set(PV::Tint, ffx::Linear(cfg.glass.tint));
			g.params.Set(P::Opacity, cfg.glass.opacity);
			g.params.Set(P::Glow, cfg.glass.edge);
			g.params.Set(P::Frost, cfg.glass.frost);
			ffx::DrawItem& d = Add(MatSlot::Metal, &ffx::meshlib::Disc(), Vec3(0.6f, 0.6f, 0.0f),
			                       ffx::Basis::AxisAngle(Vec3(1.0f, 0.0f, 0.0f), 1.2f).Scaled(0.3f));
			d.params.Set(P::Seed, 1.0f);
			ffx::DrawItem& l = Add(MatSlot::Metal, &ffx::meshlib::Lance(), Vec3(1.4f, 0.2f, 0.0f),
			                       ffx::Basis::AxisAngle(Vec3(0.0f, 0.0f, 1.0f), 0.4f).Scaled(0.5f, 1.2f, 0.5f));
			l.params.Set(P::Heat, 0.0f);
			ffx::DrawItem& h2 = Add(MatSlot::Metal, &ffx::meshlib::Rod(), Vec3(2.2f, 0.2f, 0.0f),
			                        ffx::Basis::AxisAngle(Vec3(0.0f, 0.0f, 1.0f), -0.4f).Scaled(0.5f, 1.2f, 0.5f));
			h2.params.Set(P::Heat, 0.85f);
		}
		Shoot("g07_crystal_metal", Vec3(0.0f, 1.5f, 4.4f), Vec3(0.0f, 0.6f, -0.2f), 1.0f, w, h, 50.0f);
		// 8. air: crescent, pressure cone, vortex funnel layers
		{
			ffx::DrawItem& c = Add(MatSlot::Wind, &ffx::meshlib::Crescent(), Vec3(-2.0f, 1.0f, 0.0f),
			                       ffx::Basis::FromFwdUp(Vec3(0.0f, 0.0f, 1.0f), Vec3(0.3f, 1.0f, 0.0f)).Scaled(0.9f));
			c.params.Set(P::Style, 0.0f);
			c.params.Set(P::Dusty, 0.25f);
			c.params.Set(P::Opacity, 1.0f);
			c.params.Set(P::Cover, 0.5f);
			c.params.Set(P::Scroll, 0.7f);
			c.params.Set(P::Age, 0.5f);
			c.params.Set(PV::Color, ffx::Linear(cfg.DustColor(ffx::Fam::Wind)));
			ffx::DrawItem& p = Add(MatSlot::Wind, &ffx::meshlib::Cone(), Vec3(-0.6f, 1.0f, 0.0f),
			                       ffx::Basis::FromUpFwd(Vec3(1.0f, 0.0f, 0.2f), Vec3(0.0f, 1.0f, 0.0f)).Scaled(0.7f, 2.2f, 0.7f));
			p.params.Set(P::Style, 1.0f);
			p.params.Set(P::Age, 0.35f);
			p.params.Set(P::Opacity, 0.55f);
			ffx::MeshData& outer = NewMesh();
			ffx::MeshData& inner = NewMesh();
			const int rows = 16;
			float rad[rows], ys[rows], rad2[rows], ys2[rows];
			for (int i = 0; i < rows; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(rows - 1);
				rad[i] = ffx::Lerp(0.25f, 1.1f, std::pow(t, 1.35f)) + 0.4f * std::pow(1.0f - t, 6.0f);
				ys[i] = t * 2.6f;
				rad2[i] = ffx::Lerp(0.14f, 0.68f, std::pow(t, 1.35f)) + 0.16f * std::pow(1.0f - t, 6.0f);
				ys2[i] = t * 2.4f;
			}
			ffx::meshlib::BuildLathe(outer, rad, ys, rows, 22, true);
			ffx::meshlib::BuildLathe(inner, rad2, ys2, rows, 22, true);
			const ffx::VortexLook& vl = cfg.Vortex(ffx::Infusion::None);
			for (int layer = 0; layer < 2; ++layer) {
				ffx::DrawItem& v = Add(MatSlot::Vortex, layer ? &inner : &outer, Vec3(1.8f, 0.0f, -0.5f));
				v.params.Set(PV::Color, ffx::Linear(vl.a));
				v.params.Set(PV::Color2, ffx::Linear(vl.b));
				v.params.Set(P::Opacity, vl.opacity * (layer ? 1.0f : 0.8f));
				v.params.Set(P::Cover, vl.cover);
				v.params.Set(P::Glow, vl.glow);
				v.params.Set(P::Phase, layer ? 1.8f : 1.0f);
				v.params.Set(P::Scroll, 1.0f);
				v.params.Set(P::Height, 2.6f);
				v.params.Set(P::Radius, 1.1f);
				v.sortPriority = layer ? 2 : 1;
			}
		}
		Shoot("g08_air", Vec3(0.0f, 1.6f, 4.8f), Vec3(0.0f, 1.0f, -0.2f), 1.0f, w, h, 50.0f);
		// 9. shells and rings
		{
			const ffx::ShellStyle styles[4] = {ffx::ShellStyle::NullBubble, ffx::ShellStyle::Corona, ffx::ShellStyle::StaticField,
			                                   ffx::ShellStyle::Aura};
			for (int i = 0; i < 4; ++i) {
				const ffx::ShellLook& sl = cfg.Shell(styles[i]);
				const ffx::Color col = sl.color.a > 0.0f ? sl.color : cfg.MatColor(sl.fam);
				ffx::DrawItem& it = Add(MatSlot::Shell, &ffx::meshlib::Sphere(2), Vec3(-2.4f + 1.25f * static_cast<float>(i), 0.9f, -0.3f),
				                        ffx::Basis::Identity().Scaled(0.55f));
				it.params.Set(PV::Color, ffx::Linear(col));
				it.params.Set(PV::Color2, ffx::Linear(sl.coreColor.a > 0.0f ? sl.coreColor : col));
				it.params.Set(P::Rim, sl.rim);
				it.params.Set(P::Streak, sl.streak);
				it.params.Set(P::Crackle, sl.crackle);
				it.params.Set(P::Core, sl.core);
				it.params.Set(P::Pulse, sl.pulse);
				it.params.Set(P::Opacity, sl.opacity);
				it.params.Set(P::Glow, sl.glow);
				it.params.Set(P::Absorb, sl.absorb);
				it.params.Set(P::Phase, 0.7f);
				it.params.Set(P::Cover, 0.5f);
			}
			for (int st = 0; st < 5; ++st) {
				ffx::DrawItem& it = Add(MatSlot::Ring, &ffx::meshlib::GroundQuad(), Vec3(-2.4f + 1.2f * static_cast<float>(st), 0.02f, 1.0f),
				                        ffx::Basis::Identity().Scaled(0.5f));
				it.params.Set(P::Style, static_cast<float>(st));
				it.params.Set(PV::Color, ffx::Linear(ffx::Color(0.9f, 0.8f, 0.6f)));
				it.params.Set(P::Radius, st == 4 ? 0.5f : 0.75f);
				it.params.Set(P::Width, 0.08f);
				it.params.Set(P::Opacity, 1.0f);
				it.params.Set(P::Cover, 0.4f);
				it.params.Set(P::Glow, 1.2f);
				it.params.Set(P::Phase, 0.4f);
			}
		}
		Shoot("g09_shells_rings", Vec3(0.0f, 2.0f, 4.8f), Vec3(0.0f, 0.5f, 0.0f), 1.0f, w, h, 50.0f);
		// 10. ground decals 0..8 and ground strips (sand, rime, mud)
		{
			for (int st = 0; st < 9; ++st) {
				ffx::DrawItem& it = Add(MatSlot::Ground, &ffx::meshlib::GroundQuad(),
				                        Vec3(-2.4f + 1.2f * static_cast<float>(st % 5), 0.012f, -1.0f + 1.3f * static_cast<float>(st / 5)),
				                        ffx::Basis::Identity().Scaled(0.55f));
				it.params.Set(P::Style, static_cast<float>(st));
				it.params.Set(P::Phase, 0.8f);
				it.params.Set(P::Heat, st == 3 ? 0.8f : 0.0f);
				it.params.Set(P::Seed, static_cast<float>(st) * 0.3f);
			}
			for (int i = 0; i < 3; ++i) {
				ffx::MeshData& m = NewMesh();
				std::vector<Vec3> pts;
				std::vector<float> wd;
				for (int k = 0; k < 10; ++k) {
					const float t = static_cast<float>(k) / 9.0f;
					pts.push_back(Vec3(-1.0f + 2.6f * t, 0.0f, 1.0f + 0.6f * static_cast<float>(i)));
					wd.push_back(0.5f);
				}
				ffx::StripParams sp;
				sp.heightFront = 0.25f;
				sp.heightTail = 0.12f;
				ffx::AppendPathStrip(m, pts, wd, sp);
				m.ComputeTangents();
				ffx::DrawItem& it = Add(MatSlot::GroundStrip, &m, Vec3(1.2f, 0.0f, 0.0f));
				it.params.Set(P::Style, static_cast<float>(i));
				it.params.Set(P::Flow, 0.3f);
			}
		}
		Shoot("g10_ground", Vec3(0.0f, 3.2f, 4.2f), Vec3(0.0f, 0.0f, 0.3f), 1.0f, w, h, 55.0f);
	}
};

// ------------------------------------------------------------------------------------------------ scenes
struct Shot {
	std::string name;
	int element, sub, slot, tier;
	std::vector<int> ticks;   // ticks after the try at which to render
	float camDist = 7.0f;
};

}  // namespace

int main(int argc, char** argv) {
	std::string noisePath, fbDir, outDir = ".", only;
	bool gallery = false;
	int width = 640, height = 360;
	for (int a = 1; a < argc; ++a) {
		const std::string s = argv[a];
		if (s == "--noise" && a + 1 < argc) noisePath = argv[++a];
		else if (s == "--fb" && a + 1 < argc) fbDir = argv[++a];
		else if (s == "--out" && a + 1 < argc) outDir = argv[++a];
		else if (s == "--only" && a + 1 < argc) only = argv[++a];
		else if (s == "--gallery") gallery = true;
		else if (s == "--size" && a + 2 < argc) {
			width = std::atoi(argv[++a]);
			height = std::atoi(argv[++a]);
		}
	}
	std::string err;
	if (!ff::Session::DataOk(&err)) {
		std::printf("core data failed: %s\n", err.c_str());
		return 1;
	}
	TexData noiseData;
	if (noisePath.empty() || !LoadRaw(noisePath, 256, 256, noiseData)) {
		std::printf("need --noise <raw 256x256 RGBA8> (Tools/vfx/noise_textures.py --raw)\n");
		return 1;
	}
	hlsl::Texture2D noiseTex;
	noiseTex.data = &noiseData;
	// flipbooks: <fbDir>/<name>.rgba8 (1024 x 1024; puff_atlas 512 x 512), placeholders otherwise
	std::vector<std::unique_ptr<TexData>> fbData;
	std::vector<std::unique_ptr<hlsl::Texture2D>> fbTex;
	Renderer r;
	r.noise = &noiseTex;
	for (int f = 1; f < ffx::kNumFlipbooks; ++f) {
		auto d = std::make_unique<TexData>();
		const std::string name(ffx::kFlipbookNames[static_cast<size_t>(f)]);
		const int size = static_cast<ffx::Flipbook>(f) == ffx::Flipbook::PuffAtlas ? 512 : 1024;
		if (fbDir.empty() || !LoadRaw(fbDir + "/" + name + ".rgba8", size, size, *d)) {
			const ffx::Flipbook fbk = static_cast<ffx::Flipbook>(f);
			MakePlaceholderAtlas(*d, fbk == ffx::Flipbook::PuffAtlas ? 2 : 8,
			                     fbk == ffx::Flipbook::FireLoop || fbk == ffx::Flipbook::FireBurst || fbk == ffx::Flipbook::Explosion);
		}
		auto t = std::make_unique<hlsl::Texture2D>();
		t->data = d.get();
		r.flipbooks[f] = t.get();
		fbData.push_back(std::move(d));
		fbTex.push_back(std::move(t));
	}

	if (gallery) {
		Gallery g{r, outDir, ffx::FxConfig(), {}, {}, 1};
		g.Run(width, height);
		std::printf("gallery written to %s\n", outDir.c_str());
		return 0;
	}
	// Shots: (element, sub, slot, tier) tried in the Lab, rendered at a few ticks after the input.
	std::vector<Shot> shots;
	const char* kEl[4] = {"earth", "water", "fire", "air"};
	for (int e = 0; e < 4; ++e)
		for (int sub = 0; sub < 4; ++sub)
			for (int slot = 0; slot < 10; ++slot)
				for (int tier : {0, 3}) {
					Shot s;
					char nm[64];
					std::snprintf(nm, sizeof(nm), "%s%d_slot%d_t%d", kEl[e], sub, slot, tier);
					s.name = nm;
					s.element = e;
					s.sub = sub;
					s.slot = slot;
					s.tier = tier;
					// T3 holds the charge 1.8 s (108 ticks) before the release
					s.ticks = tier == 0 ? std::vector<int>{8, 18, 32, 55} : std::vector<int>{60, 116, 132, 160};
					s.camDist = 5.0f;
					shots.push_back(s);
				}
	int rendered = 0;
	for (const Shot& shot : shots) {
		if (!only.empty() && shot.name.find(only) == std::string::npos) continue;
		ff::Session s;
		if (!s.LoadScenario("lab")) return 1;
		s.LabClear();
		s.LabHeal();
		ffx::FxDirector dir;
		ff::Snapshot prev = s.GetSnapshot();
		s.LabTry(shot.element, shot.sub, static_cast<ff::Slot>(shot.slot), shot.tier);
		const std::string move = s.ResolveMove(shot.element, shot.sub, static_cast<ff::Slot>(shot.slot));
		std::vector<ff::Event> events;
		int frameIdx = 0;
		const int maxTick = shot.ticks.back();
		for (int tick = 1; tick <= maxTick; ++tick) {
			prev = s.GetSnapshot();
			ff::InputFrame in;
			s.Step(in, 0.0f);
			events.clear();
			s.TakeEvents(events);
			const ff::Snapshot& cur = s.GetSnapshot();
			// camera: side-on to the player, framing the 6 m in front of him (where the move plays)
			Vec3 a;
			float facing = 0.0f;
			r.actors.clear();
			for (const ff::ActorView& av : cur.actors) {
				r.actors[av.id] = {av.pos, av.facing};
				if (av.is_player) {
					a = av.pos;
					facing = av.facing;
				}
			}
			const Vec3 dir2(std::sin(facing), 0.0f, std::cos(facing));
			const Vec3 side(dir2.z, 0.0f, -dir2.x);
			const Vec3 camPos = a + side * shot.camDist + dir2 * 2.6f + Vec3(0.0f, 2.1f, 0.0f);
			const Vec3 target = a + dir2 * 3.0f + Vec3(0.0f, 0.8f, 0.0f);
			ffx::FxFrameIn fi;
			fi.prev = &prev;
			fi.curr = &cur;
			fi.alpha = 1.0f;
			fi.events = &events;
			fi.dt = 1.0f / 60.0f;
			fi.arena = &s.Arena();
			fi.quality = 2;
			fi.cam.pos = camPos;
			fi.cam.fwd = ffx::Norm(target - camPos);
			const ffx::DrawList& dl = dir.Update(fi);
			r.time = static_cast<float>(tick) / 60.0f;
			if (std::find(shot.ticks.begin(), shot.ticks.end(), tick) == shot.ticks.end()) continue;
			if (std::getenv("FFX_DUMP")) {
				for (const ffx::DrawItem& it : dl.items)
					std::printf("  tick %d key %u %s pos (%.2f %.2f %.2f) scale (%.2f %.2f %.2f) verts %d attach %d\n", tick, it.key,
					            std::string(ffx::MatSlotName(it.mat)).c_str(), it.xform.pos.x, it.xform.pos.y, it.xform.pos.z,
					            it.xform.basis.x.length(), it.xform.basis.y.length(), it.xform.basis.z.length(),
					            it.mesh ? it.mesh->NumVerts() : 0, it.attachActor);
				for (const ff::BodyView& b : cur.bodies)
					std::printf("  body %d mat %d form %d tag %s radius %.2f zone %.2f liquid %.2f\n", b.id, static_cast<int>(b.mat),
					            static_cast<int>(b.form), b.tag.c_str(), b.radius, b.zone_radius, b.liquid);
			}
			r.cam = MakeCamera(camPos, target, width, height, 50.0f);
			r.Begin();
			r.SetLights(dl);
			r.DrawGround();
			for (const ff::ActorView& av : cur.actors) r.DrawActor(av.pos);
			r.DrawItems(dl);
			char path[512];
			std::snprintf(path, sizeof(path), "%s/%s_%d.ppm", outDir.c_str(), shot.name.c_str(), frameIdx++);
			r.WritePPM(path);
			++rendered;
		}
		std::printf("%s (%s): %d frames\n", shot.name.c_str(), move.c_str(), frameIdx);
	}
	std::printf("rendered %d frames\n", rendered);
	return 0;
}

#endif  // FF_LOGIC_TESTS
