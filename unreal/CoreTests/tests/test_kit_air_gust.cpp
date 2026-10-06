// Port of game/tests/sim/test_kit_air_gust.gd: Air / Gust (sub 0): the legacy palm gust / cyclone / updraft / dash / Wind
// Guard stay exact at T0-T1, Gale and Hurricane Palm follow the lava rule, every new move runs at T0-T3, the owner's
// "deflect it with wind" example. The animation clip existence check is not ported (clip table: animation stream).
#include "ff_test.h"
#include "kit_air_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Air/AirGust.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>
#include <set>

using namespace ff;
using namespace fft;

namespace {
const char* const GUST_MOVES[] = {"air_attack", "gust_crescent", "gust_dust_line", "gust_crosswind", "gust_wall", "gust_downdraft",
                                  "gust_tailwind", "gust_grip"};

struct AG : AirCase {
	std::pair<ActorState*, ActorState*> _a_vs_r(double dist = 10.0) { return duel(0, Sim::EARTH, 3, dist); }
	struct WaveRun {
		BodyRef wave;
		double liquid0 = 0.0;
		Snap base;
		ActorState* a = nullptr;
	};
	WaveRun _wave_after_gust(int tier, double mass) {
		auto [a, r] = _a_vs_r(14.0);
		(void)r;
		WaveRun o;
		o.a = a;
		run_when(
		    a, "air_attack", tier,
		    [&]() {
			    o.wave = lava_wave(mass, V3(0, 0, a->pos.z - 4.0f));
			    o.liquid0 = o.wave->liquid;
			    o.base = snap(*h().w);
		    },
		    28, 12);
		return o;
	}
	static double flat_speed(Vec3 v) { return static_cast<double>(Vec2(v.x, v.z).length()); }
	std::vector<Dict> fx_where(const std::string& fx, const std::string& mat = "") {
		return h().filter("fx", [&](const Dict& e) { return ev_s(e, "fx") == fx && (mat.empty() || ev_s(e, "mat") == mat); });
	}
};
}  // namespace

// ---------------------------------------------------------------- legacy kit stays exact

FF_TEST_F(test_kit_air_gust, AG, test_legacy_palm_gust_and_cyclone_numbers) {
	auto [a, r] = _a_vs_r(4.0);
	SimHarness* h = &this->h();
	const double f0 = a->focus;
	h->press(a, "attack");
	h->step();
	h->release(a, "attack");
	h->step(40);
	Dict g = h->last_event("gust");
	check(!g.empty() && !ev_b(g, "heavy"), "a tap fires the palm gust");
	near(ev_f(g, "range"), 5.5, 1e-6, "palm gust range 5.5 m");
	check((r->health < 100.0 && r->vel.length() > 0.0f) || r->stun > 0.0 || r->health < 100.0, "the rival at 4 m is hit");
	near(f0 - a->focus, 5.0, 0.8, "palm gust costs 5 Focus");
	const std::vector<Dict> fx = fx_where("cone", "wind");
	check(!fx.empty() && std::fabs(ev_f(fx[0], "angle") - 35.0) < 1e-3, "cone fx 35 deg");
	// the hold: cyclone push (legacy T1) 7 m 45 deg
	std::tie(a, r) = _a_vs_r(4.0);
	h = &this->h();
	h->press(a, "attack");
	h->step(40);
	h->release(a, "attack");
	h->step(30);
	g = h->last_event("gust");
	check(!g.empty() && ev_b(g, "heavy") && ev_i(g, "tier") == 1, "a 0.6 s hold fires the cyclone push at T1");
	near(ev_f(g, "range"), 7.0, 1e-6, "cyclone range 7 m");
}

