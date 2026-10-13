// Fourfold core - open world definition (ff/OpenWorld.h): terrain sampling + world.json parsing.
#include "ff/OpenWorld.h"

#include "ff/Json.h"
#include "Util/GdUtil.h"

#include <cmath>
#include <cstring>

namespace ff {

namespace {
Vec3 arr3(const Value& v, Vec3 def = Vec3()) {
	if (v.type() == Value::Type::Vec3) return v.as_vec3();
	const Array a = v.as_array();
	if (a.size() < 3) return def;
	return Vec3(static_cast<float>(vnum(a.get(0))), static_cast<float>(vnum(a.get(1))), static_cast<float>(vnum(a.get(2))));
}

Vec2 arr2(const Value& v) {
	const Array a = v.as_array();
	if (a.size() < 2) return Vec2();
	return Vec2(static_cast<float>(vnum(a.get(0))), static_cast<float>(vnum(a.get(1))));
}

bool fail(std::string* error, const std::string& msg) {
	if (error) *error = msg;
	return false;
}
}  // namespace

bool WorldDef::Parse(std::string_view json, const void* heights_blob, size_t bytes, WorldDef& out, std::string* error) {
	Value root;
	JsonError je;
	if (!ParseJson(json, root, &je)) return fail(error, "world.json: parse error");
	const Dict d = root.as_dict();
	const Dict t = ddict(d, "terrain");
	WorldDef w;
	w.nx = dint(t, "nx");
	w.nz = dint(t, "nz");
	w.x0 = dnum(t, "x0");
	w.z0 = dnum(t, "z0");
	w.cell = dnum(t, "cell", 2.0);
	if (w.nx < 2 || w.nz < 2 || w.cell <= 0.0) return fail(error, "world.json: bad terrain grid");
	const size_t n = static_cast<size_t>(w.nx) * static_cast<size_t>(w.nz);
	if (heights_blob == nullptr || bytes != n * sizeof(float)) return fail(error, "heightfield: expected " + std::to_string(n * 4) + " bytes");
	w.heights.resize(n);
	std::memcpy(w.heights.data(), heights_blob, bytes);   // little-endian float32 (every target platform)
	const Dict wa = ddict(d, "water");
	w.water_level = dnum(wa, "level", -1.0e9);
	w.wade_depth = dnum(wa, "wade_depth", 0.85);
	for (const Value& v : darr(d, "solids")) {
		const Dict s = v.as_dict();
		w.solids.push_back(WorldBox{arr3(s.get("min")), arr3(s.get("max")), dstr(s, "kind", "wall"), dstr(s, "surface", "stone"), dstr(s, "name")});
	}
	for (const Value& v : darr(d, "metal")) {
		const Dict s = v.as_dict();
		w.metal.push_back(WorldRect{arr2(s.get("min")), arr2(s.get("max"))});
	}
	for (const Value& v : darr(d, "sites")) {
		const Dict s = v.as_dict();
		EncounterSite e;
		e.id = dstr(s, "id");
		e.name = dstr(s, "name", "Rival");
		e.region = dstr(s, "region");
		e.pos = arr3(s.get("pos"));
		e.facing = static_cast<float>(dnum(s, "facing"));
		e.element = dint(s, "element");
		e.sub = dint(s, "sub", -1);
		e.preset = dstr(s, "preset", "adept");
		e.aggro_radius = static_cast<float>(dnum(s, "aggro_radius", 9.0));
		if (e.element < 0 || e.element > 3) return fail(error, "site " + e.id + ": bad element");
		w.sites.push_back(e);
	}
	for (const Value& v : darr(d, "shrines")) {
		const Dict s = v.as_dict();
		w.shrines.push_back(Shrine{dstr(s, "id"), dstr(s, "name"), arr3(s.get("pos"))});
	}
	const Dict sp = ddict(d, "spawn");
	w.player_spawn = arr3(sp.get("pos"), Vec3(static_cast<float>(w.x0 + w.SizeX() * 0.5), 0.0f, static_cast<float>(w.z0 + w.SizeZ() * 0.5)));
	w.spawn_facing = static_cast<float>(dnum(sp, "facing"));
	out = std::move(w);
	return true;
}

// ------------------------------------------------------------------------------------------------ terrain edits
namespace {
constexpr double kPiD = 3.14159265358979323846;

struct EditSample {
	int i, k;
	double w;
};

// Adds v to sample (i, k) (clamped); returns the change actually applied.
double edit_add(WorldDef& w, int i, int k, double v) {
	TerrainEdits& e = *w.edits;
	if (e.delta.empty()) e.delta.assign(static_cast<size_t>(w.nx) * static_cast<size_t>(w.nz), 0.0f);
	float& d = e.delta[static_cast<size_t>(k) * static_cast<size_t>(w.nx) + static_cast<size_t>(i)];
	const float before = d;
	float after = static_cast<float>(before + v);
	after = after < TerrainEdits::kMinDelta ? TerrainEdits::kMinDelta : (after > TerrainEdits::kMaxDelta ? TerrainEdits::kMaxDelta : after);
	d = after;
	const int ci[2] = {i / TerrainEdits::kChunkCells, (i - 1) / TerrainEdits::kChunkCells};
	const int ck[2] = {k / TerrainEdits::kChunkCells, (k - 1) / TerrainEdits::kChunkCells};
	for (int a = 0; a < 2; ++a)
		for (int b = 0; b < 2; ++b) {
			if (ci[a] < 0 || ck[b] < 0 || ci[a] >= e.chunks_x || ck[b] >= e.chunks_z) continue;
			e.chunk_rev[static_cast<size_t>(ck[b]) * static_cast<size_t>(e.chunks_x) + static_cast<size_t>(ci[a])] += 1u;
		}
	return static_cast<double>(after) - static_cast<double>(before);
}

// Deterministic per-sample roughness in [-1, 1] (integer hash): broken, craggy raised earth instead of smooth hills.
double crag(int i, int k) {
	uint32_t h = static_cast<uint32_t>(i) * 73856093u ^ static_cast<uint32_t>(k) * 19349663u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return (static_cast<double>(h & 0xffffu) / 32767.5) - 1.0;
}

// Samples within `reach` metres of (x, z) (clipped to the grid), visited row by row (deterministic order).
template <typename F>
void for_samples(const WorldDef& w, double x0, double z0, double x1, double z1, F&& f) {
	const int i0 = static_cast<int>(std::floor((x0 - w.x0) / w.cell));
	const int i1 = static_cast<int>(std::ceil((x1 - w.x0) / w.cell));
	const int k0 = static_cast<int>(std::floor((z0 - w.z0) / w.cell));
	const int k1 = static_cast<int>(std::ceil((z1 - w.z0) / w.cell));
	for (int k = k0 < 1 ? 1 : k0; k <= k1 && k < w.nz - 1; ++k)
		for (int i = i0 < 1 ? 1 : i0; i <= i1 && i < w.nx - 1; ++i) f(i, k, w.x0 + i * w.cell, w.z0 + k * w.cell);
}

// Applies `cut` (signed heights per sample) and spreads the opposite volume over `fill` weights.
double cut_and_fill(WorldDef& w, const std::vector<EditSample>& cut, const std::vector<EditSample>& fill) {
	double moved = 0.0;
	for (const EditSample& c : cut) moved += edit_add(w, c.i, c.k, c.w);
	double wsum = 0.0;
	for (const EditSample& f : fill) wsum += f.w;
	if (wsum > 1e-9)
		for (const EditSample& f : fill) edit_add(w, f.i, f.k, -moved * f.w / wsum);
	if (w.edits) w.edits->revision += 1;
	return std::fabs(moved) * w.cell * w.cell;
}
}  // namespace

double WorldDef::EditCrater(double x, double z, double r, double depth) {
	if (!edits || r <= 0.0) return 0.0;
	std::vector<EditSample> cut, fill;
	const double reach = r * 1.6;
	for_samples(*this, x - reach, z - reach, x + reach, z + reach, [&](int i, int k, double sx, double sz) {
		const double d = std::sqrt((sx - x) * (sx - x) + (sz - z) * (sz - z)) / r;
		if (d < 1.0) cut.push_back({i, k, -depth * (1.0 - d * d)});
		else if (d < 1.6) fill.push_back({i, k, std::sin(kPiD * (d - 1.0) / 0.6)});
	});
	return cut_and_fill(*this, cut, fill);
}

double WorldDef::EditRidge(double ax, double az, double bx, double bz, double width, double height) {
	if (!edits || width <= 0.0) return 0.0;
	std::vector<EditSample> raise, trench;
	const double half = width * 0.5;
	const double reach = width * 1.75;
	const double dx = bx - ax, dz = bz - az;
	const double len2 = dx * dx + dz * dz;
	const double lo_x = (ax < bx ? ax : bx) - reach, hi_x = (ax > bx ? ax : bx) + reach;
	const double lo_z = (az < bz ? az : bz) - reach, hi_z = (az > bz ? az : bz) + reach;
	for_samples(*this, lo_x, lo_z, hi_x, hi_z, [&](int i, int k, double sx, double sz) {
		double t = len2 > 1e-9 ? ((sx - ax) * dx + (sz - az) * dz) / len2 : 0.0;
		t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		const double px = ax + dx * t - sx, pz = az + dz * t - sz;
		const double dd = std::sqrt(px * px + pz * pz);
		if (dd < half) {
			const double profile = 0.5 + 0.5 * std::cos(kPiD * dd / half);
			raise.push_back({i, k, height * profile * (1.0 + 0.32 * crag(i, k))});
		}
		else if (dd < reach) trench.push_back({i, k, std::sin(kPiD * (dd - half) / (reach - half))});
	});
	return cut_and_fill(*this, raise, trench);
}

double WorldDef::EditMound(double x, double z, double r, double h) {
	if (!edits || r <= 0.0) return 0.0;
	double moved = 0.0;
	for_samples(*this, x - r, z - r, x + r, z + r, [&](int i, int k, double sx, double sz) {
		const double d = std::sqrt((sx - x) * (sx - x) + (sz - z) * (sz - z)) / r;
		if (d < 1.0) moved += edit_add(*this, i, k, h * (0.5 + 0.5 * std::cos(kPiD * d)));
	});
	edits->revision += 1;
	return std::fabs(moved) * cell * cell;
}

}  // namespace ff
