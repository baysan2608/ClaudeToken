// Fourfold core tests - the data-only "test kit" of game/tests/sim/test_core_verbs.gd (register_test_kit): one move per
// verb on every slot of sub-elements 1-3 of every element. Shared by test_core_verbs and ff_perf (perf_core.gd).
#include "test_kit.h"

#include "Combat/Moves.h"
#include "Sim/Interactions.h"
#include "Sim/Sim.h"
#include "Util/GdUtil.h"

#include <string>
#include <utility>
#include <vector>

using namespace ff;

namespace fft {
namespace {
Dict _t(const Dict& extra = Dict()) {
	Dict t = D({{"t1", D({{"power", 11.0}, {"damage", 10.0}})}, {"t2", D({{"power", 18.0}, {"damage", 13.0}})}, {"t3", D({{"power", 28.0}, {"damage", 16.0}})}});
	for (const auto& kv : extra) {
		Dict row = t.get(kv.first).as_dict();
		for (const auto& f : kv.second.as_dict()) row.set(f.first, f.second);
	}
	return t;
}
}  // namespace

// The test kit: data-only moves on every verb.
void register_test_kit() {
	std::vector<std::pair<std::string, Dict>> d;
	// Earth-like (element 0, sub 1)
	d.push_back({"tk_disc", D({{"element", 0}, {"verb", "projectile"}, {"startup", 0.16}, {"active", 0.05}, {"recovery", 0.24}, {"cancel", 0.6}, {"chain", 0.25},
	                           {"cost", 4.0}, {"metal", 0.0}, {"source", "metal"}, {"mat", "metal"}, {"mass", 2.0}, {"speed", 24.0}, {"homing", 10.0}, {"tag", "disc"},
	                           {"tiers", _t(D({{"t1", D({{"count", 2}})}, {"t2", D({{"count", 3}, {"ricochet", 1}})}, {"t3", D({{"count", 4}, {"pierce", 1}})}}))},
	                           {"fx", D({{"mat", "metal"}})}})});
	d.push_back({"tk_spear", D({{"element", 0}, {"verb", "projectile"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3}, {"cost", 6.0}, {"source", "ground"},
	                            {"mat", "stone"}, {"mass", 12.0}, {"speed", 26.0}, {"gravity", 0.4}, {"tag", "spear"}, {"on_impact", "stick"},
	                            {"tiers", _t(D({{"t3", D({{"on_impact", "shatter"}})}}))}})});
	d.push_back({"tk_surge", D({{"element", 0}, {"verb", "ground_line"}, {"startup", 0.25}, {"active", 0.05}, {"recovery", 0.3}, {"cost", 8.0}, {"source", "ground"},
	                            {"mat", "sand"}, {"mass", 10.0}, {"tag", "sand_surge"}, {"speed", 9.0}, {"budget", 10.0}, {"width", 2.0}, {"leave_zone", "quicksand"},
	                            {"zone_life", 2.0}, {"tiers", _t(D({{"t2", D({{"width", 2.5}})}}))}, {"fx", D({{"mat", "sand"}, {"release", "erupt"}})}})});
	d.push_back({"tk_fan", D({{"element", 0}, {"verb", "cone"}, {"startup", 0.15}, {"active", 0.1}, {"recovery", 0.25}, {"cost", 5.0}, {"cls", "sand"}, {"range", 5.0},
	                          {"angle", 40.0}, {"power", 6.0}, {"status", "blinded"}, {"status_t", 0.6}, {"tiers", _t()}, {"fx", D({{"mat", "sand"}})}})});
	d.push_back({"tk_dune", D({{"element", 0}, {"verb", "barrier"}, {"barrier", "wall"}, {"mat", "sand"}, {"tag", "sand"}, {"mass", 100.0}, {"cost", 7.0},
	                           {"tiers", D({{"t1", D({{"mass", 130.0}})}, {"t2", D({{"mass", 160.0}})}})}, {"counter", D({{"cls", "wall_sand"}})}})});
	d.push_back({"tk_push", D({{"element", 0}, {"verb", "burst"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 4.0}, {"at", "ahead"},
	                           {"distance", 2.5}, {"radius", 2.0}, {"power", 12.0}, {"fx", D({{"mat", "sand"}})}})});
	d.push_back({"tk_pit", D({{"element", 0}, {"verb", "zone"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 6.0}, {"tag", "quicksand"},
	                          {"radius", 2.5}, {"life", 2.5}, {"at", "ahead"}, {"actor_status", "slowed"}, {"status_t", 0.3}, {"power", 18.0},
	                          {"tiers", _t(D({{"t2", D({{"radius", 3.0}})}}))}})});
	d.push_back({"tk_seize", D({{"element", 0}, {"verb", "grip"}, {"startup", 0.1}, {"active", 0.06}, {"recovery", 0.28}, {"cost", 4.0}, {"ccls", "grip_stone"},
	                            {"reach", 7.5}, {"rip_source", "ground"}, {"rip_mass", 15.0}, {"shape", "split"}, {"pieces", 3}, {"speed", 18.0}})});
	d.push_back({"tk_burrow", D({{"element", 0}, {"verb", "dash"}, {"startup", 0.0}, {"active", 0.25}, {"recovery", 0.1}, {"cost", 5.0}, {"distance", 3.5},
	                             {"burrow", true}, {"trail", "dust"}, {"trail_life", 0.8}})});
	d.push_back({"tk_skin", D({{"element", 0}, {"verb", "stance"}, {"startup", 0.0}, {"active", 0.0}, {"recovery", 0.1}, {"upkeep", 6.0}, {"stance", "stone_skin"},
	                           {"armor", 0.4}, {"anchored", true}, {"anchor_cp", 40.0}})});
	// Water-like (element 1, sub 1)
	d.push_back({"tk_shard", D({{"element", 1}, {"verb", "projectile"}, {"startup", 0.15}, {"active", 0.05}, {"recovery", 0.25}, {"cost", 4.0}, {"source", "moisture"},
	                            {"mat", "water"}, {"frozen", true}, {"mass", 1.0}, {"speed", 26.0}, {"tag", "needle"},
	                            {"tiers", _t(D({{"t1", D({{"mass", 4.0}})}, {"t3", D({{"count", 3}, {"mass", 5.0}})}}))}})});
	d.push_back({"tk_jet", D({{"element", 1}, {"verb", "beam"}, {"startup", 0.12}, {"active", 0.4}, {"recovery", 0.2}, {"cost", 4.0}, {"cls", "water"}, {"range", 9.0},
	                          {"pulse", 0.1}, {"power", 8.0}, {"wet", true}, {"tiers", _t()}})});
	d.push_back({"tk_tide", D({{"element", 1}, {"verb", "ground_line"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3}, {"cost", 8.0}, {"source", "waterskin"},
	                           {"mat", "water"}, {"mass", 4.0}, {"tag", "water_wave"}, {"speed", 9.0}, {"budget", 10.0}, {"width", 2.0}, {"power", 18.0}, {"channel", "K"},
	                           {"tiers", _t()}})});
	d.push_back({"tk_frost", D({{"element", 1}, {"verb", "cone"}, {"startup", 0.12}, {"active", 0.1}, {"recovery", 0.2}, {"cost", 5.0}, {"cls", "frost"}, {"range", 5.0},
	                            {"angle", 45.0}, {"power", 6.0}, {"status", "chilled"}, {"tiers", _t()}, {"fx", D({{"mat", "ice"}})}})});
	d.push_back({"tk_icewall", D({{"element", 1}, {"verb", "barrier"}, {"barrier", "wall"}, {"mat", "water"}, {"tag", "ice"}, {"source", "moisture"}, {"mass", 50.0},
	                              {"cost", 8.0}, {"tiers", D({{"t1", D({{"mass", 65.0}})}, {"t2", D({{"mass", 80.0}})}})}})});
	d.push_back({"tk_orb", D({{"element", 1}, {"verb", "projectile"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 4.0}, {"source", "waterskin"},
	                          {"mat", "water"}, {"mass", 2.0}, {"speed", 14.0}, {"on_impact", "puddle"}})});
	d.push_back({"tk_floor", D({{"element", 1}, {"verb", "zone"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 5.0}, {"tag", "ice_floor"}, {"at", "self"},
	                            {"radius", 3.0}, {"life", 3.0}, {"walk_height", 0.0}, {"friction", 0.15}, {"surface", "ice"}})});
	d.push_back({"tk_fog", D({{"element", 1}, {"verb", "summon"}, {"startup", 0.15}, {"active", 0.0}, {"recovery", 0.2}, {"cost", 5.0}, {"upkeep", 3.0}, {"tag", "fog"},
	                          {"mat", "air"}, {"radius", 3.0}, {"power", 4.0}, {"at", "aim"}, {"range", 9.0}, {"steer_speed", 3.0}, {"linger", 2.0},
	                          {"tiers", _t(D({{"t2", D({{"radius", 4.0}})}}))}})});
	d.push_back({"tk_glide", D({{"element", 1}, {"verb", "dash"}, {"startup", 0.0}, {"active", 0.25}, {"recovery", 0.08}, {"cost", 4.0}, {"distance", 5.0}, {"iframes", 0.15}})});
	d.push_back({"tk_fly", D({{"element", 1}, {"verb", "mode"}, {"startup", 0.0}, {"active", 0.0}, {"recovery", 0.1}, {"upkeep", 8.0}, {"kind", "hover"}, {"height", 1.5}})});
	// Fire-like (element 2, sub 1)
	d.push_back({"tk_needle", D({{"element", 2}, {"verb", "beam"}, {"startup", 0.1}, {"active", 0.1}, {"recovery", 0.2}, {"heat", 60.0}, {"cls", "blue_fire"},
	                             {"range", 7.0}, {"tiers", _t()}, {"fx", D({{"mat", "blue"}})}})});
	d.push_back({"tk_ball", D({{"element", 2}, {"verb", "projectile"}, {"startup", 0.12}, {"active", 0.05}, {"recovery", 0.25}, {"heat", 100.0}, {"source", "heat"},
	                           {"mat", "fire"}, {"mass", 1.0}, {"speed", 18.0}, {"gravity", 0.3}, {"tag", "fireball"}, {"on_impact", "burst"}, {"impact_radius", 2.0},
	                           {"tiers", _t()}})});
	d.push_back({"tk_line", D({{"element", 2}, {"verb", "ground_line"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3}, {"heat", 120.0}, {"source", "heat"},
	                           {"mat", "fire"}, {"mass", 1.0}, {"tag", "fire_line"}, {"speed", 12.0}, {"budget", 10.0}, {"width", 1.2}, {"trail_zone", "fire_field"},
	                           {"trail_life", 1.0}})});
	d.push_back({"tk_mine", D({{"element", 2}, {"verb", "burst"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"heat", 80.0}, {"at", "aim"}, {"range", 8.0},
	                           {"radius", 2.0}, {"power", 12.0}, {"fuse", 0.4}, {"tiers", _t(D({{"t2", D({{"radius", 3.0}})}}))}, {"fx", D({{"mat", "blast"}})}})});
	d.push_back({"tk_aegis", D({{"element", 2}, {"verb", "barrier"}, {"barrier", "aura"}, {"upkeep", 6.0}, {"counter", D({{"cls", "aura_blue"}, {"power", A({16, 20, 24})}})},
	                            {"tiers", D({{"t1", Dict()}, {"t2", Dict()}})}})});
	d.push_back({"tk_flash", D({{"element", 2}, {"verb", "cone"}, {"startup", 0.08}, {"active", 0.05}, {"recovery", 0.2}, {"heat", 80.0}, {"cls", "flame"},
	                            {"range", 4.0}, {"angle", 60.0}, {"fx", D({{"mat", "blue"}})}})});
	d.push_back({"tk_kiln", D({{"element", 2}, {"verb", "ranged_heat"}, {"startup", 0.12}, {"active", 0.0}, {"recovery", 0.2}, {"cost", 6.0}, {"rate", 450.0},
	                           {"range", 6.0}, {"target_temp", 600.0}})});
	d.push_back({"tk_shimmer", D({{"element", 2}, {"verb", "dash"}, {"startup", 0.0}, {"active", 0.12}, {"recovery", 0.1}, {"cost", 5.0}, {"distance", 3.0}, {"hidden", true}})});
	d.push_back({"tk_hop", D({{"element", 2}, {"verb", "mode"}, {"startup", 0.0}, {"active", 0.0}, {"recovery", 0.1}, {"upkeep", 6.0}, {"kind", "flight"}, {"height", 2.0},
	                          {"speed_mult", 1.2}})});
	// Air-like (element 3, sub 1)
	d.push_back({"tk_clap", D({{"element", 3}, {"verb", "cone"}, {"startup", 0.1}, {"active", 0.1}, {"recovery", 0.2}, {"cost", 4.0}, {"cls", "sound"}, {"range", 6.0},
	                           {"angle", 30.0}, {"power", 8.0}, {"tiers", _t()}, {"fx", D({{"mat", "sound"}})}})});
	d.push_back({"tk_rail", D({{"element", 3}, {"verb", "beam"}, {"startup", 0.1}, {"active", 0.1}, {"recovery", 0.3}, {"cost", 10.0}, {"cls", "lightning"},
	                           {"range", 14.0}, {"damage", 14.0}, {"power", 14.0}, {"tiers", _t(D({{"t2", D({{"power", 36.0}, {"damage", 20.0}})}}))},
	                           {"fx", D({{"mat", "lightning"}})}})});
	d.push_back({"tk_tremor", D({{"element", 3}, {"verb", "ground_line"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3}, {"cost", 7.0}, {"source", "none"},
	                             {"mat", "air"}, {"mass", 2.0}, {"tag", "tremor"}, {"speed", 18.0}, {"budget", 10.0}, {"width", 1.5}, {"power", 12.0}, {"tiers", _t()}})});
	d.push_back({"tk_twister", D({{"element", 3}, {"verb", "zone"}, {"startup", 0.12}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 6.0}, {"tag", "tornado"},
	                              {"radius", 2.0}, {"life", 2.0}, {"at", "ahead"}, {"distance", 4.0}, {"power", 12.0}, {"walk_speed", 3.0},
	                              {"tiers", _t(D({{"t2", D({{"radius", 2.5}})}}))}})});
	d.push_back({"tk_bubble", D({{"element", 3}, {"verb", "barrier"}, {"barrier", "zone"}, {"tag", "null_bubble"}, {"upkeep", 6.0}, {"radius", 1.8},
	                             {"counter", D({{"cls", "bubble_null"}, {"power", A({14, 18, 22})}})}, {"tiers", D({{"t1", Dict()}, {"t2", D({{"radius", 2.2}})}})}})});
	d.push_back({"tk_wave", D({{"element", 3}, {"verb", "burst"}, {"startup", 0.08}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 4.0}, {"at", "self"}, {"radius", 3.0},
	                           {"power", 16.0}, {"cls", "vacuum"}})});
	d.push_back({"tk_anchor", D({{"element", 3}, {"verb", "stance"}, {"startup", 0.0}, {"active", 0.6}, {"recovery", 0.1}, {"held", false}, {"stance", "anchor"},
	                             {"anchored", true}, {"anchor_cp", 35.0}})});
	d.push_back({"tk_eye", D({{"element", 3}, {"verb", "summon"}, {"startup", 0.14}, {"active", 0.0}, {"recovery", 0.18}, {"cost", 10.0}, {"upkeep", 10.0},
	                          {"tag", "tornado"}, {"radius", 2.5}, {"power", 25.0}, {"at", "aim"}, {"range", 10.0}, {"steer_speed", 4.0}, {"linger", 2.0},
	                          {"tiers", _t(D({{"t2", D({{"radius", 3.0}})}}))}})});
	d.push_back({"tk_boom", D({{"element", 3}, {"verb", "dash"}, {"startup", 0.0}, {"active", 0.2}, {"recovery", 0.1}, {"cost", 6.0}, {"distance", 6.0}, {"dir", "aim"}})});
	d.push_back({"tk_glider", D({{"element", 3}, {"verb", "mode"}, {"startup", 0.0}, {"active", 0.0}, {"recovery", 0.1}, {"upkeep", 6.0}, {"kind", "glide"}})});
	for (const auto& kv : d) Moves::register_def(kv.first, kv.second);
	const std::vector<std::vector<std::string>> slots = {
	    {"tk_disc", "tk_spear", "tk_surge", "tk_fan", "tk_dune", "tk_push", "tk_pit", "tk_seize", "tk_burrow", "tk_skin"},
	    {"tk_shard", "tk_jet", "tk_tide", "tk_frost", "tk_icewall", "tk_orb", "tk_floor", "tk_fog", "tk_glide", "tk_fly"},
	    {"tk_needle", "tk_ball", "tk_line", "tk_mine", "tk_aegis", "tk_flash", "tk_kiln", "tk_kiln", "tk_shimmer", "tk_hop"},
	    {"tk_clap", "tk_rail", "tk_tremor", "tk_twister", "tk_bubble", "tk_wave", "tk_anchor", "tk_eye", "tk_boom", "tk_glider"},
	};
	for (int e = 0; e < 4; ++e)
		for (size_t k = 0; k < 10; ++k)
			for (int s : {1, 2, 3}) {
				const std::string slot = Sim::SLOTS[k];
				const auto& row = slots[static_cast<size_t>(e)];
				Moves::bind(e, s, slot, (s > 1 && slot != "guard") ? row[(k + static_cast<size_t>(s) - 1) % 10] : row[k]);
			}
	Interactions::add_rule("stone", "wave_water", D({{"outcome", "capture"}, {"partial", "slow"}, {"release_speed", 9.0}}));
	Interactions::add_rule("solid_light", "quicksand", D({{"outcome", "sink"}, {"partial", "slow"}}));
	Interactions::add_rule("flame", "fog", D({{"outcome", "weaken"}, {"partial", "weaken"}, {"fail", "pass"}}));
	Interactions::add_rule("solid_light", "tornado", D({{"outcome", "capture"}, {"partial", "bend"}, {"fail", "pass"}, {"release_speed", 10.0}}));
	Interactions::add_rule("sound", "wall_ice", D({{"outcome", "pass"}, {"partial", "pass"}, {"fail", "shatter"}}));
}

}  // namespace fft