FF_TEST_F(test_kit_air_gust, AG, test_gust_tiers_have_the_designed_ranges_cones_and_powers) {
	const double want[4][3] = {{5.5, 35.0, 7.0}, {7.0, 45.0, 11.0}, {8.0, 50.0, 18.0}, {10.0, 60.0, 28.0}};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _a_vs_r(14.0);
		(void)r;
		SimHarness& h = this->h();
		run_move(a, "air_attack", tier, tier == 0 ? 1 : 30, 70);
		const Dict g = h.last_event("gust");
		check(!g.empty(), S("T", tier, " fires"));
		near(ev_f(g, "range"), want[tier][0], 1e-6, S("T", tier, " range"));
		const std::vector<Dict> fx = fx_where("cone");
		check(!fx.empty(), S("T", tier, " cone fx"));
		if (!fx.empty()) {
			near(ev_f(fx[0], "angle"), want[tier][1], 1e-3, S("T", tier, " cone angle"));
			near(ev_f(fx[0], "power"), want[tier][2], 1e-3, S("T", tier, " pressure"));
		}
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
		fx_catalogued(S("gust T", tier));
	}
}

FF_TEST_F(test_kit_air_gust, AG, test_gust_hold_tier_ladder_from_real_input) {
	// The real hold clock: 0.4 s T1, 1.0 s T2, 1.8 s T3 (Charge), drain from T1 on only for T2+.
	auto [a, r] = _a_vs_r(14.0);
	(void)r;
	SimHarness& h = this->h();
	h.press(a, "attack");
	std::set<int> tiers;
	for (int k = 0; k < 150; ++k) {
		h.step();
		if (a->action != nullptr && a->action->id == "air_attack") tiers.insert(a->action->tier());
	}
	check(tiers.count(1) && tiers.count(2) && tiers.count(3), "held 2.5 s reaches T3");
	check(h.events("charge").size() == 3, S("one charge event per tier (", h.events("charge").size(), ")"));
	h.release(a, "attack");
	h.step(40);
	const Dict g = h.last_event("gust");
	check(!g.empty() && ev_i(g, "tier") == 3 && std::fabs(ev_f(g, "range") - 10.0) < 1e-6, "released at T3: Hurricane Palm");
}

FF_TEST_F(test_kit_air_gust, AG, test_gust_costs_per_tier) {
	const double costs[4] = {5.0, 12.0, 18.0, 24.0};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _a_vs_r(14.0);
		(void)r;
		SimHarness& h = this->h();
		const double f0 = a->focus;
		double fmin = f0;
		h.w->start_action(*a, "air_attack", h.it(a), D({{"slot", "strike"}, {"tier", tier}, {"charge_frozen", true}}));
		h.it(a).attack_held = tier > 0;
		for (int k = 0; k < 30; ++k) {
			h.step();
			fmin = minf(fmin, a->focus);
		}
		h.it(a).attack_held = false;
		for (int k = 0; k < 50; ++k) {
			h.step();
			fmin = minf(fmin, a->focus);
		}
		check(h.has_event("gust"), S("T", tier, " fired"));
		near(f0 - fmin, costs[tier], 1.0, S("T", tier, " total cost"));
	}
}

FF_TEST_F(test_kit_air_gust, AG, test_gust_without_focus_falls_back_a_tier_never_negative) {
	auto [a, r] = _a_vs_r(14.0);
	(void)r;
	SimHarness& h = this->h();
	a->focus = 14.0;   // pays the 5 start + 7 heavy, not the +12 hurricane
	run_move(a, "air_attack", 3, 30, 60);
	const Dict g = h.last_event("gust");
	check(!g.empty() && ev_i(g, "tier") < 3, "short of Focus the hurricane fires a tier lower (T" + ev_s(g, "tier") + ")");
	check(a->focus >= 0.0, "Focus never negative");
}

// ---------------------------------------------------------------- the lava rule

