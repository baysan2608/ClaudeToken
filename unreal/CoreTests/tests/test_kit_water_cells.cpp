// Port of game/tests/sim/test_kit_water_cells.gd: the Water column of the counter matrix (docs/MOVESET.md §8.2).
// WaterRules.CELLS is rebuilt from Data/rules.json (every rule with owner "water", registration order). The per-cell
// reference metadata (move / tier / expect) is not part of the exported data: the reference test reads it from
// CoreTests/data/kit_cell_refs.json when present (Tools/godot_export/extract_cell_refs.py), else it notes the skip.
#include "ff_test.h"
#include "kit_cells.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Util/GdUtil.h"

#include <map>

using namespace ff;
using namespace fft;

namespace {
struct RefSpec {
	double tp, mass;
};
// WaterRules.REF
const std::map<std::string, RefSpec>& REF() {
	static const std::map<std::string, RefSpec> m = {
	    {"stone", {17.0, 20.0}},     {"stone_heavy", {31.5, 45.0}}, {"boulder", {110.0, 200.0}}, {"hot_rock", {26.8, 20.0}},
	    {"magma", {35.0, 20.0}},     {"lava_wave", {27.3, 20.0}},   {"metal", {12.0, 6.0}},      {"sand", {10.0, 8.0}},
	    {"sand_cloud", {8.0, 6.0}},  {"sand_surge", {20.0, 14.0}},  {"water", {9.6, 12.0}},      {"water_wave", {24.0, 14.0}},
	    {"ice", {9.0, 6.0}},         {"mist", {3.0, 2.0}},          {"steam", {6.0, 1.0}},       {"vine", {14.0, 10.0}},
	    {"flame", {8.0, 0.0}},       {"blue_fire", {16.0, 0.0}},    {"lightning", {24.0, 0.0}},  {"blast", {16.0, 0.0}},
	    {"gust", {11.0, 0.0}},       {"tornado", {30.0, 0.0}},      {"vacuum", {18.0, 0.0}},     {"sound", {16.0, 0.0}},
	    {"puddle", {6.0, 3.0}},      {"fire_field", {8.0, 0.0}},
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
	    {"Water", {"shield_water", "wave_water", "water_jet", "spray", "grip_water"}},
	    {"Ice", {"wall_ice", "rime", "frost", "freeze", "grip_ice"}},
	    {"Mist", {"screen_steam", "fog", "condense", "grip_vapor"}},
	    {"Plant", {"wall_vine", "grip_vine", "anchor"}},
	};
	return c;
}
const char* const KNOWN_OUTCOMES[] = {
    "pass", "block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter", "sink", "conduct", "ground",
    "amplify", "extinguish", "weaken", "bend", "slow", "overwhelm", "clash", "disrupt", "neutralize", "heat", "push", "disperse",
    "water_carry", "water_ridge", "water_freeze", "water_skin", "water_hot_block", "water_dampen", "water_condense_in", "water_brittle",
    "water_feed", "water_drown", "water_melt", "water_sling", "water_quench", "water_burn"};

struct WC : WaterCase {
	AgentRef _counter_for(const Dict& ref, const std::string& ccls, ActorState* actor) {
		const std::string move = dstr(ref, "move", "");
		const int tier = dint(ref, "tier", 0);
		const bool perfect = dbool(ref, "perfect", false);
		if (move == "guard") {
			AgentRef g = std::make_shared<Agent>();
			g->kind = "guard";
			g->ccls = ccls;
			g->power = 6.0;   // a 6 kg waterskin shield: 6 kg x 1.0
			g->perfect = perfect;
			g->actor = actor;
			return g;
		}
		AgentRef a = Agent::of_move(h().w, actor, move, tier, perfect);
		a->ccls = ccls;
		return a;
	}
};

std::string join_lines(const std::vector<std::string>& v) {
	std::string all;
	for (const std::string& b : v) all += "\n         " + b;
	return all;
}
}  // namespace

