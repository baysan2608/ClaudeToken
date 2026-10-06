// Port of game/tests/sim/test_energy_and_soak.gd: energy accounting under heat/draw ping-pong, and a 10-minute seeded
// random-input soak of two fully equipped fighters. The soak is run once per seed and cached for the checks below.
// The state hash is an FNV-1a digest of the same fields GDScript hash()es (only compared against itself).
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
constexpr int SOAK_TICKS = 36000;   // 10 simulated minutes
constexpr int STUCK_TICKS = 300;    // 5 s
constexpr double MASS_REL_TOL = 1e-3;
constexpr int SOAK_SEED = 11;

Dict full_kit() {
	return D({{"magma", true}, {"heat_draw", true}, {"lightning", true}, {"redirect_current", true}, {"glide", true}});
}

BodyRef keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }

std::string f2(double v, int prec) {
	char b[64];
	std::snprintf(b, sizeof(b), "%.*f", prec, v);
	return b;
}

std::string ledger_text(const EnergyLedger& l) {
	return S("{generated ", l.generated, ", ambient ", l.ambient, ", vapor ", l.vapor, ", reserve_dissipated ", l.reserve_dissipated,
	         ", vented ", l.vented, ", spent ", l.spent, ", freeze_dump ", l.freeze_dump, ", removed ", l.removed, "}");
}

// ================================================================ soak state

struct Soak {
	std::unique_ptr<SimHarness> h;
	Rng rng;
	std::map<int, int64_t> hold_until, next_decision, move_until;
	std::map<std::string, std::vector<std::string>> violations;   // category -> first few
	std::map<std::string, int> event_counts;
	int max_bodies = 0;
	double worst_stone = 0.0, worst_water = 0.0, worst_energy = 0.0;
	int64_t worst_energy_tick = -1;
	std::map<int, int> longest_busy, longest_stun, busy_streak, stun_streak;
	std::vector<uint64_t> checkpoints;
	uint64_t final_hash = 0;
	double min_balance = 0.0, water0 = 0.0, stone0 = 0.0, e0 = 0.0;
	int heals = 0;

	void note(const std::string& cat, const std::string& msg) {
		std::vector<std::string>& list = violations[cat];
		if (list.size() < 4) list.push_back(S("t", static_cast<long long>(h->w->tick), ": ", msg));
	}
};
using SoakRef = std::shared_ptr<Soak>;

bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}

SoakRef make_soak(int seed_value) {
	SoakRef s = std::make_shared<Soak>();
	s->h = std::make_unique<SimHarness>(static_cast<uint64_t>(seed_value));
	s->rng.set_seed(static_cast<uint64_t>(seed_value) * 7919u + 13u);
	const int ea = static_cast<int>(s->rng.randi() % 4u);
	const int eb = static_cast<int>(s->rng.randi() % 4u);
	ActorState* pa = s->h->actor("A", V3(0, 0, 6), 0, full_kit(), ea);
	ActorState* pb = s->h->actor("B", V3(0, 0, -6), 1, full_kit(), eb);
	for (ActorState* x : {pa, pb}) {
		s->hold_until[x->id] = 0;
		s->next_decision[x->id] = 0;
		s->move_until[x->id] = 0;
		s->longest_busy[x->id] = 0;
		s->longest_stun[x->id] = 0;
		s->busy_streak[x->id] = 0;
		s->stun_streak[x->id] = 0;
	}
	s->water0 = s->h->w->water_mass();
	s->stone0 = s->h->w->stone_mass();
	s->e0 = s->h->w->system_energy() - s->h->w->ledger_balance();
	return s;
}