FF_TEST_F(test_kit_air_gust, AG, test_you_can_not_block_lava_with_a_simple_air_attack) {
	// 20 kg lava wave: TP 27.3. Palm Gust T0 (7) and Cyclone T1 (11) fail, Gale T2 (18) crusts and slows, Hurricane Palm T3 (28) sets it.
	for (int tier = 0; tier < 4; ++tier) {
		WaveRun o = _wave_after_gust(tier, 20.0);
		SimHarness& h = this->h();
		MatBody& wave = *o.wave;
		if (tier <= 1) {
			check(wave.form == Form::Wave && wave.liquid >= o.liquid0 - 0.08, S("T", tier, " can't touch the lava (liquid ", wave.liquid, ")"));
		} else if (tier == 2) {
			check(wave.form == Form::Wave && wave.liquid < o.liquid0 - 0.2 && wave.liquid > 0.0,
			      S("T2 Gale crusts it: liquid ", o.liquid0, " -> ", wave.liquid, ", still a wave"));
			check(Thermal::flow_factor(wave) < 1.0, S("and slows it (flow ", Thermal::flow_factor(wave), ")"));
		} else {
			check(wave.form != Form::Wave && wave.liquid <= 0.0, S("T3 Hurricane Palm stalls the wave and sets it into rock (form ", Sim::form_name(wave.form), ")"));
		}
		const int ix = h.count_events("interaction", [](const Dict& e) { return ev_s(e, "threat") == "lava_wave" && ev_s(e, "counter") == "gust"; });
		if (tier >= 2) check(ix >= 1, S("T", tier, ": an interaction event"));
		// energy: every HU the wind took is booked (ambient)
		ledgers_ok(o.base, S("lava T", tier), 1e-4);
	}
}

FF_TEST_F(test_kit_air_gust, AG, test_a_45_kg_lava_wave_survives_a_hurricane) {
	WaveRun o = _wave_after_gust(3, 45.0);
	MatBody& wave = *o.wave;
	check(wave.alive && wave.form == Form::Wave && wave.liquid > 0.5, S("a 45 kg wave (TP 61.5) is still flowing after Hurricane Palm (liquid ", wave.liquid, ")"));
	ledgers_ok(o.base, "45 kg", 1e-4);
}

// ---------------------------------------------------------------- deflect it with wind (owner example)

FF_TEST_F(test_kit_air_gust, AG, test_wind_guard_deflects_a_20_kg_stone_and_a_perfect_one_returns_it) {
	auto [a, r] = _a_vs_r(12.0);
	SimHarness& h = this->h();
	h.press(a, "guard");
	h.step(30);
	const BodyRef s1 = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r));
	h.until([&]() { return h.has_event("deflect") || h.has_event("hit") || h.has_event("block"); }, 60);
	check(h.has_event("deflect", "actor", Value(a->id)) && a->health == 100.0, "Wind Guard (12 x 1.5 = 18 >= 17) deflects the shot");
	check(s1->attack_id == 0, "the deflected stone is spent");
	h.release(a, "guard");
	h.step(40);
	h.log.clear();
	h.press(a, "guard");
	h.step(2);
	const BodyRef s2 = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 2.5));
	h.until([&]() { return h.has_event("perfect_deflect") || h.has_event("hit"); }, 40);
	check(h.any_event("perfect_deflect", [](const Dict& e) { return dstr(e, "verb", "") == "reflect"; }),
	      "a perfect guard (x1.5 = 27) is Return Wind: back to the sender");
	check(s2->attack_owner == a->id && s2->vel.dot(r->pos - s2->pos) > 0.0f, "now A's stone, flying at R");
}

