// Port of game/tests/sim/test_kit_water_ledger.gd: scripted 60 s exchanges between Water fighters (all four sub-elements,
// random input like a player): the energy ledger and every mass ledger stay exact, the body cap holds, nobody gets
// stuck, every fx key is catalogued and the same seed plays the same way (docs/MOVESET.md §15.9). Random input uses
// ff::Rng (Godot's PCG32) with GDScript's left-to-right evaluation order made explicit.
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

#include <cmath>
#include <map>
#include <set>

using namespace ff;
using namespace fft;

namespace {
constexpr int SOAK_TICKS = 3600;
const char* const kKeys[5] = {"energy", "water", "earth", "metal", "plant"};

struct Soak {
	SimHarness* h = nullptr;
	Rng rng;
	std::map<int, int64_t> next, hold_until;
	WaterCase::Snap base;
	std::array<double, 5> worst{};
	std::vector<std::string> bad;
	std::map<int, int> busy;
	std::set<std::string> fx_keys, outcomes, moves;
	std::map<std::string, int> counts;
	int max_bodies = 0;
	std::string hash;
};

struct WL : WaterCase {
	static void _bad(Soak& s, const std::string& msg) {
		if (s.bad.size() < 8) s.bad.push_back(S("t", static_cast<long long>(s.h->w->tick), ": ", msg));
	}

	static void _input(Soak& s, ActorState* x, ActorState* other, bool rival_subs) {
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
		const double r = s.rng.randf();
		if (s.rng.randf() < 0.5) hh.aim(x, other->pos - x->pos);
		int64_t hold = 0;
		if (r < 0.32) {
			hh.press(x, "attack");
			if (s.rng.randf() < 0.55) it.attack_gesture = 1 + static_cast<int>(s.rng.randi() % 3u);
			hold = s.rng.randf() < 0.3 ? 0 : 5 + static_cast<int64_t>(s.rng.randi() % 130u);
		} else if (r < 0.47) {
			hh.press(x, "guard");
			hold = 6 + static_cast<int64_t>(s.rng.randi() % 90u);
		} else if (r < 0.55 && x->action != nullptr && x->action->id == "guard") {
			it.guard_gesture = 1 + static_cast<int>(s.rng.randi() % 2u);
			it.guard_held = true;
		} else if (r < 0.68) {
			hh.press(x, "tech");
			hold = 6 + static_cast<int64_t>(s.rng.randi() % 120u);
		} else if (r < 0.78) {
			hh.press(x, "evade");
			it.evade_held = s.rng.randf() < 0.5;
			const double mx = static_cast<double>(s.rng.randf()) - 0.5;
			const double mz = static_cast<double>(s.rng.randf()) - 0.5;
			it.move = V3(mx, 0, mz);
			hold = 2 + static_cast<int64_t>(s.rng.randi() % 60u);
		} else if (r < 0.86 && x->element == Sim::WATER) {
			hh.sub(x, static_cast<int>(s.rng.randi() % 4u));
		} else if (r < 0.9 && rival_subs) {
			hh.element(x, static_cast<int>(s.rng.randi() % 4u));
		} else {
			const double a = static_cast<double>(s.rng.randf()) * kTau;
			const Vec3 dir = V3(std::sin(a), 0, std::cos(a));
			it.move = dir * static_cast<double>(s.rng.randf());
		}
		s.hold_until[x->id] = t + hold;
	}