void random_input(Soak& s, ActorState* x) {
	SimHarness& h2 = *s.h;
	const int64_t t = h2.w->tick;
	ActorIntent& it = h2.it(x);
	if (t >= s.hold_until[x->id]) {
		if (it.attack_held) h2.release(x, "attack");
		if (it.tech_held) {
			if (s.rng.randf() < 0.15) h2.cancel_tech(x);
			else h2.release(x, "tech");
		}
		if (it.guard_held) h2.release(x, "guard");
	}
	if (t >= s.move_until[x->id]) it.move = Vec3();
	if (t < s.next_decision[x->id]) return;
	s.next_decision[x->id] = t + 3 + static_cast<int64_t>(s.rng.randi() % 38u);
	// GDScript: actors[1] if x.id == 1 else actors[0] (actor ids start at 1, so each aims at the other).
	ActorState* other = x->id == 1 ? h2.w->actors[1].get() : h2.w->actors[0].get();
	const double r = static_cast<double>(s.rng.randf());
	if (s.rng.randf() < 0.35) {
		const double ang = static_cast<double>(s.rng.randf()) * kTau;
		h2.aim(x, V3(std::sin(ang), 0, std::cos(ang)));
	} else if (s.rng.randf() < 0.3) {
		h2.aim(x, other->pos - x->pos);
	} else if (s.rng.randf() < 0.2) {
		it.aim_active = false;
	}
	int64_t hold_ticks = 0;
	if (r < 0.24) {
		h2.press(x, "attack");
		hold_ticks = s.rng.randf() < 0.4 ? 0 : 5 + static_cast<int64_t>(s.rng.randi() % 85u);
	} else if (r < 0.46) {
		h2.press(x, "tech");
		hold_ticks = 6 + static_cast<int64_t>(s.rng.randi() % 150u);
	} else if (r < 0.60) {
		h2.press(x, "guard");
		hold_ticks = 4 + static_cast<int64_t>(s.rng.randi() % 70u);
	} else if (r < 0.72) {
		const double ang2 = static_cast<double>(s.rng.randf()) * kTau;
		it.move = V3(std::sin(ang2), 0, std::cos(ang2));
		s.move_until[x->id] = t + 2;
		h2.press(x, "evade");
	} else if (r < 0.82) {
		h2.element(x, static_cast<int>(s.rng.randi() % 4u));
	} else if (r < 0.94) {
		const double ang3 = static_cast<double>(s.rng.randf()) * kTau;
		it.move = V3(std::sin(ang3), 0, std::cos(ang3)) * f32(0.3 + 0.7 * static_cast<double>(s.rng.randf()));
		s.move_until[x->id] = t + 10 + static_cast<int64_t>(s.rng.randi() % 80u);
	}
	s.hold_until[x->id] = t + hold_ticks;
}

struct Fnv {
	uint64_t v = 1469598103934665603ull;
	void bytes(const void* p, size_t n) {
		const unsigned char* c = static_cast<const unsigned char*>(p);
		for (size_t i = 0; i < n; ++i) {
			v ^= c[i];
			v *= 1099511628211ull;
		}
	}
	void d(double x) { bytes(&x, sizeof(x)); }
	void i(int64_t x) { bytes(&x, sizeof(x)); }
	void s(const std::string& x) {
		bytes(x.data(), x.size());
		i(static_cast<int64_t>(x.size()));
	}
	void vec(Vec3 x) {
		d(static_cast<double>(x.x));
		d(static_cast<double>(x.y));
		d(static_cast<double>(x.z));
	}
};

uint64_t state_hash(const Soak& s) {
	const CombatWorld& w = *s.h->w;
	Fnv f;
	f.i(w.tick);
	f.i(static_cast<int64_t>(w.rng.state()));
	const EnergyLedger& l = w.ledger;
	for (double x : {l.generated, l.ambient, l.vapor, l.reserve_dissipated, l.vented, l.spent, l.freeze_dump, l.removed}) f.d(x);
	const MassLedger& m = w.mass_ledger;
	for (double x : {m.ground_taken, m.ground_returned, m.vapor, m.evaporated, m.metal_taken, m.metal_returned, m.water_to_plant,
	                 m.plant_from_ground, m.plant_returned, m.moisture_taken, m.burned, m.sand_to_glass, m.glass_to_sand, m.sand_to_sandstone})
		f.d(x);
	for (const auto& xp : w.actors) {
		const ActorState& x = *xp;
		f.vec(x.pos);
		f.vec(x.vel);
		for (double v : {x.facing, x.health, x.balance, x.focus, x.heat_reserve, x.wetness, x.water_carried}) f.d(v);
		f.i(x.element);
		f.d(x.stun);
		f.s(x.stun_kind);
		f.d(x.iframes);
		f.i(x.guarding ? 1 : 0);
		f.i(x.held_body);
		f.i(x.wall_body);
		f.s(x.action != nullptr ? x.action->id : std::string());
		f.i(x.action != nullptr ? static_cast<int64_t>(x.action->phase) : -1);
	}
	for (const BodyRef& b : w.bodies) {
		if (!b->alive) continue;
		f.i(b->id);
		f.i(static_cast<int64_t>(b->mat));
		f.i(static_cast<int64_t>(b->form));
		f.i(static_cast<int64_t>(b->phase));
		f.d(b->mass);
		f.d(b->temp);
		f.d(b->liquid);
		f.vec(b->pos);
		f.vec(b->vel);
		f.i(b->controller);
		f.i(b->attack_id);
		f.d(b->wall_rise);
		f.d(b->wave_budget);
	}
	return f.v;
}

