// Port of game/tests/sim/test_core_charge.gd: unified charge tiers (MOVESET §4; COMBAT_SPEC "Engine" §E3).
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Charge.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct ChargeFx : TestCase {
	std::unique_ptr<SimHarness> hp;
	ActorState* p = nullptr;
	SimHarness& _setup() {
		hp = std::make_unique<SimHarness>(2);
		SimHarness& h = *hp;
		h.begin_scope();
		Moves::register_def("t_shot", D({{"element", 0}, {"verb", "projectile"}, {"startup", 0.2}, {"active", 0.05}, {"recovery", 0.3},
		                                 {"cost", 5.0}, {"heavy_cost", 9.0}, {"charge_drain", 8.0}, {"source", "ground"}, {"mat", "stone"},
		                                 {"mass", 10.0}, {"speed", 18.0},
		                                 {"tiers", D({{"t1", D({{"mass", 16.0}})}, {"t2", D({{"mass", 22.0}})},
		                                               {"t3", D({{"mass", 30.0}, {"on_impact", "shatter"}})}})}}));
		Moves::bind(0, 1, "strike", "t_shot");
		p = h.actor("P", Vec3(0, 0, 4), 0, Dict(), Sim::EARTH);
		ActorState* o = h.actor("O", Vec3(0, 0, -8), 1, Dict(), Sim::FIRE);
		o->is_dummy = true;
		h.sub(p, 1);
		h.step(10);
		h.log.clear();
		return h;
	}
};
bool arr3(const std::array<double, 3>& a, double x, double y, double z) {
	return std::fabs(a[0] - x) < 1e-9 && std::fabs(a[1] - y) < 1e-9 && std::fabs(a[2] - z) < 1e-9;
}
}  // namespace

FF_TEST_F(test_core_charge, ChargeFx, test_tier_math_and_param_ladder) {
	const Dict d = D({{"heavy_min", 0.55}, {"tiers", D({{"t1", D({{"a", 1}})}, {"t3", D({{"a", 3}, {"b", 9}})}})}, {"a", 0}, {"b", 5}});
	check(Charge::max_tier(d) == 3, "t3 data -> max tier 3");
	check(arr3(Charge::tier_times(d), 0.55, 1.0, 1.8), "heavy_min gates T1");
	check(Charge::tier_for(d, 0.5) == 0 && Charge::tier_for(d, 0.55) == 1 && Charge::tier_for(d, 1.0) == 2 && Charge::tier_for(d, 5.0) == 3, "tier_for");
	check(Charge::max_tier(D({{"heavy_min", 0.4}})) == 1, "legacy charge moves stop at T1");
	check(Charge::max_tier(Dict()) == 0, "no charge data: T0 only");
	check(arr3(Charge::tier_times(D({{"tier_times", A({0.65, 1.2, 1.8})}})), 0.65, 1.2, 1.8), "per-move tier_times");
	check(Charge::pget(d, 0, "a") == 0, "T0 reads the def");
	check(Charge::pget(d, 1, "a") == 1, "T1 reads t1");
	check(Charge::pget(d, 2, "a") == 1, "T2 inherits t1 (ladder)");
	check(Charge::pget(d, 3, "b") == 9 && Charge::pget(d, 2, "b") == 5, "t3 only at T3");
	check(Charge::pget(d, 3, "zz", 7) == 7, "default");
	check(Charge::counter_power(D({{"counter", D({{"power", A({22, 30, 40, 55})}})}}), 2) == 40.0, "counter power by tier");
	check(Charge::counter_power(D({{"counter", D({{"power", 12}})}}), 3) == 12.0, "scalar counter power");
}