FF_TEST_F(test_kit_air_gust, AG, test_palm_gust_t0_only_bends_a_stone_a_cyclone_turns_it) {
	// The legacy gust cell follows the counter rule (MOVESET §5.4): Palm Gust 7 x2 = 14 vs a 20 kg stone at 17 m/s
	// (TP 17, ratio 0.82) only bends it; Cyclone 11 x2 = 22 (ratio 1.29) turns it back along the push.
	auto [a0, r0] = _a_vs_r(12.0);
	(void)r0;
	{
		SimHarness& h = this->h();
		AgentRef th = threat(*h.w, "stone", 17.0, 20.0, "K");
		const IxResult p0 = Interactions::predict(h.w, *th, *Agent::of_move(h.w, a0, "air_attack", 0, false));
		check(p0.outcome == "bend", S("palm gust T0 vs TP 17: bend (", p0.outcome, " r", p0.ratio, ")"));
		const IxResult p1 = Interactions::predict(h.w, *th, *Agent::of_move(h.w, a0, "air_attack", 1, false));
		check(p1.outcome == "redirect", S("cyclone T1 vs TP 17: redirect (", p1.outcome, " r", p1.ratio, ")"));
		AgentRef fast = threat(*h.w, "stone", 43.4, 29.0, "K");
		const IxResult pf = Interactions::predict(h.w, *fast, *Agent::of_move(h.w, a0, "air_attack", 0, false));
		check(pf.outcome == "pass", S("a 29 kg stone at 30 m/s (TP 43) ignores a palm gust (", pf.outcome, " r", pf.ratio, ")"));
	}
	// In play: the cyclone push turns the stone and re-owns it.
	auto [a, r] = _a_vs_r(12.0);
	SimHarness& h = this->h();
	BodyRef stone;
	run_when(a, "air_attack", 1, [&]() { stone = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 4.0)); }, 28, 30);
	const std::vector<Dict> ix = h.events("deflect");
	check(!ix.empty() && dstr(ix[0], "verb", "") == "gust", "the cyclone turned it");
	check(stone->alive && stone->attack_owner == a->id, "turned and re-owned, not stopped");
	check(a->health == 100.0, "it never reached A");
}

FF_TEST_F(test_kit_air_gust, AG, test_gale_and_hurricane_deflect_a_stone_heavy_stones_only_bend) {
	for (int tier : {2, 3}) {
		{
			auto [a, r] = _a_vs_r(12.0);
			SimHarness& h = this->h();
			BodyRef stone;
			run_when(a, "air_attack", tier, [&]() { stone = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 3.0)); }, 28, 12);
			check(h.has_event("deflect") && stone->attack_id == 0, S("T", tier, " deflects a 20 kg stone (x1.5)"));
		}
		auto [a2, r2] = _a_vs_r(12.0);
		SimHarness& h = this->h();
		BodyRef heavy;
		Vec3 v;
		run_when(
		    a2, "air_attack", tier,
		    [&]() {
			    heavy = keep(h.launch_at(a2, "stone", 45.0, 14.0, Sim::AMBIENT_C, "", r2, 5.5));
			    v = heavy->vel;
		    },
		    28, 6);
		check(h.has_event("bend"), S("T", tier, ": a 45 kg stone (31.5) only bends (x0.6)"));
		check(heavy->alive && heavy->attack_id != 0 && heavy->vel.length() > v.length() * 0.6f, "it keeps coming");
	}
}

FF_TEST_F(test_kit_air_gust, AG, test_a_gale_puts_out_a_flame_a_palm_gust_only_turns_it) {
	// fire bands (flame x gust) at every tier: < 1 fan, 1-2 blow aside, >= 2 extinguish.
	auto [a, r] = _a_vs_r(12.0);
	(void)r;
	SimHarness& h = this->h();
	AgentRef th = threat(*h.w, "flame", 8.0, 0.0, "H");
	AgentRef cp = Agent::of_move(h.w, a, "air_attack", 2, false);
	IxResult pr = Interactions::predict(h.w, *th, *cp);
	check(pr.outcome == "extinguish", S("Gale (18) vs a blaze (8): ratio ", pr.ratio, " -> ", pr.outcome));
	pr = Interactions::predict(h.w, *threat(*h.w, "flame", 30.0, 0.0, "H"), *cp);
	check(pr.outcome == "amplify", "against 30 PU of fire the wind only fans it (" + pr.outcome + ")");
	pr = Interactions::predict(h.w, *threat(*h.w, "flame", 12.0, 0.0, "H"), *cp);
	check(pr.outcome == "deflect", "ratio 1.5: blown aside (" + pr.outcome + ")");
	AgentRef t0 = Agent::of_move(h.w, a, "air_attack", 0, false);
	// The fire bands apply at every tier (weak wind feeds fire): a palm gust (7) fans a Sunfall-size fireball (19).
	check(!dbool(Interactions::rule("flame", "gust", 0), "legacy", false), "T0 flame x gust is the kit's fire band too");
	check(!dbool(Interactions::rule("flame", "gust", 2), "legacy", false), "T2 is the kit's");
	check(t0->power == 7.0, "palm gust CP 7");
	pr = Interactions::predict(h.w, *threat(*h.w, "flame", 19.0, 0.0, "H"), *t0);
	check(pr.outcome == "amplify", S("palm gust vs a 19 PU fireball: fanned (", pr.outcome, " r", pr.ratio, ")"));
	pr = Interactions::predict(h.w, *threat(*h.w, "flame", 3.0, 0.0, "H"), *t0);
	check(pr.outcome == "extinguish", "palm gust vs a flare (3): put out (" + pr.outcome + ")");
}

