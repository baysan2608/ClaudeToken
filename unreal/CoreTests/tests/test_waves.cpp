// Port of game/tests/sim/test_waves.gd: lava waves: a magma fighter seizes a loose stone with the Fire technique, heats it
// until MOLTEN, aims and releases (pour). The wave flows: stopped by anything taller than WAVE_STEP, drops off ledges
// (wave_drop, 1 m of budget), quenched by water (steam) and cooled to rock, travels at most base_budget + budget_per_kg x
// mass (plus one step of overshoot).
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>
#include <map>
#include <optional>

using namespace ff;
using namespace fft;

namespace {
constexpr double STEP_EPS = 0.13;   // one tick of wave travel (7.5 m/s / 60)

struct Wv : HarnessCase {
	ActorState* p = nullptr;
	BodyRef stone;

	void _world(Vec3 p_pos, Vec3 stone_pos, double mass = 20.0, std::optional<Vec3> enemy_pos = std::nullopt) {
		SimHarness& hh = H(1);
		p = hh.actor("P", p_pos, 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		stone = hh.w->spawn_body(Mat::Stone, Form::Chunk, mass, stone_pos, "scenario")->shared_from_this();
		if (enemy_pos) hh.actor("E", *enemy_pos, 1, Dict(), Sim::EARTH);
		hh.step(25);
		hh.log.clear();
	}
	// Seize + heat the stone until MOLTEN, aim, release. True when the wave exists.
	bool _pour(Vec3 dir) {
		SimHarness& hh = h();
		hh.aim(p, dir);
		hh.press(p, "tech");
		const int n = hh.until([&]() { return stone->phase == Phase::Molten; }, 240);
		if (n < 0) {
			check(false, S("the stone never became molten (", stone->describe(), ", focus ", p->focus, ")"));
			return false;
		}
		hh.release(p, "tech");
		const int m = hh.until([&]() { return stone->form == Form::Wave; }, 60);
		check(m > 0, "releasing a molten stone pours a wave");
		return m > 0;
	}
	struct Run {
		double dist = 0.0, min_z = 0.0, max_z = 0.0;
		int ticks = 0;
	};
	// Follows the wave until it stops being a wave.
	Run _follow(int max_ticks = 900) {
		SimHarness& hh = h();
		Run r;
		Vec3 last = stone->pos;
		r.min_z = r.max_z = stone->pos.z;
		for (int k = 0; k < max_ticks; ++k) {
			hh.step();
			if (stone->form != Form::Wave) break;
			++r.ticks;
			r.dist += Vec2(stone->pos.x - last.x, stone->pos.z - last.z).length();
			last = stone->pos;
			r.min_z = minf(r.min_z, stone->pos.z);
			r.max_z = maxf(r.max_z, stone->pos.z);
		}
		return r;
	}
	static double _budget(double mass) {
		const Dict d = Moves::defs().get("pour").as_dict();
		return dnum(d, "base_budget") + dnum(d, "budget_per_kg") * mass;
	}
	int _count(const std::string& type) { return static_cast<int>(h().events(type).size()); }
	std::string settle_why() { return dstr(h().last_event("wave_settle"), "why", ""); }
};
}  // namespace

// ---------------------------------------------------------------- stopped by solids

FF_TEST_F(test_waves, Wv, test_wave_stops_before_the_cover_wall_then_cools_to_rock) {
	_world(V3(-3.75, 0, 2.5), V3(-3.75, 0.2, 1.5));
	SimHarness& h = this->h();
	const ArenaSolid* cover = h.w->arena.solid_named("cover_wall");
	check(cover != nullptr, "the lab has a cover wall");
	if (cover == nullptr) return;
	const double wall_face = cover->max.z;   // the +z face the wave runs into
	if (!_pour(V3(0, 0, -1))) return;
	check(stone->attack_owner == p->id && stone->attack_id != 0, "the wave is P's attack while it flows");
	const Run run = _follow();
	check(_count("wave_blocked") == 1, S("exactly one wave_blocked, got ", _count("wave_blocked")));
	const std::vector<Dict> settle = h.events("wave_settle");
	check(settle.size() == 1 && ev_s(settle[0], "why") == "blocked", "settles because it was blocked");
	check(stone->form == Form::Blob, S("a still-liquid settled wave is a lava blob (", Sim::form_name(stone->form), ")"));
	check(stone->attack_id == 0, "a settled wave is no longer an attack");
	check(stone->pos.z >= wall_face, S("the lava stopped on the near side of the wall (z ", stone->pos.z, ", face ", wall_face, ")"));
	check(run.min_z >= wall_face, S("it never crossed into the wall (min z ", run.min_z, ")"));
	check(wall_face - run.min_z < 0.0 + 0.01, "and never entered the wall's footprint");
	check(!h.has_event("hit"), "nobody was hit");
	near(stone->mass, 20.0, 1e-9, "mass preserved");
	// It stays put and cools to rock without flicker.
	const double z0 = stone->pos.z;
	int changes = 0;
	Phase last = stone->phase;
	for (int k = 0; k < 900; ++k) {
		h.step();
		if (stone->phase != last) {
			++changes;
			last = stone->phase;
		}
	}
	near(stone->pos.z, z0, 1e-9, "the settled lava does not creep");
	check(stone->phase == Phase::Solid && stone->form == Form::Chunk, "later cooled to rock (" + stone->describe() + ")");
	check(h.any_event("transform", [](const Dict& e) { return ev_s(e, "from") == "lava" && ev_s(e, "to") == "rock" && ev_s(e, "why") == "cooled"; }),
	      "lava -> rock transform reported");
	check(changes <= 2, S("monotonic cooling labels (", changes, " changes)"));
	check(stone->temp >= Sim::AMBIENT_C && stone->temp < 400.0, S("the rock is cooling (", ftos(stone->temp, 0), " degC)"));
}

FF_TEST_F(test_waves, Wv, test_wave_stops_at_terrace_and_step_block_but_not_open_ground) {
	// The terrace (0.6 m) and the step block (0.35 m) are higher than WAVE_STEP (0.3): unclimbable.
	_world(V3(0, 0, 6), V3(0, 0.2, 7));
	if (_pour(V3(0, 0, 1))) {
		_follow();
		check(_count("wave_blocked") == 1, "terrace face blocks the wave");
		check(stone->pos.z < 10.0f, S("stopped in front of the terrace (z ", stone->pos.z, ")"));
	}
	_world(V3(6.5, 0, 10.5), V3(7.3, 0.2, 10.5));
	if (_pour(V3(1, 0, 0))) {
		_follow();
		check(_count("wave_blocked") == 1, "step block face blocks the wave");
		check(stone->pos.x < 9.0f, S("stopped in front of the step block (x ", stone->pos.x, ")"));
	}
	// Open ground: not blocked at all.
	_world(V3(-4, 0, 7), V3(-3.2, 0.2, 7));
	if (_pour(V3(1, 0, 0))) {
		_follow();
		check(_count("wave_blocked") == 0, "open ground: no blocking");
		const std::string why = settle_why();
		check(why == "budget" || why == "viscous", "ends by exhausting itself (" + why + ")");
	}
}

FF_TEST_F(test_waves, Wv, test_wave_poured_off_the_terrace_edge_drops_and_continues) {
	_world(V3(0, 0.6, 12.5), V3(0, 0.8, 11.5));
	SimHarness& h = this->h();
	check(std::fabs(p->pos.y - 0.6f) < 1e-6f, "setup: P stands on the terrace");
	if (!_pour(V3(0, 0, -1))) return;
	check(std::fabs(stone->pos.y - 0.6f) < 0.05f, S("the wave starts on top of the terrace (y ", stone->pos.y, ")"));
	std::vector<double> ys;
	double dist = 0.0;
	Vec3 last = stone->pos;
	for (int k = 0; k < 900; ++k) {
		h.step();
		if (stone->form != Form::Wave) break;
		dist += Vec2(stone->pos.x - last.x, stone->pos.z - last.z).length();
		last = stone->pos;
		ys.push_back(stone->pos.y);
	}
	const std::vector<Dict> drops = h.events("wave_drop");
	check(drops.size() == 1, S("exactly one wave_drop for one edge, got ", drops.size()));
	if (drops.size() == 1)
		check(std::fabs(ev_f(drops[0], "from") - 0.6) < 0.01 && std::fabs(ev_f(drops[0], "to")) < 0.01,
		      S("dropped from ", ev_f(drops[0], "from"), " to ", ev_f(drops[0], "to")));
	check(_count("wave_blocked") == 0, "the drop does not block the wave");
	check(stone->pos.z < 10.0f - 1.0f, S("the wave continued well past the edge on lower ground (z ", stone->pos.z, ")"));
	check(std::fabs(stone->pos.y) < 0.01f, S("and now flows at ground level (y ", stone->pos.y, ")"));
	double min_y = 9.0;
	for (double y : ys) min_y = minf(min_y, y);
	check(min_y > -0.01, "it never sank below the ground");
	// The drop costs 1 m of budget: total path + drop <= budget + one step.
	const double bound = _budget(20.0);
	check(dist + 1.0 <= bound + STEP_EPS, S("path ", dist, " m + 1 m drop exceeds the ", bound, " m budget"));
	check(dist > 3.0, S("and it still travelled a good way (", dist, " m)"));
}

// ---------------------------------------------------------------- budget

FF_TEST_F(test_waves, Wv, test_wave_travel_never_exceeds_its_budget) {
	std::map<double, double> travelled;
	for (double m : {10.0, 20.0, 35.0}) {
		_world(V3(-4, 0, 7), V3(-3.2, 0.2, 7), m);
		if (!_pour(V3(1, 0, 0))) continue;
		const Vec3 start = stone->pos;
		const Run run = _follow();
		const double budget = _budget(m);
		check(run.dist <= budget + STEP_EPS, S("mass ", m, ": travelled ", run.dist, " m, budget ", budget, " m"));
		check(run.dist >= 0.5 * budget, S("mass ", m, ": the wave actually flows (", run.dist, " of ", budget, " m)"));
		check(stone->pos.x - start.x <= budget + STEP_EPS, S("mass ", m, ": displacement within budget"));
		const std::string why = settle_why();
		check(why == "budget" || why == "viscous", S("mass ", m, ": ends by budget/viscosity (", why, ")"));
		near(stone->mass, m, 1e-9, S("mass ", m, ": preserved"));
		note(S("mass ", m, ": travelled ", run.dist, " of ", budget, " m (", why, ")"));
		travelled[m] = run.dist;
	}
	if (travelled.size() == 3) check(travelled[10.0] < travelled[20.0] && travelled[20.0] < travelled[35.0], "more molten rock flows farther");
}

// ---------------------------------------------------------------- water

FF_TEST_F(test_waves, Wv, test_wave_entering_the_pool_makes_steam_and_solidifies_quickly) {
	_world(V3(3, 0, -1), V3(3.8, 0.2, -1));
	int64_t t_pour = 0, t_solid = -1;
	{
		SimHarness& h = this->h();
		const double wm = h.w->water_mass();
		const double pool_mass = h.w->pool->mass;
		const double e0 = h.w->system_energy();
		if (!_pour(V3(1, 0, 0))) return;
		t_pour = h.w->tick;
		for (int k = 0; k < 600; ++k) {
			h.step();
			if (t_solid < 0 && stone->phase == Phase::Solid) t_solid = h.w->tick;
		}
		check(h.has_event("steam"), "steam events while the lava meets the water");
		check(h.count_events("wave_drop", [](const Dict& e) { return ev_f(e, "to") < -0.1; }) >= 1, "the wave dropped into the pool basin");
		note(S("pool quench: ", static_cast<long long>(t_solid - t_pour), " ticks pour->solid, pool lost ", pool_mass - h.w->pool->mass, " kg"));
		check(t_solid > 0, "the lava solidified");
		check(t_solid - t_pour < 100, S("quenched fast: ", static_cast<long long>(t_solid - t_pour), " ticks from pour to solid"));
		check(stone->form == Form::Chunk && stone->phase == Phase::Solid, "now rock (" + stone->describe() + ")");
		check(stone->pos.x >= 7.0f && stone->pos.x < 13.0f, S("it stopped inside the pool region (x ", stone->pos.x, ")"));
		// Water accounting: whatever left the pool as steam is in the vapor ledger once the clouds are gone.
		h.step(300);
		bool steam_left = false;
		for (const BodyRef& b : h.w->bodies) steam_left = steam_left || (b->alive && b->mat == Mat::Steam);
		check(!steam_left, "all steam dissipated");
		const double lost = pool_mass - h.w->pool->mass;
		check(lost > 1.0, S("the pool boiled away ", lost, " kg"));
		near(h.w->mass_ledger.vapor, lost, 1e-4, "mass_ledger.vapor equals the water the pool lost");
		near(h.w->water_mass(), wm, 1e-4, "water mass conserved");
		near(h.w->system_energy() - e0, h.w->ledger_balance(), 0.05, "energy ledger balances through the quench");
		near(stone->mass, 20.0, 1e-9, "stone mass preserved");
	}
	// Control: the same pour over dry ground takes far longer to solidify.
	_world(V3(3, 0, 6), V3(3.8, 0.2, 6));
	SimHarness& h = this->h();
	if (_pour(V3(1, 0, 0))) {
		const int64_t t0 = h.w->tick;
		int64_t t1 = -1;
		for (int k = 0; k < 900; ++k) {
			h.step();
			if (t1 < 0 && stone->phase == Phase::Solid) {
				t1 = h.w->tick;
				break;
			}
		}
		note(S("dry control: ", static_cast<long long>(t1 - t0), " ticks to solid"));
		check(t1 < 0 || t1 - t0 > 2 * (t_solid - t_pour), S("dry ground takes much longer (", static_cast<long long>(t1 - t0), " vs ", static_cast<long long>(t_solid - t_pour), " ticks)"));
		check(!h.has_event("steam"), "no steam on dry ground");
	}
}

FF_TEST_F(test_waves, Wv, test_wave_boils_a_small_puddle_without_losing_water) {
	_world(V3(0, 0, 7), V3(0.8, 0.2, 7));
	SimHarness& h = this->h();
	// 0.9 kg puddle on the wave's path: the first contact tick boils 0.8975 kg, leaving a 0.0025 kg residue.
	const BodyRef puddle = h.w->spawn_body(Mat::Water, Form::Puddle, 0.9, V3(4.0, 0, 7), "scenario")->shared_from_this();
	puddle->update_radius_puddle();
	const double wm = h.w->water_mass();
	if (!_pour(V3(1, 0, 0))) return;
	_follow();
	check(!puddle->alive, "the puddle was boiled away");
	check(h.has_event("steam"), "it produced steam");
	h.step(240);
	near(h.w->water_mass(), wm, 1e-6, "no water vanished (even the sub-threshold residue)");
}

FF_TEST_F(test_waves, Wv, test_earth_wall_blocks_a_wave_and_without_it_the_wave_hits) {
	for (bool wall : {true, false}) {
		_world(V3(0, 0, 8), V3(0, 0.2, 7), 20.0, V3(0, 0, 0));
		SimHarness& h = this->h();
		ActorState* e = h.w->actors[1].get();
		if (wall) {
			h.press(e, "guard");   // Earth guard raises a wall in front of E (facing P)
			h.step(12);
			check(e->wall_body >= 0, "setup: E raised a wall");
		}
		if (!_pour(V3(0, 0, -1))) continue;
		const Run run = _follow();
		h.step(60);
		if (wall) {
			MatBody* w_body = h.w->get_body(e->wall_body);
			check(h.any_event("block", [](const Dict& x) { return ev_s(x, "kind") == "wave_wall"; }), "block(wave_wall) reported");
			check(_count("wave_blocked") == 1, "wave_blocked once");
			check(e->health == 100.0 && !h.has_event("hit"), S("E untouched behind the wall (health ", e->health, ")"));
			check(w_body != nullptr && stone->pos.z > w_body->pos.z,
			      S("the lava stayed on P's side of the wall (z ", stone->pos.z, " vs wall ", w_body ? w_body->pos.z : 0.0f, ")"));
			check(run.min_z > 0.5, S("it never reached E (min z ", run.min_z, ")"));
		} else {
			const int hits = h.count_events("hit", [&](const Dict& x) { return ev_i(x, "actor", -1) == e->id && ev_s(x, "kind") == "lava"; });
			check(hits == 1, S("without a wall the wave hits E exactly once, got ", hits));
			check(e->health < 100.0, S("E is hurt (", e->health, ")"));
			check(_count("wave_blocked") == 0, "nothing blocked it");
		}
	}
}

FF_TEST_F(test_waves, Wv, test_wave_lifecycle_conserves_stone_mass_and_energy) {
	_world(V3(-3.75, 0, 2.5), V3(-3.75, 0.2, 1.5));
	SimHarness& h = this->h();
	const double m0 = h.w->stone_mass();
	const double e0 = h.w->system_energy();
	near(m0, 20.0, 1e-9, "setup: one 20 kg stone");
	if (!_pour(V3(0, 0, -1))) return;
	double worst_m = 0.0, worst_e = 0.0;
	int gone_at = -1;
	for (int k = 0; k < 3600; ++k) {   // 60 s: pour, block, cool, rock decays after REMNANT_LIFETIME
		h.step();
		worst_m = maxf(worst_m, absf(h.w->stone_mass() - m0));
		if (k % 30 == 0) worst_e = maxf(worst_e, absf((h.w->system_energy() - e0) - h.w->ledger_balance()));
		if (gone_at < 0 && !stone->alive) gone_at = k;
	}
	check(worst_m < 1e-6, S("stone mass ledger drifted by ", worst_m, " kg"));
	check(worst_e < 0.05, S("energy ledger drifted by ", worst_e, " HU"));
	check(gone_at > 0, "the cooled rock eventually crumbles back into the ground");
	if (gone_at > 0) {
		check(gone_at > 40 * Sim::HZ, S("but only after its remnant lifetime (tick ", gone_at, ")"));
		near(h.w->mass_ledger.ground_returned, 20.0, 1e-9, "its mass went back to the ground ledger");
	}
}

// ---------------------------------------------------------------- steering

FF_TEST_F(test_waves, Wv, test_wave_bends_toward_the_target_within_the_turn_rate) {
	// E stands 25 degrees off the pour direction; the wave may bend toward it, slowly.
	_world(V3(0, 0, 8), V3(0, 0.2, 7), 20.0, V3(7.0, 0, 7.0 - 15.0));
	{
		SimHarness& h = this->h();
		if (!_pour(V3(0, 0, -1))) return;
		Vec3 prev = stone->wave_dir;
		double max_step = 0.0, total = 0.0;
		for (int k = 0; k < 300; ++k) {
			h.step();
			if (stone->form != Form::Wave) break;
			const double a = std::atan2(static_cast<double>(stone->wave_dir.x), static_cast<double>(stone->wave_dir.z));
			const double b = std::atan2(static_cast<double>(prev.x), static_cast<double>(prev.z));
			const double d = absf(wrapf(a - b, -kPi, kPi));
			max_step = maxf(max_step, d);
			total += d;
			prev = stone->wave_dir;
		}
		const double limit = deg_to_rad(CombatWorld::WAVE_TURN_RATE) * Sim::DT;
		check(max_step <= limit + 1e-6, S("per-tick turn ", max_step, " rad exceeds the rate limit ", limit));
		note(S("steering: max ", max_step, " rad/tick (limit ", limit, "), total ", rad_to_deg(total), " deg"));
		check(total > deg_to_rad(2.0), S("the wave did bend toward its target (", rad_to_deg(total), " degrees)"));
		check(stone->wave_dir.x > 0.0f, "and it bent the right way (toward +x)");
	}
	// A target more than 70 degrees off the heading is ignored.
	_world(V3(0, 0, 8), V3(0, 0.2, 7), 20.0, V3(14.0, 0, 6.0));
	if (_pour(V3(0, 0, -1))) {
		const Vec3 d0 = stone->wave_dir;
		_follow();
		check(stone->wave_dir.distance_to(d0) < 1e-9f, "a target behind/beside the wave does not steer it");
	}
}