FF_TEST_F(test_core_charge, ChargeFx, test_tiers_events_drain_and_release) {
	SimHarness& h = _setup();
	const double f0 = p->focus;
	h.press(p, "attack");
	h.step(115);   // 1.92 s
	const int pid = p->id;
	auto ev = h.filter("charge", [&](const Dict& e) { return e.get("actor") == pid; });
	check(ev.size() == 3, S("three tier-ups (", ev.size(), ")"));
	if (ev.size() == 3) {
		check(ev[0].get("tier") == 1 && ev[1].get("tier") == 2 && ev[2].get("tier") == 3 && ev_b(ev[2], "ready") && !ev_b(ev[1], "ready"), "T1 T2 T3, ready at the top");
		check(ev[0].get("move") == "t_shot" && ev[0].get("sub") == 1, "event fields");
		near(ev_f(ev[1], "tick") - ev_f(ev[0], "tick"), 36.0, 1.0, "T1 0.40 s -> T2 1.00 s");
	}
	const Vec2 prog = Charge::progress(*p->action);
	check(prog == Vec2(3, 1.0f), S("progress at the top (", prog.x, ", ", prog.y, ")"));
	const double drained = f0 - 5.0 - p->focus;
	near(drained, 8.0 * (1.92 - 0.40), 0.6, "drain 8 Focus/s from T1");
	h.release(p, "attack");
	h.step(2);
	const Dict launch = h.last_event("launch");
	MatBody* b = h.w->get_body(ev_i(launch, "body", -1));
	check(b != nullptr && is_equal_approx(b->mass, 30.0) && b->tier == 3, "released at T3: 30 kg, tier 3");
	check(b != nullptr && dstr(b->props, "on_impact", "") == "shatter", "T3 property");
	check(b != nullptr && b->residual_authority > 0.8 && b->residual_authority <= 0.9, S("cohesion 0.6 + 0.1*3, decaying (", b ? b->residual_authority : 0.0, ")"));
	h.end_scope();
}

FF_TEST_F(test_core_charge, ChargeFx, test_stall_on_empty_focus_keeps_the_tier) {
	SimHarness& h = _setup();
	h.press(p, "attack");
	h.step(30);
	p->focus = 1.0;
	h.step(60);
	auto ins = h.filter("insufficient", [](const Dict& e) { return dstr(e, "reason", "") == "charge"; });
	check(ins.size() == 1, S("one insufficient event (", ins.size(), ")"));
	check(p->action != nullptr && p->action->tier() == 1, S("stalled at T1 (", p->action ? p->action->tier() : -1, ")"));
	h.release(p, "attack");
	h.step(3);
	check(h.has_event("launch"), "a stalled charge still releases");
	h.end_scope();
}

FF_TEST_F(test_core_charge, ChargeFx, test_hit_interrupts_a_charge_and_spent_focus_is_lost) {
	SimHarness& h = _setup();
	h.press(p, "attack");
	h.step(40);
	const double f = p->focus;
	h.w->hit_actor(*p, D({{"attacker", 2}, {"attack_id", h.w->new_attack_id()}, {"damage", 5.0}, {"balance", 30.0}, {"kind", "stone"}}));
	h.step();
	check(p->action == nullptr && h.has_event("interrupt"), "interrupted");
	check(p->focus <= f + 0.01, "no refund");
	h.release(p, "attack");
	h.step(20);
	const int pid = p->id;
	check(!h.any_event("launch", [&](const Dict& e) { return e.get("actor") == pid; }), "nothing released");
	h.end_scope();
}

FF_TEST_F(test_core_charge, ChargeFx, test_legacy_strikes_stay_t0_t1) {
	hp = std::make_unique<SimHarness>(1);
	SimHarness& h = *hp;
	ActorState* f = h.actor("F", Vec3(0, 0, 4), 0, Dict(), Sim::FIRE);
	h.actor("T", Vec3(0, 0, -4), 1, Dict(), Sim::EARTH);
	h.step(10);
	h.press(f, "attack");
	h.step(80);
	const int fid = f->id;
	auto ev = h.filter("charge", [&](const Dict& e) { return e.get("actor") == fid; });
	check(ev.size() == 1 && ev[0].get("tier") == 1, S("one tier-up to T1 for a legacy strike (", ev.size(), ")"));
	check(!h.has_event("insufficient"), "no drain on legacy strikes");
	near(f->focus, 100.0, 1e-6, "legacy hold costs no extra Focus");
	h.release(f, "attack");
	h.step(5);
	check(h.has_event("flare", "heavy", true), "legacy blaze");
}