// ---------------------------------------------------------------- the new moves, T0-T3

FF_TEST(test_kit_air_gust, test_every_gust_move_has_a_full_def_and_a_binding) {
	Moves::ensure_ready();
	for (const char* idc : GUST_MOVES) {
		const std::string id = idc;
		check(Moves::defs().has(id), id + " registered");
		if (!Moves::defs().has(id)) continue;
		const Dict d = Moves::defs().get(id).as_dict();
		for (const char* k : {"name", "desc", "slot", "sub", "element", "anim", "fx", "ai"})
			check(d.has(k) || (id == "air_attack" && std::string(k) == "anim") || id == "air_tech", id + " has " + k);
		if (id == "air_attack") {
			for (const char* k : {"tiers", "counter", "threat"}) check(d.has(k), id + " has " + k);
			continue;
		}
		check(dint(d, "sub") == 0 && dint(d, "element") == 3, id + " is Air/Gust");
		check(d.has("startup") && d.has("recovery") && d.has("cost"), id + " has frames and a cost");
		check(d.has("tiers") || in_list(id, {"gust_wall", "gust_downdraft", "gust_tailwind", "gust_grip"}), id + " has tier data");
		check(d.has("counter") || d.has("threat") || id == "gust_tailwind", id + " has counter / threat metadata");
	}
	for (const char* slot : Sim::SLOTS) check(!Moves::resolve(3, 0, slot).empty(), std::string("Air/Gust slot ") + slot + " bound");
	check(Moves::resolve(3, 0, "strike") == "air_attack" && Moves::resolve(3, 0, "tech") == "air_tech" && Moves::resolve(3, 0, "evade") == "air_dash",
	      "the legacy strike, technique and evade keep their ids");
	check(Moves::resolve(3, 0, "guard") == "guard", "the Wind Guard keeps the id guard");
	check(Moves::resolve(3, 0, "thrust") == "gust_crescent" && Moves::resolve(3, 0, "ground") == "gust_dust_line", "thrust / ground bound");
}

FF_TEST_F(test_kit_air_gust, AG, test_wind_crescent_t0_to_t3) {
	const int counts[4] = {1, 2, 1, 1};
	const double powers[4] = {8.0, 11.0, 15.0, 22.0};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _a_vs_r(14.0);
		(void)r;
		SimHarness& h = this->h();
		run_move(a, "gust_crescent", tier, 1, 13);
		const std::vector<BodyRef> cr = bodies_tagged("crescent");
		check(static_cast<int>(cr.size()) == counts[tier], S("T", tier, ": ", counts[tier], " crescent(s) (", cr.size(), ")"));
		for (const BodyRef& b : cr) {
			check(b->mat == Mat::Air && b->tier == tier && std::fabs(b->power - powers[tier]) < 1e-6,
			      S("T", tier, ": AIR body, tier ", b->tier, ", P ", powers[tier], " (", b->power, ")"));
			check(b->attack_owner == a->id && b->attack_id != 0, "armed with its own attack id");
			near(b->vel.length(), 22.0, 0.6, "speed 22 m/s");
			check(b->radius >= (tier >= 2 ? 1.0 : 0.5) - 1e-6, S("T", tier, " width radius ", b->radius));
		}
		if (tier == 3 && !cr.empty()) check(dint(cr[0]->props, "pierce", 0) == 2, "the Wind Scythe pierces 2 targets");
		if (tier == 1 && cr.size() == 2) check(cr[0]->vel.normalized().dot(cr[1]->vel.normalized()) < 0.9999f, "the pair crosses (different headings)");
		h.step(80);
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
		fx_catalogued(S("crescent T", tier));
	}
}

