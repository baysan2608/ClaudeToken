// Port of game/tests/sim/test_ai_planner.gd: matrix-driven counter planner (AiPlanner + AiBrain planner mode, docs/AI.md):
// full-outcome counters per kit, REC over BLK, costs and time to impact, difficulty, honesty (no hidden state),
// determinism.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
BodyRef _wave(CombatWorld& w, ActorState* owner, double mass, Vec3 p, Vec3 dir) {
	const Dict d = Moves::defs().get("pour").as_dict();
	MatBody* b = w.spawn_body(Mat::Stone, Form::Wave, mass, p, "test", Sim::STONE_MELT_C);
	w.mass_ledger.ground_taken += mass;
	b->liquid = 1.0;
	b->phase = Phase::Molten;
	b->on_ground = true;
	b->update_radius();
	b->wave_dir = dir;
	b->wave_budget = 30.0;
	b->wave_width = 1.1 + mass * 0.025;
	b->wave_path.assign(1, p);
	b->max_life = -1.0;
	b->attack_id = w.new_attack_id();
	b->attack_owner = owner->id;
	b->hit_set.add(owner->id);
	b->damage = dnum(d, "damage");
	b->balance_damage = dnum(d, "balance");
	return b->shared_from_this();
}

// A configured brain facing a rival.
AiRig _setup(const AiKit& kit, const Dict& opts = Dict(), uint64_t seed_value = 3, double gap = 11.0) {
	AiRig r;
	r.h = std::make_unique<SimHarness>(seed_value);
	r.p = r.h->actor("player", V3(0, 0, gap * 0.5), 0, Dict(), Sim::FIRE);
	r.o = r.h->actor("opponent", V3(0, 0, -gap * 0.5), 1, Dict(), kit[0].first);
	r.ai = std::make_unique<AiBrain>(*r.h->w, *r.o, Dict(), seed_value);
	Dict c = D({{"preset", "master"}, {"elements", kit_elements(kit)}, {"subs", kit_subs(kit)}, {"drill", "passive"}, {"counter", 1.0}, {"misjudge", 0.0}});
	merge_into(c, opts);
	r.ai->configure(c);
	r.tick(10);
	return r;
}

AiOption _best(AiRig& d, const AiThreat& th, const Dict& extra = Dict()) {
	AiParams prm = d.ai->_planner_params();
	prm.merge(extra);
	const std::vector<AiOption> opts = AiPlanner::counters(*d.h->w, *d.o, th, d.ai->kit, prm, 0.0);
	return opts.empty() ? AiOption() : opts[0];
}

struct Resolved {
	bool hit = false;
	std::vector<std::string> outcomes;
};
// Steps the duel until the body is gone / settled; returns whether the AI was hit by it.
Resolved _run_until_resolved(AiRig& d, const BodyRef& b, int ticks = 300) {
	Resolved res;
	SimHarness& h = *d.h;
	for (int k = 0; k < ticks; ++k) {
		d.think();
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "hit" && ev_i(e, "actor", -1) == d.o->id) res.hit = true;
			if (ev_s(e, "type") == "interaction" && ev_i(e, "counter_actor", -1) == d.o->id) res.outcomes.push_back(ev_s(e, "outcome"));
		}
		if (!b->alive || (b->form == Form::Wave && b->wave_budget <= 0.0)) break;
	}
	return res;
}

std::string joined(const std::vector<std::string>& v) {
	std::string s;
	for (const std::string& x : v) s += x + " ";
	return s;
}

void _test_moves(SimHarness& h, double cost_rec = 5.0, double startup_rec = 0.1, bool swap = false) {
	h.begin_scope();
	const Dict base = D({{"element", 0}, {"sub", 1}, {"verb", "projectile"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"cost", 5.0},
	                     {"source", "none"}, {"mat", "stone"}, {"mass", 5.0}, {"speed", 20.0}, {"damage", 1.0},
	                     {"ai", D({{"role", "counter"}, {"range", A({0.0, 12.0})}})}});
	Dict rec = base.duplicate(true);
	rec.set("counter", D({{"cls", "ai_t_rec"}, {"power", A({20.0})}}));
	rec.set("cost", cost_rec);
	rec.set("startup", startup_rec);
	Dict blk = base.duplicate(true);
	blk.set("counter", D({{"cls", "ai_t_blk"}, {"power", A({20.0})}}));
	const std::vector<std::string> ids = swap ? std::vector<std::string>{"ai_t_blk", "ai_t_rec"} : std::vector<std::string>{"ai_t_rec", "ai_t_blk"};
	for (const std::string& id : ids) Moves::register_def(id, id == "ai_t_rec" ? rec : blk);
	Moves::bind(0, 1, "thrust", ids[0]);
	Moves::bind(0, 1, "sweep", ids[1]);
	Interactions::add_rule("stone", "ai_t_rec", D({{"outcome", "reclaim"}, {"partial", "weaken"}, {"fail", "overwhelm"}}));
	Interactions::add_rule("stone", "ai_t_blk", D({{"outcome", "block"}, {"partial", "weaken"}, {"fail", "overwhelm"}}));
}