void check_tick(Soak& s) {
	CombatWorld& w = *s.h->w;
	const double half = w.arena.half_size;
	for (const auto& xp : w.actors) {
		ActorState* x = xp.get();
		const double px = static_cast<double>(x->pos.x), py = static_cast<double>(x->pos.y), pz = static_cast<double>(x->pos.z);
		if (!finite3(x->pos) || !finite3(x->vel) || !std::isfinite(x->facing)) {
			s.note("nan_actor", S(x->name, " pos ", x->pos, " vel ", x->vel, " facing ", x->facing));
		} else if (std::fabs(px) > half + 1e-6 || std::fabs(pz) > half + 1e-6 || py < -1.0 || py > 40.0) {
			s.note("out_of_arena", S(x->name, " at ", x->pos));
		}
		if (!(x->focus >= 0.0 && x->focus <= Sim::FOCUS_MAX + 1e-9)) s.note("focus_range", S(x->name, " focus ", f2(x->focus, 5)));
		if (!(x->heat_reserve >= 0.0 && x->heat_reserve <= Sim::RESERVE_MAX + 1e-9))
			s.note("reserve_range", S(x->name, " reserve ", f2(x->heat_reserve, 5)));
		if (!(x->health >= 0.0 && x->health <= Sim::HEALTH_MAX + 1e-9)) s.note("health_range", S(x->name, " health ", f2(x->health, 5)));
		if (!(x->balance <= Sim::BALANCE_MAX + 1e-9)) s.note("balance_range", S(x->name, " balance ", f2(x->balance, 5)));
		s.min_balance = std::min(s.min_balance, x->balance);
		if (!(x->wetness >= 0.0 && x->wetness <= 1.0 + 1e-9)) s.note("wetness_range", S(x->name, " wetness ", f2(x->wetness, 5)));
		if (!(x->water_carried >= -1e-9 && x->water_carried <= 6.0 + 1e-9))
			s.note("waterskin_range", S(x->name, " carries ", f2(x->water_carried, 5)));
		// Stuck detection.
		const ActorIntent& it = s.h->it(x);
		const bool holding = it.attack_held || it.guard_held || it.tech_held;
		const bool busy = x->action != nullptr || x->stun > 0.0;
		if (busy && !holding) s.busy_streak[x->id] += 1;
		else s.busy_streak[x->id] = 0;
		if (x->stun > 0.0) s.stun_streak[x->id] += 1;
		else s.stun_streak[x->id] = 0;
		s.longest_busy[x->id] = std::max(s.longest_busy[x->id], s.busy_streak[x->id]);
		s.longest_stun[x->id] = std::max(s.longest_stun[x->id], s.stun_streak[x->id]);
		if (s.busy_streak[x->id] == STUCK_TICKS + 1)
			s.note("stuck_action", S(x->name, " busy for 5 s without holding anything: action ", x->action ? x->action->id : std::string("none"),
			                         " phase ", x->action ? x->action->phase_name() : "-", " stun ", f2(x->stun, 2), " (", x->stun_kind, ")"));
		if (s.stun_streak[x->id] == STUCK_TICKS + 1) s.note("stuck_stun", S(x->name, " stunned for 5 s (", x->stun_kind, ")"));
	}
	int alive = 0;
	for (const BodyRef& body : w.bodies) {
		if (!body->alive) continue;
		alive += 1;
		if (!finite3(body->pos) || !finite3(body->vel) || !std::isfinite(body->mass) || !std::isfinite(body->temp) || !std::isfinite(body->liquid))
			s.note("nan_body", body->describe());
		else if (body->mass < -1e-9)
			s.note("negative_mass", body->describe());
	}
	s.max_bodies = std::max(s.max_bodies, alive);
	if (alive > Sim::MAX_BODIES) s.note("body_cap", S(alive, " alive bodies (cap ", Sim::MAX_BODIES, ")"));
	const double sd = std::fabs(w.stone_mass() - s.stone0);
	s.worst_stone = std::max(s.worst_stone, sd);
	if (sd > MASS_REL_TOL * std::max(1.0, w.mass_ledger.ground_taken))
		s.note("stone_mass", S("stone mass drifted ", f2(sd, 5), " kg (taken ", f2(w.mass_ledger.ground_taken, 1), ")"));
	const double wd = std::fabs(w.water_mass() - s.water0);
	s.worst_water = std::max(s.worst_water, wd);
	if (wd > MASS_REL_TOL * s.water0) s.note("water_mass", S("water mass drifted ", f2(wd, 5), " kg"));
}

