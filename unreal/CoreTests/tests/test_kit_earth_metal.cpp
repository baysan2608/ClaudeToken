// Port of game/tests/sim/test_kit_earth_metal.gd: Earth / Metal (sub 1): satchel, Razor Disc ladder, Iron Lance -> rod,
// Lodestone Line caltrops, Chain Arc, Aegis Plate (+ heat, lightning, Magnet Catch), Plate Rush, Rod Plant, Lodestone
// Grip / Reforge / Recall / plate scrap, Magnet Glide, Iron Stance and the Metal column of MOVESET §8.1.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Earth/KitEarth.h"
#include "Sim/Conduction.h"
#include "Sim/Materials.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct MetalFx : HarnessCase {
	EU::Duel _duel(double dist = 7.0, int t_elem = Sim::FIRE) {
		SimHarness& hh = H(4);
		return EU::duel(hh, 1, t_elem, dist);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
	std::vector<BodyRef> bodies_where(const std::function<bool(const MatBody&)>& f) {
		std::vector<BodyRef> out;
		for (const BodyRef& b : h().w->bodies)
			if (f(*b)) out.push_back(b);
		return out;
	}
	Dict bolt(double damage) {
		return D({{"range", 16.0}, {"damage", damage}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	}
};
}  // namespace

FF_TEST_F(test_kit_earth_metal, MetalFx, test_razor_disc_ladder_spends_the_satchel) {
	std::vector<int> counts;
	for (int tier = 0; tier < 4; ++tier) {
		EU::Duel s = _duel();
		SimHarness& h = this->h();
		ActorState* a = s.a;
		const double mm0 = h.w->metal_mass();
		EU::perform(h, a, "strike", tier);
		h.until([&]() { return h.has_event("launch"); }, 40);
		h.step(tier == 3 ? 30 : 2);
		const std::vector<BodyRef> discs = bodies_where([](const MatBody& b) { return b.alive && b.tag == "disc" && b.mat == Mat::Metal; });
		counts.push_back(static_cast<int>(discs.size()));
		near(a->metal_carried, 12.0 - 2.0 * static_cast<double>(discs.size()), 1e-6, S("T", tier, ": 2 kg per disc from the satchel"));
		near(h.w->metal_mass(), mm0, 1e-6, S("T", tier, ": metal mass conserved (satchel -> field)"));
		if (tier == 2) {
			bool all = true;
			for (const BodyRef& b : discs)
				if (dint(b->props, "ricochet", 0) != 1) all = false;
			check(all, "T2 discs ricochet once");
		}
		if (tier == 3) {
			bool all = true;
			for (const BodyRef& b : discs)
				if (!(b->props.has("orbit_until") || b->vel.length() > 5.0f)) all = false;
			check(all, "T3 Disc Storm: orbiting");
			h.step(40);
			bool fired = true;
			for (const BodyRef& b : discs)
				if (b->props.has("orbit_until")) fired = false;
			check(fired, "then they fire");
		}
	}
	check(counts == std::vector<int>({1, 2, 3, 5}), S("1 / 2 / 3 / 5 discs: ", counts.size() == 4 ? counts[0] * 1000 + counts[1] * 100 + counts[2] * 10 + counts[3] : -1));
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_iron_lance_embeds_and_railspike_pierces) {
	{
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		EU::wall(h, Mat::Stone, "", 120.0, V3(0, 0, -1.0), t, 0.0);
		EU::perform(h, a, "thrust", 0);
		const int n = h.until([&]() { return h.has_event("stick"); }, 60);
		check(n > 0, "the lance embeds in the rival's Bulwark");
		const std::vector<BodyRef> rods = bodies_where([](const MatBody& b) { return b.alive && b.tag == "rod" && b.mat == Mat::Metal; });
		check(rods.size() == 1 && rods[0]->static_body && Materials::conducts(*rods[0]), "it stays as a conductive rod");
		check(!rods.empty() ? dint(rods[0]->props, "metal_owner", -1) == a->id : false, "recallable (owned)");
	}
	EU::Duel s2 = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	EU::perform(h, a2, "thrust", 3);
	h.until([&]() { return h.has_event("launch"); }, 40);
	const std::vector<BodyRef> ls = bodies_where([](const MatBody& b) { return b.alive && b.tag == "lance"; });
	check(ls.size() == 1 && is_equal_approx(ls[0]->mass, 12.0) && dint(ls[0]->props, "pierce", 0) == 1, "Railspike: 12 kg, pierces one body");
	check(ev_f(h.last_event("launch"), "speed", 0.0) >= 42.0, "@42 m/s");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_lodestone_line_springs_caltrops_that_slow_and_conduct) {
	EU::Duel s = _duel(6.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double mm0 = h.w->metal_mass();
	EU::perform(h, a, "ground", 0);
	auto caltrops = [&]() { return bodies_where([](const MatBody& b) { return b.alive && b.tag == "caltrops" && b.form == Form::Zone; }); };
	const int n = h.until([&]() { return !caltrops().empty(); }, 120);
	check(n > 0, "the filings spring into a caltrops zone");
	BodyRef z;
	if (n > 0) {
		const std::vector<BodyRef> zs = bodies_where([](const MatBody& b) { return b.alive && b.tag == "caltrops"; });
		if (!zs.empty()) z = zs[0];
	}
	check(z != nullptr && Materials::conducts(*z) && z->mat == Mat::Metal, "a metal conductor node");
	check(z != nullptr && is_equal_approx(z->zone_radius, 2.0), "r 2 m at T0");
	if (z != nullptr) t->pos = z->pos;
	h.step(20);
	check(t->status.has("slowed"), "a walker inside is slowed");
	check(t->health < 100.0, "and chipped");
	near(h.w->metal_mass(), mm0, 1e-6, "metal mass conserved");
	h.step(300);
	check(z != nullptr && (!z->alive || z->form != Form::Zone), "the field ends after 4 s (the metal stays as scrap)");
	near(h.w->metal_mass(), mm0, 1e-6, "metal mass conserved after");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_chain_arc_wraps_a_stone_out_of_the_air) {
	{
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		h.flick(a, "attack", static_cast<int>(Gesture::Side));
		h.step(36);
		BodyRef stone = EU::keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t, 4.5));
		h.release(a, "attack");
		h.until([&]() { return h.has_event("capture") || a->health < 100.0; }, 40);
		check(h.has_event("capture", "body", Value(stone->id)), "Chain Arc T1 (16 x 1.2 >= 17) wraps the stone (CAP)");
		check(a->health == 100.0, "it never hits the swinger");
		h.step(40);
		check(KitEarthUtil::flat_dist2(stone->pos, a->pos) < 2.5f,
		      S("yanked to the swinger's feet (", KitEarthUtil::flat_dist2(stone->pos, a->pos), " m)"));
	}
	EU::Duel s2 = _duel(4.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	ActorState* t2 = s2.t;
	h.flick(a2, "attack", static_cast<int>(Gesture::Side));
	h.step();
	h.release(a2, "attack");
	h.until([&]() { return h.any_event("hit", [&](const Dict& e) { return is(e, "actor", t2->id); }); }, 40);
	check(t2->vel.dot(a2->pos - t2->pos) > 0.0f, "a fighter in the arc is yanked toward the swinger");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_aegis_plate_blocks_and_magnet_catches_metal) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(20);
	MatBody* plate = h.w->held(*a);
	check(plate != nullptr && plate->tag == "plate" && is_equal_approx(plate->mass, 6.0), "a 6 kg plate from the satchel");
	near(a->metal_carried, 6.0, 1e-6, "satchel 12 -> 6");
	h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t);
	h.until([&]() { return h.has_event("block") || h.has_event("hit"); }, 40);
	check(h.any_event("block", [&](const Dict& e) { return is(e, "actor", a->id); }), "Aegis (CP 20 >= 17) blocks a stone shot");
	h.release(a, "guard");
	h.step(30);
	near(a->metal_carried, 12.0, 1e-6, "guard down: the plate goes back into the satchel");
	h.log.clear();
	h.press(a, "guard");
	h.step(2);
	const double mm0 = h.w->metal_mass();
	BodyRef disc = EU::keep(EU::shot(h, Mat::Metal, 2.0, a->chest() + a->forward() * 2.2, -a->forward() * 24.0, t, "disc"));
	h.until([&]() { return !disc->alive || h.has_event("hit"); }, 30);
	check(!disc->alive && h.has_event("satchel"), "perfect Aegis: Magnet Catch pulls the disc into the satchel");
	near(h.w->metal_mass(), mm0, 1e-6, "metal mass conserved");
	h.release(a, "guard");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_aegis_plate_heats_red_hot_and_is_dropped) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	h.press(a, "guard");
	h.step(10);
	BodyRef plate = EU::keep(h.w->held(*a));
	check(plate != nullptr, "setup: plate held");
	if (plate == nullptr) return;
	const double e0 = EU::e0(h);
	h.w->ledger.generated += h.w->heat_body(*plate, 20.0);
	h.step(3);
	check(h.w->held(*a) == plate.get(), S("warm plate still held (", plate->temp, " C)"));
	h.w->ledger.generated += h.w->heat_body(*plate, 20.0);
	h.step(3);
	check(h.w->held(*a) == nullptr && h.has_event("drop"), "red-hot (>= 300 C): dropped");
	check(dint(plate->props, "metal_owner", -1) == a->id, "still A's metal on the field");
	check(EU::energy_drift(h, e0) < 1e-6, "energy balanced");
	h.release(a, "guard");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_aegis_grounds_bolts_on_stone_and_conducts_them_on_the_plate) {
	std::vector<double> dmg;
	for (const char* where : {"stone", "plate"}) {
		EU::Duel s = _duel(8.0, Sim::FIRE);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		if (std::string(where) == "plate") {
			a->pos = V3(-9, 0.02, -1);
			t->pos = V3(-9, 0, -9);
			h.step(5);
		}
		h.press(a, "guard");
		h.step(20);
		const double hp0 = a->health;
		t->lock_target = a->id;
		Conduction::discharge(*h.w, *t, a->chest(),
		                      D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}}),
		                      h.w->new_attack_id(), true);
		dmg.push_back(hp0 - a->health);
		h.release(a, "guard");
	}
	check(dmg[0] == 0.0, S("on stone the plate grounds the bolt (", dmg[0], ")"));
	check(dmg[1] > 24.0, S("on the metal plate it conducts into the holder x1.2 (", dmg[1], ")"));
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_plate_rush_hurls_the_plate) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(15);
	BodyRef plate = EU::keep(h.w->held(*a));
	check(plate != nullptr, "setup: plate held");
	if (plate == nullptr) return;
	h.flick(a, "guard", static_cast<int>(Gesture::Up));
	h.step();
	h.release(a, "guard");
	h.until([&]() { return plate->attack_id != 0; }, 30);
	check(plate->attack_id != 0 && plate->attack_owner == a->id && plate->tag == "plate", "the plate flies as A's attack");
	h.until([&]() { return t->health < 100.0; }, 60);
	check(t->health < 100.0, "and hits");
	near(a->metal_carried, 6.0, 1e-6, "the plate is on the field now (recallable)");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_rod_plant_draws_and_grounds_bolts_until_it_melts) {
	EU::Duel s = _duel(12.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(10);
	h.flick(a, "guard", static_cast<int>(Gesture::Down));
	h.step();
	h.release(a, "guard");
	h.step(20);
	const std::vector<BodyRef> rods = bodies_where([](const MatBody& b) { return b.alive && b.tag == "rod" && b.form == Form::Zone; });
	check(rods.size() == 1, "a rod is planted (a 6 m field)");
	BodyRef rod = rods.empty() ? nullptr : rods[0];
	t->lock_target = a->id;
	const Dict out = Conduction::discharge(*h.w, *t, a->chest(), bolt(36.0), h.w->new_attack_id(), true);
	h.step();
	check(dbool(out, "blocked") && a->health == 100.0, "E 36 <= 60: drawn to the rod and grounded");
	check(h.has_event("grounded", "via", Value("rod")), "grounded via the rod");
	const Dict out2 = Conduction::discharge(*h.w, *t, a->chest(), bolt(70.0), h.w->new_attack_id(), true);
	check(!dbool(out2, "blocked") && rod != nullptr && !rod->alive, "E 70 > 60: the rod melts");
	near(dnum(out2, "e", 0.0), 40.0, 1e-6, "and the bolt continues with 70 - 0.5 x 60");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_rod_touching_a_puddle_conducts_into_it) {
	EU::Duel s = _duel(12.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	ActorState* bystander = h.actor("B", V3(3, 0, 1.6), 1, Dict(), Sim::WATER);
	bystander->is_dummy = true;
	MatBody* pd = h.w->spawn_body(Mat::Water, Form::Puddle, 6.0, V3(3, 0, 1.6), "scenario");
	pd->update_radius_puddle();
	MatBody* rod = h.w->spawn_zone("rod", V3(3, 0, 2.1), 6.0, a->id, 60.0, Mat::Metal, 3.0, -1.0, "test");
	h.w->mass_ledger.metal_taken += 3.0;
	rod->static_body = true;
	rod->props.set("ccls", "rod");
	rod->props.set("barrier", true);
	rod->props.set("height", 3.5);
	h.step(3);
	t->lock_target = a->id;
	Conduction::discharge(*h.w, *t, a->chest(), bolt(30.0), h.w->new_attack_id(), true);
	h.step();
	check(a->health == 100.0, "the rod grounds the bolt");
	check(bystander->health < 100.0, S("but it conducts into the puddle it touches (", bystander->health, ")"));
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_lodestone_grip_steals_reforges_and_throws) {
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	BodyRef disc = EU::keep(EU::shot(h, Mat::Metal, 2.0, a->chest() + a->forward() * 6.0, -a->forward() * 8.0, t, "disc"));
	h.press(a, "tech");
	h.until([&]() { return h.w->held(*a) != nullptr; }, 40);
	check(h.w->held(*a) == disc.get(), "Lodestone Grip seizes the rival's disc in flight (REC)");
	h.step(3);
	h.it(a).attack_pressed = true;
	h.step();
	check(h.has_event("shape", "to", Value("lance")) && disc->tag == "lance", "T+A Reforge: a lance");
	h.release(a, "tech");
	h.step(2);
	check(disc->attack_owner == a->id && disc->attack_id != 0 && disc->tag == "lance", "thrown back as A's lance");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_recall_brings_every_piece_back_through_the_rival) {
	EU::Duel s = _duel(5.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double mm0 = h.w->metal_mass();
	// Two discs lie behind the rival.
	for (double x : {-0.6, 0.6}) {
		MatBody* d = h.w->spawn_body(Mat::Metal, Form::Chunk, 2.0, V3(x, 0.3, -6.0), "test");
		h.w->mass_ledger.metal_taken += 2.0;
		d->tag = "disc";
		d->on_ground = true;
		d->props.set("metal_owner", a->id);
	}
	const double sat0 = a->metal_carried;
	h.press(a, "tech");
	h.step(24);
	h.release(a, "tech");
	h.step();
	check(h.has_event("recall"), "nothing to grip: Recall");
	h.until([&]() { return a->metal_carried >= sat0 + 4.0 - 1e-6; }, 120);
	near(a->metal_carried, sat0 + 4.0, 1e-6, "both discs back in the satchel");
	check(t->health < 100.0, S("they hit the rival on the way (from behind) (", t->health, ")"));
	near(h.w->metal_mass(), mm0, 1e-6, "metal mass conserved");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_scrap_rip_on_the_arena_plate) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	a->pos = V3(-9, 0.02, 0);
	h.step(3);
	const double taken0 = h.w->mass_ledger.metal_taken;
	h.press(a, "tech");
	h.until([&]() { return h.w->held(*a) != nullptr; }, 40);
	MatBody* b = h.w->held(*a);
	check(b != nullptr && is_equal_approx(b->mass, 10.0), "10 kg of scrap ripped from the plate");
	near(h.w->mass_ledger.metal_taken, taken0 + 10.0, 1e-6, "booked metal_taken");
	h.release(a, "tech");
	h.step(30);
	h.log.clear();
	h.press(a, "tech");
	h.step(30);
	check(!h.has_event("rip"), "1.2 s cooldown");
	h.release(a, "tech");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_magnet_glide_dashes_to_metal_and_iron_stance_anchors) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	MatBody* rod = h.w->spawn_body(Mat::Metal, Form::Chunk, 3.0, a->pos + V3(5, 0.3, 0), "test");
	h.w->mass_ledger.metal_taken += 3.0;
	rod->tag = "rod";
	rod->static_body = true;
	const Vec3 p0 = a->pos;
	h.press(a, "evade");
	h.step(30);
	check((a->pos - rod->pos).length() < 1.8f, S("Magnet Glide lands at the rod (", (a->pos - rod->pos).length(), " m)"));
	check((a->pos - p0).length() > 3.5f, S("a long dash (", (a->pos - p0).length(), " m)"));
	h.step(20);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(20);
	check(a->stance == "iron" && a->anchored && is_equal_approx(a->armor, 0.25), "Iron Stance: anchored, 25 % armor");
	h.it(a).evade_held = false;
	h.step(15);
	check(a->stance.empty() && !a->anchored, "released");
}

FF_TEST_F(test_kit_earth_metal, MetalFx, test_metal_column_cells) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(20);
	MatBody* plate = h.w->held(*a);
	AgentRef g = Agent::of_guard(*h.w, *a);
	check(g->ccls == "plate_metal" && is_equal_approx(g->power, 19.8), S("Aegis: plate_metal, CP 6 x 3.3 (", g->ccls, " ", g->power, ")"));
	auto P = [&](MatBody* b) {
		AgentRef th = Agent::of_body(*h.w, *b, a);
		AgentRef gd = Agent::of_guard(*h.w, *a);
		return Interactions::predict(h.w, *th, *gd);
	};
	MatBody* stone = EU::shot(h, Mat::Stone, 20.0, V3(0, 1, 0), V3(0, 0, 17), t);
	check(P(stone).outcome == "block", "stone shot: BLK Aegis (20 vs 17)");
	MatBody* heave = EU::shot(h, Mat::Stone, 45.0, V3(1, 1, 0), V3(0, 0, 14), t);
	const IxResult ph = P(heave);
	check(ph.band == "partial" && ph.outcome == "weaken", S("heavy stone: WKN Aegis (plate knocked back) (", ph.ratio, ")"));
	MatBody* boulder = EU::shot(h, Mat::Stone, 200.0, V3(2, 1, 0), V3(0, 0, 11), t);
	check(P(boulder).outcome == "overwhelm", "boulder: FAIL");
	MatBody* hot = EU::shot(h, Mat::Stone, 20.0, V3(-1, 1, 0), V3(0, 0, 17), t, "", 1000.0);
	check(P(hot).outcome == "earth_plate_heat", "hot rock: the plate heats");
	MatBody* wave = EU::lava_wave(h, 20.0, V3(-2, 0, 0), V3(0, 0, 1), t);
	check(P(wave).outcome == "overwhelm", "lava wave: FAIL (jump)");
	MatBody* disc = EU::shot(h, Mat::Metal, 2.0, V3(3, 1, 0), V3(0, 0, 24), t, "disc");
	check(Interactions::allows(*disc, "grip_metal"), "metal: Lodestone Grip may seize it (REC)");
	auto PM = [&](MatBody* b, const char* move) {
		AgentRef th = Agent::of_body(*h.w, *b, a);
		AgentRef mv = Agent::of_move(h.w, a, move, 0, false);
		return Interactions::predict(h.w, *th, *mv);
	};
	check(PM(disc, "lodestone_grip").outcome == "reclaim", "metal x grip_metal: reclaim");
	MatBody* ice = EU::shot(h, Mat::Water, 4.0, V3(4, 1, 0), V3(0, 0, 24), t, "", -5.0);
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	check(PM(ice, "chain_arc").outcome == "deflect", "ice: DEF Chain Arc");
	MatBody* vine = h.w->spawn_body(Mat::Plant, Form::Chunk, 6.0, V3(5, 1, 0), "test");
	vine->vel = V3(0, 0, 10);
	vine->attack_id = h.w->new_attack_id();
	vine->attack_owner = t->id;
	check(PM(vine, "chain_arc").outcome == "shatter", "vines: cut by the chain");
	auto PV = [&](const char* cls, const Dict& ch) {
		AgentRef v = Agent::of_volume(h.w, t, nullptr, cls, t->chest(), V3(0, 0, 1), ch);
		AgentRef gd = Agent::of_guard(*h.w, *a);
		return Interactions::predict(h.w, *v, *gd);
	};
	check(PV("flame", D({{"H", 8.0}, {"heat_hu", 160.0}})).outcome == "earth_plate_heat", "flame: BLK Aegis, the plate heats");
	check(PV("sound", D({{"P", 14.0}})).outcome == "reflect", "sound: RFL Aegis (x1.2)");
	check(PV("blast", D({{"P", 16.0}})).outcome == "block", "combustion: BLK Aegis");
	MatBody* rod = h.w->spawn_zone("rod", V3(0, 0, 0), 6.0, a->id, 60.0, Mat::Metal, 3.0);
	h.w->mass_ledger.metal_taken += 3.0;
	rod->props.set("ccls", "rod");
	check(EU::prv(h, "lightning", D({{"E", 52.0}}), rod).outcome == "earth_rod_ground", "Skybreak E 52: Rod Plant grounds it (cap 60)");
	check(EU::prv(h, "lightning", D({{"E", 70.0}}), rod).outcome == "earth_rod_melt", "E 70: over capacity, the rod melts");
	a->status.set("anchored", D({{"t", -1.0}, {"mag", 30.0}, {"src", a->id}}));
	AgentRef tor = Agent::of_volume(h.w, t, nullptr, "tornado", t->chest(), V3(0, 0, 1), D({{"P", 25.0}}));
	AgentRef st = Agent::of_stance(h.w, *a);
	check(Interactions::predict(h.w, *tor, *st).outcome == "block", "tornado 25: Iron Stance anchor (30) holds");
	h.release(a, "guard");
	check(plate != nullptr, "plate existed");
}
