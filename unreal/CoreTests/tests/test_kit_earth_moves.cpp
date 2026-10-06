// Port of game/tests/sim/test_kit_earth_moves.gd: every move of the four Earth sub-elements (MOVESET §7.1-§7.4) at
// T0..T3 starts, pays its cost, ends cleanly and emits only catalogued fx keys; every def carries the §15.2 schema.
// test_anim_clips_exist is not ported: the clip table belongs to the animation stream (not core data).
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Sim/Charge.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
const char* const SUBS[4] = {"Stone", "Metal", "Sand", "Magma"};

void prep_for(int s, SimHarness& h, ActorState* a, ActorState* t) {
	(void)a;
	if (s == 0) {
		EU::shot(h, Mat::Stone, 20.0, V3(0, 1.2, 0.5), Vec3(), t);
	} else if (s == 1) {
		MatBody* d = EU::shot(h, Mat::Metal, 2.0, V3(0.3, 1.0, 1.0), Vec3(), nullptr, "disc");
		d->attack_id = 0;
		d->on_ground = true;
	} else if (s == 3) {
		EU::lava(h, 12.0, V3(0.5, 0.2, 2.0));
	}
}

void run_sub(TestCase& tc, int s) {
	for (const char* slot_c : Sim::SLOTS) {
		const std::string slot = slot_c;
		const std::string id = Moves::resolve(Sim::EARTH, s, slot);
		const Dict d = Moves::defs().get(id).as_dict();
		int mt = in_list(slot, {"strike", "thrust", "ground", "sweep", "push", "sink"}) ? 3 : (slot == "guard" ? Charge::max_tier(d) : 0);
		if (slot == "push" || slot == "sink") mt = mini(3, maxi(Charge::max_tier(d), 1));
		for (int tier = 0; tier <= mt; ++tier) {
			EU::MoveRun r = EU::run_move(s, slot, tier, [s](SimHarness& h, ActorState* a, ActorState* t) { prep_for(s, h, a, t); });
			const std::string tag = S(SUBS[s], " ", slot, " T", tier);
			tc.check(r.started, tag + " starts (" + r.move + ")");
			tc.check(r.paid, tag + " pays");
			tc.check(r.ended, tag + " ends cleanly");
			std::string bad;
			for (const std::string& x : r.bad_fx) bad += x + " ";
			tc.check(r.bad_fx.empty(), tag + " fx keys catalogued: " + bad);
			if (!Moves::is_base(r.move)) tc.check(r.fx > 0, tag + " emits fx");
			if (in_list(slot, {"strike", "thrust", "ground", "sweep"}) && Charge::max_tier(d) >= tier)
				tc.check(r.tier >= tier, S(tag, ": charge event reached tier ", r.tier));
		}
	}
}
}  // namespace

FF_TEST(test_kit_earth_moves, test_every_slot_is_bound_with_a_complete_def) {
	Moves::ensure_ready();
	for (int s = 0; s < 4; ++s) {
		for (const char* slot_c : Sim::SLOTS) {
			const std::string slot = slot_c;
			const std::string id = Moves::resolve(Sim::EARTH, s, slot);
			check(!id.empty(), S(SUBS[s], " ", slot, " is bound"));
			if (id.empty() || id == "guard" || id == "evade") continue;
			const Dict d = Moves::defs().get(id).as_dict();
			if (s > 0 || !Moves::is_base(id)) check(dint(d, "sub", -1) == s && dstr(d, "slot", "") == slot, id + ": sub / slot");
			for (const char* k : {"name", "desc", "anim", "fx", "ai"}) check(d.has(k), id + " has " + k);
			check(FxEvents::is_known("mat", dstr(ddict(d, "fx"), "mat", "")), id + " fx mat");
			check(FxEvents::is_known("shape", dstr(ddict(d, "fx"), "shape", "")), id + " fx shape");
			const Dict ai = ddict(d, "ai");
			check(ai.has("role") && ai.has("range") && ai.has("tags"), id + " ai metadata");
			check(in_list(dstr(ai, "role", ""), {"poke", "zone", "counter", "finisher", "mobility", "setup"}), id + " ai role");
		}
	}
	// The guard specs of subs 1-3 are real defs (sub 0 keeps the legacy Bulwark).
	for (int s : {1, 2, 3}) {
		const std::string g = Moves::resolve(Sim::EARTH, s, "guard");
		check(g != "guard" && Moves::defs().has(g), S(SUBS[s], " guard spec"));
	}
	size_t n = 0;
	for (int s = 0; s < 4; ++s) n += Moves::list(Sim::EARTH, s).size();
	check(n >= 40, S("Earth binds ", n, " moves across 4 sub-elements"));
}

FF_TEST(test_kit_earth_moves, test_stone_moves_t0_to_t3) { run_sub(*this, 0); }
FF_TEST(test_kit_earth_moves, test_metal_moves_t0_to_t3) { run_sub(*this, 1); }
FF_TEST(test_kit_earth_moves, test_sand_moves_t0_to_t3) { run_sub(*this, 2); }
FF_TEST(test_kit_earth_moves, test_magma_moves_t0_to_t3) { run_sub(*this, 3); }