FF_TEST_F(test_kit_air_gust, AG, test_a_crescent_deflects_a_light_shot_and_cuts_vines) {
	{
		auto [a, r] = _a_vs_r(14.0);
		SimHarness& h = this->h();
		const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 8.0));   // meets the crescent mid-way
		run_move(a, "gust_crescent", 2, 1, 0);
		h.until([&]() { return h.has_event("interaction"); }, 90);
		const std::vector<Dict> ix = h.filter("interaction", [](const Dict& e) { return ev_s(e, "counter") == "crescent"; });
		check(!ix.empty(), "the crescent met the stone through the rules");
		check(!ix.empty() && in_list(ev_s(ix[0], "outcome"), {"deflect", "bend"}), "outcome " + (!ix.empty() ? ev_s(ix[0], "outcome") : std::string("-")));
		check(stone->attack_id == 0 || stone->vel.dot(V3(0, 0, 1)) < 17.0f * 0.9f, "the stone is no longer a clean hit on A");
	}
	// vines
	auto [a2, r2] = _a_vs_r(14.0);
	(void)r2;
	SimHarness& h = this->h();
	MatBody* vine = h.w->spawn_body(Mat::Plant, Form::Chunk, 10.0, V3(0, 1.2, a2->pos.z - 5.0f), "test");
	h.w->mass_ledger.plant_from_ground += 10.0;
	vine->static_body = false;
	vine->gravity_scale = 0.0;
	const Snap base = snap(*h.w);
	run_move(a2, "gust_crescent", 0, 1, 0);
	h.until([&]() { return h.has_event("transform", "to", Value("cut")); }, 90);
	check(h.has_event("transform", "to", Value("cut")), "the crescent cut the vine");
	h.step(5);
	ledgers_ok(base, "vine cut", 1e-5);
}

// ---------------------------------------------------------------- dust line, crosswind, wall of wind, downdraft

FF_TEST_F(test_kit_air_gust, AG, test_dust_devil_line_is_a_tripping_wind_wave) {
	const double widths[4] = {1.6, 1.9, 2.4, 3.0};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _a_vs_r(12.0);
		SimHarness& h = this->h();
		run_move(a, "gust_dust_line", tier, 1, 14);
		const std::vector<BodyRef> ws = bodies_tagged("dust_line");
		check(ws.size() == 1 && ws[0]->form == Form::Wave && ws[0]->mat == Mat::Air, S("T", tier, ": one AIR wave"));
		if (ws.size() == 1) {
			near(ws[0]->wave_width, widths[tier], 1e-6, S("T", tier, " width"));
			near(dnum(ws[0]->props, "speed"), 14.0, 1e-6, "14 m/s");
		}
		h.step(120);
		if (tier == 0) {
			check(r->health < 100.0 || r->balance < 100.0, S("the line reaches the rival: trips (balance ", r->balance, ")"));
			check(Status::has(*r, "blinded") || h.has_event("status", "status", Value("blinded")), "and raises dust (light blind)");
		}
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
	}
	fx_catalogued("dust line");
}