FF_TEST_F(test_kit_water_cells, WC, test_every_reference_cell_gives_its_documented_outcome) {
	SimHarness& h = H(3);
	// Beside the pool: the documented powers are those of a full-water wave (Tidal Rush scales with the water in reach).
	ActorState* w = h.actor("W", V3(6.0, 0, -1.0), 0, Dict(), Sim::WATER);
	const std::vector<KitCell> cells = kit_cells("water");
	const Dict refs = kit_cell_refs("water");
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
		AgentRef th = threat(*h.w, t, tp, spec.mass);
		AgentRef ctr = _counter_for(ref, c, w);
		const IxResult pr = Interactions::predict(h.w, *th, *ctr);
		++n;
		if (pr.outcome != dstr(ref, "expect"))
			bad.push_back(S(t, " x ", c, " [", dstr(ref, "move", ""), " T", dint(ref, "tier", 0), dbool(ref, "perfect", false) ? "*" : "", "] TP ", tp,
			                " CP ", pr.cp_eff, " eff ", pr.eff, " -> ", pr.outcome, " (", pr.band, "), expected ", dstr(ref, "expect")));
	}
	check(bad.empty(), S(bad.size(), " reference cells off:", join_lines(bad)));
	check(n >= 60, S("at least 60 reference cells are checked (", n, ")"));
	note(S(n, " reference cells checked, ", cells.size(), " cells registered"));
}