SoakRef run_soak(int seed_value, int ticks = SOAK_TICKS) {
	SoakRef s = make_soak(seed_value);
	CombatWorld& w = *s->h->w;
	for (int k = 0; k < ticks; ++k) {
		for (const auto& xp : w.actors) {
			ActorState* x = xp.get();
			random_input(*s, x);
			if (x->health < 25.0) {
				x->health = Sim::HEALTH_MAX;   // keep the fight going: the referee revives a loser
				s->heals += 1;
			}
		}
		s->h->step();
		for (const Dict& e : s->h->log) s->event_counts[ev_s(e, "type")] += 1;
		s->h->log.clear();
		check_tick(*s);
		if (k % 600 == 599) {
			const double d = std::fabs(w.system_energy() - w.ledger_balance() - s->e0);
			if (d > s->worst_energy) {
				s->worst_energy = d;
				s->worst_energy_tick = w.tick;
			}
			s->checkpoints.push_back(state_hash(*s));
		}
	}
	s->final_hash = state_hash(*s);
	return s;
}

SoakRef soak_for(int seed_value) {
	static std::map<int, SoakRef> cache;
	SoakRef& slot = cache[seed_value];
	if (slot == nullptr) slot = run_soak(seed_value);
	return slot;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
	std::string o;
	for (size_t i = 0; i < v.size(); ++i) o += (i ? sep : "") + v[i];
	return o;
}

std::string counts_text(const std::map<std::string, int>& m) {
	std::string o = "{";
	bool first = true;
	for (const auto& [k, v] : m) {
		o += S(first ? "" : ", ", k, ": ", v);
		first = false;
	}
	return o + "}";
}

std::string ids_text(const std::map<int, int>& m) {
	std::string o = "{";
	bool first = true;
	for (const auto& [k, v] : m) {
		o += S(first ? "" : ", ", k, ": ", v);
		first = false;
	}
	return o + "}";
}

// ================================================================ energy ping-pong fixture

struct ES : HarnessCase {
	ActorState* a = nullptr;
	ActorState* b = nullptr;
	BodyRef stone;

	double _e0 = 0.0;
	std::map<int, double> _prev_focus;
	double _focus_spent = 0.0;
	double _max_reserve = 0.0;
	double _worst_drift = 0.0;
	int64_t _drift_tick = -1;
	double _reserve_over = 0.0;

	void _fire_pair(uint64_t seed_value = 5) {
		SimHarness& hh = H(seed_value);
		a = hh.actor("A", V3(0, 0, 2), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		b = hh.actor("B", V3(0, 0, -2), 1, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		stone = keep(hh.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0, 0.2, 0), "scenario"));
		hh.step(20);
		hh.log.clear();
		_begin_accounting();
	}
	void _begin_accounting() {
		_e0 = h().w->system_energy() - h().w->ledger_balance();
		_prev_focus.clear();
		for (const auto& x : h().w->actors) _prev_focus[x->id] = x->focus;
		_focus_spent = 0.0;
		_max_reserve = 0.0;
		_worst_drift = 0.0;
		_drift_tick = -1;
		_reserve_over = 0.0;
	}
	// Steps n ticks, checking the energy identity, reserve cap and Focus accounting every tick.
	void _run(int n) {
		for (int k = 0; k < n; ++k) {
			h().step();
			const double drift = std::fabs(h().w->system_energy() - h().w->ledger_balance() - _e0);
			if (drift > _worst_drift) {
				_worst_drift = drift;
				_drift_tick = h().w->tick;
			}
			for (const auto& x : h().w->actors) {
				_max_reserve = std::max(_max_reserve, x->heat_reserve);
				_reserve_over = std::max(_reserve_over, x->heat_reserve - Sim::RESERVE_MAX);
				const double d = _prev_focus[x->id] - x->focus;
				if (d > 0.0) _focus_spent += d;   // spends and regen never share a tick: a drop is a spend
				_prev_focus[x->id] = x->focus;
			}
		}
	}
	static double _tolerance() { return 0.05; }