FF_TEST_F(test_kit_air_gust, AG, test_crosswind_curves_a_projectile_in_flight) {
	auto [a, r] = _a_vs_r(12.0);
	SimHarness& h = this->h();
	const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 3.5));
	const Vec3 dir0 = stone->vel.normalized();
	run_move(a, "gust_crosswind", 1, 1, 0);
	h.until([&]() { return h.has_event("bend") || h.has_event("interaction"); }, 40);
	const double ang = rad_to_deg(angle_to(dir0, stone->vel.normalized()));
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "counter") == "crosswind"; }), "met through the rules as crosswind");
	check(ang > 3.0 && ang <= 40.0 + 1e-3, S("curved by ", ang, " deg (<= 40)"));
	check(stone->alive && stone->attack_id != 0, "still flying");
}

FF_TEST_F(test_kit_air_gust, AG, test_wall_of_wind_and_downdraft_from_the_guard) {
	auto [a, r] = _a_vs_r(10.0);
	SimHarness* h = &this->h();
	h->press(a, "guard");
	h->step(20);
	h->flick(a, "guard", static_cast<int>(Gesture::Up));
	h->step(3);
	check(a->action != nullptr && a->action->id == "gust_wall", "guard flick up = Wall of Wind");
	h->step(14);
	const std::vector<BodyRef> ws = bodies_tagged("wind_wall");
	check(ws.size() == 1 && ws[0]->form == Form::Wave, "a moving wall (wave tag wind_wall)");
	if (ws.size() == 1)
		check(ws[0]->wave_width == 3.0 && std::fabs(dnum(ws[0]->props, "speed") - 8.0) < 1e-6 && std::fabs(ws[0]->power - 14.0) < 1e-6, "3 m wide, 8 m/s, P 14");
	h->release(a, "guard");
	h->step(80);
	check(a->action == nullptr, "the wall move ends");
	// downdraft
	std::tie(a, r) = _a_vs_r(10.0);
	h = &this->h();
	h->press(a, "guard");
	h->step(20);
	h->flick(a, "guard", static_cast<int>(Gesture::Down));
	h->step(3);
	check(a->action != nullptr && a->action->id == "gust_downdraft", "guard flick down = Downdraft");
	h->release(a, "guard");
	h->step(60);
	check(a->action == nullptr, "Downdraft ends");
	fx_catalogued("wall / downdraft");
}

FF_TEST_F(test_kit_air_gust, AG, test_downdraft_slams_an_airborne_enemy) {
	auto [a, r] = _a_vs_r(3.0);
	SimHarness& h = this->h();
	r->is_dummy = false;
	r->pos.y = 2.0f;
	r->grounded = false;
	r->vel.y = 3.0f;
	const double b0 = r->balance;
	run_move(a, "gust_downdraft", 0, 1, 0);
	h.step(12);
	check(r->vel.y < -5.0f || r->pos.y < 1.0f, S("the airborne enemy is driven down (vy ", r->vel.y, ")"));
	check(r->balance < b0, "and loses balance");
}

FF_TEST_F(test_kit_air_gust, AG, test_tailwind_runs_faster_while_held) {
	auto [a, r] = _a_vs_r(14.0);
	(void)r;
	SimHarness& h = this->h();
	h.it(a).move = V3(0, 0, -1);
	h.step(40);
	const double v0 = flat_speed(a->vel);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(16);
	check(a->action != nullptr && a->action->id == "gust_tailwind" && a->action->slot == "evade_hold", "held 0.2 s: Tailwind");
	h.step(40);
	const double v1 = flat_speed(a->vel);
	check(v1 > v0 * 1.1, S("x1.3 run speed (", v0, " -> ", v1, ")"));
	h.it(a).evade_held = false;
	h.it(a).move = Vec3();
	h.step(30);
	check(a->action == nullptr || a->action->phase == ActionPhase::Recovery, "released: the wind drops");
}

// ---------------------------------------------------------------- Wind Grip (context technique)

FF_TEST_F(test_kit_air_gust, AG, test_updraft_stays_the_legacy_updraft_with_nothing_to_grip) {
	auto [a, r] = _a_vs_r(14.0);
	(void)r;
	SimHarness& h = this->h();
	h.press(a, "tech");
	h.step(30);
	check(a->action != nullptr && a->action->id == "air_tech" && h.has_event("updraft"), "no light body: the legacy updraft");
	check(!a->grounded || a->pos.y > 0.1f, "lifted");
	const Dict pv = h.w->tech_preview(*a, V3(0, 0, -1));
	check(dstr(pv, "mode", "") != "WIND GRIP", "preview: " + dstr(pv, "mode", ""));
}

