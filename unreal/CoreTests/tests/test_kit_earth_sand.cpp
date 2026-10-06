// Port of game/tests/sim/test_kit_earth_sand.gd: Earth / Sand (sub 2): Grit Shot ladder (blind, cloud, sandstorm),
// Sandblast, Sand Surge (carry back, crust lava, smother fire, mud), Veil of Grit, Dune Wall / Engulf (glass, mud), Dune
// Push, Quicksand, Sandform / Compress, Sand Surf and the Sand column of MOVESET §8.1. Sand <-> glass <-> sandstone booked.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Sim/Conduction.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct SandFx : HarnessCase {
	EU::Duel _duel(double dist = 7.0, int t_elem = Sim::FIRE) {
		SimHarness& hh = H(5);
		return EU::duel(hh, 2, t_elem, dist);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
	BodyRef first_where(const std::function<bool(const MatBody&)>& f) {
		for (const BodyRef& b : h().w->bodies)
			if (f(*b)) return b;
		return nullptr;
	}
	Dict bolt(double range, double damage) {
		return D({{"range", range}, {"damage", damage}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	}
};
}  // namespace

FF_TEST_F(test_kit_earth_sand, SandFx, test_grit_shot_blinds_and_the_charged_slugs_burst_into_clouds) {
	{
		EU::Duel s = _duel();
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		const double em0 = h.w->earth_mass();
		EU::perform(h, a, "strike", 0);
		h.until([&]() { return t->status.has("blinded"); }, 60);
		check(t->status.has("blinded"), "a grit shot blinds (lock-on off)");
		check(Status::lock_blocked(*t), "blinded: no lock");
		h.step(120);
		near(h.w->earth_mass(), em0, 1e-6, "the spent slug settles back into the ground (earth mass booked)");
	}
	for (int tier : {2, 3}) {
		EU::Duel s2 = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a2 = s2.a;
		const double em1 = h.w->earth_mass();
		EU::perform(h, a2, "strike", tier);
		auto sand_zone = [](const MatBody& b) { return b.alive && b.form == Form::Zone && b.mat == Mat::Sand; };
		const int n = h.until([&]() { return first_where(sand_zone) != nullptr; }, 90);
		check(n > 0, S("T", tier, ": the slug bursts into a sand zone"));
		BodyRef z = n > 0 ? first_where(sand_zone) : nullptr;
		if (z != nullptr) {
			check(z->tag == (tier == 3 ? "sandstorm" : "sand_cloud"), S("T", tier, ": ", z->tag));
			near(z->zone_radius, tier == 3 ? 4.0 : 2.0, 1e-6, S("T", tier, " radius"));
			near(z->mass, tier == 3 ? 30.0 : 15.0, 1e-6, "the cloud is the slug's sand");
		}
		h.step(240);
		near(h.w->earth_mass(), em1, 1e-6, S("T", tier, ": earth mass conserved after the cloud settles"));
	}
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sandblast_sustained_jet_hits_repeatedly) {
	EU::Duel s = _duel(6.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	EU::perform(h, a, "thrust", 2);
	h.step(80);
	const int hits = h.count_events("hit", [&](const Dict& e) { return is(e, "actor", t->id); });
	check(hits >= 3, S("T2: a 1 s jet pulses several hits (", hits, ")"));
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sand_surge_carries_a_stone_back_and_crusts_a_lava_wave) {
	double em0 = 0.0;
	{
		EU::Duel s = _duel(12.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		em0 = h.w->earth_mass();
		// A stone the T0 surge can hold (TP 7 <= CP 7.5): a faster one (TP 12) is only slowed once and hits softer.
		BodyRef stone = EU::keep(h.launch_at(a, "stone", 20.0, 7.0, Sim::AMBIENT_C, "", t, 7.0));
		stone->pos.y = 0.6f;
		stone->vel.y = 0.0f;
		const int sid = stone->id;
		EU::perform(h, a, "ground", 0);
		h.until([&]() { return h.has_event("capture", "body", Value(sid)) || a->health < 100.0; }, 60);
		check(h.has_event("capture", "body", Value(sid)), "Sand Surge captures the incoming stone");
		check(a->health == 100.0, "it never reaches the caster");
		h.until([&]() { return h.has_event("release_captured", "body", Value(sid)); }, 120);
		check(stone->attack_owner == a->id && (stone->attack_id != 0 || h.has_event("hit", "body", Value(sid))),
		      "carried back and released as A's attack");
	}
	// Lava wave vs sand surge: the lava crusts (x1.5) and stalls.
	EU::Duel s2 = _duel(12.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	ActorState* t2 = s2.t;
	const double e0 = EU::e0(h);
	BodyRef wave = EU::keep(EU::lava_wave(h, 20.0, V3(0, 0, -5), V3(0, 0, 1), t2));
	EU::perform(h, a2, "ground", 0);
	h.until([&]() { return wave->form != Form::Wave; }, 120);
	check(wave->form != Form::Wave, "the lava wave stalls");
	check(h.any_event("interaction", [](const Dict& e) { return dstr(e, "outcome") == "earth_crust"; }), "crusted by the sand (x1.5)");
	check(a2->health == 100.0, "it never reaches the caster");
	check(EU::energy_drift(h, e0) < 1e-6, S("the heat taken is booked (", EU::energy_drift(h, e0), ")"));
	h.step(200);
	check(true, "settled");
	check(em0 >= 0.0, "");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sand_surge_smothers_a_fire_field_and_buries_a_puddle) {
	EU::Duel s = _duel(10.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	BodyRef ff_ = EU::keep(h.w->spawn_zone("fire_field", V3(0, 0, 0), 1.5, t->id, 8.0, Mat::Fire, 0.0, 10.0));
	ff_->heat_payload = 160.0;
	h.w->ledger.generated += 160.0;
	BodyRef pd = EU::keep(h.w->spawn_body(Mat::Water, Form::Puddle, 4.0, V3(0, 0, -2.5), "scenario"));
	pd->update_radius_puddle();
	const double wm0 = h.w->water_mass();
	const double e0 = EU::e0(h);
	EU::perform(h, a, "ground", 1);
	h.until([&]() { return !ff_->alive && !pd->alive; }, 120);
	check(!ff_->alive && h.has_event("extinguish"), "the fire field is smothered");
	check(!pd->alive, "the puddle is soaked up");
	check(h.any_event("transform", [](const Dict& e) { return dstr(e, "to", "") == "mud"; }), "the surge turns to mud");
	near(h.w->water_mass(), wm0, 1e-6, "water mass booked (soaked = evaporated)");
	check(EU::energy_drift(h, e0) < 1e-6, "energy booked");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_veil_of_grit_blinds_drags_and_halves_bolts_into_glass) {
	EU::Duel s = _duel(5.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	t->is_dummy = false;
	EU::perform(h, a, "sweep", 0);
	auto cloud = [](const MatBody& b) { return b.alive && b.tag == "sand_cloud"; };
	h.until([&]() { return first_where(cloud) != nullptr; }, 40);
	BodyRef z = first_where(cloud);
	check(z != nullptr, "setup: a sand cloud");
	if (z == nullptr) return;
	h.step(3);
	check(t->status.has("blinded"), "the rival inside is blinded");
	BodyRef shot = EU::keep(EU::shot(h, Mat::Stone, 20.0, z->pos + V3(0, 1.0, -z->zone_radius + 0.2), V3(0, 0, 17), t));
	h.step(2);
	near(shot->vel.length(), 17.0 * 0.7, 0.2, "a stone crossing the cloud is dragged once (-30 % K)");
	h.step(6);
	check(shot->vel.length() > 11.0f, S("only once (", shot->vel.length(), ")"));
	const double em0 = h.w->earth_mass();
	t->pos = z->pos + V3(0, 0, -z->zone_radius - 1.5);
	h.step();
	t->lock_target = a->id;
	const double hp0 = a->health;
	const double g0 = h.w->mass_ledger.sand_to_glass;
	Conduction::discharge(*h.w, *t, a->chest(), bolt(14.0, 24.0), h.w->new_attack_id(), true);
	h.step();
	near(hp0 - a->health, 12.0, 1e-6, "a bolt through the cloud loses half (24 -> 12)");
	check(h.w->mass_ledger.sand_to_glass > g0, "and fuses a glass bead");
	near(h.w->earth_mass(), em0, 1e-6, "sand -> glass booked");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_dune_wall_captures_engulfs_glasses_and_muds) {
	{
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		h.press(a, "guard");
		h.step(20);
		BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
		check(wall != nullptr && wall->mat == Mat::Sand && wall->tag == "sand" && is_equal_approx(wall->mass, 100.0), "a 100 kg Dune Wall");
		if (wall == nullptr) return;
		MatBody* stone = h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t);
		const int sid = stone->id;
		h.until([&]() { return h.has_event("capture") || a->health < 100.0; }, 40);
		check(h.has_event("capture", "body", Value(sid)) && a->health == 100.0, "Dune (25 x 1.2 = 30 >= 17) captures the stone");
		h.step(70);
		check(wall->mass >= 130.0 - 1e-6, S("held 1.0 s+: thickened (", wall->mass, " kg)"));
		// Lightning: grounded, the wall turns to glass.
		t->lock_target = a->id;
		const double g0 = h.w->mass_ledger.sand_to_glass;
		const Dict out = Conduction::discharge(*h.w, *t, a->chest(), bolt(16.0, 36.0), h.w->new_attack_id(), true);
		h.step();
		check(dbool(out, "blocked") && a->health == 100.0, S("the dune grounds a Storm Bolt (CP ", wall->mass * 0.25, " x 2.4)"));
		check(wall->alive && wall->mat == Mat::Glass && wall->tag == "glass", "and fuses into a glass wall");
		check(h.w->mass_ledger.sand_to_glass > g0 + wall->mass - 1e-6, "booked sand_to_glass");
		h.release(a, "guard");
	}
	// Water on a fresh dune: a mud wall (+5 CP) that collapses after 4 s.
	EU::Duel s2 = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	ActorState* t2 = s2.t;
	h.press(a2, "guard");
	h.step(20);
	BodyRef dune = EU::keep(h.w->get_body(a2->wall_body));
	check(dune != nullptr, "setup: a dune");
	if (dune == nullptr) return;
	BodyRef water = EU::keep(h.launch_at(a2, "water", 6.0, 14.0, Sim::AMBIENT_C, "", t2));
	water->form = Form::Blob;
	const double wm0 = h.w->water_mass();
	h.until([&]() { return !water->alive; }, 40);
	check(dune->tag == "mud", "water makes it a mud wall");
	AgentRef dg = Agent::of_body(*h.w, *dune);
	near(Interactions::counter_power(*dg), dune->mass * 0.25 + 5.0, 1e-6, "+5 CP");
	near(h.w->water_mass(), wm0, 1e-6, "the water is soaked in (booked)");
	h.step(260);
	check(!dune->alive, "the mud wall collapses after 4 s");
	h.release(a2, "guard");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_engulf_spits_a_light_projectile_back) {
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(2);
	BodyRef stone = EU::keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t, 2.6));
	h.until([&]() { return stone->attack_owner == a->id || a->health < 100.0; }, 30);
	check(stone->attack_owner == a->id && stone->vel.dot(t->pos - stone->pos) > 0.0f, "perfect Dune Wall (Engulf): spat back at the thrower");
	h.release(a, "guard");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_dune_push_collapses_the_wall_into_a_surge) {
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(70);
	BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
	check(wall != nullptr, "setup: a dune");
	if (wall == nullptr) return;
	const double m = wall->mass;
	h.flick(a, "guard", static_cast<int>(Gesture::Up));
	h.step();
	h.release(a, "guard");
	h.until([&]() { return wall->form == Form::Wave; }, 30);
	check(wall->form == Form::Wave && wall->tag == "sand_surge" && is_equal_approx(wall->mass, m), S("the ", m, " kg dune becomes the surge"));
	h.until([&]() { return t->health < 100.0; }, 120);
	check(t->health < 100.0, "and runs into the rival");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_quicksand_mires_roots_sinks_and_crusts_lava) {
	EU::Duel s = _duel(4.5);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	EU::perform(h, a, "sink", 0);
	auto qs = [](const MatBody& b) { return b.alive && b.tag == "quicksand"; };
	h.until([&]() { return first_where(qs) != nullptr; }, 30);
	BodyRef z = first_where(qs);
	check(z != nullptr, "setup: quicksand");
	if (z == nullptr) return;
	t->pos = z->pos;
	h.step(10);
	check(t->status.has("mired"), "a walker is mired (x0.3)");
	h.step(60);
	check(h.any_event("status", [&](const Dict& e) { return is(e, "actor", t->id) && dstr(e, "status") == "rooted" && ev_b(e, "on"); }),
	      "rooted after 1 s");
	BodyRef stone = EU::keep(EU::shot(h, Mat::Stone, 20.0, z->pos + V3(0.3, 0.4, 0), V3(0, -3, 0), t));
	h.until([&]() { return !stone->alive; }, 20);
	check(!stone->alive && h.has_event("sink", "body", Value(stone->id)), "a landing stone sinks");
	BodyRef wave = EU::keep(EU::lava_wave(h, 20.0, z->pos + V3(0, 0, -2.2), V3(0, 0, 1), t));
	const double e0 = EU::e0(h);
	h.until([&]() { return wave->form != Form::Wave; }, 60);
	check(wave->form != Form::Wave, "a lava wave entering it crusts and stalls");
	check(EU::energy_drift(h, e0) < 1e-6, "booked");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sandform_gathers_compresses_and_seizes_a_rival_cloud) {
	{
		EU::Duel s = _duel(8.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		const double em0 = h.w->earth_mass();
		h.press(a, "tech");
		h.step(160);
		BodyRef b = EU::keep(h.w->held(*a));
		check(b != nullptr && b->mat == Mat::Sand && b->mass > 18.0 && b->mass <= 30.0,
		      S("gathered sand while held, 6 kg/s (", b != nullptr ? b->mass : 0.0, " kg)"));
		h.it(a).attack_pressed = true;
		h.step();
		check(b != nullptr && b->mat == Mat::Stone && b->tag == "sandstone", "T+A Compress: sandstone");
		check(h.w->mass_ledger.sand_to_sandstone > 18.0, "booked sand_to_sandstone");
		h.release(a, "tech");
		h.step(3);
		check(b != nullptr && b->attack_owner == a->id && b->attack_id != 0, "thrown as a stone");
		h.step(120);
		near(h.w->earth_mass(), em0, 1e-6, "earth mass conserved");
	}
	// Seize the rival's sand cloud.
	EU::Duel s2 = _duel(8.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	ActorState* t2 = s2.t;
	MatBody* cloud = h.w->spawn_zone("sand_cloud", V3(0, 0, 0), 2.5, t2->id, 8.0, Mat::Sand, 12.0, 4.0);
	h.w->mass_ledger.ground_taken += 12.0;
	h.press(a2, "tech");
	h.until([&]() { return h.w->held(*a2) != nullptr; }, 40);
	check(h.w->held(*a2) == cloud && cloud->form != Form::Zone, "Sandform seizes the rival's cloud (REC)");
	h.release(a2, "tech");
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sand_surf_slides_and_rides) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	const Vec3 p0 = a->pos;
	h.it(a).move = V3(1, 0, 0);
	h.press(a, "evade");
	h.step(30);
	check((a->pos - p0).length() > 3.5f, S("Sand Surf: a 4 m slide (", (a->pos - p0).length(), ")"));
	h.step(10);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(20);
	check(a->stance == "surf", "held: riding the sand");
	const Vec3 p1 = a->pos;
	h.step(30);
	const double v = (a->pos - p1).length() / 0.5;
	check(v > 7.0, S("at ~8 m/s (", v, ")"));
	h.it(a).evade_held = false;
	h.it(a).move = Vec3();
	h.step(10);
}

FF_TEST_F(test_kit_earth_sand, SandFx, test_sand_column_cells) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	MatBody* dune = EU::wall(h, Mat::Sand, "sand", 100.0, V3(0, 0, 2), a);
	MatBody* dune2 = EU::wall(h, Mat::Sand, "sand", 130.0, V3(3, 0, 2), a);
	MatBody* stone = EU::shot(h, Mat::Stone, 20.0, V3(0, 1, 0), V3(0, 0, 17), t);
	check(EU::pr(h, stone, dune).outcome == "capture", "stone shot: CAP Dune Wall (30 eff)");
	check(EU::pr(h, stone, dune, 0, true).outcome == "reflect", "stone shot: REC Engulf* (spit back)");
	check(EU::pr(h, stone, "quicksand", 0, false, a).outcome == "sink", "stone shot: SNK Quicksand");
	MatBody* heave = EU::shot(h, Mat::Stone, 45.0, V3(1, 1, 0), V3(0, 0, 14), t);
	check(EU::pr(h, heave, dune2).outcome == "capture", "heavy stone: CAP Dune held T2 (130 kg)");
	const IxResult ph0 = EU::pr(h, heave, dune);
	check(ph0.band == "partial", S("heavy stone vs a fresh dune: partial (", ph0.ratio, ")"));
	check(EU::pr(h, heave, "quicksand", 2, false, a).outcome == "sink", "heavy stone: SNK Quicksand T2");
	MatBody* boulder = EU::shot(h, Mat::Stone, 200.0, V3(2, 1, 0), V3(0, 0, 11), t);
	check(EU::pr(h, boulder, "quicksand", 3, false, a).outcome == "slow", "boulder: WKN Quicksand T3 (bogged)");
	MatBody* hot = EU::shot(h, Mat::Stone, 20.0, V3(-1, 1, 0), V3(0, 0, 17), t, "", 1000.0);
	check(EU::pr(h, hot, dune).outcome == "capture", "hot rock: CAP Dune (30 >= 27)");
	MatBody* blob = EU::lava(h, 20.0, V3(4, 1, 0));
	blob->vel = V3(0, 0, 12);
	blob->attack_id = h.w->new_attack_id();
	blob->attack_owner = t->id;
	check(EU::pr(h, blob, dune).outcome == "earth_crust", "magma blob: crusted by the Dune (x1.5)");
	MatBody* wave = EU::lava_wave(h, 20.0, V3(-3, 0, 0), V3(0, 0, 1), t);
	check(EU::pr(h, wave, dune).outcome == "earth_crust", "lava wave: glass crust (Dune)");
	check(EU::pr(h, wave, "quicksand", 0, false, a).outcome == "earth_crust", "lava wave: glass crust (Quicksand)");
	MatBody* lance = EU::shot(h, Mat::Metal, 12.0, V3(5, 1.2, 0), V3(0, 0, 42), t, "lance");
	check(EU::pr(h, lance, dune).band == "partial", "Railspike: partial pierce of a dune");
	MatBody* sand = EU::shot(h, Mat::Sand, 5.0, V3(-4, 1, 0), V3(0, 0, 22), t, "slug");
	check(Interactions::allows(*sand, "grip_sand"), "sand: REC Sandform");
	check(EU::pr(h, sand, dune).outcome == "absorb", "sand: ABS Dune (adds mass)");
	MatBody* water = EU::shot(h, Mat::Water, 6.0, V3(-5, 1, 0), V3(0, 0, 16), t);
	check(EU::pr(h, water, dune).outcome == "earth_mud", "water: Dune -> mud wall");
	MatBody* ww = EU::shot(h, Mat::Water, 18.0, V3(6, 0, 0), V3(0, 0, 9), t);
	ww->form = Form::Wave;
	check(EU::pr(h, ww, "quicksand", 0, false, a).outcome == "earth_mud", "water wave: Quicksand -> mud bog");
	MatBody* veil = h.w->spawn_zone("sand_cloud", V3(0, 0, -1), 4.0, a->id, 6.0, Mat::Sand, 8.0);
	h.w->mass_ledger.ground_taken += 8.0;
	MatBody* mist = h.w->spawn_body(Mat::Water, Form::Cloud, 2.0, V3(0, 1, -1), "test");
	check(EU::pr(h, mist, veil).outcome == "absorb", "mist: ABS Veil of Grit");
	check(EU::prv(h, "steam", D({{"H", 6.0}}), dune).outcome == "block", "steam: Dune absorbs (blocks)");
	check(EU::prv(h, "flame", D({{"H", 8.0}}), dune).outcome == "block", "flame: smothered by the Dune (x2)");
	check(EU::prv(h, "flame", D({{"H", 8.0}}), veil).outcome == "earth_smother", "flame: smothered by the Veil");
	check(EU::prv(h, "blue_fire", D({{"H", 12.0}}), dune).outcome == "earth_glassify", "blue fire: the dune fuses to glass");
	check(EU::prv(h, "lightning", D({{"E", 52.0}}), dune).outcome == "earth_glass_ground", "Skybreak E 52: grounded (cap 60) -> glass");
	check(EU::prv(h, "lightning", D({{"E", 70.0}}), dune).outcome == "shatter", "E 70 > 60: the dune is blasted");
	check(EU::prv(h, "lightning", D({{"E", 24.0}}), veil).outcome == "earth_bolt_grit", "bolt through the Veil: -50 %, glass");
	check(EU::prv(h, "blast", D({{"P", 34.0}}), dune).outcome == "block", "combustion: ABS Dune (x1.5)");
	check(EU::prv(h, "gust", D({{"P", 28.0}}), dune).outcome == "block", "gust: BLK Dune");
	check(EU::prv(h, "sound", D({{"P", 32.0}}), dune).outcome == "block", "sound: ABS Dune (x1.5)");
	check(EU::prv(h, "sound", D({{"P", 8.0}}), veil).outcome == "absorb", "sound (Clap 8): ABS Veil (6 x 1.5)");
	MatBody* surge = EU::shot(h, Mat::Sand, 30.0, V3(-6, 0, 0), V3(0, 0, 9), t);
	surge->form = Form::Wave;
	check(Interactions::allows(*surge, "grip_sand"), "sand surge: REC Sandform contest");
}