const Dict& only_test_moves() {
	static const Dict d = D({{"only", A({"ai_t_rec", "ai_t_blk"})}});
	return d;
}
}  // namespace

// ------------------------------------------------------------------ full-outcome counters by kit

FF_TEST(test_ai_planner, test_lava_wave_full_counter_per_kit) {
	// Earth/Stone: a wall answer (Bulwark, Ram Wall, Swallow trench, Rising Fangs) with a full outcome.
	{
		AiRig de = _setup({{0, {0}}});
		const BodyRef we = _wave(*de.h->w, de.p, 20.0, V3(0, 0, 4.5), V3(0, 0, -1));
		const AiOption be = _best(de, AiPlanner::body_threat(*de.h->w, *de.o, *we));
		note(S("earth/stone vs lava wave: ", be.label, " ", be.outcome, "/", be.band, " r", be.ratio));
		check(be.valid && be.element == Sim::EARTH && be.band == "full", "Earth/Stone answers the wave with a full outcome (" + be.label + ")");
		check(in_list(be.slot, {"guard", "push", "sink", "ground"}), "a wall / trench / spike answer (" + be.slot + ")");
		const Resolved re = _run_until_resolved(de, we);
		check(!re.hit, "the wall answer stops the lava (" + joined(re.outcomes) + ")");
	}
	// Fire/Flame with heat draw: DRAW sets the lava.
	{
		AiRig df = _setup({{2, {0}}}, D({{"kit", D({{"heat_draw", true}})}}));
		const BodyRef wf = _wave(*df.h->w, df.p, 20.0, V3(0, 0, 4.5), V3(0, 0, -1));
		const AiOption bf = _best(df, AiPlanner::body_threat(*df.h->w, *df.o, *wf));
		note("fire/flame vs lava wave: " + bf.label + " " + bf.mode);
		check(bf.id == "fire_tech" && bf.mode == "DRAW", "Fire/Flame draws the heat (" + bf.label + ")");
		const Resolved rf = _run_until_resolved(df, wf);
		check(!rf.hit, "the draw sets the wave before it arrives");
	}
	// Air/Gust, far wave: only a T3 gale (Hurricane Palm) answers it fully.
	AiRig da = _setup({{3, {0}}}, Dict(), 3, 22.0);
	const BodyRef wa = _wave(*da.h->w, da.p, 20.0, V3(0, 0, 4.5), V3(0, 0, -1));
	const AiOption ba = _best(da, AiPlanner::body_threat(*da.h->w, *da.o, *wa));
	note(S("air/gust vs far lava wave: ", ba.label, " ", ba.outcome, "/", ba.band, " r", ba.ratio));
	check(ba.valid && ba.element == Sim::AIR && ba.tier == 3 && ba.band == "full", "Air/Gust needs a T3 gale for a full answer (" + ba.label + ")");
}

