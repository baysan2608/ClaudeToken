// Port of game/tests/sim/test_kit_fire_cells.gd: the Fire column of the counter matrix (MOVESET §8.3). FireRules.CELLS is
// rebuilt from Data/rules.json (every rule with owner "fire", registration order). The per-cell reference metadata
// (move / tier / expect) is not part of the exported data: test_every_reference_cell_gives_its_documented_outcome reads it
// from CoreTests/data/kit_cell_refs.json when present (not generated - see docs/core/PORT_STATUS.md), else it notes the skip.
#include "ff_test.h"
#include "kit_cells.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Util/GdUtil.h"

#include <map>

using namespace ff;
using namespace fft;

namespace {
struct RefSpec {
	double tp, mass;
};
const std::map<std::string, RefSpec>& REF() {
	static const std::map<std::string, RefSpec> m = {
	    {"stone", {17.0, 20.0}},      {"stone_heavy", {31.5, 45.0}}, {"boulder", {110.0, 200.0}},   {"hot_rock", {27.0, 20.0}},
	    {"magma", {35.0, 20.0}},      {"lava_wave", {27.3, 20.0}},   {"metal", {8.0, 6.0}},         {"sand", {10.0, 8.0}},
	    {"sand_cloud", {8.0, 6.0}},   {"sand_surge", {20.0, 14.0}},  {"water", {9.6, 12.0}},        {"water_wave", {24.0, 14.0}},
	    {"ice", {9.0, 4.0}},          {"mist", {3.0, 2.0}},          {"steam", {6.0, 1.0}},         {"vine", {14.0, 10.0}},
	    {"flame", {8.0, 0.0}},        {"blue_fire", {16.0, 0.0}},    {"lightning", {24.0, 0.0}},    {"blast", {16.0, 0.0}},
	    {"gust", {11.0, 0.0}},        {"tornado", {30.0, 0.0}},      {"vacuum", {18.0, 0.0}},       {"sound", {16.0, 0.0}},
	    {"fire_field", {8.0, 0.0}},   {"ember", {6.0, 0.5}},         {"molten_metal", {20.0, 6.0}},
	};
	return m;
}
RefSpec ref_of(const std::string& t) {
	auto it = REF().find(t);
	return it == REF().end() ? RefSpec{10.0, 10.0} : it->second;
}
const char* const ROWS[] = {"stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand", "sand_surge", "water",
                            "water_wave", "ice", "mist", "steam", "vine", "flame", "blue_fire", "lightning", "blast", "gust", "tornado",
                            "vacuum", "sound"};
struct Column {
	const char* name;
	std::vector<const char*> classes;
};
const std::vector<Column>& COLUMNS() {
	static const std::vector<Column> c = {
	    {"Flame", {"aura_flame", "flame", "fire_field", "fireball", "heat_grip", "draw_heat", "heat_ranged"}},
	    {"Blue", {"aura_blue", "blue_fire", "corona", "magma_rift", "heat_ranged"}},
	    {"Lightning", {"ward_static", "lightning", "ground_current", "static_field"}},
	    {"Combustion", {"guard_blast", "blast", "ember"}},
	};
	return c;
}
std::string channel_for(const std::string& t) {
	if (in_list(t, {"flame", "blue_fire", "fire_field", "ember"})) return "H";
	if (t == "lightning") return "E";
	if (in_list(t, {"blast", "gust", "tornado", "vacuum", "sound"})) return "P";
	return "K";
}
}  // namespace

