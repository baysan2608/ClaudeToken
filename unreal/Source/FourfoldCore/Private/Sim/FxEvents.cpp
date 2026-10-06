// Fourfold core - port of game/core/fx_events.gd.
#include "Sim/FxEvents.h"

#include "Sim/ActorState.h"
#include "Sim/CombatWorld.h"
#include "Sim/MatBody.h"
#include "Util/GdUtil.h"

namespace ff {
namespace FxEvents {
namespace {

const char* const kFxKeys[] = {"cast", "release", "cone", "beam", "burst", "ring", "erupt", "trail", "splash", "aura"};
const char* const kFxMats[] = {"stone", "metal", "sand",  "glass", "magma", "water", "ice",    "mist",   "steam",
                               "plant", "flame", "blue",  "lightning", "blast", "wind", "vortex", "vacuum", "sound"};
const char* const kFxShapes[] = {
	"spear", "fan",  "disc",   "lance",    "rod",      "plate",   "fireball", "comet",    "ember",  "crescent", "spiral",
	"needles", "seed", "ground", "down",   "small",    "open",    "short",    "split",    "freeze", "condense", "compress",
	"cool",  "reforge", "retag", "flight", "glide",    "surf",    "skate",    "hover",    "burrow", "run",      "walk",
	"roots", "stone_skin", "iron", "anchor", "grounding", "stance", "wind", "storm",   "sound",  "vacuum"};
const char* const kTagsProjectile[] = {"spear", "rubble", "crag", "disc", "lance", "rod", "plate", "caltrops", "slug", "glob", "bomb",
                                       "fireball", "comet", "ember", "crescent", "twister", "spiral", "needle", "seed", "orb", "block"};
const char* const kTagsWave[] = {"", "water_wave", "sand_surge", "rime", "fire_line", "ground_current", "roots", "tremor",
                                 "dust_line", "wind_wall", "funnel", "magma_rift", "spike_line"};
const char* const kTagsWall[] = {"", "obsidian", "glass", "sand", "mud", "ice", "vine", "plate", "ridge", "spikes"};
const char* const kTagsZone[] = {"fog", "mist", "steam", "sand_cloud", "sandstorm", "fire_field", "quicksand", "ice_floor", "mud",
                                 "caltrops", "tornado", "vacuum_well", "null_bubble", "mine", "melt_pit", "corona", "eddy", "briar",
                                 "geyser", "static_field", "wind_guard", "vortex_wall", "sound_barrier", "steam_screen",
                                 "lava_pool", "fuse", "flight_field", "inrush"};
const char* const kEventKinds[] = {"charge", "fx", "interaction", "status", "zone", "morph", "clash"};
const char* const kOutcomeNames[] = {"block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter",
                                     "sink",  "conduct", "ground",   "pass",    "amplify", "extinguish", "weaken", "bend", "slow",
                                     "overwhelm", "clash", "disrupt", "neutralize", "heat", "push", "disperse"};

}  // namespace

bool is_known(std::string_view kind, std::string_view key) {
	if (kind == "fx") return in_list(key, kFxKeys);
	if (kind == "mat") return in_list(key, kFxMats);
	if (kind == "shape") return key.empty() || in_list(key, kFxShapes);
	if (kind == "tag")
		return in_list(key, kTagsProjectile) || in_list(key, kTagsWave) || in_list(key, kTagsWall) || in_list(key, kTagsZone);
	if (kind == "event") return in_list(key, kEventKinds);
	if (kind == "outcome") return in_list(key, kOutcomeNames);
	return false;
}

void fx(CombatWorld& w, const std::string& fx_key, const std::string& mat, const Dict& d) {
	if (!w._record_events) return;
	// seed: (tick * 2654435761 + body * 97 + actor) & 0x7fffffff (GDScript 64-bit int arithmetic)
	const int64_t body = d.has("body") ? d.get("body").as_int() : 0;
	const int64_t actor = d.has("actor") ? d.get("actor").as_int() : 0;
	const int64_t seed = (w.tick * 2654435761LL + body * 97 + actor) & 0x7fffffffLL;
	Dict e = D({{"fx", fx_key},       {"mat", mat},   {"shape", ""},     {"actor", -1},     {"body", -1},    {"element", -1},
	            {"sub", 0},           {"move", ""},   {"tier", 0},       {"pos", Vec3()},   {"dir", Vec3()}, {"radius", 0.0},
	            {"length", 0.0},      {"angle", 0.0}, {"height", 0.0},   {"dur", 0.0},      {"power", 0.0},  {"seed", seed}});
	e.merge(d, true);
	w.emit("fx", e);
}

void fx_for(CombatWorld& w, const ActorState& a, const ActionInst& inst, const std::string& fx_key, const std::string& mat, const Dict& d) {
	Dict base = D({{"actor", a.id},
	               {"element", inst.element},
	               {"sub", inst.sub},
	               {"move", inst.id},
	               {"tier", inst.tier()},
	               {"pos", a.hand_point()},
	               {"dir", inst.data.get("face", Value(a.forward()))}});
	base.merge(d, true);
	fx(w, fx_key, mat, base);
}

void charge(CombatWorld& w, const ActorState& a, const ActionInst& inst, int tier, bool ready) {
	w.emit("charge", D({{"actor", a.id}, {"move", inst.id}, {"element", inst.element}, {"sub", inst.sub}, {"tier", tier},
	                    {"ready", ready}, {"slot", inst.slot}}));
}

void zone(CombatWorld& w, const MatBody& b, const std::string& phase) {
	w.emit("zone", D({{"body", b.id}, {"kind", b.tag}, {"phase", phase}, {"radius", b.zone_radius}, {"owner", b.owner},
	                  {"pos", b.pos}, {"tier", b.tier}}));
}

void status(CombatWorld& w, const ActorState& a, const std::string& nm, bool on, double t, double mag) {
	w.emit("status", D({{"actor", a.id}, {"status", nm}, {"on", on}, {"t", t}, {"mag", mag}}));
}

std::string mat_of(const MatBody& b) {
	switch (b.mat) {
		case Mat::Stone:
			if (b.tag == "obsidian") return "stone";
			return b.liquid > 0.0 ? "magma" : "stone";
		case Mat::Water:
			if (b.phase == Phase::Frozen) return "ice";
			if (b.form == Form::Cloud || b.form == Form::Zone) return "mist";
			return "water";
		case Mat::Steam: return "steam";
		case Mat::Metal: return "metal";
		case Mat::Sand: return "sand";
		case Mat::Glass: return "glass";
		case Mat::Plant: return "plant";
		case Mat::Fire: return (b.tag == "comet" || dtruthy(b.props, "blue")) ? "blue" : "flame";
		case Mat::Air:
			if (in_list(b.tag, {"tornado", "twister", "eddy", "funnel", "vortex_wall"})) return "vortex";
			if (in_list(b.tag, {"vacuum_well", "null_bubble", "mine"})) return "vacuum";
			if (in_list(b.tag, {"tremor", "sound_barrier"})) return "sound";
			return "wind";
	}
	return "stone";
}

}  // namespace FxEvents
}  // namespace ff
