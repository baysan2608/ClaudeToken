// Port of game/tests/sim/test_kit_water_mist.gd: Water / Mist (sub 2, docs/MOVESET.md §7.7): steam = water + heat (paid,
// booked vapor), fog = a fog ZONE that conceals, dampens fire and conducts lightning at 60 %. Scald Puff, Steam Jet,
// Geyser, Boiling Pillars, Fog Lance, Creeping Fog, Veil, Steam Screen (+ Condense), Steam Blast, Dew Fall, Vapor Draw
// (+ Condense), Mist Step, Fog Walk.
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Sim/Conduction.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
constexpr int NONE = static_cast<int>(Gesture::None);
constexpr int UP = static_cast<int>(Gesture::Up);
constexpr int DOWN = static_cast<int>(Gesture::Down);
constexpr int SIDE = static_cast<int>(Gesture::Side);

bool is_eq(const Dict& e, const char* k, const char* v) { return ev_s(e, k) == v; }

struct WM : WaterCase {
	std::pair<ActorState*, ActorState*> _mist_duel(double dist = 8.0, int rival_element = Sim::EARTH, uint64_t seed_value = 3) {
		auto s = duel(2, rival_element, seed_value, dist);
		s.first->heat_reserve = 300.0;   // steam moves pay heat from the reserve first (then Focus at 10 HU each)
		return s;
	}
	void _tap(ActorState* p, int gesture, int hold_ticks = 2) {
		h().flick(p, "attack", gesture);
		h().step(hold_ticks);
		h().release(p, "attack");
	}
	void _hold(ActorState* p, int gesture, double secs) {
		h().flick(p, "attack", gesture);
		h().step(static_cast<int>(secs * 60.0));
		h().release(p, "attack");
	}
	BodyRef _fog_at(ActorState* owner, Vec3 p, double kg = 2.0, double radius = 4.0, double life = 30.0) {
		owner->water_carried -= kg;
		return keep(WaterUtil::zone(*h().w, "fog", p, radius, owner->id, life,
		                            D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 3.0}, {"rate", 0.15}}),
		                            Mat::Steam, kg, 6.0));
	}
	// The fog test adds actors (each carries a waterskin and a satchel): extend the baseline by them.
	Snap base_after_actors(const Snap& base) {
		Snap b = base;
		const double n = static_cast<double>(h().w->actors.size()) - 2.0;
		b.v[1] += 6.0 * n;
		b.v[3] += 12.0 * n;
		return b;
	}
	AgentRef _screen_agent(ActorState* w, bool perfect) {
		AgentRef a = Agent::of_move(h().w, w, "steam_screen", 0, perfect);
		a->actor = w;
		return a;
	}
	ActorState* _dummy(const std::string& nm, Vec3 p) {
		ActorState* a = h().w->add_actor(nm, p, 1, Dict(), Sim::EARTH);
		h().intents[a->id] = ActorIntent();
		a->is_dummy = true;
		return a;
	}
	static double flat(Vec3 a, Vec3 b) { return static_cast<double>(Vec2(a.x - b.x, a.z - b.z).length()); }
	bool scalded(ActorState* r) { return Status::has(*r, "scalded") || h().has_event("status", "status", Value("scalded")); }
};
}  // namespace

// ---------------------------------------------------------------- strike

FF_TEST_F(test_kit_water_mist, WM, test_scald_puff_boils_half_a_kilo_and_scalds) {
	auto [w, r] = _mist_duel(2.5);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	const double vapor0 = h.w->ledger.vapor;
	const double skin0 = w->water_carried;
	_tap(w, NONE);   // plain tap (no gesture): the strike
	h.step(50);
	check(h.has_event("action", "move", Value("scald_puff")), "Scald Puff started");
	near(skin0 - w->water_carried, 0.5, 0.01, "0.5 kg of water boiled");
	check(h.w->ledger.vapor > vapor0, "booked in the vapor ledger");
	check(r->health < 100.0 || Status::has(*r, "scalded") || h.has_event("hit"), S("the rival is scalded (hp ", r->health, ")"));
	check(!bodies_of(Mat::Steam).empty() || !zones_tagged("steam").empty(), "a steam cloud / obscuring zone");
	check(h.any_event("fx", [](const Dict& e) { return is_eq(e, "mat", "steam"); }), "steam fx cue");
	h.step(240);
	ledgers_ok(base, "Scald Puff");
}

