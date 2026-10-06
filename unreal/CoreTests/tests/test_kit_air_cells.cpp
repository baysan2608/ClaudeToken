// Port of game/tests/sim/test_kit_air_cells.gd: the Air column of the counter matrix (docs/MOVESET.md §8.4). AirRules.CELLS
// is rebuilt from Data/rules.json (every rule with owner "air", registration order). The per-cell reference metadata
// (move / tier / expect) is not part of the exported data: the reference test reads CoreTests/data/kit_cell_refs.json when
// present (Tools/godot_export), else it notes the skip - test_golden_matrix pins every move's counter at every tier.
#include "ff_test.h"
#include "kit_air_util.h"
#include "kit_cells.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <map>

using namespace ff;
using namespace fft;

namespace {
struct RefSpec {
	double tp, mass;
};
// AirRules.REF
const std::map<std::string, RefSpec>& REF() {
	static const std::map<std::string, RefSpec> m = {
	    {"stone", {17.0, 20.0}},    {"stone_heavy", {31.5, 45.0}}, {"boulder", {110.0, 200.0}}, {"hot_rock", {26.8, 20.0}},  {"magma", {35.0, 20.0}},
	    {"lava_wave", {27.3, 20.0}}, {"metal", {12.0, 6.0}},       {"sand", {10.0, 8.0}},       {"sand_cloud", {8.0, 6.0}},  {"sand_surge", {20.0, 14.0}},
	    {"water", {9.6, 12.0}},     {"water_wave", {24.0, 14.0}},  {"ice", {9.0, 6.0}},         {"mist", {3.0, 2.0}},        {"steam", {6.0, 1.0}},
	    {"vine", {14.0, 10.0}},     {"flame", {8.0, 0.0}},         {"blue_fire", {16.0, 0.0}},  {"lightning", {24.0, 0.0}},  {"blast", {16.0, 0.0}},
	    {"gust", {11.0, 0.0}},      {"tornado", {30.0, 0.0}},      {"vacuum", {18.0, 0.0}},     {"sound", {16.0, 0.0}},      {"glass", {9.0, 6.0}},
	    {"ember", {4.0, 0.0}},      {"fire_field", {8.0, 0.0}},    {"puddle", {6.0, 3.0}},
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
// AirRules.COLUMNS
const std::vector<Column>& COLUMNS() {
	static const std::vector<Column> c = {
	    {"Gust", {"gust", "guard_wind", "crescent", "grip_wind"}},
	    {"Vortex", {"wall_vortex", "tornado", "eddy"}},
	    {"Vacuum", {"vacuum_well", "bubble_null", "suction", "vacuum"}},
	    {"Sound", {"barrier_sound", "sound", "ground_ping"}},
	};
	return c;
}
const char* const KNOWN_OUTCOMES[] = {
    "pass", "block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter", "sink", "conduct", "ground", "amplify",
    "extinguish", "weaken", "bend", "slow", "overwhelm", "clash", "disrupt", "neutralize", "heat", "push", "disperse", "air_cool", "air_cut", "air_split",
    "air_shrink", "air_shatter", "air_infuse", "air_catch", "air_slow_bend", "air_spatter", "air_contest", "air_compress", "air_snuff", "air_spit",
    "air_pop", "air_still"};
std::string channel_for(const std::string& t) {
	if (in_list(t, {"flame", "blue_fire", "ember"})) return "H";
	if (t == "lightning") return "E";
	if (in_list(t, {"blast", "gust", "tornado", "vacuum", "sound"})) return "P";
	return "K";
}
std::string join_lines(const std::vector<std::string>& v) {
	std::string all;
	for (const std::string& b : v) all += "\n         " + b;
	return all;
}

struct AC : AirCase {
	AgentRef _counter_for(const Dict& ref, const std::string& ccls, ActorState* actor) {
		const std::string move = dstr(ref, "move", "");
		const int tier = dint(ref, "tier", 0);
		const bool perfect = dbool(ref, "perfect", false);
		if (move == "wind_guard") {
			AgentRef g = std::make_shared<Agent>();
			g->kind = "guard";
			g->ccls = "guard_wind";
			g->power = Interactions::WIND_GUARD_CP;
			g->perfect = perfect;
			g->actor = actor;
			return g;
		}
		AgentRef a = Agent::of_move(h().w, actor, move, tier, perfect);
		a->ccls = ccls;
		return a;
	}
};
}  // namespace

FF_TEST_F(test_kit_air_cells, AC, test_every_reference_cell_gives_its_documented_outcome) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::AIR);
	const std::vector<KitCell> cells = kit_cells("air");
	const Dict refs = kit_cell_refs("air");
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
		const std::string t = dstr(ref, "threat", dstr(cell, "threat"));
		const std::string c = dstr(cell, "counter");
		const RefSpec spec = ref_of(t);
		const double tp = dnum(ref, "tp", spec.tp);
		AgentRef th = threat(*h.w, t, tp, dnum(ref, "mass", spec.mass), channel_for(t));
		AgentRef ctr = _counter_for(ref, c, w);
		const IxResult pr = Interactions::predict(h.w, *th, *ctr);
		++n;
		if (pr.outcome != dstr(ref, "expect"))
			bad.push_back(S(t, " x ", c, " [", dstr(ref, "move", ""), " T", dint(ref, "tier", 0), dbool(ref, "perfect", false) ? "*" : "", "] TP ", tp,
			                " CP ", pr.cp_eff, " eff ", pr.eff, " -> ", pr.outcome, " (", pr.band, "), expected ", dstr(ref, "expect")));
	}
	check(bad.empty(), S(bad.size(), " reference cells off:", join_lines(bad)));
	check(n >= 80, S("at least 80 reference cells are checked (", n, ")"));
	note(S(n, " reference cells checked, ", cells.size(), " cells registered"));
}