	BodyRef _molten_blob() {
		h().w->remove_body(*stone, "scenario");
		BodyRef blob = keep(h().w->spawn_body(Mat::Stone, Form::Blob, 40.0, V3(0, 0.2, 0.5), "scenario"));
		blob->temp = 1200.0;
		blob->liquid = 1.0;
		blob->phase = Phase::Molten;
		return blob;
	}

	void _report(const Soak& s, std::initializer_list<const char*> cats) {
		for (const char* c : cats) {
			auto found = s.violations.find(c);
			const std::vector<std::string> list = found != s.violations.end() ? found->second : std::vector<std::string>();
			check(list.empty(), S(c, " violations: ", join(list, "; ")));
		}
	}
};
}  // namespace

FF_TEST_F(test_energy_and_soak, ES, test_heat_draw_pingpong_keeps_the_energy_ledger_balanced) {
	_fire_pair();
	SimHarness& h = this->h();
	const int rounds = 12;
	std::map<int, double> drew = {{a->id, 0.0}, {b->id, 0.0}};
	for (int r = 0; r < rounds; ++r) {
		ActorState* heater = r % 2 == 0 ? a : b;
		ActorState* drawer = r % 2 == 0 ? b : a;
		h.log.clear();
		// Heater seizes the stone and pours heat into it (reserve first, then Focus), then lets go.
		h.press(heater, "tech");
		_run(45);
		h.cancel_tech(heater);
		_run(4);
		h.it(heater).tech_cancel = false;
		check(stone->alive && stone->controller == -1, S("round ", r, ": the stone was dropped, not lost"));
		// Drawer pulls the heat back out into its own reserve.
		const double reserve0 = drawer->heat_reserve;
		h.press(drawer, "tech");
		double peak = reserve0;
		for (int k = 0; k < 110; ++k) {
			_run(1);
			peak = std::max(peak, drawer->heat_reserve);
		}
		h.release(drawer, "tech");
		_run(20);
		drew[drawer->id] += std::max(0.0, peak - reserve0);
		check(_worst_drift <= _tolerance(),
		      S("round ", r, ": energy ledger drifted by ", f2(_worst_drift, 5), " HU at tick ", static_cast<long long>(_drift_tick)));
		check(_reserve_over <= 1e-9, S("round ", r, ": a reserve exceeded RESERVE_MAX by ", f2(_reserve_over, 6)));
		// Alternate a flare from the drawer's reserve into the heater: more ledger paths.
		if (r % 3 == 2) {
			h.press(drawer, "attack");
			_run(1);
			h.release(drawer, "attack");
			_run(30);
		}
		if (h.w->get_actor(heater->id)->balance < 30.0) heater->balance = 100.0;
		heater->health = 100.0;
		drawer->health = 100.0;
	}
	check(_worst_drift <= _tolerance(), S("final: energy ledger drift ", f2(_worst_drift, 5), " HU (worst at tick ", static_cast<long long>(_drift_tick), ")"));
	near(h.w->system_energy() - _e0, h.w->ledger_balance(), _tolerance(), "system_energy delta == ledger_balance at the end");
	check(drew[a->id] > 50.0 && drew[b->id] > 50.0, S("both fighters really drew heat (A ", f2(drew[a->id], 0), " HU, B ", f2(drew[b->id], 0), " HU)"));
	check(_max_reserve > 100.0, S("reserves filled during the exchange (max ", f2(_max_reserve, 0), " HU)"));
	check(_max_reserve <= Sim::RESERVE_MAX + 1e-9, S("no reserve above RESERVE_MAX (max ", f2(_max_reserve, 3), ")"));
	const EnergyLedger& l = h.w->ledger;
	check(l.generated > 0.0 && l.reserve_dissipated > 0.0 && l.ambient < 0.0, "the ledger saw generation, dissipation and air loss " + ledger_text(l));
	const double cap = _focus_spent * Sim::HU_PER_FOCUS;
	check(l.generated <= cap + 1e-6, S("heat created (", f2(l.generated, 1), " HU) never exceeds Focus spent (", f2(_focus_spent, 1),
	                                   ") x HU_PER_FOCUS = ", f2(cap, 1)));
	check(l.generated > 0.0, "some heat was actually created from Focus");
	note(S("pingpong: generated ", f2(l.generated, 0), " HU from ", f2(_focus_spent, 1), " Focus, max reserve ", f2(_max_reserve, 0),
	       ", worst drift ", f2(_worst_drift, 6)));
	near(stone->mass, 20.0, 1e-9, "the stone survived with its mass");
}