	static std::string _hash(CombatWorld& w) {
		std::string out = itos(w.tick) + "|";
		for (double v : {w.ledger.generated, w.ledger.ambient, w.ledger.vapor, w.ledger.reserve_dissipated, w.ledger.vented, w.ledger.spent,
		                 w.ledger.freeze_dump, w.ledger.removed, w.mass_ledger.ground_taken, w.mass_ledger.ground_returned, w.mass_ledger.vapor,
		                 w.mass_ledger.evaporated, w.mass_ledger.moisture_taken, w.mass_ledger.water_to_plant, w.mass_ledger.burned})
			out += ftos(v, 6) + ",";
		for (const auto& ap : w.actors) {
			const ActorState& a = *ap;
			out += a.name + ":" + ftos(a.pos.x, 5) + "," + ftos(a.pos.y, 5) + "," + ftos(a.pos.z, 5) + "," + ftos(a.vel.x, 5) + "," + ftos(a.vel.z, 5) +
			       "|" + ftos(a.health, 4) + "|" + ftos(a.focus, 4) + "|" + ftos(a.balance, 4) + "|" + ftos(a.water_carried, 5) + "|" + a.stance + "|" +
			       (a.action ? a.action->id : std::string()) + "|" + itos(a.subs[0]) + itos(a.subs[1]) + itos(a.subs[2]) + itos(a.subs[3]) + ";";
			for (const auto& kv : a.status) out += kv.first + ",";
		}
		for (const BodyRef& b : w.bodies)
			if (b->alive)
				out += itos(b->id) + ":" + itos(static_cast<int>(b->mat)) + ":" + itos(static_cast<int>(b->form)) + ":" + b->tag + ":" + ftos(b->mass, 5) +
				       ":" + ftos(b->temp, 4) + ":" + ftos(b->liquid, 4) + ":" + ftos(b->pos.x, 4) + ":" + ftos(b->pos.z, 4) + ":" + itos(b->tier) + ":" +
				       ftos(b->zone_radius, 4) + ":" + ftos(b->heat_payload, 4) + ";";
		return out;
	}

	Soak _run(uint64_t seed_value, int ticks, int rival_element) {
		Soak s;
		s.h = &H(seed_value);
		s.rng.set_seed(seed_value * 31 + 7);
		SimHarness& h = *s.h;
		ActorState* a = h.actor("A", V3(-2, 0, 6), 0, D({{"glide", true}, {"lightning", true}}), Sim::WATER);
		ActorState* b = h.actor("B", V3(2, 0, -6), 1, D({{"glide", true}, {"lightning", true}}), rival_element);
		a->subs = {{0, 0, 0, 0}};
		b->subs = {{0, 0, 0, 0}};
		CombatWorld& w = *h.w;
		s.base = snap(w);
		for (int k = 0; k < ticks; ++k) {
			for (const auto& xp : w.actors) {
				ActorState* x = xp.get();
				_input(s, x, x == a ? b : a, rival_element != Sim::WATER);
				if (x->health < 25.0) x->health = Sim::HEALTH_MAX;
			}
			const double pre_e = w.system_energy() - w.ledger_balance();
			h.step();
			const double post_e = w.system_energy() - w.ledger_balance();
			if (absf(post_e - pre_e) > 1e-3 && s.bad.size() < 6) {
				std::string types;
				for (const Dict& e : h.log) {
					const std::string ty = dstr(e, "type");
					if (ty == "fx" || ty == "spawn") continue;
					std::string what = e.has("move") ? ev_s(e, "move") : e.has("outcome") ? ev_s(e, "outcome") : e.has("phase") ? ev_s(e, "phase")
					                                                                       : e.has("reason") ? ev_s(e, "reason") : std::string();
					types += ty + ":" + what + ", ";
				}
				_bad(s, S("energy jump ", post_e - pre_e, ": ", types));
			}
			for (const Dict& e : h.log) {
				const std::string ty = dstr(e, "type");
				s.counts[ty] += 1;
				if (ty == "fx") s.fx_keys.insert(ev_s(e, "fx") + "/" + ev_s(e, "mat") + "/" + ev_s(e, "shape"));
				else if (ty == "interaction") s.outcomes.insert(ev_s(e, "outcome"));
				else if (ty == "action" && ev_s(e, "phase") == "startup") s.moves.insert(ev_s(e, "move"));
			}
			h.log.clear();
			int alive = 0;
			for (const BodyRef& bd : w.bodies) {
				if (!bd->alive) continue;
				++alive;
				if (!(std::isfinite(static_cast<double>(bd->pos.x)) && std::isfinite(static_cast<double>(bd->pos.y)) && std::isfinite(bd->mass) &&
				      std::isfinite(bd->temp)) ||
				    bd->mass < -1e-9)
					_bad(s, "body " + bd->describe());
			}
			s.max_bodies = maxi(s.max_bodies, alive);
			for (const auto& xp : w.actors) {
				ActorState* x = xp.get();
				if (!(std::isfinite(static_cast<double>(x->pos.x)) && std::isfinite(static_cast<double>(x->pos.z))) || std::fabs(x->pos.x) > 16.0f ||
				    std::fabs(x->pos.z) > 16.0f)
					_bad(s, S(x->name, " out of the arena ", x->pos.x, ",", x->pos.z));
				if (x->focus < -1e-9 || x->focus > 100.0 + 1e-9 || x->water_carried < -1e-9 || x->water_carried > 6.0 + 1e-6)
					_bad(s, S(x->name, " resources focus ", x->focus, " water ", x->water_carried));
				const ActorIntent& it = h.it(x);
				const bool holding = it.attack_held || it.guard_held || it.tech_held || it.evade_held;
				s.busy[x->id] = (x->action != nullptr && !holding) ? s.busy[x->id] + 1 : 0;
				if (s.busy[x->id] == 300) _bad(s, S(x->name, " stuck in ", x->action->id, " (", x->action->phase_name(), ")"));
			}
			if (k % 60 == 59) {
				const Snap now = snap(w);
				for (size_t i = 0; i < 5; ++i) s.worst[i] = maxf(s.worst[i], absf(now.v[i] - s.base.v[i]));
			}
		}
		s.hash = _hash(w);
		return s;
	}