FF_TEST_F(test_kit_air_cells, AC, test_rules_use_known_outcomes_and_simple_cells_follow_their_bands) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::AIR);
	std::vector<std::string> bad;
	const Dict empty;
	int checked = 0;
	for (const KitCell& cell : kit_cells("air")) {
		const int probe = cell.tiers.empty() ? -1 : cell.tiers[0];
		const Dict r = Interactions::rule(cell.threat, cell.counter, probe, &empty);
		for (const char* k : {"outcome", "partial", "fail", "perfect", "else", "inert", "inert_else", "fallback"})
			if (r.has(k) && !in_list(dstr(r, k), KNOWN_OUTCOMES)) bad.push_back(cell.key + ": " + k + " '" + dstr(r, k) + "'");
		for (const Value& bd : darr(r, "bands")) {
			const std::string o = bd.as_array().get(1).to_string();
			if (!in_list(o, KNOWN_OUTCOMES)) bad.push_back(cell.key + ": band '" + o + "'");
		}
		if (r.has("bands") || r.has("when") || r.has("by_form") || r.has("inert")) continue;
		const RefSpec spec = ref_of(cell.threat);
		const double eff = dnum(r, "eff", 1.0);
		const double full_at = dnum(r, "full_at", 1.0);
		const double partial_at = dnum(r, "partial_at", 0.5);
		if (partial_at <= 0.0 || full_at <= partial_at) continue;
		std::vector<std::string> got;
		for (double ratio : {full_at * 1.2 + 0.01, (full_at + partial_at) * 0.5, partial_at * 0.6}) {
			AgentRef th = threat(*h.w, cell.threat, spec.tp, spec.mass);
			AgentRef ctr = std::make_shared<Agent>();
			ctr->kind = "guard";
			ctr->ccls = cell.counter;
			ctr->power = ratio * spec.tp / maxf(eff, 1e-6);
			ctr->tier = probe >= 0 ? probe : 0;
			ctr->actor = w;
			got.push_back(Interactions::predict(h.w, *th, *ctr).outcome);
		}
		const std::vector<std::string> want = {dstr(r, "outcome", "block"), dstr(r, "partial", "weaken"), dstr(r, "fail", "overwhelm")};
		++checked;
		if (got != want) bad.push_back(cell.key + ": bands " + got[0] + "/" + got[1] + "/" + got[2] + ", rule says " + want[0] + "/" + want[1] + "/" + want[2]);
	}
	check(bad.empty(), S(bad.size(), " problems: ", join_lines(bad)));
	note(S("partial bands checked on ", checked, " cells"));
}