FF_TEST_F(test_energy_and_soak, ES, test_draw_stops_at_reserve_cap_and_never_overfills) {
	_fire_pair();
	SimHarness& h = this->h();
	// A 40 kg blob of molten rock holds ~870 HU, far more than a reserve can take (500 HU).
	BodyRef blob = _molten_blob();
	_begin_accounting();
	const double before = blob->thermal_energy();
	h.press(a, "tech");
	_run(30);
	check(a->action != nullptr && dstr(a->action->data, "mode", "") == "DRAW", "A chose DRAW for a molten target");
	_run(300);
	check(_max_reserve > Sim::RESERVE_MAX - 30.0, S("the reserve filled to the cap (max ", f2(_max_reserve, 1), ")"));
	check(_reserve_over <= 1e-9, S("the reserve never exceeded RESERVE_MAX (over by ", f2(_reserve_over, 6), ")"));
	check(a->heat_reserve <= Sim::RESERVE_MAX + 1e-9, S("reserve ", f2(a->heat_reserve, 2), " <= cap"));
	check(blob->thermal_energy() >= 0.0, "the blob keeps non-negative heat");
	check(blob->thermal_energy() < before, "heat left the blob");
	check(_worst_drift <= _tolerance(), S("energy ledger drift ", f2(_worst_drift, 5), " HU at tick ", static_cast<long long>(_drift_tick)));
	const double drawn_focus = _focus_spent;
	check(drawn_focus <= 500.0 / Sim::DRAW_HU_PER_FOCUS * 1.6 + 1.0, S("drawing costs only Focus/DRAW_HU_PER_FOCUS (", f2(drawn_focus, 1), " Focus)"));
	check(h.w->ledger.generated == 0.0, "drawing creates no heat");
}

FF_TEST_F(test_energy_and_soak, ES, test_reserve_full_event_is_reported) {
	_fire_pair();
	SimHarness& h = this->h();
	BodyRef blob = _molten_blob();
	h.press(a, "tech");
	bool full = false;
	for (int k = 0; k < 400; ++k) {
		h.step();
		if (h.has_event("reserve_full")) {
			full = true;
			break;
		}
	}
	check(full, "reserve_full reported once the reserve is topped up");
	check(a->heat_reserve <= Sim::RESERVE_MAX + 1e-9, S("at or below the cap (", f2(a->heat_reserve, 2), ")"));
}

FF_TEST_F(test_energy_and_soak, ES, test_vent_dumps_the_reserve_into_the_ledger) {
	// No stone, no water in reach: the thermal technique vents a reserve of at least 40 HU.
	struct Case {
		double reserve;
		bool vents;
	};
	for (const Case& c : {Case{300.0, true}, Case{40.0, true}, Case{39.0, false}}) {
		SimHarness& h = H(5);
		a = h.actor("A", V3(-4, 0, 6), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		h.step(5);
		a->heat_reserve = c.reserve;
		_begin_accounting();
		h.press(a, "tech");
		_run(40);
		h.release(a, "tech");
		_run(60);
		const std::string label = S("reserve ", f2(c.reserve, 0));
		if (c.vents) {
			check(h.has_event("vent"), label + ": vented");
			check(a->heat_reserve == 0.0, S(label, ": emptied (", f2(a->heat_reserve, 2), ")"));
			check(h.w->ledger.vented > c.reserve - 12.0 && h.w->ledger.vented <= c.reserve, S(label, ": ledger.vented ", f2(h.w->ledger.vented, 2)));
		} else {
			check(!h.has_event("vent"), label + ": too little to vent");
		}
		check(_worst_drift <= _tolerance(), S(label, ": ledger drift ", f2(_worst_drift, 5)));
		check(a->action == nullptr, label + ": no stuck action");
	}
}

FF_TEST_F(test_energy_and_soak, ES, test_reserve_passively_dissipates_and_is_accounted) {
	SimHarness& h = H(5);
	a = h.actor("A", V3(-4, 0, 6), 0, Dict(), Sim::FIRE);
	h.step(5);
	a->heat_reserve = 100.0;
	_begin_accounting();
	_run(60);
	near(a->heat_reserve, 100.0 - Sim::RESERVE_DISSIPATE, 0.01, "reserve dissipates at RESERVE_DISSIPATE per second");
	check(_worst_drift <= _tolerance(), S("ledger drift ", f2(_worst_drift, 6)));
	_run(60 * 20);
	check(a->heat_reserve == 0.0, "reserve eventually empties and never goes negative");
	near(h.w->ledger.reserve_dissipated, 100.0, 1e-6, "all 100 HU went into reserve_dissipated");
	check(_worst_drift <= _tolerance(), S("ledger drift ", f2(_worst_drift, 6)));
}

// ================================================================ targeted energy-leak regressions

FF_TEST_F(test_energy_and_soak, ES, test_heavy_earth_gather_onto_a_warm_stone_creates_no_heat) {
	// A heavy earth attack gathers extra stone from the ground (ambient temperature) into the stone in hand. If the stone
	// in hand is warm, the energy of the combined body must be the sum of the parts, not the warm temperature applied to
	// the new mass.
	SimHarness& h = H(5);
	a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
	b = h.actor("B", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	stone = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0, 0.2, 5.2), "scenario"));
	stone->temp = 400.0;
	h.step(20);
	h.log.clear();
	_begin_accounting();
	const double heat0 = stone->thermal_energy();
	h.press(a, "attack");
	_run(60);
	check(h.has_event("acquire"), "the heavy attack gathered extra stone");
	check(std::fabs(stone->mass - dnum(Moves::defs().get("earth_attack").as_dict(), "heavy_mass")) < 1e-9,
	      S("the stone grew to the heavy mass (", f2(stone->mass, 1), " kg)"));
	check(stone->thermal_energy() <= heat0 + 1e-6,
	      S("gathering ambient stone cannot add heat: ", f2(heat0, 2), " HU -> ", f2(stone->thermal_energy(), 2), " HU"));
	check(_worst_drift <= _tolerance(), S("energy ledger drifted by ", f2(_worst_drift, 3), " HU at tick ", static_cast<long long>(_drift_tick)));
	h.release(a, "attack");
	_run(60);
	check(_worst_drift <= _tolerance(), S("energy ledger drift after the throw ", f2(_worst_drift, 3), " HU"));
}

