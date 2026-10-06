// Port of game/tests/sim/test_review_fixes.gd: regressions for the review fixes (docs/REVIEW.md "Round 3"): counter
// strength vs threat power, lightning vs held water / wind, once-per-contact partials, softer hits after partial counters,
// clean blocks, ledger leaks, NaN merges, sound vs held guards, the grounded stance and the Lab's push / sink tiers.
#include "ff_test.h"
#include "sim_harness.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Lab/LabScript.h"
#include "Sim/Charge.h"
#include "Sim/Conduction.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}

struct RF : HarnessCase {
	bool _all_finite() {
		for (const BodyRef& b : h().w->bodies)
			if (b->alive && (!finite3(b->pos) || !finite3(b->vel) || std::isnan(b->liquid) || std::isnan(b->temp))) return false;
		return true;
	}
	static AgentRef _guard_agent(const std::string& ccls, double power, bool perfect) {
		AgentRef g = std::make_shared<Agent>();
		g->kind = "move";
		g->ccls = ccls;
		g->cls = ccls;
		g->power = power;
		g->perfect = perfect;
		return g;
	}
	static AgentRef _threat(const std::string& cls, const std::string& ch, double tp, double mass) {
		AgentRef g = std::make_shared<Agent>();
		g->kind = "volume";
		g->cls = cls;
		g->ccls = cls;
		g->mass = mass;
		g->hostile = true;
		g->ch.set(ch, tp);
		return g;
	}
	struct GuardRun {
		double drift = 0.0;
		bool staggered = false, stopped = false, finite = true;
		ActorState* d = nullptr;
	};
	GuardRun _guard_vs_tap(int de, int ds, int ae, int asub, double dist, int gesture) {
		SimHarness& hh = H(5);
		ActorState* d = hh.actor("D", V3(0, 0, dist * 0.5), 0, Dict(), de);
		ActorState* a = hh.actor("A", V3(0, 0, -dist * 0.5), 1, Dict(), ae);
		d->subs[static_cast<size_t>(de)] = ds;
		a->subs[static_cast<size_t>(ae)] = asub;
		hh.step(20);
		a->heat_reserve = 200.0;
		hh.aim(a, d->pos - a->pos);
		hh.aim(d, a->pos - d->pos);
		hh.press(d, "guard");
		hh.step(12);
		const double e0 = hh.w->system_energy() - hh.w->ledger_balance();
		if (gesture == 0) hh.press(a, "attack");
		else hh.flick(a, "attack", gesture);
		hh.step(1);
		hh.release(a, "attack");
		GuardRun r;
		for (int k = 0; k < 70; ++k) {
			hh.step(1);
			r.finite = r.finite && _all_finite();
		}
		for (const Dict& e : hh.log) {
			if (ev_i(e, "tick") <= 32) continue;
			const std::string ty = ev_s(e, "type");
			if (ty == "stagger" && ev_i(e, "actor", -1) == d->id) r.staggered = true;
			if (ty == "interaction" && ev_i(e, "counter_actor", -1) == d->id && in_list(ev_s(e, "outcome"), {"absorb", "extinguish", "neutralize"})) r.stopped = true;
		}
		r.drift = hh.w->system_energy() - hh.w->ledger_balance() - e0;
		r.d = d;
		return r;
	}
};
}  // namespace

// ---------------------------------------------------------------- counter strength scales with the threat

