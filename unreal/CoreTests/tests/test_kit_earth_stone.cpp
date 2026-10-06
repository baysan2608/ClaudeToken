// Port of game/tests/sim/test_kit_earth_stone.gd: Earth / Stone (sub 0): the legacy ladder past the heave, Split,
// Swallow, Rising Fangs, Ram Wall, Bulwark thickening, Stone Skin / Burrow Step and the Stone column of MOVESET §8.1.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Earth/Earth.h"
#include "Combat/Kits/Earth/KitEarth.h"
#include "Sim/Conduction.h"
#include "Sim/Materials.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct StoneFx : HarnessCase {
	EU::Duel _duel(double dist = 7.0) {
		SimHarness& hh = H(3);
		return EU::duel(hh, 0, Sim::FIRE, dist);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
};
}  // namespace

FF_TEST_F(test_kit_earth_stone, StoneFx, test_stone_shot_ladder_boulder_and_crag_breaker) {
	std::vector<double> masses;
	for (int hold : {40, 70, 118}) {
		EU::Duel s = _duel();
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		const double m0 = h.w->stone_mass();
		h.press(a, "attack");
		h.step(hold);
		MatBody* b = h.w->held(*a);
		masses.push_back(b != nullptr ? b->mass : -1.0);
		h.release(a, "attack");
		h.until([&]() { return h.has_event("launch"); }, 40);
		const Dict ev = h.last_event("launch");
		BodyRef sb = EU::keep(h.w->get_body(ev_i(ev, "body", -1)));
		if (hold == 118) {
			check(sb != nullptr && sb->tag == "crag", "T3 is a Crag Breaker");
			const int hit = h.until([&]() { return h.has_event("shatter"); }, 120);
			check(hit > 0, "the crag bursts on impact");
			const int sid = sb != nullptr ? sb->id : -2;
			const int parts = h.count_events("split", [&](const Dict& e) { return is(e, "parent", sid); });
			check(parts == 2, S("into 3 rubble (2 splits) (", parts, ")"));
			check(t->health < 100.0, S("it hit the target (", t->health, ")"));
		}
		h.step(30);
		near(h.w->stone_mass(), m0, 1e-6, S("stone mass booked from the ground (hold ", hold, ")"));
	}
	near(masses[0], 45.0, 1e-6, "T1 heave keeps the legacy 45 kg");
	near(masses[1], 65.0, 1e-6, "T2 Boulder 65 kg");
	near(masses[2], 80.0, 1e-6, "T3 Crag Breaker 80 kg");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_heave_t1_has_no_drain_and_t2_drains) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	h.press(a, "attack");
	h.step(40);
	const double f1 = a->focus;
	h.step(20);
	check(a->focus >= f1 - 1e-6, S("no Focus drain while holding the T1 heave (", f1, " -> ", a->focus, ")"));
	h.step(20);
	const double f2 = a->focus;
	h.step(20);
	check(a->focus < f2 - 1.0, S("T2 Boulder drains 8 Focus/s (", f2, " -> ", a->focus, ")"));
	h.release(a, "attack");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_split_it_and_spike_it_back) {
	// Owner example: "someone throws a stone at me: I can split it and spike it back".
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	MatBody* stone = h.launch_at(a, "stone", 20.0, 9.0, Sim::AMBIENT_C, "", t, 5.0);
	h.press(a, "tech");
	h.until([&]() { return h.w->held(*a) != nullptr; }, 40);
	check(h.w->held(*a) == stone, "Seize caught the incoming stone (REC)");
	h.step(4);
	h.it(a).attack_pressed = true;
	h.step();
	check(h.has_event("shape", "shape", Value("split")), "T+A: split");
	h.release(a, "tech");
	h.step(3);
	const std::vector<Dict> spikes = h.filter("launch", [&](const Dict& e) { return is(e, "actor", a->id) && dstr(e, "kind", "") == "split"; });
	check(spikes.size() == 3, S("three spikes (", spikes.size(), ")"));
	for (const Dict& x : spikes) {
		MatBody* sb = h.w->get_body(ev_i(x, "body", -1));
		check(sb != nullptr && is_equal_approx(sb->mass, 20.0 / 3.0) && sb->tag == "spear", "a third of the stone, a spear");
		check(sb != nullptr && sb->vel.dot(t->pos - sb->pos) > 0.0f && sb->attack_owner == a->id, "flying back at the thrower");
	}
	const int n = h.until([&]() { return t->health < 100.0; }, 90);
	check(n > 0, S("the spikes hit the thrower (", t->health, ")"));
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_swallow_sinks_an_incoming_stone) {
	// Owner example: "... or put it down into the ground".
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double m0 = h.w->stone_mass();
	const double r0 = h.w->mass_ledger.ground_returned;
	h.press(a, "guard");
	h.step(12);
	BodyRef stone = EU::keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t, 6.5));
	h.flick(a, "guard", static_cast<int>(Gesture::Down));
	h.step();
	check(a->action != nullptr && a->action->id == "swallow", "guard flick down: Swallow");
	h.until([&]() { return !stone->alive; }, 40);
	check(!stone->alive && h.has_event("sink", "body", Value(stone->id)), "CP 22 >= TP 17: the stone sinks");
	check(h.any_event("interaction", [](const Dict& e) { return dstr(e, "counter") == "swallow" && dstr(e, "outcome") == "sink"; }),
	      "interaction swallow -> sink");
	check(a->health == 100.0, "it never reached the fighter");
	check(h.w->mass_ledger.ground_returned >= r0 + 20.0 - 1e-6, "booked as ground_returned");
	h.release(a, "guard");
	h.step(40);
	near(h.w->stone_mass(), m0, 1e-6, "stone mass conserved");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_swallow_strength_follows_the_guard_hold) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	MatBody* heave = EU::shot(h, Mat::Stone, 45.0, V3(0, 1, 0), V3(0, 0, 14));
	const IxResult p0 = EU::pr(h, heave, "swallow", 0, false, a);
	const IxResult p2 = EU::pr(h, heave, "swallow", 2, false, a);
	check(p0.band == "partial" && p0.outcome == "weaken", S("T0 22 vs heave 31.5: partial weaken (", p0.band, " ", p0.ratio, ")"));
	check(p2.outcome == "sink", "T2 40 vs 31.5: sink");
	// Live: a guard held 1.0 s then the flick swallows at T2.
	h.press(a, "guard");
	h.step(62);
	h.flick(a, "guard", static_cast<int>(Gesture::Down));
	h.step();
	check(a->action != nullptr && a->action->id == "swallow" && a->action->tier() == 2,
	      S("guard held 1.0 s: Swallow T2 (", a->action != nullptr ? a->action->tier() : -1, ")"));
	h.release(a, "guard");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_swallow_drains_a_lava_wave_from_t1) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	MatBody* w0 = EU::lava_wave(h, 20.0, V3(0, 0, 1), V3(0, 0, 1), t);
	const IxResult r0 = EU::pr(h, w0, "swallow", 0, false, a);
	const IxResult r1 = EU::pr(h, w0, "swallow", 1, false, a);
	near(r0.tp, 27.3, 0.05, "20 kg lava wave TP 27.3");
	check(r0.outcome == "weaken" && r0.band == "partial", "T0 (22): the trench only weakens it");
	check(r1.outcome == "sink", "T1 (30): it drains into the trench");
	const double e0 = EU::e0(h);
	AgentRef th = Agent::of_body(*h.w, *w0, a);
	AgentRef co = Agent::of_move(h.w, a, "swallow", 1, false);
	const IxResult res = Interactions::resolve(*h.w, *th, *co);
	check(res.stopped && !w0->alive, "drained");
	check(EU::energy_drift(h, e0) < 1e-6, "its heat is booked (removed)");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_rising_fangs_stop_a_lava_wave_and_stand_as_spikes) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double m0 = h.w->stone_mass();
	const double e0 = EU::e0(h);
	MatBody* wave = EU::lava_wave(h, 20.0, V3(0, 0, -1.0), V3(0, 0, 1), t);
	const int wid = wave->id;
	h.flick(a, "attack", static_cast<int>(Gesture::Down));
	h.step();
	h.release(a, "attack");
	const int n = h.until([&]() { return h.has_event("wave_blocked", "body", Value(wid)) || a->health < 100.0; }, 120);
	check(n > 0 && h.has_event("wave_blocked", "body", Value(wid)), "the spike line stops the lava wave");
	check(a->health == 100.0, "the lava never reaches the caster");
	int spikes = 0;
	for (const BodyRef& b : h.w->bodies)
		if (b->alive && b->form == Form::Wall && b->tag == "spikes") ++spikes;
	check(spikes == 1, "it erupts into standing spikes where they met");
	h.step(140);
	bool none = true;
	for (const BodyRef& b : h.w->bodies)
		if (b->alive && b->tag == "spikes") none = false;
	check(none, "the spikes sink after 1.5 s");
	near(h.w->stone_mass(), m0, 1e-6, "stone mass conserved");
	check(EU::energy_drift(h, e0) < 1e-6, S("energy ledger balanced (", EU::energy_drift(h, e0), ")"));
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_rising_fangs_launch_the_target) {
	EU::Duel s = _duel(7.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.flick(a, "attack", static_cast<int>(Gesture::Down));
	h.step();
	h.release(a, "attack");
	double vy = 0.0;
	const int n = h.until(
	    [&]() {
		    vy = maxf(vy, t->vel.y);
		    return h.any_event("hit", [&](const Dict& e) { return is(e, "actor", t->id); });
	    },
	    120);
	check(n > 0, "the spike line hits");
	h.step();
	vy = maxf(vy, t->vel.y);
	check(vy > 3.0, S("and launches (vy ", vy, ")"));
	auto any_spikes = [&]() {
		for (const BodyRef& b : h.w->bodies)
			if (b->alive && b->tag == "spikes") return true;
		return false;
	};
	h.until(any_spikes, 60);
	check(any_spikes(), "then stands as spikes");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_earthrise_rings_the_caster_with_spikes) {
	EU::Duel s = _duel(3.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	EU::perform(h, a, "ground", 3);
	h.step(30);
	int ring = 0;
	for (const BodyRef& b : h.w->bodies) {
		if (!(b->alive && b->form == Form::Wall && b->tag == "spikes")) continue;
		++ring;
		near(KitEarthUtil::flat_dist2(b->pos, a->pos), 3.5, 0.3, "at r 3.5");
	}
	check(ring == 4, S("4 spike walls around the caster (", ring, ")"));
	check(t->health < 100.0, "the eruption hits a fighter in the ring");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_ram_wall_shoves_a_lava_wave_back_at_the_pourer) {
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(12);
	BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
	check(wall != nullptr, "setup: Bulwark");
	if (wall == nullptr) return;
	BodyRef wave = EU::keep(EU::lava_wave(h, 20.0, V3(0, 0, -1.5), V3(0, 0, 1), t));
	h.flick(a, "guard", static_cast<int>(Gesture::Up));
	h.step();
	check(a->action != nullptr && a->action->id == "ram_wall", "guard flick up: Ram Wall");
	h.release(a, "guard");
	h.until([&]() { return wave->attack_owner == a->id; }, 60);
	check(wave->attack_owner == a->id && wave->wave_dir.z < 0.0f, "K 54 > TP 27: the wave is shoved back, now A's");
	check(h.any_event("interaction", [](const Dict& e) { return dstr(e, "counter") == "ram" && dstr(e, "outcome") == "earth_ram_push"; }),
	      "interaction ram -> push");
	h.until([&]() { return !wall->alive; }, 90);
	check(!wall->alive && h.has_event("wall_crumble"), "the ram ends in rubble");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_ram_wall_contest_against_enemy_walls) {
	struct Enemy {
		const char* name;
		Mat mat;
		double mass;
	};
	std::map<std::string, std::pair<bool, bool>> results;
	for (const Enemy& enemy : {Enemy{"sand", Mat::Sand, 40.0}, Enemy{"bulwark", Mat::Stone, 120.0}, Enemy{"thick", Mat::Stone, 240.0}}) {
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		BodyRef ew = EU::keep(EU::wall(h, enemy.mat, enemy.mat == Mat::Sand ? "sand" : "", enemy.mass, V3(0, 0, -0.5), t));
		h.press(a, "guard");
		h.step(12);
		BodyRef mine = EU::keep(h.w->get_body(a->wall_body));
		h.flick(a, "guard", static_cast<int>(Gesture::Up));
		h.step();
		h.release(a, "guard");
		if (mine != nullptr) h.until([&]() { return !mine->alive; }, 90);
		results[enemy.name] = {ew->alive, mine != nullptr && mine->alive};
		check(h.any_event("interaction", [](const Dict& e) { return dstr(e, "rule") == "ram_contest"; }),
		      S(enemy.name, ": the ram contest resolved"));
	}
	check(!results["sand"].first, "K 54 vs a 40 kg dune (CP 10): the dune breaks");
	check(!results["bulwark"].first, "K 54 vs Bulwark CP 30 (ratio 0.56): both walls break");
	check(results["thick"].first, "K 54 vs a 240 kg wall (CP 60): it holds, the ram stops");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_bulwark_thickens_with_the_guard_hold_and_grounds_a_storm_bolt) {
	struct Out {
		bool blocked;
		double mass;
		bool alive;
	};
	std::vector<Out> out;
	for (int hold : {10, 66}) {
		EU::Duel s = _duel(8.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		t->is_dummy = false;
		h.press(a, "guard");
		h.step(hold);
		BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
		const Dict storm = D({{"range", 16.0}, {"damage", 36.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
		t->lock_target = a->id;
		const Dict r = Conduction::discharge(*h.w, *t, a->chest(), storm, h.w->new_attack_id(), true);
		out.push_back({dbool(r, "blocked"), wall != nullptr && wall->alive ? wall->mass : -1.0, wall != nullptr && wall->alive});
		h.release(a, "guard");
	}
	check(!out[0].blocked && !out[0].alive, "a fresh Bulwark (CP 30) is blasted by a Storm Bolt (E 36)");
	check(out[1].blocked && out[1].alive && is_equal_approx(out[1].mass, 160.0),
	      S("held 1.0 s: 160 kg (CP 40) grounds it (", out[1].blocked, " ", out[1].mass, " ", out[1].alive, ")"));
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_stone_skin_anchors_and_burrow_step_passes_under_a_wave) {
	EU::Duel s = _duel(6.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(20);
	check(a->stance == "stone_skin" && a->anchored, "evade held in place: Stone Skin, anchored");
	const std::string res = h.w->hit_actor(*a, D({{"attacker", t->id}, {"attack_id", h.w->new_attack_id()}, {"damage", 10.0}, {"balance", 10.0},
	                                              {"knock", V3(0, 6, 9)}, {"kind", "air"}, {"from", t->chest()}, {"power", 25.0}}));
	check(res == "hit" && a->vel.length() < 1.0f, S("a gust's knock does not move it (vel ", a->vel.length(), ")"));
	h.it(a).evade_held = false;
	h.step(20);
	check(a->stance.empty(), "released: stance off");
	// Burrow Step: evade held with a direction.
	const Vec3 p0 = a->pos;
	EU::lava_wave(h, 20.0, a->pos + V3(0, 0, -2.0), V3(0, 0, 1), t);
	h.it(a).move = V3(1, 0, 0);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(14);
	check(a->action != nullptr && a->action->id == "stone_skin" && dstr(a->action->data, "mode", "") == "burrow",
	      "evade held with a direction: Burrow Step");
	h.it(a).evade_held = false;
	h.step(20);
	h.it(a).move = Vec3();
	check((a->pos - p0).length() > 3.5f, S("resurfaced away (", (a->pos - p0).length(), " m)"));
	check(h.count_events("hit", [&](const Dict& e) { return is(e, "actor", a->id); }) == 1, "the wave passed over the burrow (only the gust hit)");
	check(h.any_event("fx", [&](const Dict& e) { return is(e, "actor", a->id) && dstr(e, "fx") == "erupt"; }), "erupt cue at the exit");
}

FF_TEST_F(test_kit_earth_stone, StoneFx, test_stone_column_cells) {
	// MOVESET §8.1, Stone column, against each row's reference threat.
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	MatBody* bul = EU::wall(h, Mat::Stone, "", 120.0, V3(0, 0, 2), a);
	MatBody* stone = EU::shot(h, Mat::Stone, 20.0, V3(0, 1, 0), V3(0, 0, 17), t);
	check(EU::pr(h, stone, "swallow", 0, false, a).outcome == "sink", "stone shot: SNK Swallow (22 >= 17)");
	check(EU::pr(h, stone, bul).outcome == "block", "stone shot: BLK Bulwark");
	MatBody* boulder = EU::shot(h, Mat::Stone, 200.0, V3(2, 1, 0), V3(0, 0, 11), t);
	const IxResult pb = EU::pr(h, boulder, "swallow", 3, false, a);
	check(pb.band == "partial" && pb.outcome == "weaken", S("boulder 110: Swallow T3 (55) only weakens (", pb.ratio, ")"));
	MatBody* hot = EU::shot(h, Mat::Stone, 20.0, V3(-2, 1, 0), V3(0, 0, 17), t, "", 1000.0);
	check(EU::pr(h, hot, "swallow", 1, false, a).outcome == "sink", "hot rock: SNK Swallow T1");
	MatBody* blob = EU::lava(h, 20.0, V3(3, 1, 0));
	blob->vel = V3(0, 0, 12);
	blob->attack_id = h.w->new_attack_id();
	blob->attack_owner = t->id;
	const IxResult pm1 = EU::pr(h, blob, "swallow", 1, false, a);
	const IxResult pm2 = EU::pr(h, blob, "swallow", 2, false, a);
	check(pm1.band == "partial" && pm2.outcome == "sink", S("magma blob (TP ", pm1.tp, "): Swallow T1 partial, T2 sinks"));
	MatBody* lance = EU::shot(h, Mat::Metal, 6.0, V3(0, 1.2, 1.0), V3(0, 0, 30), t, "lance");
	check(EU::pr(h, lance, bul).outcome == "earth_embed", "metal lance: embeds in the Bulwark (a rod)");
	AgentRef lt = Agent::of_body(*h.w, *lance, a);
	AgentRef bc = Agent::of_body(*h.w, *bul);
	IxCtx ctx;
	ctx.site = "wall";
	const IxResult res = Interactions::resolve(*h.w, *lt, *bc, ctx);
	check(res.stopped && lance->tag == "rod" && lance->static_body, "the lance stays as a conductive rod");
	check(Materials::conducts(*lance), "and conducts");
	MatBody* ice = EU::shot(h, Mat::Water, 4.0, V3(0, 1, -1), V3(0, 0, 24), t, "", -5.0);
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	check(EU::pr(h, ice, "swallow", 0, false, a).outcome == "sink", "ice lance: SNK Swallow");
	check(EU::prv(h, "flame", D({{"H", 8.0}}), bul).outcome == "block", "flame: BLK Bulwark (wall heats)");
	check(EU::prv(h, "sound", D({{"P", 22.0}}), bul).outcome == "reflect", "sound: RFL Bulwark");
	check(EU::prv(h, "gust", D({{"P", 28.0}}), bul).outcome == "block", "gust: BLK Bulwark");
	check(EU::prv(h, "lightning", D({{"E", 24.0}}), bul).outcome == "ground", "bolt E 24: GND Bulwark (legacy)");
	check(EU::prv(h, "lightning", D({{"E", 36.0}}), bul).outcome == "shatter", "Storm Bolt E 36: the Bulwark shatters");
	MatBody* sw = EU::lava_wave(h, 20.0, V3(4, 0, 0), V3(0, 0, 1), t);
	MatBody* spikes = EU::wall(h, Mat::Stone, "spikes", 24.0, V3(4, 0, 2), a);
	spikes->hardness = EarthStone::SPIKE_CP / 24.0;
	check(EU::pr(h, sw, spikes).outcome == "earth_spike_stop", "lava wave: Rising Fangs spikes stop it");
	MatBody* ww = EU::shot(h, Mat::Water, 18.0, V3(-4, 0, 0), V3(0, 0, 9), t);
	ww->form = Form::Wave;
	check(EU::pr(h, ww, "swallow", 0, false, a).outcome == "weaken", "water wave: WKN Swallow (diverts)");
	// Tornado / suction vs Stone Skin: the core anchor cell at the stance's CP 40.
	a->status.set("anchored", D({{"t", -1.0}, {"mag", 40.0}, {"src", a->id}}));
	AgentRef tor = Agent::of_volume(h.w, t, nullptr, "tornado", t->chest(), V3(0, 0, 1), D({{"P", 35.0}}));
	AgentRef st = Agent::of_stance(h.w, *a);
	check(Interactions::predict(h.w, *tor, *st).outcome == "block", "tornado 35: Stone Skin anchor (40) holds");
}