	void _check_soak(const Soak& s, const std::string& label) {
		std::string bad;
		for (const std::string& b : s.bad) bad += b + "; ";
		check(s.bad.empty(), label + " violations: " + bad);
		near(s.worst[0], 0.0, 1e-3, label + " energy ledger");
		for (size_t i = 1; i < 5; ++i) near(s.worst[i], 0.0, 1e-5, label + " " + kKeys[i] + " mass ledger");
		check(s.max_bodies <= Sim::MAX_BODIES + 2, S(label, " body count bounded (", s.max_bodies, ")"));
		for (const std::string& fk : s.fx_keys) {
			const size_t p1 = fk.find('/');
			const size_t p2 = fk.find('/', p1 + 1);
			const std::string fx = fk.substr(0, p1), mat = fk.substr(p1 + 1, p2 - p1 - 1), shape = fk.substr(p2 + 1);
			check(FxEvents::is_known("fx", fx) && FxEvents::is_known("mat", mat) && (FxEvents::is_known("shape", shape) || mode_shape(shape)),
			      label + " catalogued fx key " + fk);
		}
		for (const std::string& o : s.outcomes) check(FxEvents::is_known("outcome", o), label + " catalogued outcome " + o);
	}

	static std::string _worst(const Soak& s) {
		std::string out = "{";
		for (size_t i = 0; i < 5; ++i) out += S(kKeys[i], ": ", s.worst[i], i < 4 ? ", " : "}");
		return out;
	}
	static std::string _set(const std::set<std::string>& v) {
		std::string out;
		for (const std::string& x : v) out += x + " ";
		return out;
	}
};
}  // namespace

FF_TEST_F(test_kit_water_ledger, WL, test_sixty_second_water_mirror_match_keeps_every_ledger_exact) {
	const Soak s = _run(41, SOAK_TICKS, Sim::WATER);
	_check_soak(s, "mirror");
	check(s.moves.size() >= 14, S("the soak played many different moves (", s.moves.size(), ": ", _set(s.moves), ")"));
	auto cnt = [&](const char* k) { auto it = s.counts.find(k); return it == s.counts.end() ? 0 : it->second; };
	note(S("mirror: moves ", s.moves.size(), ", events: fx ", cnt("fx"), " interaction ", cnt("interaction"), " zone ", cnt("zone"), " status ",
	       cnt("status"), "; max bodies ", s.max_bodies, "; worst ", _worst(s)));
	note("outcomes seen: " + _set(s.outcomes));
}

FF_TEST_F(test_kit_water_ledger, WL, test_sixty_second_exchanges_against_the_other_elements_keep_every_ledger_exact) {
	const char* const names[4] = {"earth", "water", "fire", "air"};
	for (int el : {Sim::EARTH, Sim::FIRE, Sim::AIR}) {
		const Soak s = _run(static_cast<uint64_t>(50 + el), SOAK_TICKS, el);
		_check_soak(s, S("vs ", names[el]));
		note(S("vs ", names[el], ": moves ", s.moves.size(), ", outcomes ", _set(s.outcomes), ", worst ", _worst(s)));
	}
}

FF_TEST_F(test_kit_water_ledger, WL, test_water_kit_is_deterministic_for_a_seed) {
	const std::string a = _run(5, 1200, Sim::WATER).hash;
	const std::string b = _run(5, 1200, Sim::WATER).hash;
	const std::string c = _run(6, 1200, Sim::WATER).hash;
	check(a == b, "same seed: same state hash");
	check(a != c, "a different seed plays differently");
}
