// Port of game/tests/sim/test_ai_offense.gd: offense reads (MOVESET §13.6): wet / in the pool -> lightning, rival behind a
// barrier -> Storm Bolt, Melt & Return or a sound bank shot, charging rival -> Disrupt / interrupt, chains after contact.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

using namespace ff;
using namespace fft;

namespace {
AiRig _setup(const AiKit& kit, Vec3 foe_pos = V3(0, 0, 4), Vec3 me_pos = V3(0, 0, -4), const Dict& opts = Dict()) {
	AiRig r;
	r.h = std::make_unique<SimHarness>(6);
	r.p = r.h->actor("player", foe_pos, 0, Dict(), Sim::EARTH);
	r.o = r.h->actor("opponent", me_pos, 1, Dict(), kit[0].first);
	r.ai = std::make_unique<AiBrain>(*r.h->w, *r.o, Dict(), 6);
	Dict c = D({{"preset", "master"}, {"elements", kit_elements(kit)}, {"subs", kit_subs(kit)}});
	merge_into(c, opts);
	r.ai->configure(c);
	r.h->step(2);
	return r;
}

AiOption _top(AiRig& d, uint64_t seed_value = 1) {
	Rng rng;
	rng.set_seed(seed_value);
	const std::vector<AiOption> opts = AiPlanner::offense(*d.h->w, *d.o, *d.p, d.ai->kit, d.ai->_planner_params(), rng);
	return opts.empty() ? AiOption() : opts[0];
}

MatBody* _wall_between(AiRig& d) {
	CombatWorld& w = *d.h->w;
	const Vec3 mid = (d.p->pos + d.o->pos) * 0.5f;
	MatBody* b = w.spawn_body(Mat::Stone, Form::Wall, Sim::WALL_MASS, mid, "test");
	w.mass_ledger.ground_taken += Sim::WALL_MASS;
	b->wall_yaw = 0.0;
	b->wall_half = V3(1.6, 1.0, 0.28);
	b->wall_rise = 1.0;
	b->static_body = true;
	b->last_actor = d.p->id;
	return b;
}

std::string reasons(const AiOption& o) {
	std::string s = "[";
	for (const std::string& r : o.reasons) s += r + " ";
	return s + "]";
}
}  // namespace

FF_TEST(test_ai_offense, test_wet_target_draws_lightning) {
	const AiKit kit = {{1, {0}}, {2, {0, 2}}, {0, {0}}};
	int n_wet = 0, n_dry = 0;
	for (uint64_t s = 0; s < 6; ++s) {
		AiRig d = _setup(kit);
		const AiOption dry = _top(d, s);
		n_dry += dry.has_reason("conduct") ? 1 : 0;
		d.p->wetness = 1.0;
		const AiOption wet = _top(d, s);
		n_wet += wet.has_reason("conduct") ? 1 : 0;
		if (s == 0) note("dry: " + dry.label + ", wet: " + wet.label + " " + reasons(wet));
	}
	check(n_wet == 6, S("a wet rival draws an electric attack (", n_wet, "/6)"));
	check(n_dry == 0, S("a dry rival does not count as conductive (", n_dry, "/6)"));
}

FF_TEST(test_ai_offense, test_barrier_answers) {
	{
		// Lightning kit: Storm Bolt (spark T2) blasts through the wall.
		AiRig dl = _setup({{2, {2}}});
		_wall_between(dl);
		const AiOption tl = _top(dl);
		note("lightning vs wall: " + tl.label + " " + reasons(tl));
		check(tl.id == "spark" && tl.tier >= 2 && tl.has_reason("barrier"), "Storm Bolt through the wall (" + tl.label + ")");
	}
	{
		// Blue fire kit: Smelter heats the wall face (Melt & Return, step 1).
		AiRig db = _setup({{2, {1}}, {0, {3}}});
		MatBody* wb = _wall_between(db);
		const AiOption tb = _top(db);
		note("blue/magma vs wall: " + tb.label + " " + reasons(tb));
		check(tb.id == "smelter" && tb.aim_body == wb->id, "Smelter on the wall face (" + tb.label + ")");
	}
	// Sound kit: rival behind a Bulwark near the west arena wall -> a bank shot off the arena wall.
	AiRig ds = _setup({{3, {3}}}, V3(-13.0, 0, -4.0), V3(-13.0, 0, 4.0));
	_wall_between(ds);
	const AiOption ts = _top(ds);
	note(S("sound vs cover: ", ts.label, " aim ", ts.aim.x, ",", ts.aim.y, ",", ts.aim.z));
	check(ts.id == "sound_lance" && ts.aim != Vec3(), "a banked Sound Lance (" + ts.label + ")");
}