FF_TEST_F(test_energy_and_soak, ES, test_drawing_from_a_partially_thawed_puddle_conserves_energy) {
	// A puddle at 0 degC that is 30% ice holds negative latent energy. Moving its water into a stream moves that energy
	// with it (or turns the stream into slush); it must not appear or vanish.
	SimHarness& h = H(5);
	a = h.actor("D", V3(-4, 0, 5), 0, Dict(), Sim::WATER);
	h.step(5);
	BodyRef puddle = keep(h.w->spawn_body(Mat::Water, Form::Puddle, 4.0, V3(-4, 0, 6.5), "scenario"));
	puddle->update_radius_puddle();
	puddle->temp = 0.0;
	puddle->liquid = 0.7;
	a->water_carried = 0.0;
	_begin_accounting();
	const double water0 = h.w->water_mass();
	h.press(a, "tech");
	_run(40);
	MatBody* held = h.w->held(*a);
	check(held != nullptr && begins_with(held->origin, "draw:"), "D is holding water drawn from the puddle");
	check(puddle->mass < 4.0, S("the puddle was drawn from (", f2(puddle->mass, 2), " kg left)"));
	near(h.w->water_mass(), water0, 1e-6, "water mass conserved");
	check(_worst_drift <= _tolerance(), S("energy ledger drifted by ", f2(_worst_drift, 3), " HU at tick ", static_cast<long long>(_drift_tick)));
}

// ================================================================ soak

