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

double WorldDef::HeightAt(double x, double z) const {
	if (nx < 2 || nz < 2 || heights.size() < static_cast<size_t>(nx) * static_cast<size_t>(nz)) return 0.0;
	double fx = (x - x0) / cell;
	double fz = (z - z0) / cell;
	fx = fx < 0.0 ? 0.0 : (fx > nx - 1 ? nx - 1 : fx);
	fz = fz < 0.0 ? 0.0 : (fz > nz - 1 ? nz - 1 : fz);
	int i = static_cast<int>(fx);
	int k = static_cast<int>(fz);
	if (i > nx - 2) i = nx - 2;
	if (k > nz - 2) k = nz - 2;
	const double tx = fx - i;
	const double tz = fz - k;
	const size_t row = static_cast<size_t>(nx);
	const size_t b = static_cast<size_t>(k) * row + static_cast<size_t>(i);
	const double h00 = heights[b];
	const double h10 = heights[b + 1];
	const double h01 = heights[b + row];
	const double h11 = heights[b + row + 1];
	return (h00 * (1.0 - tx) + h10 * tx) * (1.0 - tz) + (h01 * (1.0 - tx) + h11 * tx) * tz;
}

double WorldDef::FloorAt(double x, double z) const {
	const double h = HeightAt(x, z);
	if (h < water_level) {
		const double wade = water_level - wade_depth;
		return h > wade ? h : wade;
	}
	return h;
}

bool WorldDef::OnMetal(double x, double z) const {
	for (const WorldRect& r : metal)
		if (x > r.min.x && x < r.max.x && z > r.min.y && z < r.max.y) return true;
	return false;
}

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

}  // namespace ff
