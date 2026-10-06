// Fourfold core - port of game/core/materials.gd (values identical; sim.json "materials" mirrors them).
#include "Sim/Materials.h"

#include "Sim/MatBody.h"
#include "Util/GdUtil.h"

namespace ff {
namespace Materials {
namespace {

// c, melt, latent, max_temp, hardness, hardness_frozen, ignite, burn_rate, fire_decay,
// conductive, brittle, porous, flammable, magnetic, insulator
const Props kMatProps[9] = {
	{0.01, 1000.0, 10.0, 1350.0, 0.25, -1.0, -1.0, 0.0, 0.0, false, false, false, false, false, false},   // stone
	{0.05, 0.0, 3.3, 100.0, 1.0, 0.44, -1.0, 0.0, 0.0, true, false, false, false, false, false},          // water
	{0.0, 0.0, 0.0, 100.0, 0.0, -1.0, -1.0, 0.0, 0.0, false, false, false, false, false, false},          // steam
	{0.02, 1200.0, 8.0, 1600.0, 3.3, -1.0, -1.0, 0.0, 0.0, true, false, false, false, true, false},       // metal
	{0.01, 1200.0, 10.0, 1600.0, 0.25, -1.0, -1.0, 0.0, 0.0, false, false, true, false, false, true},     // sand
	{0.01, 1200.0, 10.0, 1600.0, 0.30, -1.0, -1.0, 0.0, 0.0, false, true, false, false, false, true},     // glass
	{0.04, 1.0e9, 0.0, 600.0, 0.4, -1.0, 250.0, 1.5, 0.0, false, false, true, true, false, false},        // plant
	{0.0, 0.0, 0.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.35, false, false, false, false, false, false},           // fire
	{0.0, 0.0, 0.0, 0.0, 0.0, -1.0, -1.0, 0.0, 0.0, false, false, false, false, false, false},            // air
};

struct TagHard {
	const char* tag;
	double h;
};
const TagHard kTagHardness[] = {{"obsidian", 0.33}, {"glass", 0.30}, {"ice", 0.44}, {"vine", 0.4},
                                {"mud", 0.25},      {"plate", 3.3},  {"sand", 0.25}};
const char* const kBrittleTags[] = {"obsidian", "glass", "ice", "crust"};
const char* const kConductiveTags[] = {"caltrops", "rod", "plate"};
const char* const kInsulatingTags[] = {"vacuum", "vacuum_well", "null_bubble", "ice_floor", "glass", "ice", "sand"};

}  // namespace

const Props& props(Mat m) {
	const int i = static_cast<int>(m);
	return kMatProps[(i >= 0 && i < 9) ? i : 0];
}

Value prop(int mat, std::string_view key, const Value& def) {
	if (mat < 0 || mat > 8) return def;
	const Props& p = kMatProps[mat];
	if (key == "c") return p.c;
	if (key == "melt") return p.melt;
	if (key == "latent") return p.latent;
	if (key == "max_temp") return p.max_temp;
	if (key == "hardness") return p.hardness;
	if (key == "hardness_frozen") return p.hardness_frozen >= 0.0 ? Value(p.hardness_frozen) : def;
	if (key == "ignite") return p.ignite >= 0.0 ? Value(p.ignite) : def;
	if (key == "burn_rate") return mat == static_cast<int>(Mat::Plant) ? Value(p.burn_rate) : def;
	if (key == "fire_decay") return mat == static_cast<int>(Mat::Fire) ? Value(p.fire_decay) : def;
	if (key == "conductive") return p.conductive;
	if (key == "brittle") return p.brittle;
	if (key == "porous") return p.porous;
	if (key == "flammable") return p.flammable;
	if (key == "magnetic") return p.magnetic;
	if (key == "insulator") return p.insulator;
	return def;
}

bool is_fusible(Mat m) { return m == Mat::Stone || m == Mat::Metal || m == Mat::Sand || m == Mat::Glass; }
double c(Mat m) { return props(m).c; }
double melt(Mat m) { return props(m).melt; }
double latent(Mat m) { return props(m).latent; }
double max_temp(Mat m) { return props(m).max_temp; }

double tag_hardness(std::string_view tag, bool* found) {
	for (const TagHard& t : kTagHardness)
		if (tag == t.tag) {
			if (found) *found = true;
			return t.h;
		}
	if (found) *found = false;
	return 0.0;
}

bool is_brittle_tag(std::string_view tag) { return in_list(tag, kBrittleTags); }
bool is_conductive_tag(std::string_view tag) { return in_list(tag, kConductiveTags); }
bool is_insulating_tag(std::string_view tag) { return in_list(tag, kInsulatingTags); }

double hardness(const MatBody& b) {
	if (b.hardness >= 0.0) return b.hardness;
	bool found = false;
	const double h = tag_hardness(b.tag, &found);
	if (found) return h;
	if (b.mat == Mat::Water && b.phase == Phase::Frozen) return kMatProps[1].hardness_frozen;
	return props(b.mat).hardness;
}

bool is_brittle(const MatBody& b) {
	if (is_brittle_tag(b.tag)) return true;
	if (b.mat == Mat::Water && b.phase == Phase::Frozen) return true;
	return props(b.mat).brittle;
}

bool conducts(const MatBody& b) {
	if (b.charge > 0.0 || dbool(b.props, "conducts", false)) return true;
	if (is_conductive_tag(b.tag)) return true;
	if (b.mat == Mat::Metal) return true;
	if (b.mat == Mat::Water && b.phase == Phase::Liquid) return true;
	return b.tag == "fog";
}

double conduction_factor(const MatBody& b) { return b.tag == "fog" ? FOG_CONDUCTION : 1.0; }

bool insulates(const MatBody& b) {
	if (is_insulating_tag(b.tag)) return true;
	if (b.mat == Mat::Water && b.phase == Phase::Frozen) return true;
	return props(b.mat).insulator;
}

bool is_flammable(const MatBody& b) { return props(b.mat).flammable; }
bool is_magnetic(const MatBody& b) { return props(b.mat).magnetic; }

}  // namespace Materials
}  // namespace ff
