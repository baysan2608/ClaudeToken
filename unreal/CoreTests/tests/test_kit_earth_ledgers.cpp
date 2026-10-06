// Port of game/tests/sim/test_kit_earth_ledgers.gd: Earth kit ledgers and determinism. A scripted exchange between two
// Earth fighters cycling through every sub-element, slot and tier keeps the ground ledger (stone + sand + glass, with the
// sand <-> glass <-> sandstone conversions), the metal ledger, the water ledger and the energy identity exact; the same
// seed gives the same final state (compared as the full state text instead of its sha256).
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <set>

using namespace ff;
using namespace fft;

namespace {
struct Step {
	int sub;
	const char* slot;
	int tier;
};
const Step STEPS[] = {
    {0, "strike", 0}, {1, "strike", 2}, {2, "ground", 1}, {3, "strike", 2}, {0, "ground", 0},     {1, "guard", 0},
    {2, "strike", 3}, {3, "ground", 0}, {0, "sink", 1},   {1, "thrust", 1}, {2, "guard", 2},      {3, "guard", 0},
    {0, "push", 0},   {1, "ground", 0}, {2, "sweep", 1},  {3, "thrust", 3}, {0, "tech", 0},       {1, "tech", 0},
    {2, "tech", 0},   {3, "tech", 0},   {0, "thrust", 2}, {1, "sweep", 2},  {2, "sink", 1},       {3, "sink", 1},
    {0, "sweep", 3},  {1, "sink", 0},   {2, "push", 1},   {3, "push", 0},   {0, "evade_hold", 0}, {1, "evade", 0},
    {2, "thrust", 2}, {3, "sweep", 1},  {0, "strike", 3}, {1, "push", 0},   {2, "evade_hold", 0}, {3, "evade_hold", 0},
};
constexpr size_t NSTEPS = sizeof(STEPS) / sizeof(STEPS[0]);

void _drive(SimHarness& h, ActorState* f, const std::string& slot, int tier) {
	const int hold = EU::TIER_HOLD[tier];
	if (slot == "strike") {
		h.press(f, "attack");
		h.step(hold);
		h.release(f, "attack");
	} else if (slot == "thrust" || slot == "ground" || slot == "sweep") {
		h.flick(f, "attack", EU::slot_gesture(slot));
		h.step(hold);
		h.release(f, "attack");
	} else if (slot == "guard") {
		h.press(f, "guard");
		h.step(maxi(hold, 30));
		h.release(f, "guard");
	} else if (slot == "push" || slot == "sink") {
		h.press(f, "guard");
		h.step(maxi(hold, 12));
		h.flick(f, "guard", static_cast<int>(slot == "push" ? Gesture::Up : Gesture::Down));
		h.step();
		h.release(f, "guard");
	} else if (slot == "tech") {
		h.press(f, "tech");
		h.step(30);
		h.it(f).attack_pressed = true;
		h.step(8);
		h.release(f, "tech");
	} else if (slot == "evade") {
		h.press(f, "evade");
	} else if (slot == "evade_hold") {
		h.press(f, "evade");
		h.it(f).evade_held = true;
		h.step(30);
		h.it(f).evade_held = false;
	}
}

std::string _hash(SimHarness& h) {
	std::string out = "t" + itos(h.w->tick);
	for (const auto& ap : h.w->actors) {
		const ActorState& a = *ap;
		out += "|" + a.name + " " + ftos(a.pos.x, 4) + " " + ftos(a.pos.z, 4) + " " + ftos(a.health, 4) + " " + ftos(a.focus, 4) + " " +
		       ftos(a.balance, 4) + " " + ftos(a.metal_carried, 4);
	}
	for (const BodyRef& b : h.w->bodies) {
		if (!b->alive) continue;
		out += "|" + itos(b->id) + " " + itos(static_cast<int>(b->mat)) + " " + itos(static_cast<int>(b->form)) + " " + b->tag + " " +
		       ftos(b->mass, 4) + " " + ftos(b->temp, 3) + " " + ftos(b->pos.x, 3) + " " + ftos(b->pos.y, 3) + " " + ftos(b->pos.z, 3);
	}
	return out;
}

std::string _exchange(TestCase* tc, uint64_t seed_value, double seconds, bool checks) {
	SimHarness h(seed_value);
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	ActorState* b = h.actor("B", V3(0.5, 0, -4), 1, Dict(), Sim::EARTH);
	a->facing = kPi;
	h.step(5);
	const double em0 = h.w->earth_mass();
	const double mm0 = h.w->metal_mass();
	const double wm0 = h.w->water_mass();
	const double e0 = EU::e0(h);
	double worst[4] = {0.0, 0.0, 0.0, 0.0};
	const int64_t ticks = static_cast<int64_t>(seconds * Sim::HZ);
	size_t k = 0;
	ActorState* fighters[2] = {a, b};
	while (h.w->tick < ticks) {
		for (size_t i = 0; i < 2; ++i) {
			ActorState* f = fighters[i];
			f->health = maxf(f->health, 40.0);
			f->focus = maxf(f->focus, 30.0);
			if (f->action == nullptr && f->stun <= 0.0) {
				const Step& st = STEPS[(k + i * 7) % NSTEPS];
				h.sub(f, st.sub);
				h.step();
				_drive(h, f, st.slot, st.tier);
			}
		}
		++k;
		h.step(20);
		worst[0] = maxf(worst[0], absf(h.w->earth_mass() - em0));
		worst[1] = maxf(worst[1], absf(h.w->metal_mass() - mm0));
		worst[2] = maxf(worst[2], absf(h.w->water_mass() - wm0));
		worst[3] = maxf(worst[3], EU::energy_drift(h, e0));
	}
	if (checks) {
		tc->check(worst[0] < 1e-6, S("ground ledger (stone + sand + glass) exact over ", seconds, " s (worst ", worst[0], ")"));
		tc->check(worst[1] < 1e-6, S("metal ledger exact (worst ", worst[1], ")"));
		tc->check(worst[2] < 1e-6, S("water ledger exact (worst ", worst[2], ")"));
		tc->check(worst[3] < 1e-3, S("energy identity (worst drift ", worst[3], " HU)"));
		const MassLedger& ml = h.w->mass_ledger;
		const double conv = ml.sand_to_glass + ml.sand_to_sandstone;
		tc->note(S("ground taken ", ml.ground_taken, " kg, returned ", ml.ground_returned, " kg, sand->glass ", ml.sand_to_glass, ", sand->sandstone ",
		           ml.sand_to_sandstone, ", metal taken ", ml.metal_taken, ", bodies ", h.w->alive_count(), ", ticks ", static_cast<long long>(h.w->tick)));
		tc->check(ml.ground_taken > 500.0, "the exchange moved a lot of earth");
		tc->check(conv >= 0.0, "conversions booked");
		std::set<std::string> used;
		for (const Dict& e : h.log)
			if (dstr(e, "type") == "action" && dstr(e, "phase") == "startup") used.insert(dstr(e, "move"));
		tc->note(S("moves used: ", used.size()));
		tc->check(used.size() >= 25, S("most of the kit was exercised (", used.size(), " moves)"));
	}
	return _hash(h);
}
}  // namespace

FF_TEST(test_kit_earth_ledgers, test_scripted_60_s_exchange_keeps_every_ledger) { _exchange(this, 11, 60.0, true); }

FF_TEST(test_kit_earth_ledgers, test_same_seed_same_hash) {
	const std::string h1 = _exchange(this, 23, 20.0, false);
	const std::string h2 = _exchange(this, 23, 20.0, false);
	check(h1 == h2, "deterministic: same seed, same final state");
}
