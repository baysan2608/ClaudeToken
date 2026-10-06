// Port of game/tests/sim/test_core_verbs.gd: moveset engine: a test kit registers one move per verb (every slot,
// sub-elements 1-3 of every element, tiers T0-T3) and two fighters play it with random input for 60 s. Invariants: finite
// state, every mass ledger and the energy ledger exact, body cap, nobody stuck, every fx/interaction key catalogued,
// deterministic for a seed (docs/COMBAT_SPEC.md "Engine" §E9). Random input: ff::Rng with explicit evaluation order.
#include "ff_test.h"
#include "sim_harness.h"
#include "test_kit.h"

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
std::array<double, 5> _snap(CombatWorld& w) {
	return {w.system_energy() - w.ledger_balance(), w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
}

struct Soak {
	std::unique_ptr<SimHarness> h;
	Rng rng;
	std::map<int, int64_t> next, hold_until;
	std::array<double, 5> base{}, worst{};
	std::vector<std::string> bad;
	std::map<int, int> busy;
	std::set<std::string> fx_keys, outcomes;
	std::map<std::string, int> counts;
	int max_bodies = 0;
	void add_bad(const std::string& msg) {
		if (bad.size() < 8) bad.push_back(S("t", static_cast<long long>(h->w->tick), ": ", msg));
	}
	int count(const char* k) const {
		auto it = counts.find(k);
		return it == counts.end() ? 0 : it->second;
	}
};

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
	const double r = s.rng.randf();
	if (s.rng.randf() < 0.5) hh.aim(x, other->pos - x->pos);
	int64_t hold = 0;
	if (r < 0.30) {
		hh.press(x, "attack");
		if (s.rng.randf() < 0.5) it.attack_gesture = 1 + static_cast<int>(s.rng.randi() % 3u);
		hold = s.rng.randf() < 0.3 ? 0 : 5 + static_cast<int64_t>(s.rng.randi() % 130u);
	} else if (r < 0.45) {
		hh.press(x, "guard");
		hold = 6 + static_cast<int64_t>(s.rng.randi() % 90u);
	} else if (r < 0.52 && x->action != nullptr && x->action->id == "guard") {
		it.guard_gesture = 1 + static_cast<int>(s.rng.randi() % 2u);
		it.guard_held = true;
	} else if (r < 0.66) {
		hh.press(x, "tech");
		hold = 6 + static_cast<int64_t>(s.rng.randi() % 120u);
	} else if (r < 0.76) {
		hh.press(x, "evade");
		it.evade_held = s.rng.randf() < 0.5;
		const double mx = static_cast<double>(s.rng.randf()) - 0.5;
		const double mz = static_cast<double>(s.rng.randf()) - 0.5;
		it.move = V3(mx, 0, mz);
		hold = 2 + static_cast<int64_t>(s.rng.randi() % 60u);
	} else if (r < 0.84) {
		hh.element(x, static_cast<int>(s.rng.randi() % 4u));
	} else if (r < 0.92) {
		hh.sub(x, static_cast<int>(s.rng.randi() % 4u));
	} else {
		const double a = static_cast<double>(s.rng.randf()) * kTau;
		const Vec3 dir = V3(std::sin(a), 0, std::cos(a));
		it.move = dir * static_cast<float>(s.rng.randf());
	}
	s.hold_until[x->id] = t + hold;
}

std::string _hash(CombatWorld& w) {
	std::string out = itos(w.tick) + "|";
	for (double v : {w.ledger.generated, w.ledger.ambient, w.ledger.vapor, w.ledger.spent, w.ledger.removed, w.mass_ledger.ground_taken,
	                 w.mass_ledger.ground_returned, w.mass_ledger.vapor, w.mass_ledger.moisture_taken})
		out += ftos(v, 6) + ",";
	for (const auto& ap : w.actors) {
		const ActorState& a = *ap;
		out += a.name + ":" + ftos(a.pos.x, 5) + "," + ftos(a.pos.z, 5) + "," + ftos(a.vel.x, 5) + "|" + ftos(a.health, 4) + "|" + ftos(a.focus, 4) + "|" +
		       ftos(a.balance, 4) + "|" + ftos(a.water_carried, 5) + "|" + ftos(a.metal_carried, 5) + "|" + a.stance + "|" + (a.action ? a.action->id : std::string()) + ";";
	}
	for (const BodyRef& b : w.bodies)
		if (b->alive)
			out += itos(b->id) + ":" + itos(static_cast<int>(b->mat)) + ":" + itos(static_cast<int>(b->form)) + ":" + b->tag + ":" + ftos(b->mass, 5) + ":" +
			       ftos(b->temp, 4) + ":" + ftos(b->pos.x, 4) + ":" + ftos(b->pos.z, 4) + ";";
	return out;
}