FF_TEST_F(test_review_fixes, RF, test_plain_guards_scale_with_the_threat_and_perfect_needs_a_holdable_threat) {
	SimHarness& h = H(3);
	AgentRef boulder = _threat("boulder", "K", 159.0, 200.0);
	const std::pair<const char*, double> guards[] = {{"guard_earth", 10.0}, {"guard", 10.0},       {"aura_flame", 10.0}, {"aura_blue", 16.0},
	                                                  {"ward_static", 12.0}, {"guard_blast", 18.0}, {"guard_wind", 12.0}};
	for (const auto& c : guards) {
		for (bool perfect : {false, true}) {
			const IxResult pr = Interactions::predict(h.w, *boulder, *_guard_agent(c.first, c.second, perfect));
			check(pr.outcome == "overwhelm", S("200 kg boulder vs ", c.first, perfect ? " (perfect)" : "", ": overwhelmed (", pr.outcome, " r", pr.ratio, ")"));
		}
	}
	const IxResult ps = Interactions::predict(h.w, *_threat("stone", "K", 17.0, 20.0), *_guard_agent("guard", 10.0, true));
	check(ps.outcome == "deflect", "perfect plain guard deflects a 20 kg stone (" + ps.outcome + ")");
	const IxResult ph = Interactions::predict(h.w, *_threat("stone_heavy", "K", 31.5, 45.0), *_guard_agent("guard", 10.0, true));
	check(ph.outcome == "block", S("a 45 kg heave (TP 31.5) is only blocked, even perfect (", ph.outcome, " r", ph.ratio, ")"));
	// Molten: the wind wrap and the fire / static / blast guards are no wall against lava (the owner's rule).
	AgentRef lava = _threat("lava_wave", "H", 27.3, 20.0);
	const std::pair<const char*, double> molten[] = {{"guard_wind", 12.0}, {"ward_static", 12.0}, {"aura_flame", 10.0}, {"guard_blast", 18.0}};
	for (const auto& c : molten) {
		const IxResult pl = Interactions::predict(h.w, *lava, *_guard_agent(c.first, c.second, false));
		check(pl.outcome == "overwhelm", S("lava wave vs held ", c.first, ": overwhelmed (", pl.outcome, " r", pl.ratio, ")"));
	}
}

FF_TEST_F(test_review_fixes, RF, test_a_heavy_stone_chips_harder_through_a_plain_guard) {
	SimHarness& h = H(3);
	ActorState* d = h.actor("D", V3(0, 0, 4), 0, Dict(), Sim::FIRE);   // Flame Guard: a plain CP 10 guard
	h.step(5);
	std::vector<double> hits;
	for (const auto& m : {std::pair<double, double>{20.0, 15.0}, std::pair<double, double>{45.0, 14.0}}) {
		d->health = 100.0;
		d->balance = 100.0;
		h.press(d, "guard");
		h.step(20);
		const BodyRef b = h.launch_at(d, "stone", m.first, m.second, Sim::AMBIENT_C, "", nullptr, 6.0)->shared_from_this();
		const double dmg = 12.0 * std::sqrt(m.first / 20.0);
		b->damage = dmg;
		h.until([&]() { return !b->alive || b->attack_id == 0; }, 60);
		hits.push_back((100.0 - d->health) / dmg);
		h.release(d, "guard");
		h.step(40);
	}
	near(hits[0], 0.12, 0.011, "a 20 kg stone: the legacy 12 % chip");
	check(hits[1] > 0.16, S("a 45 kg heave chips harder through the same guard (", hits[1], " of its damage)"));
}

// ---------------------------------------------------------------- lightning

FF_TEST_F(test_review_fixes, RF, test_lightning_conducts_through_a_water_shield_and_ignores_a_wind_guard) {
	SimHarness& h = H(3);
	AgentRef bolt = _threat("lightning", "E", 36.0, 0.0);
	const IxResult sh = Interactions::predict(h.w, *bolt, *_guard_agent("shield_water", 6.0, false));
	check(sh.outcome == "conduct" && std::fabs(dnum(sh.rule, "factor", 0.0) - 1.5) < 1e-6, "water shield conducts x1.5 (" + sh.outcome + ")");
	const IxResult wg = Interactions::predict(h.w, *bolt, *_guard_agent("guard_wind", 12.0, true));
	check(wg.outcome == "pass", "wind guard: the bolt passes (" + wg.outcome + ")");
	const IxResult pg = Interactions::predict(h.w, *_threat("lightning", "E", 24.0, 0.0), *_guard_agent("guard_earth", 10.0, false));
	check(pg.outcome == "weaken", "a held plain guard only takes its own power off a bolt (" + pg.outcome + ")");
}

FF_TEST_F(test_review_fixes, RF, test_the_grounded_stance_replaces_the_guard_chip) {
	SimHarness& h = H(3);
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, -4), 1, Dict(), Sim::FIRE);
	h.step(5);
	h.aim(a, t->pos - a->pos);
	h.press(a, "guard");
	h.step(20);
	check(a->surface == "stone" && a->grounded, "the Earth fighter guards on stone");
	t->lock_target = a->id;
	const size_t n0 = h.events("block").size();
	const int aid = h.w->new_attack_id();
	Conduction::discharge(*h.w, *t, a->chest(), D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}}), aid, true);
	near(100.0 - a->health, 24.0 * 0.4, 1e-6, "the grounded stance takes 40 % (no extra guard chip on top)");
	check(h.events("block").size() == n0, "no plain-guard block on top of the stance");
}

