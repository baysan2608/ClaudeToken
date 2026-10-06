// Fourfold core - perf / soak / determinism runner (ports of game/tests/sim/perf_core.gd and perf_air.gd, plus a Session
// autoplay soak and a same-seed determinism check).
//
//   ff_perf [mode] [seconds]      mode: all (default) | core | air | session | determinism; seconds default 120
//
// core:        two fighters, random input over the legacy kits + the test kit of test_core_verbs (every verb), 60 Hz.
// air:         an Air fighter cycling every Air sub-element vs a fighter cycling every element; checks finite state, the
//              body cap and exact energy / mass ledgers every second (perf_air.gd). Exit 1 on a violation.
// session:     ff::Session "spar" in autoplay duel mode (both fighters on master AI) - the cost the Unreal game pays per
//              Session::Step (sim + AI + snapshot).
// determinism: the core soak run twice with one seed and once with another; state hashes must match / differ.
// Prints mean / p95 / p99 / max step time in ms like the Godot scripts.
#include "sim_harness.h"
#include "test_kit.h"

#include "ff/Session.h"
#include "Combat/Moves.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace ff;
using namespace fft;

namespace {

using Clock = std::chrono::steady_clock;

struct Stats {
	double mean = 0.0, p95 = 0.0, p99 = 0.0, max = 0.0;
};

Stats stats_of(const std::vector<double>& times) {
	Stats s;
	if (times.empty()) return s;
	std::vector<double> sorted = times;
	std::sort(sorted.begin(), sorted.end());
	for (double t : times) s.mean += t;
	s.mean /= static_cast<double>(times.size());
	s.p95 = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.95)];
	s.p99 = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99)];
	s.max = sorted.back();
	return s;
}

bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}

void clear_edges(SimHarness& h) {
	for (auto& kv : h.intents) {
		ActorIntent& x = kv.second;
		x.attack_pressed = false;
		x.attack_released = false;
		x.guard_pressed = false;
		x.evade_pressed = false;
		x.tech_pressed = false;
		x.tech_released = false;
		x.tech_cancel = false;
		x.element_select = -1;
		x.sub_select = -1;
		x.attack_gesture = 0;
		x.guard_gesture = 0;
	}
}

// FNV-1a over the authoritative state (tick, rng, ledgers, actors, live bodies).
uint64_t state_hash(const CombatWorld& w) {
	uint64_t v = 1469598103934665603ull;
	auto bytes = [&](const void* p, size_t n) {
		const unsigned char* c = static_cast<const unsigned char*>(p);
		for (size_t i = 0; i < n; ++i) {
			v ^= c[i];
			v *= 1099511628211ull;
		}
	};
	auto d = [&](double x) { bytes(&x, sizeof(x)); };
	auto i = [&](int64_t x) { bytes(&x, sizeof(x)); };
	auto vec = [&](Vec3 x) {
		d(static_cast<double>(x.x));
		d(static_cast<double>(x.y));
		d(static_cast<double>(x.z));
	};
	i(w.tick);
	i(static_cast<int64_t>(w.rng.state()));
	const EnergyLedger& l = w.ledger;
	for (double x : {l.generated, l.ambient, l.vapor, l.reserve_dissipated, l.vented, l.spent, l.freeze_dump, l.removed}) d(x);
	d(w.water_mass());
	d(w.earth_mass());
	d(w.metal_mass());
	d(w.plant_mass());
	for (const auto& ap : w.actors) {
		const ActorState& a = *ap;
		vec(a.pos);
		vec(a.vel);
		for (double x : {a.facing, a.health, a.balance, a.focus, a.heat_reserve, a.wetness, a.water_carried, a.stun, a.iframes}) d(x);
		i(a.element);
		i(a.held_body);
		i(a.action != nullptr ? static_cast<int64_t>(a.action->phase) : -1);
	}
	for (const BodyRef& b : w.bodies) {
		if (!b->alive) continue;
		i(b->id);
		i(static_cast<int64_t>(b->mat));
		i(static_cast<int64_t>(b->form));
		i(static_cast<int64_t>(b->phase));
		d(b->mass);
		d(b->temp);
		d(b->liquid);
		vec(b->pos);
		vec(b->vel);
		i(b->controller);
		i(b->attack_id);
	}
	return v;
}