std::unique_ptr<Soak> _run(uint64_t seed_value, int ticks) {
	auto sp = std::make_unique<Soak>();
	Soak& s = *sp;
	s.h = std::make_unique<SimHarness>(seed_value);
	s.rng.set_seed(seed_value * 31 + 7);
	const Dict kit = D({{"magma", true}, {"heat_draw", true}, {"lightning", true}, {"glide", true}});
	const int ea = static_cast<int>(s.rng.randi() % 4u);
	ActorState* a = s.h->actor("A", V3(-2, 0, 6), 0, kit, ea);
	const int eb = static_cast<int>(s.rng.randi() % 4u);
	ActorState* b = s.h->actor("B", V3(2, 0, -6), 1, kit, eb);
	a->subs = {{1, 1, 1, 1}};
	b->subs = {{1, 1, 1, 1}};
	CombatWorld& w = *s.h->w;
	s.base = _snap(w);
	for (int k = 0; k < ticks; ++k) {
		for (const auto& xp : w.actors) {
			ActorState* x = xp.get();
			_input(s, x, x == a ? b : a);
			if (x->health < 25.0) x->health = Sim::HEALTH_MAX;
		}
		const double pre_e = w.system_energy() - w.ledger_balance();
		s.h->step();
		const double post_e = w.system_energy() - w.ledger_balance();
		if (absf(post_e - pre_e) > 1e-3 && s.bad.size() < 6) {
			std::string types;
			for (const Dict& e : s.h->log) {
				const std::string ty = dstr(e, "type");
				if (ty == "fx" || ty == "spawn") continue;
				types += ty + ":" + (e.has("move") ? ev_s(e, "move") : e.has("outcome") ? ev_s(e, "outcome") : e.has("phase") ? ev_s(e, "phase") : ev_s(e, "reason")) + ", ";
			}
			s.add_bad(S("energy jump ", post_e - pre_e, ": ", types));
		}
		for (const Dict& e : s.h->log) {
			const std::string ty = dstr(e, "type");
			s.counts[ty] += 1;
			if (ty == "fx") s.fx_keys.insert(ev_s(e, "fx") + "/" + ev_s(e, "mat"));
			else if (ty == "interaction") s.outcomes.insert(ev_s(e, "outcome"));
		}
		s.h->log.clear();
		int alive = 0;
		for (const BodyRef& bd : w.bodies) {
			if (!bd->alive) continue;
			++alive;
			if (!(std::isfinite(static_cast<double>(bd->pos.x)) && std::isfinite(static_cast<double>(bd->pos.y)) && std::isfinite(bd->mass) && std::isfinite(bd->temp)) ||
			    bd->mass < -1e-9)
				s.add_bad("body " + bd->describe());
		}
		s.max_bodies = maxi(s.max_bodies, alive);
		for (const auto& xp : w.actors) {
			ActorState* x = xp.get();
			if (!(std::isfinite(static_cast<double>(x->pos.x)) && std::isfinite(static_cast<double>(x->pos.z))) || std::fabs(x->pos.x) > 16.0f || std::fabs(x->pos.z) > 16.0f)
				s.add_bad(S(x->name, " out of the arena"));
			if (x->focus < -1e-9 || x->focus > 100.0 + 1e-9 || x->water_carried < -1e-9 || x->metal_carried < -1e-9)
				s.add_bad(S(x->name, " resources focus ", x->focus, " water ", x->water_carried, " metal ", x->metal_carried));
			const ActorIntent& it = s.h->it(x);
			const bool holding = it.attack_held || it.guard_held || it.tech_held || it.evade_held;
			s.busy[x->id] = (x->action != nullptr && !holding) ? s.busy[x->id] + 1 : 0;
			if (s.busy[x->id] == 300) s.add_bad(S(x->name, " stuck in ", x->action->id, " (", x->action->phase_name(), ")"));
		}
		if (k % 60 == 59) {
			const auto now = _snap(w);
			for (size_t i = 0; i < 5; ++i) s.worst[i] = maxf(s.worst[i], absf(now[i] - s.base[i]));
		}
	}
	return sp;
}
}  // namespace