// ---------------------------------------------------------------- partials: once per contact, softer hits, heat caps

FF_TEST_F(test_review_fixes, RF, test_a_small_fog_does_not_stop_an_80_kg_lava_wave) {
	SimHarness& h = H(3);
	ActorState* o = h.actor("O", V3(10, 0, 10), 0, Dict(), Sim::WATER);
	h.step(2);
	WaterUtil::zone(*h.w, "fog", V3(0, 0, -1.0), 3.0, o->id, 30.0, D({{"height", 3.0}, {"rate", 0.1}}), Mat::Steam, 2.0, 6.0);
	o->water_carried -= 2.0;
	const BodyRef b = h.w->spawn_body(Mat::Stone, Form::Wave, 80.0, V3(0, 0, -6.0), "test")->shared_from_this();
	h.w->mass_ledger.ground_taken += 80.0;
	h.w->ledger.generated += Thermal::heat(*b, 80.0 * (Sim::STONE_C * 980.0 + Sim::STONE_LATENT));
	Thermal::update_phase(*b);
	b->wave_dir = V3(0, 0, 1);
	b->wave_budget = 14.0;
	b->vel = V3(0, 0, 7.5);
	b->attack_id = h.w->new_attack_id();
	const double e0 = h.w->system_energy() - h.w->ledger_balance();
	h.step(75);
	check(b->alive && b->pos.z > 2.5f, S("the wave rolled through the fog (z ", b->pos.z, ", ", Sim::form_name(b->form), ")"));
	check(b->liquid > 0.6, S("it is still molten (liquid ", b->liquid, ")"));
	near(h.w->system_energy() - h.w->ledger_balance(), e0, 1e-3, "energy ledger");
}

FF_TEST_F(test_review_fixes, RF, test_a_partial_applies_once_per_contact_and_softens_the_hit) {
	SimHarness& h = H(3);
	const BodyRef b = h.w->spawn_body(Mat::Stone, Form::Chunk, 80.0, V3(0, 1, 0), "test")->shared_from_this();
	h.w->mass_ledger.ground_taken += 80.0;
	b->vel = V3(0, 0, 11.0);
	b->attack_id = h.w->new_attack_id();
	MatBody* wall = h.w->spawn_body(Mat::Sand, Form::Wall, 115.0, V3(0, 0, 2), "test");
	h.w->mass_ledger.ground_taken += 115.0;
	AgentRef t = Agent::of_body(*h.w, *b);
	AgentRef c = Agent::of_body(*h.w, *wall);
	c->power = 28.8;
	const Dict rule = D({{"outcome", "block"}, {"partial", "weaken"}, {"fail", "overwhelm"}});
	IxCtx ctx;
	ctx.site = "wall";
	const IxResult r1 = Interactions::resolve(*h.w, *t, *c, ctx, &rule);
	check(r1.outcome == "weaken", S("first contact: weaken (", r1.outcome, " r", r1.ratio, ")"));
	const double s1 = h.w->hit_scale(*b);
	check(s1 < 0.6 && s1 > 0.2, S("the weakened stone will hit softer (x", s1, ")"));
	h.step(1);
	AgentRef t2 = Agent::of_body(*h.w, *b);
	IxCtx ctx2;
	ctx2.site = "wall";
	const IxResult r2 = Interactions::resolve(*h.w, *t2, *c, ctx2, &rule);
	check(r2.outcome == "pass" && std::fabs(h.w->hit_scale(*b) - s1) < 1e-9, "the same contact does not weaken again (" + r2.outcome + ")");
}

// ---------------------------------------------------------------- clean blocks, ledgers, NaN

FF_TEST_F(test_review_fixes, RF, test_a_guard_that_absorbs_the_threat_stays_up) {
	const GuardRun r = _guard_vs_tap(Sim::AIR, 1, Sim::AIR, 0, 2.6, 0);   // Vortex Wall vs a palm gust
	check(!r.staggered, "Vortex Wall vs palm gust: no stagger");
	check(r.d->health == 100.0, "no damage");
	const GuardRun r2 = _guard_vs_tap(Sim::AIR, 1, Sim::FIRE, 0, 2.6, 0);   // Vortex Wall vs a fire jab (extinguish)
	check(!r2.staggered && std::fabs(r2.drift) < 0.01, S("Vortex Wall vs a fire jab: clean and booked (drift ", r2.drift, ")"));
}

