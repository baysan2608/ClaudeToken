// Port of game/tests/sim/test_kit_earth_magma.gd: Earth / Magma (sub 3): Ember Clot ladder (globs, bomb -> lava puddle,
// Caldera pool), Lava Lash / Molten Lance (wall face -> slump), Magma Surge / Lava Tide (re-pour: "push it back on them"),
// Spatter, Magma Curtain / Obsidian Set, Slag Wave, Melt Pit, Magma Hold / Cool & Set / Reverse Tide, Cinder Step, Lava
// Wade and the Magma column of MOVESET §8.1. Every HU is booked (energy identity).
#include "ff_test.h"
#include "kit_earth_util.h"
#include "sim_harness.h"

#include "Sim/Status.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct MagmaFx : HarnessCase {
	EU::Duel _duel(double dist = 7.0, int t_elem = Sim::FIRE) {
		SimHarness& hh = H(6);
		return EU::duel(hh, 3, t_elem, dist);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
	BodyRef first_where(const std::function<bool(const MatBody&)>& f) {
		for (const BodyRef& b : h().w->bodies)
			if (f(*b)) return b;
		return nullptr;
	}
	static int wave_count(const Dict& ev) { return static_cast<int>(darr(ev, "waves").size()); }
};
}  // namespace

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_ember_clot_ladder_globs_bomb_and_caldera) {
	for (int tier = 0; tier < 4; ++tier) {
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		const double e0 = EU::e0(h);
		const double sm0 = h.w->stone_mass();
		EU::perform(h, a, "strike", tier);
		h.until([&]() { return h.has_event("launch"); }, 40);
		std::vector<BodyRef> globs;
		for (const Dict& e : h.events("launch"))
			if (is(e, "actor", a->id)) globs.push_back(EU::keep(h.w->get_body(ev_i(e, "body", -1))));
		check(globs.size() == (tier == 1 ? 3u : 1u), S("T", tier, ": ", globs.size(), " glob(s)"));
		h.step();
		for (const BodyRef& g : globs)
			check(g != nullptr && g->is_stone() && g->liquid > 0.9, S("T", tier, ": molten (liquid ", g != nullptr ? g->liquid : -1.0, ")"));
		if (tier <= 1) {
			h.until([&]() { return t->status.has("burning"); }, 60);
			check(t->status.has("burning"), S("T", tier, ": the glob burns the target"));
		} else {
			auto lp = [](const MatBody& b) { return b.alive && b.tag == "lava_pool"; };
			const int n = h.until([&]() { return first_where(lp) != nullptr; }, 90);
			check(n > 0, S("T", tier, ": the bomb splashes into a lava pool"));
			BodyRef pool = n > 0 ? first_where(lp) : nullptr;
			if (pool != nullptr) {
				near(pool->zone_radius, tier == 3 ? 1.75 : 1.0, 1e-6, S("T", tier, " pool radius"));
				near(pool->mass, tier == 3 ? 20.0 : 12.0, 1e-6, "the pool is the bomb's lava");
				check(pool->is_stone() && pool->liquid > 0.5, "still molten");
			}
		}
		check(EU::energy_drift(h, e0) < 1e-5, S("T", tier, ": every HU booked (", EU::energy_drift(h, e0), ")"));
		near(h.w->stone_mass(), sm0, 1e-6, S("T", tier, ": stone from the ground (booked)"));
	}
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_magma_surge_pushes_a_slumped_wall_back_at_the_builder) {
	// Owner example, second half of Melt & Return: the rival's wall face has slumped into a molten pool on this side;
	// Magma Surge re-pours it as a wave that reaches the builder.
	EU::Duel s = _duel(8.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double e0 = EU::e0(h);
	BodyRef slump = EU::keep(EU::lava(h, 30.0, V3(0, 0.1, 0.5), 0.55));
	h.step();
	EU::perform(h, a, "ground", 0);
	h.until([&]() { return slump->form == Form::Wave; }, 40);
	check(slump->form == Form::Wave && slump->attack_owner == a->id, "the slump becomes A's lava wave");
	check(slump->wave_dir.dot(t->pos - slump->pos) > 0.0f, "flowing at the builder");
	const int n = h.until([&]() { return t->health < 100.0; }, 180);
	check(n > 0, S("it reaches the builder (", t->health, ")"));
	check(EU::energy_drift(h, e0) < 1e-5, S("energy booked (", EU::energy_drift(h, e0), ")"));
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_melt_and_return_entirely_with_magma) {
	// Two Molten Lances (300 HU each) slump the rival's Bulwark face; Magma Surge sends it back.
	EU::Duel s = _duel(7.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	BodyRef wall = EU::keep(EU::wall(h, Mat::Stone, "", 120.0, V3(0, 0, -1.6), t, 0.0));
	const double e0 = EU::e0(h);
	const double sm0 = h.w->stone_mass();
	a->focus = 100.0;
	EU::perform(h, a, "thrust", 3);
	h.until([&]() { return a->action == nullptr; }, 120);
	check(dnum(wall->props, "face_hu", 0.0) > 250.0 || h.has_event("slump"), "the first lance pours its heat into the face");
	a->focus = 100.0;
	a->heat_reserve = 300.0;
	h.w->ledger.generated += 300.0;
	EU::perform(h, a, "thrust", 3);
	const int n = h.until([&]() { return h.has_event("slump"); }, 120);
	check(n > 0, "the second lance slumps the face");
	check(!wall->alive, "the rest of the wall crumbles");
	h.until([&]() { return a->action == nullptr; }, 120);
	BodyRef face = EU::keep(h.w->get_body(ev_i(h.last_event("slump"), "body", -1)));
	check(face != nullptr && face->liquid >= 0.5, S("a molten face on A's side (", face != nullptr ? face->liquid : -1.0, ")"));
	a->focus = 100.0;
	EU::perform(h, a, "ground", 0);
	h.until([&]() { return face != nullptr && face->form == Form::Wave; }, 40);
	check(face != nullptr && face->form == Form::Wave && face->attack_owner == a->id, "Magma Surge re-pours it");
	h.until([&]() { return t->health < 100.0; }, 180);
	check(t->health < 100.0, "back on the builder");
	near(h.w->stone_mass(), sm0, 1e-6, "stone mass conserved");
	check(EU::energy_drift(h, e0) < 1e-5, S("energy booked (", EU::energy_drift(h, e0), ")"));
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_magma_surge_raises_a_vein_and_lava_tide_merges) {
	{
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		const double e0 = EU::e0(h);
		EU::perform(h, a, "ground", 0);
		h.until([&]() { return h.has_event("magma_surge"); }, 40);
		const Dict ev = h.last_event("magma_surge");
		check(wave_count(ev) == 1, "no lava near: one vein wave");
		MatBody* v = wave_count(ev) > 0 ? h.w->get_body(vint(darr(ev, "waves")[0])) : nullptr;
		check(v != nullptr && is_equal_approx(v->mass, 8.0) && v->liquid > 0.9, "an 8 kg molten vein (160 HU)");
		check(EU::energy_drift(h, e0) < 1e-5, "booked");
	}
	{
		EU::Duel s2 = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a2 = s2.a;
		EU::lava(h, 12.0, V3(1.5, 0.1, 2.5));
		EU::lava(h, 20.0, V3(-1.5, 0.1, 2.0));
		MatBody* pool = EU::lava(h, 12.0, V3(0.0, 0.1, 1.5));
		pool->form = Form::Zone;
		pool->tag = "lava_pool";
		pool->zone_radius = 1.0;
		EU::perform(h, a2, "ground", 0);
		h.until([&]() { return h.has_event("magma_surge"); }, 40);
		check(wave_count(h.last_event("magma_surge")) == 3, "T0: every molten body within 6 m becomes a wave (incl. the pool)");
	}
	EU::Duel s3 = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a3 = s3.a;
	EU::lava(h, 12.0, V3(1.5, 0.1, 2.5));
	EU::lava(h, 20.0, V3(-1.5, 0.1, 2.0));
	EU::perform(h, a3, "ground", 3);
	h.until([&]() { return h.has_event("magma_surge"); }, 40);
	const Dict ev3 = h.last_event("magma_surge");
	check(wave_count(ev3) == 1, "T3 Lava Tide: merged into one wave");
	MatBody* big = wave_count(ev3) > 0 ? h.w->get_body(vint(darr(ev3, "waves")[0])) : nullptr;
	check(big != nullptr && is_equal_approx(big->mass, 32.0), "32 kg of lava");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_magma_curtain_sticks_absorbs_and_sets_obsidian) {
	EU::Duel s = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double e0 = EU::e0(h);
	h.press(a, "guard");
	h.step(20);
	BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
	check(wall != nullptr && wall->tag == "obsidian" && wall->heat_payload > 100.0, "a curtain with a molten face");
	if (wall == nullptr) return;
	{
		AgentRef wg = Agent::of_body(*h.w, *wall);
		near(Interactions::counter_power(*wg), 28.0, 1e-6, "CP 28");
	}
	BodyRef stone = EU::keep(h.launch_at(a, "stone", 10.0, 17.0, Sim::AMBIENT_C, "", t));
	const double m0 = wall->mass;
	h.until([&]() { return !stone->alive || a->health < 100.0; }, 40);
	check(!stone->alive && is_equal_approx(wall->mass, m0 + 10.0), "a small stone sticks and fuses into the wall");
	BodyRef blob = EU::keep(EU::lava(h, 12.0, a->chest() + a->forward() * 6.0));
	blob->vel = -a->forward() * 12.0;
	blob->attack_id = h.w->new_attack_id();
	blob->attack_owner = t->id;
	blob->gravity_scale = 0.0;
	h.until([&]() { return !blob->alive || a->health < 100.0; }, 40);
	check(!blob->alive && a->health == 100.0, "lava is absorbed into the face");
	BodyRef water = EU::keep(h.launch_at(a, "water", 3.0, 14.0, Sim::AMBIENT_C, "", t));
	water->form = Form::Blob;
	h.until([&]() { return dbool(wall->props, "set", false); }, 40);
	check(dbool(wall->props, "set", false) && h.has_event("steam"), "water: steam burst, the face sets to obsidian");
	{
		AgentRef wg = Agent::of_body(*h.w, *wall);
		near(Interactions::counter_power(*wg), wall->mass * 0.38, 1e-6, "harder (0.38 / kg: 38 for 100 kg)");
	}
	check(EU::energy_drift(h, e0) < 1e-5, S("energy booked (", EU::energy_drift(h, e0), ")"));
	h.release(a, "guard");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_slag_wave_pours_the_molten_face) {
	EU::Duel s = _duel(8.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	h.press(a, "guard");
	h.step(20);
	BodyRef wall = EU::keep(h.w->get_body(a->wall_body));
	h.flick(a, "guard", static_cast<int>(Gesture::Up));
	h.step();
	h.release(a, "guard");
	auto slag = [](const Dict& e) { return dstr(e, "why", "") == "slag_wave"; };
	h.until([&]() { return h.any_event("transform", slag); }, 30);
	const std::vector<Dict> ev = h.filter("transform", slag);
	check(!ev.empty(), "the face slumps forward as a wave");
	MatBody* wv = !ev.empty() ? h.w->get_body(ev_i(ev[0], "body", -1)) : nullptr;
	check(wv != nullptr && is_equal_approx(wv->mass, 15.0) && wv->liquid > 0.9, "15 kg of lava (face 150 + 150 HU)");
	check(wall != nullptr && wall->heat_payload == 0.0, "the face is spent");
	h.until([&]() { return t->health < 100.0; }, 160);
	check(t->health < 100.0, "it reaches the rival");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_melt_pit_melts_what_lands_and_burns_walkers) {
	EU::Duel s = _duel(4.0);
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	const double e0 = EU::e0(h);
	EU::perform(h, a, "sink", 2);
	auto mp = [](const MatBody& b) { return b.alive && b.tag == "melt_pit"; };
	h.until([&]() { return first_where(mp) != nullptr; }, 30);
	BodyRef pit = first_where(mp);
	check(pit != nullptr, "setup: a melt pit");
	if (pit == nullptr) return;
	check(pit->heat_payload > 200.0, S("T2 pit: 120 + 120 HU of heat (", pit->heat_payload, ")"));
	BodyRef stone = EU::keep(EU::shot(h, Mat::Stone, 10.0, pit->pos + V3(0, 0.4, 0), V3(0, -2, 0), t));
	h.step(30);
	check(stone->temp > 200.0, S("a landing stone heats in the pit (", stone->temp, " C)"));
	t->pos = pit->pos;
	h.step(10);
	check(t->status.has("burning"), "a walker burns");
	check(EU::energy_drift(h, e0) < 1e-5, S("booked (", EU::energy_drift(h, e0), ")"));
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_magma_hold_pours_cools_and_holds_insulated) {
	{
		EU::Duel s = _duel(8.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		BodyRef blob = EU::keep(EU::lava(h, 12.0, V3(0.5, 0.2, 2.0)));
		const double e0 = EU::e0(h);
		h.press(a, "tech");
		h.until([&]() { return h.w->held(*a) == blob.get(); }, 40);
		check(h.w->held(*a) == blob.get(), "Magma Hold seizes the lava (no burn)");
		const double liq = blob->liquid;
		h.step(60);
		check(blob->liquid >= liq - 1e-6, "held lava doesn't cool (insulated, upkeep)");
		h.release(a, "tech");
		h.step(2);
		check(blob->form == Form::Wave && blob->attack_owner == a->id, "release: poured as a wave");
		check(!a->has("magma"), "the hold's insulation is only for its duration");
		check(EU::energy_drift(h, e0) < 1e-5, "booked");
	}
	EU::Duel s2 = _duel(8.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	BodyRef blob2 = EU::keep(EU::lava(h, 12.0, V3(0.5, 0.2, 2.0)));
	const double e2 = EU::e0(h);
	h.press(a2, "tech");
	h.until([&]() { return h.w->held(*a2) == blob2.get(); }, 40);
	h.it(a2).attack_pressed = true;
	h.step();
	check(blob2->liquid <= 0.0 && blob2->temp <= Sim::AMBIENT_C + 1e-3, "T+A Cool & Set: solid rock in hand");
	h.release(a2, "tech");
	h.step(2);
	check(blob2->form != Form::Wave && blob2->attack_id != 0, "thrown as a stone");
	check(EU::energy_drift(h, e2) < 1e-5, "the heat went into the ground (removed)");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_reverse_tide_turns_the_rivals_wave_around) {
	// Owner flagship counter: the rival pours lava; Magma Hold on the wave wins the grip contest.
	{
		EU::Duel s = _duel(9.0);
		SimHarness& h = this->h();
		ActorState* a = s.a;
		ActorState* t = s.t;
		BodyRef wave = EU::keep(EU::lava_wave(h, 20.0, V3(0, 0, 0.0), V3(0, 0, 1), t));
		h.press(a, "tech");
		h.until([&]() { return h.has_event("reverse_tide") || a->health < 100.0; }, 40);
		check(h.has_event("reverse_tide"), "Reverse Tide: the contest is won");
		check(wave->attack_owner == a->id && wave->wave_dir.dot(t->pos - wave->pos) > 0.0f, "the wave turns back toward the pourer");
		h.release(a, "tech");
		h.until([&]() { return t->health < 100.0; }, 180);
		check(t->health < 100.0, "the rival's own wave hits them");
		check(a->health == 100.0, "not the caster");
	}
	// A 45 kg wave is too much authority to turn.
	EU::Duel s2 = _duel(9.0);
	SimHarness& h = this->h();
	ActorState* a2 = s2.a;
	ActorState* t2 = s2.t;
	BodyRef big = EU::keep(EU::lava_wave(h, 45.0, V3(0, 0, -0.5), V3(0, 0, 1), t2));
	h.press(a2, "tech");
	h.until([&]() { return h.has_event("control_fail") || h.has_event("reverse_tide"); }, 40);
	check(!h.has_event("reverse_tide") && big->attack_owner == t2->id, "a 45 kg wave wins the contest (evade or wall it)");
	h.release(a2, "tech");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_cinder_step_and_lava_wade) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	const Vec3 p0 = a->pos;
	h.it(a).move = V3(1, 0, 0);
	h.press(a, "evade");
	h.step(25);
	h.it(a).move = Vec3();
	check((a->pos - p0).length() > 3.0f, S("Cinder Step: a 3.5 m dash (", (a->pos - p0).length(), ")"));
	h.step(10);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(30);
	MatBody* pool = EU::lava(h, 12.0, a->pos + V3(0, 0.05, 0));
	pool->form = Form::Zone;
	pool->tag = "lava_pool";
	pool->zone_radius = 1.5;
	pool->radius = 1.5;
	h.step(20);
	check(a->stance == "lava_wade" && Status::immune(*a, "burn"), "Lava Wade: immune to lava burns");
	check(!a->status.has("burning"), "wading in the pool without burning");
	h.it(a).evade_held = false;
	h.step(20);
	check(a->status.has("burning"), "out of the stance, the pool burns");
}

FF_TEST_F(test_kit_earth_magma, MagmaFx, test_magma_column_cells) {
	EU::Duel s = _duel();
	SimHarness& h = this->h();
	ActorState* a = s.a;
	ActorState* t = s.t;
	MatBody* cur = EU::wall(h, Mat::Stone, "obsidian", 100.0, V3(0, 0, 2), a);
	cur->hardness = 0.28;
	cur->heat_payload = 150.0;
	h.w->ledger.generated += 150.0;
	MatBody* stone = EU::shot(h, Mat::Stone, 20.0, V3(0, 1, 0), V3(0, 0, 17), t);
	check(EU::pr(h, stone, cur).outcome == "earth_stick", "stone shot: CAP Magma Curtain (sticks, heats)");
	check(EU::pr(h, stone, "melt_pit", 2, false, a).outcome == "earth_melt_in", "stone shot: Melt Pit T2");
	MatBody* heave = EU::shot(h, Mat::Stone, 45.0, V3(1, 1, 0), V3(0, 0, 14), t);
	const IxResult ph = EU::pr(h, heave, cur);
	check(ph.band == "partial" && ph.outcome == "weaken", S("heavy stone: WKN Curtain (", ph.ratio, ")"));
	MatBody* hot = EU::shot(h, Mat::Stone, 20.0, V3(-1, 1, 0), V3(0, 0, 17), t, "", 1000.0);
	check(Interactions::allows(*hot, "grip_magma"), "hot rock: REC Magma Hold");
	MatBody* blob = EU::lava(h, 20.0, V3(3, 1, 0));
	check(Interactions::allows(*blob, "grip_magma"), "magma blob: REC Magma Hold");
	blob->attack_id = h.w->new_attack_id();
	blob->attack_owner = t->id;
	blob->vel = V3(0, 0, 12);
	check(EU::pr(h, blob, cur).outcome == "earth_absorb_face", "magma blob: ABS Curtain");
	MatBody* wave = EU::lava_wave(h, 20.0, V3(-3, 0, 0), V3(0, 0, 1), t);
	check(Interactions::allows(*wave, "grip_magma"), "lava wave: Reverse Tide (grip_magma)");
	MatBody* disc = EU::shot(h, Mat::Metal, 2.0, V3(4, 1, 0), V3(0, 0, 24), t, "disc");
	check(EU::pr(h, disc, cur).outcome == "earth_stick", "metal: sticks and heats in the face");
	MatBody* sand = EU::shot(h, Mat::Sand, 5.0, V3(-4, 1, 0), V3(0, 0, 22), t, "slug");
	check(EU::pr(h, sand, cur).outcome == "earth_glass_beads", "sand: glass beads");
	MatBody* surge = EU::shot(h, Mat::Sand, 30.0, V3(5, 0, 0), V3(0, 0, 9), t);
	surge->form = Form::Wave;
	check(EU::pr(h, surge, "melt_pit", 0, false, a).outcome == "earth_glaze", "sand surge: glass in the Melt Pit (stalls)");
	MatBody* water = EU::shot(h, Mat::Water, 6.0, V3(-5, 1, 0), V3(0, 0, 16), t);
	check(EU::pr(h, water, cur).outcome == "earth_set", "water: steam, the face sets to obsidian");
	MatBody* ice = EU::shot(h, Mat::Water, 4.0, V3(6, 1, 0), V3(0, 0, 24), t, "", -5.0);
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	check(EU::pr(h, ice, cur).outcome == "earth_face_heat", "ice: XFM steam (x3)");
	check(EU::prv(h, "flame", D({{"H", 8.0}}), cur).outcome == "earth_feed_face", "flame: ABS Curtain (keeps it molten)");
	check(EU::prv(h, "blue_fire", D({{"H", 12.0}}), cur).outcome == "earth_feed_face", "blue fire: ABS (heat into lava)");
	check(EU::prv(h, "lightning", D({{"E", 24.0}}), cur).outcome == "ground", "lightning: GND Curtain (stone-wall rule)");
	check(EU::prv(h, "blast", D({{"P", 34.0}}), cur).outcome == "weaken", "combustion T3: WKN Curtain (splashes)");
	check(EU::prv(h, "gust", D({{"P", 28.0}}), cur).outcome == "block", "gust: BLK Curtain");
	check(EU::prv(h, "sound", D({{"P", 22.0}}), cur).outcome == "block", "sound T2: BLK Curtain");
	check(EU::prv(h, "sound", D({{"P", 40.0}}), cur).outcome == "overwhelm", "strong sound shatters the brittle face");
	MatBody* vine = h.w->spawn_body(Mat::Plant, Form::Chunk, 6.0, V3(7, 1, 0), "test");
	vine->vel = V3(0, 0, 10);
	vine->attack_id = h.w->new_attack_id();
	vine->attack_owner = t->id;
	check(EU::pr(h, vine, cur).outcome == "transform", "vines: burn (ash)");
}