FF_TEST(test_ai_planner, test_simple_air_cannot_stop_lava) {
	// A close lava wave: no gust tier is both strong enough and in time, so the gust kit never "attacks" the
	// lava with a weak gust; it guards (chip) or evades.
	AiRig d = _setup({{3, {0}}});
	const BodyRef wv = _wave(*d.h->w, d.p, 20.0, V3(0, 0, 2.0), V3(0, 0, -1));
	const AiThreat th = AiPlanner::body_threat(*d.h->w, *d.o, *wv);
	const std::vector<AiOption> opts = AiPlanner::counters(*d.h->w, *d.o, th, d.ai->kit, d.ai->_planner_params(), 0.0);
	for (const AiOption& o : opts)
		if (in_list(o.slot, {"strike", "thrust", "ground", "sweep"})) check(o.band != "full", S("no full answer from a weak gust (", o.label, " r", o.ratio, ")"));
	if (!check(!opts.empty(), "some answer")) return;
	const AiOption& best = opts[0];
	check(!in_list(best.slot, {"strike", "thrust", "ground", "sweep"}), "best answer is not a weak gust (" + best.label + ")");
	// The counter rule itself: Palm Gust T0 fails, Hurricane T3 sets it (MOVESET §5.4).
	const IxResult t0 = Interactions::predict(d.h->w, *th.agent, *Agent::of_move(d.h->w, d.o, "air_attack", 0, false));
	const IxResult t3 = Interactions::predict(d.h->w, *th.agent, *Agent::of_move(d.h->w, d.o, "air_attack", 3, false));
	check(AiPlanner::outcome_value(t0.outcome, t0.band, t0.rule) < 0.3, S("palm gust T0 does not stop lava (", t0.outcome, "/", t0.band, " r", t0.ratio, ")"));
	check(t3.band == "full", S("hurricane T3 answers lava (r", t3.ratio, ")"));
}

// ------------------------------------------------------------------ utility rules

FF_TEST(test_ai_planner, test_prefers_reclaim_over_block_when_equal) {
	for (bool swap : {false, true}) {
		AiRig d = _setup({{0, {1}}});
		SimHarness& h = *d.h;
		_test_moves(h, 5.0, 0.1, swap);
		MatBody* st = h.launch_at(d.o, "stone", 20.0, 17.0, 20.0, "", d.p, 10.0);
		const AiThreat th = AiPlanner::body_threat(*h.w, *d.o, *st);
		const AiOption best = _best(d, th, only_test_moves());
		h.end_scope();
		check(best.id == "ai_t_rec", S("REC is preferred over an equal BLK (order ", swap, ": ", best.label, ")"));
	}
	check(AiPlanner::outcome_value("reclaim", "full") > AiPlanner::outcome_value("redirect", "full"), "REC > DEF redirect");
	check(AiPlanner::outcome_value("redirect", "full") > AiPlanner::outcome_value("transform", "full"), "DEF redirect > XFM");
	check(AiPlanner::outcome_value("transform", "full") > AiPlanner::outcome_value("block", "full"), "XFM > BLK");
	check(AiPlanner::outcome_value("block", "full") > AiPlanner::outcome_value("weaken", "partial"), "BLK > partial");
	check(AiPlanner::outcome_value("weaken", "partial") > AiPlanner::outcome_value("overwhelm", "fail"), "partial > fail");
}

FF_TEST(test_ai_planner, test_respects_costs_and_time_to_impact) {
	// Focus: a REC that costs more than the AI has is not an option; the affordable BLK is taken.
	{
		AiRig d = _setup({{0, {1}}});
		SimHarness& h = *d.h;
		_test_moves(h, 40.0);
		d.o->focus = 20.0;
		MatBody* st = h.launch_at(d.o, "stone", 20.0, 17.0, 20.0, "", d.p, 10.0);
		AiOption best = _best(d, AiPlanner::body_threat(*h.w, *d.o, *st), only_test_moves());
		check(best.id == "ai_t_blk", "an unaffordable REC is skipped (" + best.label + ")");
		d.o->focus = 100.0;
		best = _best(d, AiPlanner::body_threat(*h.w, *d.o, *st), only_test_moves());
		check(best.id == "ai_t_rec", "with Focus the REC is back (" + best.label + ")");
		h.end_scope();
	}
	// Time to impact: a slow REC (startup 0.8 s) can't meet a stone 0.5 s out; it can meet one 1.3 s out.
	{
		AiRig d2 = _setup({{0, {1}}});
		SimHarness& h2 = *d2.h;
		_test_moves(h2, 5.0, 0.8);
		MatBody* nearb = h2.launch_at(d2.o, "stone", 20.0, 17.0, 20.0, "", d2.p, 8.0);
		const AiThreat th_near = AiPlanner::body_threat(*h2.w, *d2.o, *nearb);
		const AiOption b_near = _best(d2, th_near, only_test_moves());
		check(b_near.id == "ai_t_blk", S("tti ", th_near.tti, " s: the slow REC is too late (", b_near.label, ")"));
		nearb->alive = false;
		MatBody* far = h2.launch_at(d2.o, "stone", 20.0, 12.0, 20.0, "", d2.p, 16.0);
		const AiThreat th_far = AiPlanner::body_threat(*h2.w, *d2.o, *far);
		const AiOption b_far = _best(d2, th_far, only_test_moves());
		check(b_far.id == "ai_t_rec", S("tti ", th_far.tti, " s: the slow REC is in time (", b_far.label, ")"));
		h2.end_scope();
	}
	// Charge tiers cost time: no T3 hold (1.8 s) against a threat 0.8 s out.
	AiRig d3 = _setup({{3, {0}}});
	const BodyRef wv = _wave(*d3.h->w, d3.p, 20.0, V3(0, 0, 0.0), V3(0, 0, -1));
	const AiThreat th3 = AiPlanner::body_threat(*d3.h->w, *d3.o, *wv);
	for (const AiOption& o : AiPlanner::counters(*d3.h->w, *d3.o, th3, d3.ai->kit, d3.ai->_planner_params(), 0.0))
		check(o.hold < th3.tti, S("no option holds longer than the time to impact (", o.label, " hold ", o.hold, ", tti ", th3.tti, ")"));
}

