// Fourfold core tests - port of game/tests/sim/test_kit_earth_util.gd.
#include "kit_earth_util.h"

#include "Sim/FxEvents.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

namespace fft {
namespace EU {

using namespace ff;

int slot_gesture(const std::string& slot) {
	if (slot == "thrust") return static_cast<int>(Gesture::Up);
	if (slot == "ground") return static_cast<int>(Gesture::Down);
	if (slot == "sweep") return static_cast<int>(Gesture::Side);
	return 0;
}

Duel duel(SimHarness& h, int sub, int t_elem, double dist) {
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, 4.0 - dist), 1, Dict(), t_elem);
	t->is_dummy = true;
	a->facing = kPi;
	t->facing = 0.0;
	h.sub(a, sub);
	h.step(3);
	a->facing = kPi;
	return {a, t};
}

std::vector<std::string> bad_fx(const SimHarness& h, int actor_id) {
	std::vector<std::string> bad;
	for (const Dict& e : h.events("fx")) {
		if (ev_i(e, "actor", -1) != actor_id) continue;
		const std::string fx = dstr(e, "fx");
		const std::string mat = dstr(e, "mat");
		const std::string shape = dstr(e, "shape", "");
		if (!FxEvents::is_known("fx", fx) || !FxEvents::is_known("mat", mat) || !FxEvents::is_known("shape", shape))
			bad.push_back(fx + "/" + mat + "/" + shape);
	}
	return bad;
}

static double focus_eq(const ActorState* a) { return a->focus + a->heat_reserve / Sim::HU_PER_FOCUS; }

void steps(SimHarness& h, ActorState* a, int n, double* st_min) {
	for (int k = 0; k < n; ++k) {
		h.step();
		if (st_min != nullptr) *st_min = minf(*st_min, focus_eq(a));
	}
}

void perform(SimHarness& h, ActorState* a, const std::string& slot, int tier, double* st) {
	const int hold = TIER_HOLD[clampi(tier, 0, 3)];
	if (slot == "strike") {
		h.press(a, "attack");
		steps(h, a, hold, st);
		h.release(a, "attack");
	} else if (slot == "thrust" || slot == "ground" || slot == "sweep") {
		h.flick(a, "attack", slot_gesture(slot));
		steps(h, a, hold, st);
		h.release(a, "attack");
	} else if (slot == "guard") {
		h.press(a, "guard");
		steps(h, a, maxi(hold, 20), st);
		h.release(a, "guard");
	} else if (slot == "push" || slot == "sink") {
		h.press(a, "guard");
		steps(h, a, maxi(hold, 14), st);
		h.flick(a, "guard", static_cast<int>(slot == "push" ? Gesture::Up : Gesture::Down));
		steps(h, a, 1, st);
		h.release(a, "guard");
	} else if (slot == "tech") {
		h.press(a, "tech");
		steps(h, a, 40, st);
		h.it(a).attack_pressed = true;
		steps(h, a, 4, st);
		h.release(a, "tech");
	} else if (slot == "evade") {
		h.press(a, "evade");
		steps(h, a, 1, st);
	} else if (slot == "evade_hold") {
		h.it(a).move = Vec3();
		h.press(a, "evade");
		h.it(a).evade_held = true;
		steps(h, a, 40, st);
		h.it(a).evade_held = false;
	}
}

MoveRun run_move(int sub, const std::string& slot, int tier, const Prep& prep) {
	MoveRun r;
	r.h = std::make_unique<SimHarness>(7);
	SimHarness& h = *r.h;
	const Duel s = duel(h, sub);
	ActorState* a = s.a;
	ActorState* t = s.t;
	if (prep) prep(h, a, t);
	h.log.clear();
	const double f0 = focus_eq(a);
	const double m0 = a->metal_carried;
	const std::string id = Moves::resolve(Sim::EARTH, sub, slot);
	double st = f0;
	perform(h, a, slot, tier, &st);
	st = minf(st, focus_eq(a));
	const bool ended = h.until(
	                       [&]() {
		                       st = minf(st, focus_eq(a));
		                       return a->action == nullptr || (a->action->id != id && a->action->id != "guard");
	                       },
	                       400) >= 0;
	h.step(2);
	const double min_focus = minf(st, focus_eq(a));
	bool started = h.any_event("action", [&](const Dict& e) {
		return ev_i(e, "actor", -1) == a->id && dstr(e, "move") == id && dstr(e, "phase") == "startup";
	});
	if (!started && slot == "guard")
		started = h.any_event("guard", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id && dstr(e, "spec", "") == id; });
	int top = 0;
	for (const Dict& e : h.events("charge"))
		if (ev_i(e, "actor", -1) == a->id) top = maxi(top, ev_i(e, "tier"));
	bool insufficient = false;
	for (const Dict& e : h.log)
		if (dstr(e, "type") == "insufficient" && ev_i(e, "actor", -1) == a->id) insufficient = true;
	r.move = id;
	r.started = started;
	r.paid = min_focus < f0 - 1e-6 || a->metal_carried < m0 - 1e-6 || insufficient;
	r.ended = ended;
	r.tier = top;
	r.bad_fx = bad_fx(h, a->id);
	r.a = a;
	r.t = t;
	r.fx = h.count_events("fx", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id; });
	return r;
}