FF_TEST_F(test_kit_air_cells, AC, test_every_row_has_an_answer_in_every_air_column) {
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
	// and the kit's own cells (not just legacy wildcards) cover the rows of its main classes
	std::vector<std::string> own_missing;
	const Dict empty;
	auto own_rows = [&](const char* counter, const std::vector<std::string>& rows) {
		for (const std::string& row : rows) {
			bool own = false;
			for (int tier : {-1, 0, 1, 2, 3})
				if (dstr(Interactions::rule(row, counter, tier, &empty), "owner", "") == "air") own = true;
			if (!own) own_missing.push_back("own cell " + row + " x " + counter);
		}
	};
	std::vector<std::string> all_rows(std::begin(ROWS), std::end(ROWS));
	own_rows("gust", {"stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand_cloud", "water", "water_wave", "ice", "mist",
	                  "steam", "flame", "tornado", "vacuum", "sound"});
	own_rows("tornado", all_rows);
	own_rows("bubble_null", all_rows);
	own_rows("vacuum_well", all_rows);
	own_rows("sound", all_rows);
	std::string all2;
	for (const std::string& m : own_missing) all2 += m + "; ";
	check(own_missing.empty(), "cells missing: " + all2);
}

FF_TEST_F(test_kit_air_cells, AC, test_cells_never_replace_legacy_cells) {
	H(3);
	for (const KitCell& cell : kit_cells("air")) {
		auto it = Interactions::all_rules().find(cell.threat + "|" + cell.counter);
		if (it == Interactions::all_rules().end()) continue;
		for (const Dict& r : it->second)
			if (dstr(r, "owner", "") == "air") check(!dbool(r, "legacy", false), cell.key + " is not a legacy cell");
	}
	// T0 / T1 palm gust and the legacy Wind Guard cells are the core's
	check(dstr(Interactions::rule("stone", "gust", 0), "id", "") == "legacy_gust", "T0 palm gust x stone is the legacy cell");
	check(dstr(Interactions::rule("stone", "gust", 1), "id", "") == "legacy_gust", "T1 cyclone x stone is the legacy cell");
	check(dstr(Interactions::rule("lava_wave", "gust", 1), "id", "") == "legacy_gust", "T1 x lava wave is the legacy cell (waves untouched)");
	check(dstr(Interactions::rule("lava_wave", "gust", 2), "owner", "") == "air", "T2 x lava wave is the kit's");
	check(dstr(Interactions::rule("flame", "guard_wind", 0), "id", "") == "legacy_air_vs_flare", "Wind Guard x flame stays the legacy cell");
	check(dstr(Interactions::rule("stone", "guard_wind", 0), "id", "") == "wind_guard_light", "Wind Guard x stone stays the core's power rule");
	check(dstr(Interactions::rule("ember", "guard_wind", 0), "owner", "") == "air", "Wind Guard x ember uses the fire bands");
}

FF_TEST(test_kit_air_cells, test_the_kit_registers_a_def_for_every_air_slot_of_every_sub_element) {
	Moves::ensure_ready();
	for (int sub = 0; sub < 4; ++sub) {
		for (const char* slot : Sim::SLOTS) {
			const std::string id = Moves::resolve(3, sub, slot);
			check(!id.empty(), S("Air/", sub, " ", slot, " bound"));
			if (id.empty()) continue;
			const Dict d = Moves::defs().get(id).as_dict();
			for (const char* k : {"name", "desc", "ai"})
				if (sub > 0 || !in_list(id, {"air_attack", "air_tech", "air_dash", "guard"})) check(d.has(k), id + " has " + k);
		}
	}
	int n = 0;
	for (const std::string& id : Moves::registered())
		if (dint(Moves::defs().get(id).as_dict(), "element", -1) == 3) ++n;
	check(n >= 37, S("at least 37 Air moves registered (", n, ")"));
}
