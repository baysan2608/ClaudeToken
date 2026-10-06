// Port of game/tests/sim/test_kit_fire_ledger.gd: 60 s scripted exchanges: a Fire fighter (all four sub-elements, random
// input like a player) against a rival of each element. The energy ledger and every mass ledger stay exact, the body cap
// holds, every fx key is catalogued; the same seed plays the same way (MOVESET §15.9). Random input uses ff::Rng, the
// port of Godot's RandomNumberGenerator (PCG32), with GDScript's left-to-right evaluation order made explicit.
#include "ff_test.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <cmath>
#include <map>
#include <set>

using namespace ff;
using namespace fft;

namespace {
constexpr int SOAK_TICKS = 3600;

struct Soak {
	SimHarness* h = nullptr;
	Rng rng;
	std::map<int, int64_t> next, hold_until;
	std::vector<std::string> bad;
	int max_bodies = 0;
	std::set<std::string> moves;
};

std::vector<std::string> fire_move_ids(int sub) {
	std::vector<std::string> out;
	for (const std::string& id : Moves::registered()) {
		const Dict d = Moves::defs().get(id).as_dict();
		if (dint(d, "element", -1) == 2 && dint(d, "sub", -1) == sub && in_list(dstr(d, "module", ""), {"kit_fire", "verbs"})) out.push_back(id);
	}
	return out;
}

struct LedgerFx : FireCase {
	void _input(Soak& s, ActorState* x, ActorState* other) {
		SimHarness& hh = *s.h;
		ActorIntent& it = hh.it(x);
		const int64_t t = hh.w->tick;
		if (t >= (s.hold_until.count(x->id) ? s.hold_until[x->id] : 0)) {
			it.attack_held = false;
			it.guard_held = false;
			it.tech_held = false;
			it.evade_held = false;
		}
		if (t < (s.next.count(x->id) ? s.next[x->id] : 0)) return;
		s.next[x->id] = t + 4 + static_cast<int64_t>(s.rng.randi() % 30u);
		if (s.rng.randf() < 0.5) hh.aim(x, other->pos - x->pos);
		const double r = s.rng.randf();
		int64_t hold = 0;
		if (r < 0.34) {
			hh.press(x, "attack");
			if (s.rng.randf() < 0.55) it.attack_gesture = 1 + static_cast<int>(s.rng.randi() % 3u);
			hold = s.rng.randf() < 0.3 ? 0 : 5 + static_cast<int64_t>(s.rng.randi() % 140u);
		} else if (r < 0.48) {
			hh.press(x, "guard");
			hold = 6 + static_cast<int64_t>(s.rng.randi() % 90u);
		} else if (r < 0.56 && x->action != nullptr && x->action->id == "guard") {
			it.guard_gesture = 1 + static_cast<int>(s.rng.randi() % 2u);
			it.guard_held = true;
			hold = 4 + static_cast<int64_t>(s.rng.randi() % 30u);
		} else if (r < 0.68) {
			hh.press(x, "tech");
			hold = 6 + static_cast<int64_t>(s.rng.randi() % 120u);
		} else if (r < 0.77) {
			hh.press(x, "evade");
			it.evade_held = s.rng.randf() < 0.5;
			const double mx = static_cast<double>(s.rng.randf()) - 0.5;
			const double mz = static_cast<double>(s.rng.randf()) - 0.5;
			it.move = V3(mx, 0, mz);
			hold = 2 + static_cast<int64_t>(s.rng.randi() % 60u);
		} else if (r < 0.86 && x->element == Sim::FIRE) {
			hh.sub(x, static_cast<int>(s.rng.randi() % 4u));
		} else {
			const double a = static_cast<double>(s.rng.randf()) * kTau;
			const Vec3 dir = V3(std::sin(a), 0, std::cos(a));
			it.move = dir * static_cast<double>(s.rng.randf());
		}
		s.hold_until[x->id] = t + hold;
	}