// ------------------------------------------------------------------ difficulty

namespace {
std::pair<int, int> _skill_run(const std::string& preset, uint64_t seed_value) {
	SimHarness h(seed_value);
	ActorState* p = h.actor("player", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
	ActorState* o = h.actor("opponent", V3(0, 0, -5), 1, Dict(), Sim::EARTH);
	AiBrain ai(*h.w, *o, Dict(), seed_value);
	ai.configure(D({{"preset", preset}, {"elements", A({0, 3})}, {"subs", D({{"0", A({0, 1, 2, 3})}, {"3", A({0, 1, 2, 3})}})}, {"drill", "passive"}}));
	Rng rng;
	rng.set_seed(seed_value * 7919);
	for (int k = 0; k < 15; ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		h.step();
	}
	int hits = 0, fails = 0;
	for (int n = 0; n < 4; ++n) {
		o->focus = 100.0;
		o->health = 100.0;
		o->balance = 100.0;
		const double mass = rng.randf_range(15.0f, 60.0f);
		const double speed = rng.randf_range(13.0f, 18.0f);
		const double temp = rng.randf() < 0.7 ? 20.0 : 900.0;
		const double dist = rng.randf_range(9.0f, 13.0f);
		const BodyRef b = h.launch_at(o, "stone", mass, speed, temp, "", p, dist)->shared_from_this();
		bool hit = false;
		for (int k = 0; k < 150; ++k) {
			h.intents[o->id] = ai.think(Sim::DT);
			const size_t n0 = h.log.size();
			h.step();
			for (size_t i = n0; i < h.log.size(); ++i)
				if (ev_s(h.log[i], "type") == "hit" && ev_i(h.log[i], "actor", -1) == o->id) hit = true;
			if (!b->alive || b->attack_id == 0 || b->pos.distance_to(o->chest()) > 25.0f) break;
		}
		hits += hit ? 1 : 0;
		if (!ai.last_plan.empty() && dstr(ai.last_plan, "band", "") != "full") ++fails;
		for (int k = 0; k < 40; ++k) {
			h.intents[o->id] = ai.think(Sim::DT);
			h.step();
		}
	}
	return {hits, fails};
}
}  // namespace

FF_TEST(test_ai_planner, test_novice_fails_more_than_master) {
	int nov = 0, mas = 0;
	for (uint64_t s = 0; s < 5; ++s) {   // 5 seeds x 4 threats = 20 seeded threats per preset
		nov += _skill_run("novice", 100 + s).first;
		mas += _skill_run("master", 100 + s).first;
	}
	note(S("hits taken over 20 thrown stones: novice ", nov, ", master ", mas));
	check(nov > mas, S("the novice is hit more often than the master (", nov, " vs ", mas, ")"));
	check(mas <= 6, S("the master answers most threats (", mas, "/20 hits)"));
}

// ------------------------------------------------------------------ honesty and determinism

// Two worlds with the same observable state; world B differs only in hidden state (the rival's intent, buffered press,
// unrevealed technique flags, world RNG, private body/action data). The AI must act the same.
FF_TEST(test_ai_planner, test_never_reads_hidden_state) {
	std::vector<std::vector<std::string>> streams;
	for (int variant = 0; variant < 2; ++variant) {
		AiRig d = _setup({{0, {0, 1, 2, 3}}, {3, {0, 1, 2, 3}}}, D({{"counter", 0.6}, {"misjudge", 0.2}}), 5);
		SimHarness& h = *d.h;
		ActorState* p = d.p;
		MatBody* st = h.launch_at(d.o, "stone", 25.0, 15.0, 20.0, "", p, 12.0);
		if (variant == 1) {
			h.w->rng.set_seed(987654);
			for (int k = 0; k < 17; ++k) h.w->rng.randi();
			p->buffered = "tech";
			p->buffered_tick = h.w->tick;
			p->kit = D({{"magma", true}, {"heat_draw", true}, {"lightning", true}, {"redirect_current", true}});
			ActorIntent& it = h.it(p);
			it.attack_held = true;
			it.guard_held = true;
			it.aim_dir = V3(1, 0, 0);
			st->props.set("ai_secret", 42);
			st->residual_owner = 99;
		}
		std::vector<std::string> s;
		for (int k = 0; k < 90; ++k) {
			const ActorIntent& i = d.ai->think(Sim::DT);
			s.push_back(S(i.attack_pressed, "|", i.guard_pressed, "|", i.tech_pressed, "|", i.evade_pressed, "|", i.guard_held, "|", i.tech_held, "|",
			              i.element_select, "|", i.sub_select, "|", i.guard_gesture));
			// Advance the stone by hand (identical in both worlds), the world itself does not step.
			st->pos += st->vel * f32(Sim::DT);
		}
		s.push_back(dstr(d.ai->last_plan, "label", "-"));
		streams.push_back(s);
	}
	check(streams[0] == streams[1], "the AI's decisions ignore hidden state");
	note("decision: " + streams[0].back());
}

namespace {
std::string _hash(SimHarness& h) {
	std::string out;
	for (const auto& xp : h.w->actors) {
		const ActorState& x = *xp;
		out += ftos(x.pos.x, 4) + "," + ftos(x.pos.z, 4) + "," + ftos(x.health, 4) + "," + ftos(x.focus, 3) + "," + ftos(x.heat_reserve, 3) + "," +
		       itos(x.element) + "," + itos(x.sub()) + ";";
	}
	int n = 0;
	for (const BodyRef& bd : h.w->bodies) {
		if (!bd->alive) continue;
		++n;
		out += itos(bd->id) + ":" + ftos(bd->pos.x, 3) + "," + ftos(bd->pos.z, 3) + "," + ftos(bd->mass, 2) + ";";
	}
	return out + itos(n);
}
}  // namespace

FF_TEST(test_ai_planner, test_deterministic_per_seed) {
	struct R {
		std::string hash, acts;
		size_t n;
	};
	std::vector<R> hashes;
	for (uint64_t seed : {21u, 21u, 22u}) {
		SimHarness h(seed);
		ActorState* a = h.actor("A", V3(0, 0, 7), 0, Dict(), Sim::EARTH);
		ActorState* b = h.actor("B", V3(0, 0, -7), 1, Dict(), Sim::FIRE);
		AiBrain ba(*h.w, *a, Dict(), seed * 2 + 1);
		AiBrain bb(*h.w, *b, Dict(), seed * 2 + 2);
		ba.configure(D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}}));
		bb.configure(D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}}));
		std::string acts;
		size_t n = 0;
		for (int k = 0; k < 1200; ++k) {
			h.intents[a->id] = ba.think(Sim::DT);
			h.intents[b->id] = bb.think(Sim::DT);
			h.step();
		}
		for (const Dict& e : h.log) {
			if (ev_s(e, "type") == "action" && ev_s(e, "phase") == "startup") {
				acts += ev_s(e, "actor") + ":" + ev_s(e, "move") + ",";
				++n;
			}
		}
		hashes.push_back({_hash(h), acts, n});
	}
	check(hashes[0].hash == hashes[1].hash && hashes[0].acts == hashes[1].acts, S("same seed -> same duel (", hashes[0].n, " actions)"));
	check(hashes[0].hash != hashes[2].hash, "another seed -> another duel");
}