FF_TEST_F(test_kit_water_mist, WM, test_steam_jet_needs_water_and_a_geyser_launches) {
	auto [w, r] = _mist_duel(8.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	w->water_carried = 0.0;
	_hold(w, NONE, 0.5);
	h->step(40);
	check(h->has_event("insufficient", "what", Value("water")), "no water: the puff fizzles");
	// Geyser (T2): erupts under the target and launches it.
	std::tie(w, r) = _mist_duel(8.0);
	h = &this->h();
	r->is_dummy = true;
	Snap base = snap(*h->w);
	h->press(w, "attack");
	h->step(66);
	h->release(w, "attack");
	h->step(10);
	check(zones_tagged("geyser").size() == 1, S("a geyser pocket is buried at the target (", zones_tagged("geyser").size(), ")"));
	double vy = 0.0;
	for (int k = 0; k < 80; ++k) {
		h->step();
		vy = maxf(vy, static_cast<double>(r->vel.y));
	}
	check(vy > 5.0, S("the eruption launches the target (peak vy ", vy, ")"));
	check(zones_tagged("geyser").empty(), "the pocket is spent");
	check(h->has_event("fx", "fx", Value("erupt")), "erupt cue");
	h->step(240);
	ledgers_ok(base, "Geyser");
	// Boiling Pillars (T3): three geysers in a line.
	std::tie(w, r) = _mist_duel(8.0);
	h = &this->h();
	r->is_dummy = true;
	base = snap(*h->w);
	h->press(w, "attack");
	h->step(112);
	h->release(w, "attack");
	h->step(8);
	check(zones_tagged("geyser").size() == 3, S("three pillars (", zones_tagged("geyser").size(), ")"));
	h->step(300);
	ledgers_ok(base, "Boiling Pillars");
}

FF_TEST_F(test_kit_water_mist, WM, test_fog_lance_wets_chills_and_blinds) {
	auto [w, r] = _mist_duel(8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	r->wetness = 0.0;
	const Snap base = snap(*h.w);
	_tap(w, UP);
	h.step(50);
	check(r->wetness > 0.9, S("wet (", r->wetness, ")"));
	check(h.has_event("status", "status", Value("chilled")), "chilled");
	check(h.has_event("status", "status", Value("blinded")), "blinded 0.8 s");
	h.step(240);
	ledgers_ok(base, "Fog Lance");
}

// ---------------------------------------------------------------- ground / sweep: fog

FF_TEST_F(test_kit_water_mist, WM, test_creeping_fog_rolls_settles_conceals_and_conducts_lightning) {
	auto [w, r] = _mist_duel(10.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->kit = D({{"lightning", true}});
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	_tap(w, DOWN);
	h.step(40);
	const std::vector<BodyRef> fog = zones_tagged("fog");
	check(fog.size() == 1, "one fog zone");
	if (fog.empty()) return;
	const BodyRef z = fog[0];
	check(z->mat == Mat::Steam && z->mass > 1.0 && z->zone_radius >= 4.0, S("a STEAM zone of booked water (r ", z->zone_radius, ", ", z->mass, " kg)"));
	const Vec3 p0 = z->pos;
	h.step(60);
	check(z->pos.distance_to(p0) > 0.5f, S("it rolled forward (", z->pos.distance_to(p0), " m)"));
	h.step(30);
	const Vec3 p1 = z->pos;
	h.step(30);
	check(z->pos.distance_to(p1) < 0.5f, "and settled");
	check(Materials::conducts(*z) && is_equal_approx(Materials::conduction_factor(*z), 0.6), "conducts at 60 %");
	// Fighters inside are concealed: lock-on breaks beyond 2 m.
	ActorState* a = _dummy("A", z->pos + V3(1.0, 0, 0));
	ActorState* b = _dummy("B", z->pos + V3(-1.0, 0, 0.5));
	ActorState* outsider = _dummy("O", z->pos + V3(7.0, 0, 0));
	h.step(10);
	check(Status::hidden(*a) && Status::has(*a, "fogbound"), "inside the fog: concealed and fogbound");
	check(!h.w->_lockable(*w, *a), "lock-on breaks beyond 2 m");
	check(!Status::hidden(*outsider), "outside: not concealed");
	// Lightning cast into the fog hits everyone inside at 60 %.
	const Dict def = D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	r->pos = z->pos + V3(0, 0, -9.0);
	r->lock_target = -1;
	const double hp_a = a->health, hp_b = b->health, hp_o = outsider->health;
	const int aid = h.w->new_attack_id();
	Conduction::discharge(*h.w, *r, z->pos, def, aid, true);
	h.step();
	check(a->health < hp_a && b->health < hp_b, S("everyone inside is hit (", hp_a - a->health, ", ", hp_b - b->health, ")"));
	near(hp_a - a->health, 13.0 * 0.6, 0.5, "A takes 60% of its share (13)");
	check(outsider->health == hp_o, "the outsider is not hit");
	h.step(600);
	ledgers_ok(base_after_actors(base), "Creeping Fog");
}

FF_TEST_F(test_kit_water_mist, WM, test_veil_wraps_the_caster_in_mist) {
	auto [w, r] = _mist_duel(8.0);
	(void)r;
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	_tap(w, SIDE);
	h.step(40);
	const std::vector<BodyRef> mist = zones_tagged("mist");
	check(mist.size() == 1 && dint(mist[0]->props, "attach", -1) == w->id, "a mist zone attached to the caster");
	check(Status::hidden(*w), "hard to target (concealed)");
	h.step(300);
	check(zones_tagged("mist").empty(), "it dissolves after 3 s");
	ledgers_ok(base, "Veil");
}

// ---------------------------------------------------------------- guard: Steam Screen

FF_TEST_F(test_kit_water_mist, WM, test_steam_screen_dampens_a_flame_slows_solids_and_scalds) {
	auto [w, r] = _mist_duel(8.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(14);
	const std::vector<BodyRef> sc = zones_tagged("steam_screen");
	check(sc.size() == 1 && dint(sc[0]->props, "attach", -1) == w->id, "a steam screen zone around the fighter");
	if (sc.empty()) return;
	check(Interactions::counter_class(*sc[0], h.w) == "screen_steam", "counter class screen_steam");
	// A flame ball passing through loses heat (x1.5 eff, k 0.5).
	const BodyRef f = keep(h.w->spawn_body(Mat::Fire, Form::Chunk, 1.0, w->pos + V3(0, 1.0, -1.2), "test"));
	f->heat_payload = 160.0;
	f->vel = V3(0, 0, 6.0);
	f->gravity_scale = 0.0;
	f->attack_id = h.w->new_attack_id();
	f->attack_owner = r->id;
	f->hit_set.add(r->id);
	f->max_life = 3.0;
	h.w->ledger.generated += 160.0;   // the test made this heat: book it
	const double e0 = f->heat_payload;
	h.step(12);
	check(f->heat_payload < e0 * 0.8, S("the flame lost heat in the steam (", e0, " -> ", f->heat_payload, " HU)"));
	check(h.any_event("interaction", [](const Dict& e) { return is_eq(e, "counter", "screen_steam") && is_eq(e, "threat", "flame"); }),
	      "interaction flame x screen_steam");
	// A stone is slowed.
	const BodyRef st = keep(h.launch_at(w, "stone", 10.0, 12.0, Sim::AMBIENT_C, "", r, 3.0));
	const double v0 = static_cast<double>(st->vel.length());
	h.step(10);
	const double v1 = static_cast<double>(st->vel.length());
	check(v1 < v0 * 0.95 || !st->alive || st->attack_id == 0, S("a stone slows in the screen (", v0, " -> ", v1, ")"));
	// A rival walking in is scalded.
	r->is_dummy = false;
	r->pos = w->pos + V3(0, 0, -1.0);
	h.step(30);
	check(scalded(r), "walkers are scalded");
	h.release(w, "guard");
	h.step(60);
	check(zones_tagged("steam_screen").empty(), "the screen ends with the guard");
	h.step(300);
	ledgers_ok(base, "Steam Screen");
}

FF_TEST_F(test_kit_water_mist, WM, test_condense_perfect_guard_pulls_steam_into_the_waterskin) {
	// Condense: a perfect Steam Screen absorbs water / steam / mist into the waterskin.
	auto [w, r] = _mist_duel(8.0, Sim::WATER);
	(void)r;
	SimHarness& h = this->h();
	w->water_carried = 1.0;
	h.step(2);
	const Snap base = snap(*h.w);
	const BodyRef steam = keep(h.w->spawn_body(Mat::Steam, Form::Cloud, 2.0, w->pos + V3(0, 1.2, -2.0), "test"));
	h.w->mass_ledger.moisture_taken += 2.0;
	steam->max_life = 30.0;
	AgentRef pt = Agent::of_body(*h.w, *steam);
	AgentRef pc = Agent::of_move(h.w, w, "steam_screen", 0, true);
	const IxResult g = Interactions::predict(h.w, *pt, *pc);
	check(g.outcome == "water_skin", "predicted: perfect screen x steam -> water_skin (" + g.outcome + ")");
	AgentRef th = Agent::of_body(*h.w, *steam);
	AgentRef ct = _screen_agent(w, true);
	Interactions::resolve(*h.w, *th, *ct);
	check(!steam->alive, "the steam is gone");
	near(w->water_carried, 3.0, 0.01, "its 2 kg are in the waterskin");
	h.step(5);
	ledgers_ok(base, "Condense");
}

FF_TEST_F(test_kit_water_mist, WM, test_steam_blast_bursts_the_screen_forward) {
	auto [w, r] = _mist_duel(4.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(14);
	h.flick(w, "guard", UP);
	h.step(40);
	h.release(w, "guard");
	h.step(60);
	check(zones_tagged("steam_screen").empty(), "the screen is gone (burst forward)");
	check(h.has_event("action", "move", Value("steam_blast")), "Steam Blast ran");
	check(r->health < 100.0 || scalded(r), S("the cone scalds / hits (hp ", r->health, ")"));
	h.step(240);
	ledgers_ok(base, "Steam Blast");
}

// ---------------------------------------------------------------- sink: Dew Fall

FF_TEST_F(test_kit_water_mist, WM, test_dew_fall_rains_every_vapour_within_6_m) {
	auto [w, r] = _mist_duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	const BodyRef fog = _fog_at(w, w->pos + V3(0, 0, -3.0), 2.0, 3.0, 30.0);
	h.w->_spawn_steam(w->pos + V3(2.0, 1.0, 0.0), 1.5);
	h.w->mass_ledger.moisture_taken += 1.5;
	const BodyRef far = _fog_at(w, w->pos + V3(0, 0, -9.0), 1.0, 2.0, 30.0);
	h.step(3);
	const Snap base = snap(*h.w);
	const size_t pud0 = bodies_of(Mat::Water, static_cast<int>(Form::Puddle)).size();
	h.press(w, "guard");
	h.step(12);
	h.flick(w, "guard", DOWN);
	h.step(30);
	h.release(w, "guard");
	h.step(20);
	check(!fog->alive, "the fog within 6 m is gone");
	check(far->alive, "the far fog is not touched");
	bool steam_near = false;
	for (const BodyRef& b : bodies_of(Mat::Steam))
		if (b->form == Form::Cloud && b->pos.distance_to(w->pos) < 6.0f) steam_near = true;
	check(!steam_near, "no steam within 6 m");
	check(bodies_of(Mat::Water, static_cast<int>(Form::Puddle)).size() > pud0, "it fell as puddles");
	check(h.has_event("dew"), "dew event");
	h.step(120);
	ledgers_ok(base, "Dew Fall");
}

// ---------------------------------------------------------------- technique

FF_TEST_F(test_kit_water_mist, WM, test_vapor_draw_pulls_fog_into_a_ball_and_condense_makes_water) {
	auto [w, r] = _mist_duel(8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const BodyRef fog = _fog_at(w, w->pos + V3(0, 0, -4.0), 3.0, 2.5, 30.0);
	h.step(3);
	const Snap base = snap(*h.w);
	h.press(w, "tech");
	h.step(70);
	MatBody* held = h.w->held(*w);
	check(held != nullptr && held->mat == Mat::Steam && held->mass > 1.0, "a held vapour ball (" + (held ? held->describe() : std::string("none")) + ")");
	check(fog->mass < 2.0 || !fog->alive, S("the fog gave its water (", fog->alive ? fog->mass : 0.0, " kg left)"));
	if (held == nullptr) return;
	// T+A: condense it into a water blob.
	h.press(w, "attack");
	h.step(3);
	h.release(w, "attack");
	h.step(3);
	held = h.w->held(*w);
	check(held != nullptr && held->is_water() && held->phase == Phase::Liquid, "condensed into a water blob");
	h.release(w, "tech");
	h.step(200);
	ledgers_ok(base, "Vapor Draw");
}

FF_TEST_F(test_kit_water_mist, WM, test_vapor_ball_is_a_steam_bomb) {
	auto [w, r] = _mist_duel(6.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	_fog_at(w, w->pos + V3(0, 0, -2.5), 3.0, 2.5, 30.0);
	h.step(3);
	const Snap base = snap(*h.w);
	h.press(w, "tech");
	h.step(60);
	h.release(w, "tech");
	h.step(120);
	check(h.has_event("launch"), "released: the ball is thrown");
	check(r->health < 100.0 || scalded(r), S("the steam bomb scalds (hp ", r->health, ")"));
	h.step(300);
	ledgers_ok(base, "steam bomb");
}

// ---------------------------------------------------------------- evade

FF_TEST_F(test_kit_water_mist, WM, test_mist_step_hides_and_fog_walk_conceals) {
	auto [w, r] = _mist_duel(10.0);
	SimHarness* h = &this->h();
	const Vec3 p0 = w->pos;
	const Snap base = snap(*h->w);
	h->it(w).move = V3(1, 0, 0);
	h->press(w, "evade");
	h->step(1);
	h->it(w).move = Vec3();
	h->step(5);
	check(Status::hidden(*w) && w->iframes > 0.0, "dissolved: hidden with i-frames");
	h->step(40);
	const double d = flat(w->pos, p0);
	check(d > 2.5 && d < 4.8, S("reformed about 4 m away (", d, ")"));
	check(!zones_tagged("mist").empty(), "fog puffs at the ends");
	h->step(120);
	ledgers_ok(base, "Mist Step");
	// Fog Walk: hold evade.
	std::tie(w, r) = _mist_duel(10.0);
	h = &this->h();
	h->it(w).move = V3(1, 0, 0);
	h->press(w, "evade");
	h->it(w).evade_held = true;
	h->step(40);
	check(w->action != nullptr && w->action->id == "fog_walk", "morphed into Fog Walk (" + (w->action ? w->action->id : std::string("none")) + ")");
	check(Status::has(*w, "fogwalk") && Status::hidden(*w), "untargetable by lock-on (fogwalk)");
	check(!zones_tagged("mist").empty(), "inside its own mist");
	h->it(w).evade_held = false;
	h->step(60);
	check(zones_tagged("mist").empty(), "the mist is gone when you stop");
}