MatBody* lava(SimHarness& h, double mass, Vec3 p, double liquid) {
	MatBody* b = h.w->spawn_body(Mat::Stone, Form::Blob, mass, p, "test");
	h.w->mass_ledger.ground_taken += mass;
	h.w->ledger.generated += Thermal::heat(*b, mass * (Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C) + Sim::STONE_LATENT * liquid));
	Thermal::update_phase(*b);
	b->on_ground = true;
	return b;
}

MatBody* lava_wave(SimHarness& h, double mass, Vec3 p, Vec3 dir, ActorState* owner) {
	MatBody* b = lava(h, mass, p);
	b->form = Form::Wave;
	b->wave_dir = dir.normalized();
	b->wave_budget = 12.0;
	b->wave_width = 1.1 + mass * 0.025;
	b->wave_path.assign(1, p);
	b->vel = b->wave_dir * 7.5;
	b->attack_id = h.w->new_attack_id();
	b->attack_owner = owner != nullptr ? owner->id : -1;
	b->damage = 18.0;
	b->balance_damage = 55.0;
	if (owner != nullptr) b->hit_set.add(owner->id);
	return b;
}

MatBody* wall(SimHarness& h, Mat mat, const std::string& tag, double mass, Vec3 p, ActorState* owner, double yaw) {
	MatBody* b = h.w->spawn_body(mat, Form::Wall, mass, p, "test");
	if (mat == Mat::Stone || mat == Mat::Sand || mat == Mat::Glass) h.w->mass_ledger.ground_taken += mass;
	b->tag = tag;
	b->wall_half = V3(1.1, 0.75, 0.28);
	b->wall_rise = 1.0;
	b->wall_yaw = yaw;
	b->static_body = true;
	b->props.set("standing", 999.0);
	if (owner != nullptr) b->last_actor = owner->id;
	return b;
}

IxResult pr(SimHarness& h, MatBody* threat, MatBody* counter, int tier, bool perfect, ActorState* by) {
	(void)tier;
	AgentRef c = Agent::of_body(*h.w, *counter);
	c->perfect = perfect;
	AgentRef t = Agent::of_body(*h.w, *threat, by);
	return Interactions::predict(h.w, *t, *c);
}

IxResult pr(SimHarness& h, MatBody* threat, const std::string& move, int tier, bool perfect, ActorState* by) {
	AgentRef c = Agent::of_move(h.w, by, move, tier, perfect);
	AgentRef t = Agent::of_body(*h.w, *threat, by);
	return Interactions::predict(h.w, *t, *c);
}

static AgentRef volume(SimHarness& h, const std::string& cls, const Dict& ch) {
	return Agent::of_volume(h.w, nullptr, nullptr, cls, V3(0, 1, -3), V3(0, 0, 1), ch);
}

IxResult prv(SimHarness& h, const std::string& cls, const Dict& ch, MatBody* counter) {
	AgentRef v = volume(h, cls, ch);
	AgentRef c = Agent::of_body(*h.w, *counter);
	return Interactions::predict(h.w, *v, *c);
}

IxResult prv(SimHarness& h, const std::string& cls, const Dict& ch, const std::string& move, int tier, ActorState* by) {
	AgentRef v = volume(h, cls, ch);
	AgentRef c = Agent::of_move(h.w, by, move, tier, false);
	return Interactions::predict(h.w, *v, *c);
}

MatBody* shot(SimHarness& h, Mat mat, double mass, Vec3 p, Vec3 vel, ActorState* owner, const std::string& tag, double temp) {
	MatBody* b = h.w->spawn_body(mat, Form::Chunk, mass, p, "test", temp);
	if (mat == Mat::Stone || mat == Mat::Sand || mat == Mat::Glass) h.w->mass_ledger.ground_taken += mass;
	else if (mat == Mat::Metal) h.w->mass_ledger.metal_taken += mass;
	b->tag = tag;
	b->vel = vel;
	b->gravity_scale = 0.0;
	b->attack_id = h.w->new_attack_id();
	b->attack_owner = owner != nullptr ? owner->id : -1;
	b->damage = 10.0;
	b->balance_damage = 20.0;
	if (owner != nullptr) b->hit_set.add(owner->id);
	return b;
}

double energy_drift(SimHarness& h, double e0_) { return absf(h.w->system_energy() - h.w->ledger_balance() - e0_); }
double e0(SimHarness& h) { return h.w->system_energy() - h.w->ledger_balance(); }

}  // namespace EU
}  // namespace fft
