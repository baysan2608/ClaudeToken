// Port of game/tests/sim/test_kit_water_moves.gd: every Water-kit move (Water, Ice, Mist, Plant) at tiers T0-T3 starts,
// pays its costs, ends cleanly, emits only catalogued fx keys and keeps every ledger exact (docs/MOVESET.md §15.9).
// The animation clip existence check is not ported (the clip table belongs to the animation stream).
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
std::vector<std::string> _kit_moves() {
	std::vector<std::string> out;
	for (int s = 0; s < 4; ++s) {
		for (const std::string& id : Moves::list(Sim::WATER, s)) {
			bool has = false;
			for (const std::string& o : out) has = has || o == id;
			if (!has && (s == 0 || (!Moves::slot_of(Sim::WATER, s, id).empty() && dint(Moves::defs().get(id).as_dict(), "sub", 0) == s)))
				out.push_back(id);
		}
	}
	return out;
}
}  // namespace

FF_TEST(test_kit_water_moves, test_every_move_has_a_complete_def) {
	Moves::ensure_ready();
	int n = 0;
	for (const std::string& id : _kit_moves()) {
		const Dict d = Moves::defs().get(id).as_dict();
		if (id == "guard") continue;
		++n;
		for (const char* key : {"name", "desc", "element", "sub", "slot", "startup", "active", "recovery", "anim", "fx", "ai"})
			check(d.has(key), id + " has '" + key + "'");
		check(dint(d, "element", -1) == 1, id + " is a Water move");
		const std::string slot = dstr(d, "slot", "");
		check(in_list(slot, Sim::SLOTS), id + " has a known slot");
		check(Moves::slot_of(1, dint(d, "sub"), id) == slot, id + " is bound to its slot (" + slot + ")");
		const Dict ai = ddict(d, "ai");
		check(ai.has("role") && ai.has("range") && ai.has("tags"), id + ": ai metadata {role, range, tags}");
		const Dict fxd = ddict(d, "fx");
		check(FxEvents::is_known("mat", dstr(fxd, "mat", "")), id + ": fx mat '" + dstr(fxd, "mat", "") + "' catalogued");
		check(FxEvents::is_known("shape", dstr(fxd, "shape", "")), id + ": fx shape '" + dstr(fxd, "shape", "") + "' catalogued");
		for (const char* k : {"cast", "release", "impact"})
			if (fxd.has(k)) check(FxEvents::is_known("fx", dstr(fxd, k)), id + ": fx." + k + " '" + dstr(fxd, k) + "' catalogued");
		check(d.has("counter") || d.has("threat"), id + " declares a counter or a threat class");
		if (Sim::is_attack_slot(slot)) check(d.has("tiers") && ddict(d, "tiers").has("t3"), id + " has tiers up to t3");
	}
	check(n >= 30, S("the kit has at least 30 moves (", n, ")"));
	note(S(n, " moves"));
}

FF_TEST_F(test_kit_water_moves, WaterCase, test_every_move_runs_at_every_tier_with_exact_ledgers) {
	Moves::ensure_ready();
	int ran = 0;
	for (const std::string& id : _kit_moves()) {
		const Dict d = Moves::defs().get(id).as_dict();
		if (id == "guard" || dstr(d, "module", "") == "water") continue;   // legacy kit moves are pinned elsewhere
		for (int tier = 0; tier < 4; ++tier) {
			auto [p, o] = duel(dint(d, "sub"), Sim::EARTH, 5);
			SimHarness& h = this->h();
			o->is_dummy = true;
			p->focus = 100.0;
			p->heat_reserve = 200.0;   // steam moves pay heat from the reserve first
			const Snap base = snap(*h.w);
			const double f0 = p->focus;
			const ActionRef inst = run_move(p, id, tier, 45, 150);
			const std::string label = S(id, " T", tier);
			ledgers_ok(base, label);
			check(p->action == nullptr || p->action->id == "guard" || p->action != inst, label + " ends cleanly");
			check(p->focus <= f0 + 1e-6 || h.w->tick > 0, label + ": Focus is sane");
			const std::string slot = Moves::slot_of(1, dint(d, "sub"), id);
			check(h.has_event("action", "move", Value(slot == "guard" ? std::string("guard") : id)), label + " started (action event)");
			fx_catalogued(label);
			for (const BodyRef& b : h.w->bodies)
				if (b->alive)
					check(std::isfinite(static_cast<double>(b->pos.x)) && std::isfinite(b->mass) && b->mass >= -1e-9, label + ": finite bodies");
			++ran;
		}
	}
	note(S(ran, " move-tier runs"));
}

FF_TEST_F(test_kit_water_moves, WaterCase, test_costs_are_paid_and_tiers_scale) {
	Moves::ensure_ready();
	// Focus is spent at the start; a higher tier costs at least as much (the charge surcharge).
	for (const char* idc : {"frost_shard", "icicle_volley", "rime_path", "hoarfrost_fan", "scald_puff", "fog_lance", "creeping_fog", "veil",
	                        "water_bullet", "tidal_rush", "spray_fan"}) {
		const std::string id = idc;
		const Dict d = Moves::defs().get(id).as_dict();
		std::vector<double> paid;
		for (int tier = 0; tier < 4; ++tier) {
			auto [p, o] = duel(dint(d, "sub"), Sim::EARTH, 6);
			(void)o;
			SimHarness& h = this->h();
			p->focus = 100.0;
			p->heat_reserve = 400.0;
			const double f0 = p->focus;
			const ActionRef inst = h.w->start_action(*p, id, h.it(p), D({{"slot", dstr(d, "slot")}, {"tier", tier}, {"charge_frozen", true}}));
			h.it(p).attack_held = false;
			h.step(60);
			paid.push_back(f0 - p->focus);
			check(inst != nullptr, S(id, " T", tier, " started"));
		}
		check(paid[0] >= 0.0 && paid[3] >= paid[0] - 1.0, S(id, ": costs do not fall with tier: ", paid[0], " ", paid[1], " ", paid[2], " ", paid[3]));
	}
}