FF_TEST(test_ai_offense, test_melt_and_return_in_play) {
	// Rival hides behind a Bulwark; the AI (Fire/Blue + Earth/Magma) melts the face and sends lava back.
	AiRig d = _setup({{2, {1}}, {0, {3}}}, V3(0, 0, 3.0), V3(0, 0, -3.0), D({{"aggression", 1.0}}));
	SimHarness& h = *d.h;
	ActorState* p = d.p;
	p->facing = kPi;
	h.press(p, "guard");   // the rival raises a Bulwark and keeps it up
	h.step(20);
	MatBody* wall = h.w->get_body(p->wall_body);
	check(wall != nullptr && wall->alive, "setup: the rival's wall is up");
	AiTestAccess::next_attack(*d.ai) = 0.0;
	bool slumped = false, surged = false;
	for (int k = 0; k < 60 * 8; ++k) {
		d.think();
		h.it(p).guard_held = true;
		p->health = 100.0;
		p->focus = 100.0;
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "slump") slumped = true;
			if (ev_s(e, "type") == "action" && ev_i(e, "actor", -1) == d.o->id && ev_s(e, "move") == "magma_surge") surged = true;
		}
		if (surged) break;
	}
	note(S("slumped ", slumped, ", magma surge ", surged, ", AI ", d.ai->debug_state));
	check(slumped, "the Smelter slumps the wall face");
	check(surged, "then Magma Surge sends the lava back (Melt & Return)");
}

FF_TEST(test_ai_offense, test_charging_rival_is_disrupted) {
	AiRig d = _setup({{3, {3}}}, V3(0, 0, 3), V3(0, 0, -3));
	SimHarness& h = *d.h;
	ActorState* p = d.p;
	p->element = Sim::EARTH;
	AiTestAccess::next_attack(*d.ai) = 1e9;
	h.press(p, "attack");   // a held heave: a long visible charge
	bool interrupted = false;
	std::string acted;
	for (int k = 0; k < 90; ++k) {
		d.think();
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			const std::string ty = ev_s(e, "type");
			if (ty == "action" && ev_i(e, "actor", -1) == d.o->id && ev_s(e, "phase") == "startup" && acted.empty()) acted = ev_s(e, "move");
			if ((ty == "interrupt" || ty == "hit") && ev_i(e, "actor", -1) == p->id) interrupted = true;
		}
	}
	note(S("answer to the charge: ", acted, " (", d.ai->debug_state, "), interrupted ", interrupted));
	check(acted == "sound_clap" || acted == "sound_lance" || acted == "sound_echo_ring", "a sound move answers the charge (" + acted + ")");
	check(interrupted, "the charge is interrupted");
}

FF_TEST(test_ai_offense, test_chain_after_contact) {
	AiRig d = _setup({{2, {0}}, {3, {0}}}, V3(0, 0, 1.5), V3(0, 0, -1.5), D({{"aggression", 1.0}}));
	SimHarness& h = *d.h;
	int chains = 0;
	for (int k = 0; k < 60 * 15; ++k) {
		d.think();
		d.p->health = 100.0;
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i)
			if (ev_s(h.log[i], "type") == "chain" && ev_i(h.log[i], "actor", -1) == d.o->id) ++chains;
	}
	check(chains >= 2, S("the AI continues strings after contact (", chains, " chains)"));
}