FF_TEST_F(test_kit_fire_cells, FireCase, test_every_reference_cell_gives_its_documented_outcome) {
	SimHarness& h = H(3);
	ActorState* f = h.actor("F", V3(0, 0, 4), 0, Dict(), Sim::FIRE);
	const std::vector<KitCell> cells = kit_cells("fire");
	const Dict refs = kit_cell_refs("fire");
	if (refs.empty()) {
		note("skipped: no reference metadata (CoreTests/data/kit_cell_refs.json); the cells themselves are covered by test_golden_matrix");
		return;
	}
	int n = 0;
	std::vector<std::string> bad;
	for (const auto& kv : refs) {
		const Dict cell = kv.second.as_dict();
		const Dict ref = ddict(cell, "ref");
		if (dstr(ref, "expect", "").empty()) continue;
		const std::string t = dstr(cell, "threat");
		const std::string c = dstr(cell, "counter");
		const RefSpec spec = ref_of(t);
		const double tp = dnum(ref, "tp", spec.tp);
		AgentRef th = threat(*h.w, t, tp, spec.mass, channel_for(t));
		AgentRef ctr = Agent::of_move(h.w, f, dstr(ref, "move", ""), dint(ref, "tier", 0), dbool(ref, "perfect", false));
		ctr->ccls = c;
		if (ctr->power < 0.0) ctr->power = 10.0;
		const IxResult pr = Interactions::predict(h.w, *th, *ctr);
		++n;
		if (pr.outcome != dstr(ref, "expect"))
			bad.push_back(S(t, " x ", c, " [", dstr(ref, "move", ""), " T", dint(ref, "tier", 0), dbool(ref, "perfect", false) ? "*" : "", "] TP ", tp,
			                " CP ", pr.cp_eff, " eff ", pr.eff, " -> ", pr.outcome, " (", pr.band, "), expected ", dstr(ref, "expect")));
	}
	std::string all;
	for (const std::string& b : bad) all += "\n         " + b;
	check(bad.empty(), S(bad.size(), " reference cells off:", all));
	check(n >= 90, S("at least 90 reference cells are checked (", n, ")"));
	note(S(n, " reference cells checked, ", cells.size(), " cells registered"));
}

FF_TEST_F(test_kit_fire_cells, FireCase, test_partial_and_fail_bands_follow_each_rule) {
	SimHarness& h = H(3);
	ActorState* f = h.actor("F", V3(0, 0, 4), 0, Dict(), Sim::FIRE);
	std::vector<std::string> bad;
	int checked = 0;
	const Dict empty;
	for (const KitCell& cell : kit_cells("fire")) {
		const int tier0 = cell.tiers.empty() ? 0 : cell.tiers[0];
		const Dict r = Interactions::rule(cell.threat, cell.counter, tier0, &empty);
		if (r.empty() || r.has("bands") || r.has("when") || r.has("by_form")) continue;
		const double full_at = dnum(r, "full_at", 1.0);
		const double partial_at = dnum(r, "partial_at", 0.5);
		if (partial_at <= 0.0 || full_at <= partial_at) continue;
		const double eff = dnum(r, "eff", 1.0);
		const RefSpec spec = ref_of(cell.threat);
		std::vector<std::string> got;
		for (double ratio : {full_at * 1.2 + 0.01, (full_at + partial_at) * 0.5, partial_at * 0.6}) {
			AgentRef th = threat(*h.w, cell.threat, spec.tp, spec.mass);
			AgentRef ctr = std::make_shared<Agent>();
			ctr->kind = "move";
			ctr->ccls = cell.counter;
			ctr->power = ratio * spec.tp / maxf(eff, 1e-6);
			ctr->tier = tier0;
			ctr->actor = f;
			got.push_back(Interactions::predict(h.w, *th, *ctr).outcome);
		}
		const std::vector<std::string> want = {dstr(r, "outcome", "block"), dstr(r, "partial", "weaken"), dstr(r, "fail", "overwhelm")};
		++checked;
		if (got != want) bad.push_back(cell.key + ": bands " + got[0] + "/" + got[1] + "/" + got[2] + ", rule says " + want[0] + "/" + want[1] + "/" + want[2]);
	}
	std::string all;
	for (const std::string& b : bad) all += "\n         " + b;
	check(bad.empty(), S(bad.size(), " problems: ", all));
	check(checked >= 25, S("partial bands checked on ", checked, " cells"));
}

FF_TEST_F(test_kit_fire_cells, FireCase, test_every_row_has_an_answer_in_every_sub_element_column) {
	H(3);
	std::vector<std::string> missing;
	for (const Column& col : COLUMNS()) {
		for (const char* row : ROWS) {
			bool found = false;
			for (const char* c : col.classes)
				for (int tier : {-1, 0, 1, 2, 3})
					if (Interactions::has_rule(row, c, tier)) found = true;
			if (!found) missing.push_back(std::string(row) + " x " + col.name);
		}
	}
	std::string all;
	for (const std::string& m : missing) all += m + "; ";
	check(missing.empty(), "rows without any cell in a column: " + all);
}