FF_TEST_F(test_kit_air_gust, AG, test_wind_grip_takes_a_light_stone_and_flings_it) {
	auto [a, r] = _a_vs_r(14.0);
	SimHarness& h = this->h();
	const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 12.0, Sim::AMBIENT_C, "", r, 7.0));
	const Dict pv = h.w->tech_preview(*a, V3(0, 0, -1));
	check(dstr(pv, "mode", "") == "WIND GRIP" && dint(pv, "body", -1) == stone->id, "preview: WIND GRIP on the stone");
	h.press(a, "tech");
	h.step(1);
	check(a->action != nullptr && a->action->id == "gust_grip" && h.has_event("morph"), "the technique morphs into Wind Grip");
	ActorState* ap = a;
	h.until([&]() { return h.w->held(*ap) != nullptr; }, 40);
	check(h.w->held(*a) == stone.get(), "the light stone is gripped");
	h.aim(a, V3(0, 0, -1));
	h.release(a, "tech");
	h.step(3);
	const int sid = stone->id;
	check(h.any_event("launch", [&](const Dict& e) { return ev_i(e, "body", -1) == sid && ev_i(e, "actor", -1) == ap->id; }), "released: flung as A's attack");
	h.step(40);
	MatBody* heavy = h.launch_at(a, "stone", 45.0, 3.0, Sim::AMBIENT_C, "", r, 6.0);
	check(AirGust::grip_target(*h.w, *a, V3(0, 0, -1)) != heavy, "a 45 kg stone is not a Wind Grip target");
}

FF_TEST_F(test_kit_air_gust, AG, test_a_gripped_fireball_grows_20_percent) {
	auto [a, r] = _a_vs_r(14.0);
	SimHarness& h = this->h();
	const BodyRef ball = keep(h.w->spawn_body(Mat::Fire, Form::Chunk, 0.5, V3(0, 1.2, a->pos.z - 5.0f), "test"));
	ball->heat_payload = 100.0;
	ball->tag = "fireball";
	ball->attack_id = h.w->new_attack_id();
	ball->attack_owner = r->id;
	ball->vel = V3(0, 0, 8.0);
	ball->gravity_scale = 0.0;
	ball->hit_set.add(r->id);
	h.w->ledger.generated += 100.0;
	const Snap base = snap(*h.w);
	h.press(a, "tech");
	ActorState* ap = a;
	h.until([&]() { return h.w->held(*ap) != nullptr; }, 40);
	check(h.w->held(*a) == ball.get(), "the fireball is gripped (light, <= 30 kg)");
	h.step(4);
	check(ball->heat_payload > 100.0 * 1.15 || dbool(ball->props, "fed", false), S("fed +20 % (", ball->heat_payload, " HU)"));
	h.release(a, "tech");
	h.step(20);
	ledgers_ok(base, "fed fireball", 1e-4);
}

// ---------------------------------------------------------------- determinism

FF_TEST_F(test_kit_air_gust, AG, test_gust_kit_is_deterministic) {
	std::vector<std::string> hashes;
	for (int k = 0; k < 2; ++k) {
		auto [a, r] = _a_vs_r(8.0);
		SimHarness& h = this->h();
		h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 6.0);
		run_move(a, "gust_crescent", 1, 1, 20);
		run_move(a, "air_attack", 2, 30, 40);
		run_move(a, "gust_crosswind", 2, 1, 40);
		hashes.push_back(S(a->pos.x, ",", a->pos.y, ",", a->pos.z, "|", r->pos.x, ",", r->pos.z, "|", h.w->bodies.size(), "|", ftos(a->focus, 4), "|",
		                   ftos(r->health, 4)));
	}
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: " + hashes[0] + " vs " + hashes[1]);
}