// ---------------------------------------------------------------- perf_core.gd
struct CoreRun {
	std::vector<double> times;
	size_t events = 0;
	int alive = 0;
	uint64_t hash = 0;
};

CoreRun run_core(int secs, uint64_t world_seed = 42, uint64_t input_seed = 4242) {
	SimHarness h(world_seed);
	h.begin_scope();
	register_test_kit();
	Rng rng;
	rng.set_seed(input_seed);
	const Dict kit = D({{"magma", true}, {"heat_draw", true}, {"lightning", true}, {"glide", true}});
	ActorState* a = h.actor("A", V3(-2, 0, 4), 0, kit, 0);
	ActorState* b = h.actor("B", V3(2, 0, -4), 1, kit, 2);
	std::map<int, int64_t> next = {{a->id, 0}, {b->id, 0}};
	std::map<int, int64_t> until = {{a->id, 0}, {b->id, 0}};
	CoreRun out;
	const int ticks = secs * 60;
	out.times.reserve(static_cast<size_t>(ticks));
	for (int k = 0; k < ticks; ++k) {
		for (ActorState* x : {a, b}) {
			ActorIntent& it = h.it(x);
			ActorState* other = x == a ? b : a;
			if (h.w->tick >= until[x->id]) {
				it.attack_held = false;
				it.guard_held = false;
				it.tech_held = false;
				it.evade_held = false;
			}
			if (h.w->tick >= next[x->id]) {
				next[x->id] = h.w->tick + 4 + static_cast<int64_t>(rng.randi() % 30u);
				h.aim(x, other->pos - x->pos);
				const double r = static_cast<double>(rng.randf());
				int64_t hold = 0;
				if (r < 0.3) {
					h.press(x, "attack");
					if (static_cast<double>(rng.randf()) < 0.5) it.attack_gesture = 1 + static_cast<int>(rng.randi() % 3u);
					hold = static_cast<int64_t>(rng.randi() % 120u);
				} else if (r < 0.45) {
					h.press(x, "guard");
					hold = 6 + static_cast<int64_t>(rng.randi() % 80u);
				} else if (r < 0.6) {
					h.press(x, "tech");
					hold = 6 + static_cast<int64_t>(rng.randi() % 120u);
				} else if (r < 0.7) {
					h.press(x, "evade");
					it.evade_held = static_cast<double>(rng.randf()) < 0.5;
					const double mx = static_cast<double>(rng.randf()) - 0.5;
					const double mz = static_cast<double>(rng.randf()) - 0.5;
					it.move = V3(mx, 0, mz);
					hold = static_cast<int64_t>(rng.randi() % 50u);
				} else if (r < 0.8) {
					h.element(x, static_cast<int>(rng.randi() % 4u));
				} else if (r < 0.9) {
					h.sub(x, static_cast<int>(rng.randi() % 4u));
				} else {
					const double ang = static_cast<double>(rng.randf()) * kTau;
					it.move = V3(std::sin(ang), 0, std::cos(ang));
				}
				until[x->id] = h.w->tick + hold;
			}
			if (x->health < 25.0) x->health = Sim::HEALTH_MAX;
		}
		const Clock::time_point t0 = Clock::now();
		h.w->step(h.intents);
		out.times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
		out.events += h.w->take_events().size();
		clear_edges(h);
	}
	out.alive = h.w->alive_count();
	out.hash = state_hash(*h.w);
	h.end_scope();
	return out;
}

int mode_core(int secs) {
	const CoreRun r = run_core(secs);
	const Stats s = stats_of(r.times);
	std::printf("sim step over %d ticks (%d s): mean %.3f ms  p95 %.3f ms  p99 %.3f ms  max %.3f ms  bodies %d  events %zu\n", secs * 60, secs,
	            s.mean, s.p95, s.p99, s.max, r.alive, r.events);
	return 0;
}