FF_TEST(test_core_verbs, test_verbs_soak_keeps_every_invariant) {
	SimHarness holder(1);
	holder.begin_scope();
	register_test_kit();
	const auto s = _run(17, SOAK_TICKS);
	std::string bad;
	for (const std::string& b : s->bad) bad += b + "; ";
	check(s->bad.empty(), "violations: " + bad);
	near(s->worst[0], 0.0, 1e-4, "energy ledger");
	for (size_t i = 1; i < 5; ++i) near(s->worst[i], 0.0, 1e-5, std::string(kKeys[i]) + " mass ledger");
	check(s->max_bodies <= Sim::MAX_BODIES + 2, S("body count bounded (", s->max_bodies, ")"));
	for (const std::string& fk : s->fx_keys) {
		const size_t p = fk.find('/');
		check(FxEvents::is_known("fx", fk.substr(0, p)) && FxEvents::is_known("mat", fk.substr(p + 1)), "catalogued fx key " + fk);
	}
	for (const std::string& o : s->outcomes) check(FxEvents::is_known("outcome", o), "catalogued outcome " + o);
	for (const char* ev : {"charge", "fx", "interaction", "zone", "status", "launch", "morph"})
		check(s->count(ev) > 0, S("the soak exercised '", ev, "' (", s->count(ev), ")"));
	note(S("events: charge ", s->count("charge"), " fx ", s->count("fx"), " interaction ", s->count("interaction"), " zone ", s->count("zone"), " status ",
	       s->count("status"), " morph ", s->count("morph"), " clash ", s->count("clash"), "; max bodies ", s->max_bodies));
	std::string oc;
	for (const std::string& o : s->outcomes) oc += o + " ";
	note("outcomes seen: " + oc);
	holder.end_scope();
}

FF_TEST(test_core_verbs, test_engine_is_deterministic_for_a_seed) {
	SimHarness holder(1);
	holder.begin_scope();
	register_test_kit();
	const std::string a = _hash(*_run(5, 900)->h->w);
	const std::string b = _hash(*_run(5, 900)->h->w);
	const std::string c = _hash(*_run(6, 900)->h->w);
	check(a == b, "same seed: same state hash");
	check(a != c, "a different seed plays differently");
	holder.end_scope();
}

FF_TEST(test_core_verbs, test_every_verb_runs_at_every_tier) {
	SimHarness holder(1);
	holder.begin_scope();
	register_test_kit();
	std::set<std::string> verbs;
	for (const std::string& id : Moves::registered()) {
		if (!begins_with(id, "tk_")) continue;
		const Dict d = Moves::defs().get(id).as_dict();
		for (int tier = 0; tier < 4; ++tier) {
			SimHarness h(3);
			ActorState* p = h.actor("P", V3(0, 0, 3), 0, Dict(), dint(d, "element"));
			ActorState* o = h.actor("O", V3(0, 0, -4), 1, Dict(), Sim::EARTH);
			o->is_dummy = true;
			h.step(5);
			const auto e0 = _snap(*h.w);
			const std::string slot = Moves::slot_of(dint(d, "element"), 1, id);
			p->subs = {{1, 1, 1, 1}};
			const ActionRef inst = h.w->start_action(*p, slot != "guard" ? id : "guard", h.it(p),
			                                         D({{"slot", slot}, {"tier", tier}, {"charge_frozen", true}, {"spec", slot == "guard" ? id : std::string()}}));
			ActorIntent& it = h.it(p);
			it.attack_held = true;
			it.guard_held = true;
			it.tech_held = true;
			it.evade_held = true;
			h.step(40);
			ActorIntent& it2 = h.it(p);
			it2.attack_held = false;
			it2.guard_held = false;
			it2.tech_held = false;
			it2.evade_held = false;
			h.step(120);
			const auto e1 = _snap(*h.w);
			for (size_t k = 0; k < 5; ++k) near(e1[k], e0[k], 1e-5, S(id, " T", tier, ": ", kKeys[k], " ledger"));
			check(p->action == nullptr || p->action->id == "guard" || p->action != inst, S(id, " T", tier, " ends cleanly"));
			verbs.insert(dstr(d, "verb"));
		}
	}
	std::string vl;
	for (const std::string& v : verbs) vl += v + " ";
	check(verbs.size() == 13, S("all 13 verbs exercised (", vl, ")"));
	holder.end_scope();
}