	Soak _run(uint64_t seed_value, int ticks, int rival_element) {
		Soak s;
		s.h = &H(seed_value);
		s.rng.set_seed(seed_value * 977 + static_cast<uint64_t>(rival_element));
		SimHarness& h = *s.h;
		ActorState* a = h.actor("F", V3(-1, 0, 5), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		ActorState* b = h.actor("R", V3(1, 0, -4), 1, D({{"magma", true}, {"heat_draw", true}, {"lightning", true}}), rival_element);
		Snap base = snap(*h.w);
		static const char* const names[5] = {"energy", "water", "earth", "metal", "plant"};
		for (int k = 0; k < ticks; ++k) {
			_input(s, a, b);
			_input(s, b, a);
			a->health = maxf(a->health, 30.0);
			b->health = maxf(b->health, 30.0);
			h.step();
			for (const Dict& e : h.log)
				if (dstr(e, "type") == "action" && ev_i(e, "actor", -1) == a->id) s.moves.insert(dstr(e, "move"));
			s.max_bodies = maxi(s.max_bodies, h.w->alive_count());
			if (k % 6 == 0) {
				const Snap now = snap(*h.w);
				for (size_t i = 0; i < 5; ++i) {
					const double tol = i == 0 ? 1e-3 : 1e-6;
					if (absf(now.v[i] - base.v[i]) > tol && s.bad.size() < 6) {
						s.bad.push_back(S("t", static_cast<long long>(h.w->tick), " ", names[i], " ", base.v[i], " -> ", now.v[i]));
						base.v[i] = now.v[i];
					}
				}
			}
			if (k % 600 == 599) h.log.clear();
		}
		return s;
	}

	static std::string _hash(const Soak& s) {
		std::string out;
		for (const auto& ap : s.h->w->actors) {
			const ActorState& a = *ap;
			out += a.name + ":" + ftos(a.pos.x, 4) + "," + ftos(a.pos.y, 4) + "," + ftos(a.pos.z, 4) + "|" + ftos(a.health, 3) + "|" + ftos(a.focus, 3) + "|" +
			       ftos(a.heat_reserve, 3) + ";";
		}
		for (const BodyRef& b : s.h->w->bodies)
			if (b->alive)
				out += itos(b->id) + ":" + itos(static_cast<int>(b->mat)) + ":" + b->tag + ":" + ftos(b->mass, 4) + ":" + ftos(b->pos.x, 3) + ":" +
				       ftos(b->heat_payload, 3) + ";";
		return out;
	}
};
}  // namespace

FF_TEST_F(test_kit_fire_ledger, LedgerFx, test_sixty_second_exchanges_keep_every_ledger_exact) {
	const char* const names[4] = {"Earth", "Water", "Fire", "Air"};
	std::set<std::string> all_moves;
	for (int el = 0; el < 4; ++el) {
		Soak s = _run(static_cast<uint64_t>(11 + el), SOAK_TICKS, el);
		std::string bad;
		for (const std::string& b : s.bad) bad += b + "; ";
		check(s.bad.empty(), S("vs ", names[el], ": ledger drift ", bad));
		check(s.max_bodies <= Sim::MAX_BODIES, S("vs ", names[el], ": body cap holds (", s.max_bodies, ")"));
		fx_catalogued(S("vs ", names[el]));
		all_moves.insert(s.moves.begin(), s.moves.end());
	}
	int fire_moves = 0;
	for (int sub = 0; sub < 4; ++sub)
		for (const std::string& id : fire_move_ids(sub))
			if (all_moves.count(id)) ++fire_moves;
	check(fire_moves >= 25, S("the soak played most Fire moves (", fire_moves, ")"));
	note(S("moves played: ", fire_moves, " Fire kit moves"));
}

FF_TEST_F(test_kit_fire_ledger, LedgerFx, test_fire_kit_is_deterministic_for_a_seed) {
	const std::string ha = _hash(_run(42, 900, Sim::WATER));
	const std::string hb = _hash(_run(42, 900, Sim::WATER));
	check(ha == hb, "same seed, same world after 15 s");
	const std::string hc = _hash(_run(43, 900, Sim::WATER));
	check(ha != hc, "another seed plays differently");
}