FF_TEST_F(test_kit_water_cells, WC, test_rules_use_known_outcomes_and_all_bands_resolve) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(0, 0, 4), 0, Dict(), Sim::WATER);
	std::vector<std::string> bad;
	const Dict empty;
	int checked = 0;
	for (const KitCell& cell : kit_cells("water")) {
		Dict r = Interactions::rule(cell.threat, cell.counter, -1, &empty);
		if (r.empty() || dstr(r, "owner", "") != "water")
			for (int tier : cell.tiers) r = Interactions::rule(cell.threat, cell.counter, tier, &empty);   // a tier-restricted cell
		for (const char* k : {"outcome", "partial", "fail", "perfect", "else"}) {
			if (r.has(k)) {
				const std::string o = dstr(r, k);
				if (!in_list(o, KNOWN_OUTCOMES)) bad.push_back(cell.key + ": " + k + " '" + o + "'");
			}
		}
		for (const Value& bd : darr(r, "bands")) {
			const std::string o = bd.as_array().get(1).to_string();
			if (!in_list(o, KNOWN_OUTCOMES)) bad.push_back(cell.key + ": band '" + o + "'");
		}
		if (r.has("fallback") && !in_list(dstr(r, "fallback"), KNOWN_OUTCOMES)) bad.push_back(cell.key + ": fallback '" + dstr(r, "fallback") + "'");
		// the three bands at ratios 1.2 / 0.7 / 0.3 follow the rule
		const RefSpec spec = ref_of(cell.threat);
		if (r.has("bands") || r.has("when") || r.has("by_form") || r.has("inert")) continue;
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
			ctr->tier = cell.tiers.empty() ? 0 : cell.tiers[0];
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

FF_TEST_F(test_kit_water_cells, WC, test_every_row_has_an_answer_in_every_sub_element_column) {
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

FF_TEST_F(test_kit_water_cells, WC, test_cells_never_replace_legacy_or_environment_cells) {
	H(3);
	for (const KitCell& cell : kit_cells("water")) {
		auto it = Interactions::all_rules().find(cell.threat + "|" + cell.counter);
		if (it == Interactions::all_rules().end()) continue;
		for (const Dict& r : it->second)
			if (dstr(r, "owner", "") == "water") check(!dbool(r, "legacy", false), cell.key + " is not a legacy cell");
	}
	// The legacy lash and the legacy shield cells are untouched.
	check(dstr(Interactions::rule("stone", "water_jet", 0), "id", "") == "legacy_lash", "T0 lash cell is the legacy one");
	check(dstr(Interactions::rule("stone", "water_jet", 1), "id", "") == "legacy_lash", "T1 lash cell is the legacy one");
	check(dstr(Interactions::rule("stone", "water_jet", 2), "owner", "") == "water", "T2 jet cells are ours");
	check(dstr(Interactions::rule("flame", "shield_water", 0), "id", "") == "legacy_flare_water", "flame x water shield is the legacy steam cell");
	check(dstr(Interactions::rule("lava_wave", "puddle", 0), "id", "") == "legacy_quench", "puddle quench stays legacy");
}

// ---------------------------------------------------------------- the headline physical resolutions

FF_TEST_F(test_kit_water_cells, WC, test_a_thrown_stone_is_blocked_by_an_ice_wall_at_cp_22) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(0, 0, 4), 0, Dict(), Sim::WATER);
	ActorState* r = h.actor("R", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(20);
	const BodyRef wall = keep(h.w->spawn_body(Mat::Water, Form::Wall, 50.0, V3(0, 0, 2.5), "test"));
	h.w->mass_ledger.moisture_taken += 50.0;
	WaterUtil::freeze_body(*h.w, wall.get());
	wall->tag = "ice";
	wall->hardness = 0.44;
	wall->wall_half = V3(1.2, 0.85, 0.3);
	wall->wall_rise = 1.0;
	wall->static_body = true;
	wall->props.set("standing", 99.0);
	wall->touch(w->id, "wall", 0);
	h.launch_at(w, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 7.0);
	auto is_wall_ice = [](const Dict& e) { return ev_s(e, "counter") == "wall_ice"; };
	const int t = h.until([&]() { return h.any_event("interaction", is_wall_ice); }, 80);
	check(t > 0, "the stone meets the wall");
	const std::vector<Dict> ev = h.filter("interaction", is_wall_ice);
	check(!ev.empty() && ev_s(ev[0], "outcome") == "block" && ev_s(ev[0], "threat") == "stone",
	      "block: " + (!ev.empty() ? Value(ev[0]).to_string() : std::string("none")));
	check(wall->alive && w->health == 100.0, "wall holds, fighter untouched");
	if (!ev.empty()) check(ev_f(ev[0], "cp") >= 22.0 - 0.01, S("CP 22 (", ev_f(ev[0], "cp"), ")"));
}

FF_TEST_F(test_kit_water_cells, WC, test_a_steam_screen_never_stops_a_boulder_and_a_big_wave_beats_a_small_screen) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(6.0, 0, -1.0), 0, Dict(), Sim::WATER);   // beside the pool (full-water waves)
	// Counter strength scales with the threat: a boulder (TP 110) against a Steam Screen (CP 10-18) fails or passes.
	AgentRef boulder = threat(*h.w, "boulder", 110.0, 200.0);
	const IxResult sc = Interactions::predict(h.w, *boulder, *Agent::of_move(h.w, w, "steam_screen", 3, false));
	check(sc.band == "fail" || sc.outcome == "pass", "steam screen vs boulder: " + sc.band + " / " + sc.outcome);
	const IxResult wall = Interactions::predict(h.w, *boulder, *_counter_for(D({{"move", "ice_wall"}, {"tier", 3}}), "wall_ice", w));
	check(wall.outcome == "overwhelm", S("an 80 kg ice wall vs a boulder: crushed (", wall.outcome, ", ratio ", wall.ratio, ")"));
	// The wave: T0 18 vs a 27 PU lava wave is partial, T1 24 is full (eff 1.5).
	AgentRef lava = threat(*h.w, "lava_wave", 27.3, 20.0);
	const IxResult t0 = Interactions::predict(h.w, *lava, *Agent::of_move(h.w, w, "tidal_rush", 0, false));
	const IxResult t1 = Interactions::predict(h.w, *lava, *Agent::of_move(h.w, w, "tidal_rush", 1, false));
	check(t0.band == "partial" && t1.band == "full", S("Tidal Rush T0 partial (", t0.ratio, "), T1 full (", t1.ratio, ") vs a 20 kg lava wave"));
	// Away from water, the prediction follows the waterskin (6 kg): a full T0 wave, a T3 at 6 / 22 of its power.
	ActorState* dry = h.actor("D", V3(0, 0, 8), 1, Dict(), Sim::WATER);
	AgentRef t0d = Agent::of_move(h.w, dry, "tidal_rush", 0, false);
	AgentRef t3d = Agent::of_move(h.w, dry, "tidal_rush", 3, false);
	near(t0d->power, 18.0, 1e-6, "dry T0 keeps CP 18 (6 kg skin = a full T0 wave)");
	check(t3d->power < 45.0 * 0.4, S("dry T3 predicts the weak wave the sim makes (", t3d->power, ")"));
}