FF_TEST_F(test_review_fixes, RF, test_vortex_wall_absorbing_a_gust_crescent_stays_finite) {
	const GuardRun r = _guard_vs_tap(Sim::AIR, 1, Sim::AIR, 0, 6.0, static_cast<int>(Gesture::Up));
	check(r.finite, "every body stays finite (massless merge)");
}

FF_TEST_F(test_review_fixes, RF, test_dew_fall_then_a_held_guard_stays_finite) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(0, 0, 4), 0, Dict(), Sim::WATER);
	ActorState* r = h.actor("R", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	r->is_dummy = true;
	w->subs[Sim::WATER] = 2;
	h.step(10);
	h.press(w, "guard");
	h.step(12);
	h.flick(w, "guard", static_cast<int>(Gesture::Down));
	bool finite = true;
	for (int k = 0; k < 120; ++k) {
		h.step(1);
		finite = finite && _all_finite();
	}
	check(finite, "Dew Fall + the guard held on: every body finite");
}

FF_TEST_F(test_review_fixes, RF, test_the_guard_matrix_keeps_the_energy_ledger) {
	// Every guard (16 sub-elements) against every element's tap strike (16): no unbooked heat.
	std::vector<std::string> bad;
	for (int de = 0; de < 4; ++de)
		for (int ds = 0; ds < 4; ++ds)
			for (int ae = 0; ae < 4; ++ae)
				for (int asub = 0; asub < 4; ++asub) {
					const GuardRun r = _guard_vs_tap(de, ds, ae, asub, 2.6, 0);
					if (std::fabs(r.drift) > 0.01) bad.push_back(S("D ", de, "/", ds, " vs A ", ae, "/", asub, " drift ", ftos(r.drift, 2)));
					if (!r.finite) bad.push_back(S("D ", de, "/", ds, " vs A ", ae, "/", asub, " non-finite"));
				}
	std::string all;
	for (const std::string& b : bad) all += b + ", ";
	check(bad.empty(), S(bad.size(), " guard cells leak: ", all));
}

// ---------------------------------------------------------------- sound vs held guards

FF_TEST_F(test_review_fixes, RF, test_a_sound_clap_does_not_disrupt_a_held_null_bubble) {
	const GuardRun r = _guard_vs_tap(Sim::AIR, 2, Sim::AIR, 3, 2.6, 0);
	check(!h().has_event("disrupt"), "the bubble is not disrupted");
	check(r.d->health == 100.0, S("the vacuum swallows the clap (health ", r.d->health, ")"));
}

// ---------------------------------------------------------------- the Lab plays push / sink at the requested tier

FF_TEST_F(test_review_fixes, RF, test_lab_script_push_and_sink_reach_the_requested_tier) {
	std::vector<std::string> bad;
	int n = 0;
	for (int e = 0; e < 4; ++e) {
		for (int s = 0; s < 4; ++s) {
			for (const char* slot : {"push", "sink"}) {
				const std::string id = Moves::resolve(e, s, slot);
				if (id.empty() || !Moves::defs().has(id)) continue;
				const int mt = Charge::max_tier(Moves::defs().get(id).as_dict());
				for (int tier = 1; tier <= mt; ++tier) {
					SimHarness& h = H(7);
					ActorState* d = h.actor("D", V3(0, 0, 4), 0, D({{"heat_draw", true}, {"magma", true}, {"lightning", true}}), e);
					ActorState* o = h.actor("O", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
					o->is_dummy = true;
					d->elements = {{true, true, true, true}};
					h.step(3);
					LabScript sc = LabScript::for_move(e, s, slot, tier);
					int best = -1;
					const size_t len = sc.length() + 20;
					for (size_t k = 0; k < len; ++k) {
						ActorIntent& it = h.it(d);
						it.clear();
						LabScript::apply_dict(it, sc.next());
						h.step(1);
						if (d->action != nullptr && d->action->id == id) best = maxi(best, d->action->tier());
					}
					++n;
					if (best < tier) bad.push_back(S(id, " T", tier, " reached T", best));
				}
			}
		}
	}
	std::string all;
	for (const std::string& b : bad) all += b + ", ";
	check(bad.empty(), S(bad.size(), " push / sink tiers short: ", all));
	note(S(n, " push / sink tier runs"));
}