FF_TEST_F(test_energy_and_soak, ES, test_soak_actually_exercises_the_rules) {
	SoakRef s = soak_for(SOAK_SEED);
	check(s->h->w->tick == SOAK_TICKS, S("ran ", static_cast<long long>(s->h->w->tick), " ticks"));
	auto count = [&](const std::string& t) {
		auto f = s->event_counts.find(t);
		return f != s->event_counts.end() ? f->second : 0;
	};
	for (const char* t : {"action", "hit", "launch", "guard", "evade", "control_won", "flare", "split"})
		check(count(t) > 0, S("the soak produced '", t, "' events (", count(t), ")"));
	int kinds = 0;
	for (const char* t : {"lightning", "wall", "shatter", "steam", "thermal", "perfect_deflect", "knockdown", "getup", "wave_settle", "drawing",
	                      "updraft", "intercept"})
		if (count(t) > 0) kinds += 1;
	check(kinds >= 7, S("a broad mix of mechanics fired (", kinds, " of 12 spot checks)"));
	note("events: " + counts_text(s->event_counts));
	note(S("max bodies ", s->max_bodies, ", worst stone drift ", f2(s->worst_stone, 6), " kg, worst water drift ", f2(s->worst_water, 6),
	       " kg, worst energy drift ", f2(s->worst_energy, 4), " HU, min balance ", f2(s->min_balance, 1), ", heals ", s->heals));
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_actors_stay_finite_in_the_arena_and_in_range) {
	SoakRef s = soak_for(SOAK_SEED);
	_report(*s, {"nan_actor", "nan_body", "out_of_arena", "focus_range", "reserve_range", "health_range", "wetness_range", "waterskin_range",
	             "negative_mass"});
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_body_count_never_exceeds_max_bodies) {
	SoakRef s = soak_for(SOAK_SEED);
	_report(*s, {"body_cap"});
	check(s->max_bodies <= Sim::MAX_BODIES, S("peak alive bodies ", s->max_bodies, " <= ", Sim::MAX_BODIES));
	check(s->max_bodies >= 4, S("the soak really created bodies (peak ", s->max_bodies, ")"));
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_conserves_stone_and_water_mass) {
	SoakRef s = soak_for(SOAK_SEED);
	_report(*s, {"stone_mass", "water_mass"});
	const double taken = s->h->w->mass_ledger.ground_taken;
	check(s->worst_stone <= MASS_REL_TOL * std::max(1.0, taken), S("stone mass drift ", f2(s->worst_stone, 6), " kg of ", f2(taken, 0), " kg handled"));
	check(s->worst_water <= MASS_REL_TOL * s->water0, S("water mass drift ", f2(s->worst_water, 6), " kg of ", f2(s->water0, 0), " kg"));
	check(taken > 100.0, S("lots of stone moved through the ledger (", f2(taken, 0), " kg)"));
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_no_actor_is_stuck_in_an_action_or_stun) {
	SoakRef s = soak_for(SOAK_SEED);
	_report(*s, {"stuck_action", "stuck_stun"});
	for (const auto& [id, busy] : s->longest_busy) {
		check(busy <= STUCK_TICKS, S("actor ", id, ": longest busy streak without holding a button ", busy, " ticks"));
		check(s->longest_stun[id] <= STUCK_TICKS, S("actor ", id, ": longest stun ", s->longest_stun[id], " ticks"));
	}
	note("longest busy " + ids_text(s->longest_busy) + ", longest stun " + ids_text(s->longest_stun));
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_energy_ledger_stays_balanced) {
	SoakRef s = soak_for(SOAK_SEED);
	check(s->worst_energy <= 1.0, S("energy ledger drift peaked at ", f2(s->worst_energy, 4), " HU (tick ", static_cast<long long>(s->worst_energy_tick), ")"));
	const CombatWorld& w = *s->h->w;
	near(w.system_energy() - w.ledger_balance() - s->e0, 0.0, 1.0, "final energy identity");
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_balance_stays_in_its_documented_range) {
	// Sim documents Balance as 0..100.
	SoakRef s = soak_for(SOAK_SEED);
	_report(*s, {"balance_range"});
	check(s->min_balance >= -1e-9, S("balance never went below 0 (min ", f2(s->min_balance, 2), ")"));
}

FF_TEST_F(test_energy_and_soak, ES, test_soak_is_deterministic_for_a_seed) {
	SoakRef first = soak_for(SOAK_SEED);
	SoakRef again = run_soak(SOAK_SEED);
	check(first->checkpoints.size() == again->checkpoints.size(), "same number of checkpoints");
	int diverged = -1;
	for (size_t k = 0; k < std::min(first->checkpoints.size(), again->checkpoints.size()); ++k) {
		if (first->checkpoints[k] != again->checkpoints[k]) {
			diverged = static_cast<int>(k);
			break;
		}
	}
	check(diverged < 0, S("state hashes diverged at checkpoint ", diverged, " (tick ", (diverged + 1) * 600, ")"));
	check(first->final_hash == again->final_hash, S("same seed: identical final state hash (", first->final_hash, " vs ", again->final_hash, ")"));
	check(first->event_counts == again->event_counts, "same seed: identical event counts");
	// A different seed must really play differently (the hash is sensitive): compare tick 6000.
	SoakRef other = run_soak(SOAK_SEED + 1, 6000);
	check(first->checkpoints.size() > 9 && other->final_hash != first->checkpoints[9], "a different seed gives a different state at tick 6000");
}
