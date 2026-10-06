// Port of game/tests/sim/test_kit_fire_moves.gd: every Fire move (four sub-elements, every slot) through the real input
// path at T0-T3: it starts, pays, emits its catalogued fx cues and charge events, ends cleanly, and the energy / mass
// ledgers stay exact. The animation clip existence check is not ported (the clip table belongs to the animation stream).
#include "ff_test.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Sim/Charge.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
constexpr int HOLD_EXTRA = 4;

struct MovesFx : FireCase {
	void _play(ActorState* p, const std::string& slot, int tier) {
		SimHarness& h = this->h();
		int hold = 3;
		const std::string id = Moves::resolve(Sim::FIRE, p->sub_of(Sim::FIRE), slot);
		const Dict d = Moves::defs().get(id).as_dict();
		if (tier > 0) hold = static_cast<int>(std::ceil(Charge::tier_times(d)[static_cast<size_t>(tier - 1)] * Sim::HZ)) + HOLD_EXTRA;
		if (Sim::is_attack_slot(slot)) {
			const int g = slot == "strike" ? 0 : (slot == "thrust" ? 1 : (slot == "ground" ? 2 : 3));
			if (g == 0) h.press(p, "attack");
			else h.flick(p, "attack", g);
			h.step(hold);
			h.release(p, "attack");
		} else if (slot == "guard") {
			h.press(p, "guard");
			h.step(maxi(hold, 30));
			h.release(p, "guard");
		} else if (slot == "push" || slot == "sink") {
			h.press(p, "guard");
			h.step(12);
			h.flick(p, "guard", static_cast<int>(slot == "push" ? Gesture::Up : Gesture::Down));
			h.step(hold);
			h.release(p, "guard");
		} else if (slot == "tech") {
			h.press(p, "tech");
			h.step(maxi(hold, 40));
			h.release(p, "tech");
		} else if (slot == "evade") {
			h.it(p).move = V3(1, 0, 0);
			h.press(p, "evade");
			h.step(2);
			h.it(p).move = Vec3();
		} else if (slot == "evade_hold") {
			h.it(p).move = V3(1, 0, 0);
			h.evade_hold(p, 40);
			h.it(p).move = Vec3();
		}
		h.step(1);
	}

	void _sub_case(int sub) {
		const char* const names[4] = {"Flame", "Blue", "Lightning", "Combustion"};
		for (const char* slot_c : Sim::SLOTS) {
			const std::string slot = slot_c;
			const std::string id = Moves::resolve(Sim::FIRE, sub, slot);
			if (id.empty()) continue;
			const Dict d = Moves::defs().get(id).as_dict();
			const int max_t = Sim::is_attack_slot(slot) ? Charge::max_tier(d) : 0;
			for (int tier = 0; tier <= max_t; ++tier) {
				auto [p, r] = duel(sub, Sim::EARTH, 7, 7.0, D({{"magma", true}, {"heat_draw", true}}));
				SimHarness& h = this->h();
				r->is_dummy = true;
				// Things for techniques and guards to work on: a loose stone, a puddle, a stone thrown at us.
				MatBody* st = h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, p->pos + V3(0, 0.3, -2.5), "test");
				h.w->mass_ledger.ground_taken += 20.0;
				st->on_ground = true;
				MatBody* pd = h.w->spawn_body(Mat::Water, Form::Puddle, 3.0, p->pos + V3(1.2, 0.0, -1.0), "test");
				pd->update_radius_puddle();
				p->heat_reserve = 200.0;
				Snap base = snap(*h.w);
				const double f0 = p->focus;
				const double res0 = p->heat_reserve;
				const std::string label = S(names[sub], " ", slot, " ", id, " T", tier);
				if (slot == "guard") {
					h.launch_at(p, static_cast<int>(Mat::Stone), 20.0, 17.0, Sim::AMBIENT_C, "", r, 7.0);
					h.w->mass_ledger.ground_taken += 20.0;
					base = snap(*h.w);
				}
				_play(p, slot, tier);
				ActorState* pp = p;
				const bool started = h.any_event("action", [&](const Dict& e) {
					return ev_i(e, "actor", -1) == pp->id && (dstr(e, "move") == id || (slot == "guard" && dstr(e, "move") == "guard"));
				});
				check(started, label + ": started");
				h.until([&]() { return pp->action == nullptr; }, 400);
				check(p->action == nullptr, label + ": ends cleanly (still " + (p->action ? p->action->id + "/" + p->action->phase_name() : std::string()) + ")");
				const double paid = (f0 - p->focus) * Sim::HU_PER_FOCUS + (res0 - p->heat_reserve);
				if (dnum(d, "heat", 0.0) > 0.0 || dnum(d, "cost", 0.0) > 0.0)
					check(paid > 0.0 || slot == "evade_hold", S(label, ": paid something (", paid, " HU equivalent)"));
				if (tier > 0)
					check(h.any_event("charge", [&](const Dict& e) { return ev_i(e, "actor", -1) == pp->id && ev_i(e, "tier") == tier; }),
					      S(label, ": charge event for T", tier));
				if (id != "fire_tech" && id != "evade")   // legacy moves keep their legacy events (thermal, evade)
					check(h.any_event("fx", [&](const Dict& e) { return ev_i(e, "actor", -1) == pp->id; }) || h.has_event("lightning") ||
					          h.has_event("flare"),
					      label + ": fx cues");
				ledgers_ok(base, label, 1e-3);
				fx_catalogued(label);
			}
		}
	}
};
}  // namespace

FF_TEST_F(test_kit_fire_moves, MovesFx, test_flame_moves_t0_t3) { _sub_case(0); }
FF_TEST_F(test_kit_fire_moves, MovesFx, test_blue_moves_t0_t3) { _sub_case(1); }
FF_TEST_F(test_kit_fire_moves, MovesFx, test_lightning_moves_t0_t3) { _sub_case(2); }
FF_TEST_F(test_kit_fire_moves, MovesFx, test_combustion_moves_t0_t3) { _sub_case(3); }

FF_TEST(test_kit_fire_moves, test_every_slot_is_bound_for_every_sub_element_with_a_complete_def) {
	Moves::ensure_ready();
	for (int sub = 0; sub < 4; ++sub) {
		for (const char* slot_c : Sim::SLOTS) {
			const std::string slot = slot_c;
			const std::string id = Moves::resolve(Sim::FIRE, sub, slot);
			check(!id.empty(), S("sub ", sub, " ", slot, " bound"));
			if (id.empty() || (sub == 0 && in_list(slot, {"strike", "tech", "evade"}))) continue;
			const Dict d = Moves::defs().get(id).as_dict();
			for (const char* k : {"name", "desc", "slot", "anim", "fx", "ai"}) check(d.has(k), id + " has " + k);
			const Dict fx = ddict(d, "fx");
			check(fx.has("mat") && FxEvents::is_known("mat", dstr(fx, "mat")), id + " fx mat");
			check(FxEvents::is_known("shape", dstr(fx, "shape", "")), id + " fx shape");
			if ((Sim::is_attack_slot(slot) && slot != "strike") || (slot == "strike" && sub > 0)) check(Charge::max_tier(d) == 3, id + " has T0-T3");
			if (Sim::is_attack_slot(slot) || slot == "guard") check(d.has("counter"), id + " counter metadata");
		}
	}
}