// ---------------------------------------------------------------- perf_air.gd
int mode_air(int secs) {
	SimHarness h(77);
	Rng rng;
	rng.set_seed(7777);
	ActorState* a = h.actor("A", V3(-2, 0, 4), 0, D({{"glide", true}}), Sim::AIR);
	ActorState* b = h.actor("B", V3(2, 0, -4), 1, D({{"magma", true}, {"heat_draw", true}, {"lightning", true}, {"glide", true}}), Sim::EARTH);
	std::map<int, int64_t> next = {{a->id, 0}, {b->id, 0}};
	std::map<int, int64_t> until = {{a->id, 0}, {b->id, 0}};
	std::vector<double> times;
	const int ticks = secs * 60;
	times.reserve(static_cast<size_t>(ticks));
	CombatWorld& w = *h.w;
	const double e0 = w.system_energy() - w.ledger_balance();
	const double m0[4] = {w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	int worst_bodies = 0;
	int bad = 0;
	for (int k = 0; k < ticks; ++k) {
		for (ActorState* x : {a, b}) {
			ActorIntent& it = h.it(x);
			ActorState* other = x == a ? b : a;
			if (w.tick >= until[x->id]) {
				it.attack_held = false;
				it.guard_held = false;
				it.tech_held = false;
				it.evade_held = false;
			}
			if (w.tick >= next[x->id]) {
				next[x->id] = w.tick + 4 + static_cast<int64_t>(rng.randi() % 30u);
				h.aim(x, other->pos - x->pos);
				const double r = static_cast<double>(rng.randf());
				int64_t hold = 0;
				if (r < 0.34) {
					h.press(x, "attack");
					if (static_cast<double>(rng.randf()) < 0.5) it.attack_gesture = 1 + static_cast<int>(rng.randi() % 3u);
					hold = static_cast<int64_t>(rng.randi() % 120u);
				} else if (r < 0.5) {
					h.press(x, "guard");
					if (static_cast<double>(rng.randf()) < 0.5) it.guard_gesture = 1 + static_cast<int>(rng.randi() % 2u);
					hold = 6 + static_cast<int64_t>(rng.randi() % 80u);
				} else if (r < 0.64) {
					h.press(x, "tech");
					hold = 6 + static_cast<int64_t>(rng.randi() % 120u);
				} else if (r < 0.74) {
					h.press(x, "evade");
					it.evade_held = static_cast<double>(rng.randf()) < 0.5;
					const double mx = static_cast<double>(rng.randf()) - 0.5;
					const double mz = static_cast<double>(rng.randf()) - 0.5;
					it.move = V3(mx, 0, mz);
					hold = static_cast<int64_t>(rng.randi() % 50u);
				} else if (r < 0.8 && x == b) {
					h.element(x, static_cast<int>(rng.randi() % 4u));
				} else if (r < 0.9) {
					h.sub(x, static_cast<int>(rng.randi() % 4u));
				} else {
					const double ang = static_cast<double>(rng.randf()) * kTau;
					it.move = V3(std::sin(ang), 0, std::cos(ang));
				}
				until[x->id] = w.tick + hold;
			}
			if (x->health < 25.0) x->health = Sim::HEALTH_MAX;
			if (x == a && x->element != Sim::AIR) x->element = Sim::AIR;
		}
		const Clock::time_point t0 = Clock::now();
		w.step(h.intents);
		times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
		w.take_events();
		worst_bodies = std::max(worst_bodies, w.alive_count());
		clear_edges(h);
		if (k % 60 == 59) {
			for (const auto& ac : w.actors) {
				if (!finite3(ac->pos) || !finite3(ac->vel)) {
					bad += 1;
					std::printf("NON-FINITE actor %s at tick %lld\n", ac->name.c_str(), static_cast<long long>(w.tick));
				}
			}
			for (const BodyRef& bd : w.bodies) {
				if (bd->alive && (!finite3(bd->pos) || !finite3(bd->vel) || !std::isfinite(bd->mass))) {
					bad += 1;
					std::printf("NON-FINITE body %s at tick %lld\n", bd->describe().c_str(), static_cast<long long>(w.tick));
				}
			}
			if (w.alive_count() > Sim::MAX_BODIES + 4) {
				bad += 1;
				std::printf("BODY CAP exceeded: %d\n", w.alive_count());
			}
		}
	}
	const Stats s = stats_of(times);
	const double e1 = w.system_energy() - w.ledger_balance();
	const double m1[4] = {w.water_mass(), w.earth_mass(), w.metal_mass(), w.plant_mass()};
	std::printf("air soak: %d ticks (%d s): mean %.3f ms  p95 %.3f ms  p99 %.3f ms  max %.3f ms  bodies now %d worst %d\n", ticks, secs, s.mean,
	            s.p95, s.p99, s.max, w.alive_count(), worst_bodies);
	std::printf("energy drift %.6f HU  water %.6f earth %.6f metal %.6f plant %.6f kg\n", e1 - e0, m1[0] - m0[0], m1[1] - m0[1], m1[2] - m0[2],
	            m1[3] - m0[3]);
	bool ok = bad == 0 && std::fabs(e1 - e0) < 0.5;
	for (int i = 0; i < 4; ++i) ok = ok && std::fabs(m1[i] - m0[i]) < 1e-3;
	std::printf("%s\n", ok ? "OK" : "FAILED");
	return ok ? 0 : 1;
}

// ---------------------------------------------------------------- Session autoplay duel
int mode_session(int secs) {
	Session s;
	ScenarioOptions o;
	o.autoplay = "duel";
	if (!s.LoadScenario("spar", o)) {
		std::printf("session: failed to load spar\n");
		return 1;
	}
	std::vector<double> times;
	const int ticks = secs * 60;
	times.reserve(static_cast<size_t>(ticks));
	std::vector<Event> evs;
	size_t events = 0;
	const InputFrame idle;
	for (int k = 0; k < ticks; ++k) {
		const Clock::time_point t0 = Clock::now();
		s.Step(idle, 0.0f);
		times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
		s.TakeEvents(evs);
		events += evs.size();
		evs.clear();
	}
	const Stats st = stats_of(times);
	std::printf("session duel step over %d ticks (%d s): mean %.3f ms  p95 %.3f ms  p99 %.3f ms  max %.3f ms  events %zu  bodies %zu\n", ticks,
	            secs, st.mean, st.p95, st.p99, st.max, events, s.GetSnapshot().bodies.size());
	return 0;
}

// ---------------------------------------------------------------- determinism
int mode_determinism(int secs) {
	const CoreRun a = run_core(secs);
	const CoreRun b = run_core(secs);
	const CoreRun c = run_core(secs, 43, 4243);
	const bool same = a.hash == b.hash;
	const bool differs = a.hash != c.hash;
	std::printf("determinism over %d s: seed 42 -> %016llx, again -> %016llx (%s); seed 43 -> %016llx (%s)\n", secs,
	            static_cast<unsigned long long>(a.hash), static_cast<unsigned long long>(b.hash), same ? "identical" : "MISMATCH",
	            static_cast<unsigned long long>(c.hash), differs ? "differs" : "SAME?");
	return same && differs ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
	std::string mode = argc > 1 ? argv[1] : "all";
	int secs = argc > 2 ? std::atoi(argv[2]) : 120;
	if (secs <= 0) secs = 120;
	Moves::ensure_ready();
	int rc = 0;
	if (mode == "core" || mode == "all") rc |= mode_core(secs);
	if (mode == "air" || mode == "all") rc |= mode_air(secs);
	if (mode == "session" || mode == "all") rc |= mode_session(secs);
	if (mode == "determinism" || mode == "all") rc |= mode_determinism(std::min(secs, 60));
	if (mode != "all" && mode != "core" && mode != "air" && mode != "session" && mode != "determinism") {
		std::printf("usage: ff_perf [all|core|air|session|determinism] [seconds]\n");
		return 2;
	}
	return rc;
}