FF_TEST_F(test_kit_fire_cells, FireCase, test_cells_never_replace_legacy_cells) {
	H(3);
	for (const KitCell& cell : kit_cells("fire")) {
		auto it = Interactions::all_rules().find(cell.threat + "|" + cell.counter);
		if (it == Interactions::all_rules().end()) continue;
		for (const Dict& r : it->second)
			if (dstr(r, "owner", "") == "fire") check(!dbool(r, "legacy", false), cell.key + " is not a legacy cell");
	}
	check(dstr(Interactions::rule("flame", "aura_flame", 0), "id", "") == "legacy_heat_sink", "Heat Sink stays the legacy cell");
	check(dstr(Interactions::rule("lightning", "aura_flame", 0), "id", "") == "legacy_redirect_current", "Return Current stays legacy");
	check(dbool(Interactions::rule("magma", "aura_flame", 0), "legacy", false), "magma x Flame Guard stays legacy (the extension runs before contact)");
	check(dstr(Interactions::rule("stone", "heat_grip", 0), "id", "") == "legacy_heat", "magma grip legality stays legacy");
	check(dstr(Interactions::rule("water", "flame", 0), "id", "") == "legacy_flare_water", "flare x water stays the legacy steam cell");
}

FF_TEST_F(test_kit_fire_cells, FireCase, test_counter_power_scales_with_threat_and_tier) {
	SimHarness& h = H(3);
	ActorState* f = h.actor("F", V3(0, 0, 4), 0, Dict(), Sim::FIRE);
	auto P = [&](const AgentRef& t, const AgentRef& c) { return Interactions::predict(h.w, *t, *c); };
	// Reactive Blast (18): a 20 kg stone (17) is deflected, a 45 kg one (31.5) is only blocked.
	const IxResult light = P(threat(*h.w, "stone", 17.0, 20.0), Agent::of_move(h.w, f, "reactive_blast", 0, false));
	const IxResult heavy = P(threat(*h.w, "stone_heavy", 31.5, 45.0), Agent::of_move(h.w, f, "reactive_blast", 0, false));
	check(light.outcome == "fire_reactive" && heavy.outcome == "block", "Reactive Blast: light " + light.outcome + ", heavy " + heavy.outcome);
	// Detonation by tier vs a tornado (30): Pop T1 passes, Detonation T3 disrupts it.
	const IxResult t1 = P(threat(*h.w, "tornado", 30.0, 0.0, "P"), Agent::of_move(h.w, f, "pop", 1, false));
	const IxResult t3 = P(threat(*h.w, "tornado", 30.0, 0.0, "P"), Agent::of_move(h.w, f, "pop", 3, false));
	check(t1.outcome == "pass" && t3.outcome == "fire_disrupt_zone", "tornado: T1 " + t1.outcome + ", T3 " + t3.outcome);
	// Bolts vs a stone wall (CP 30): Bolt T1 (24) grounds, Storm Bolt T2 (36) shatters.
	MatBody* wall = h.w->spawn_body(Mat::Stone, Form::Wall, 120.0, V3(0, 0, 0), "t");
	const IxResult p1 = P(threat(*h.w, "lightning", 24.0, 0.0, "E"), Agent::of_body(*h.w, *wall));
	const IxResult p2 = P(threat(*h.w, "lightning", 36.0, 0.0, "E"), Agent::of_body(*h.w, *wall));
	check(p1.outcome == "ground" && p2.outcome == "shatter", "wall vs bolts: T1 " + p1.outcome + ", T2 " + p2.outcome);
	h.w->decay_body(*wall, "t");
	// Searing Beam T2 melts a flying stone, the Blue Lance (T1) only warms it.
	const IxResult s1 = P(threat(*h.w, "stone", 17.0, 20.0), Agent::of_move(h.w, f, "blue_needle", 1, false));
	const IxResult s2 = P(threat(*h.w, "stone", 17.0, 20.0), Agent::of_move(h.w, f, "blue_needle", 2, false));
	check(s1.outcome == "fire_heat" && s2.outcome == "fire_melt", "blue vs stone: T1 " + s1.outcome + ", T2 " + s2.outcome);
	// Wind vs a fire field: weak wind fans it, strong wind snuffs it (field 15 vs gust 7 / 40).
	MatBody* field = FireUtil::spawn_field(*h.w, f->id, V3(0, 0, -2), 1.5, 2.0, 300.0);
	const IxResult weak = P(threat(*h.w, "gust", 7.0, 0.0, "P"), Agent::of_body(*h.w, *field));
	const IxResult strong = P(threat(*h.w, "gust", 40.0, 0.0, "P"), Agent::of_body(*h.w, *field));
	check(weak.outcome == "fire_fanned" && strong.outcome == "fire_snuffed", "wind vs fire: weak " + weak.outcome + ", strong " + strong.outcome);
}
