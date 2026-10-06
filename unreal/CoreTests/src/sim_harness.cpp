// Fourfold core tests - port of game/tests/sim/sim_harness.gd.
#include "sim_harness.h"

#include "Sim/MatBody.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

namespace fft {

using namespace ff;

SimHarness::SimHarness(uint64_t seed_value) : owned(std::make_unique<CombatWorld>(seed_value)), w(owned.get()) {}
SimHarness::~SimHarness() { end_scope(); }

ActorState* SimHarness::actor(const std::string& nm, Vec3 p, int team, const Dict& kit, int element_) {
	ActorState* a = w->add_actor(nm, p, team, kit, element_);
	intents[a->id] = ActorIntent();
	return a;
}

ActorIntent& SimHarness::it(const ActorState* a) { return intents[a->id]; }

void SimHarness::press(const ActorState* a, const std::string& what) {
	ActorIntent& i = it(a);
	if (what == "attack") {
		i.attack_pressed = true;
		i.attack_held = true;
	} else if (what == "guard") {
		i.guard_pressed = true;
		i.guard_held = true;
	} else if (what == "tech") {
		i.tech_pressed = true;
		i.tech_held = true;
	} else if (what == "evade") {
		i.evade_pressed = true;
	}
}

void SimHarness::release(const ActorState* a, const std::string& what) {
	ActorIntent& i = it(a);
	if (what == "attack") {
		i.attack_held = false;
		i.attack_released = true;
	} else if (what == "guard") {
		i.guard_held = false;
	} else if (what == "tech") {
		i.tech_held = false;
		i.tech_released = true;
	}
}

void SimHarness::cancel_tech(const ActorState* a) {
	ActorIntent& i = it(a);
	i.tech_cancel = true;
	i.tech_held = false;
}

void SimHarness::aim(const ActorState* a, Vec3 dir) {
	ActorIntent& i = it(a);
	i.aim_dir = dir.normalized();
	i.aim_active = dir.length() > 0.0f;
}

void SimHarness::element(const ActorState* a, int e) { it(a).element_select = e; }

void SimHarness::step(int n) {
	for (int k = 0; k < n; ++k) {
		w->step(intents);
		for (Dict& e : w->take_events()) log.push_back(std::move(e));
		for (auto& kv : intents) {
			ActorIntent& x = kv.second;
			x.attack_pressed = false;
			x.attack_released = false;
			x.guard_pressed = false;
			x.evade_pressed = false;
			x.tech_pressed = false;
			x.tech_released = false;
			x.tech_cancel = false;
			x.element_select = -1;
			x.target_cycle = false;
			x.sub_select = -1;
			x.attack_gesture = 0;
			x.guard_gesture = 0;
		}
	}
}

int SimHarness::until(const std::function<bool()>& cond, int max_ticks) {
	for (int k = 0; k < max_ticks; ++k) {
		step();
		if (cond()) return k + 1;
	}
	return -1;
}

std::vector<Dict> SimHarness::events(const std::string& type) const {
	std::vector<Dict> out;
	for (const Dict& e : log)
		if (e.get("type").as_string() == type) out.push_back(e);
	return out;
}

Dict SimHarness::last_event(const std::string& type) const {
	for (size_t k = log.size(); k-- > 0;)
		if (log[k].get("type").as_string() == type) return log[k];
	return Dict();
}

bool SimHarness::has_event(const std::string& type, const std::string& key, const Value& value) const {
	for (const Dict& e : log)
		if (e.get("type").as_string() == type && (key.empty() || e.get(key) == value)) return true;
	return false;
}

bool SimHarness::any_event(const std::string& type, const std::function<bool(const Dict&)>& pred) const {
	for (const Dict& e : log)
		if (e.get("type").as_string() == type && pred(e)) return true;
	return false;
}

int SimHarness::count_events(const std::string& type, const std::function<bool(const Dict&)>& pred) const {
	int n = 0;
	for (const Dict& e : log)
		if (e.get("type").as_string() == type && (!pred || pred(e))) ++n;
	return n;
}

std::vector<Dict> SimHarness::filter(const std::string& type, const std::function<bool(const Dict&)>& pred) const {
	std::vector<Dict> out;
	for (const Dict& e : log)
		if (e.get("type").as_string() == type && (!pred || pred(e))) out.push_back(e);
	return out;
}

void SimHarness::sub(const ActorState* a, int s) { it(a).sub_select = s; }

void SimHarness::flick(const ActorState* a, const std::string& button, int gesture, bool press_too) {
	ActorIntent& i = it(a);
	if (button == "attack") {
		i.attack_gesture = gesture;
		if (press_too) {
			i.attack_pressed = true;
			i.attack_held = true;
		}
	} else if (button == "guard") {
		i.guard_gesture = gesture;
		i.guard_held = true;
	}
}

void SimHarness::hold(const ActorState* a, const std::string& button, int ticks) {
	press(a, button);
	if (button == "evade") it(a).evade_held = true;
	step(ticks);
	if (button == "evade") it(a).evade_held = false;
	else release(a, button);
}

void SimHarness::evade_hold(const ActorState* a, int ticks) { hold(a, "evade", ticks); }

MatBody* SimHarness::launch_at(ActorState* target, int m, double mass, double speed, double temp, const std::string& tag,
                               ActorState* owner, double dist) {
	const Vec3 from = target->chest() + target->forward() * dist;
	MatBody* b = w->spawn_body(static_cast<Mat>(m), Form::Chunk, mass, from, "test", temp);
	if (m == static_cast<int>(Mat::Stone) || m == static_cast<int>(Mat::Sand) || m == static_cast<int>(Mat::Glass))
		w->mass_ledger.ground_taken += mass;
	b->tag = tag;
	if (m == static_cast<int>(Mat::Stone) && temp >= Sim::STONE_MELT_C) Thermal::heat(*b, 0.0);
	b->vel = (target->chest() - from).normalized() * speed;
	b->gravity_scale = 0.0;
	b->attack_id = w->new_attack_id();
	b->attack_owner = owner != nullptr ? owner->id : -1;
	b->damage = 10.0;
	b->balance_damage = 20.0;
	if (owner != nullptr) b->hit_set.add(owner->id);
	return b;
}

MatBody* SimHarness::launch_at(ActorState* target, const std::string& mat, double mass, double speed, double temp,
                               const std::string& tag, ActorState* owner, double dist) {
	return launch_at(target, Sim::mat_index(mat), mass, speed, temp, tag, owner, dist);
}

MatBody* SimHarness::spawn_zone(const std::string& tag, Vec3 pos, double radius, ActorState* owner, double power) {
	return w->spawn_zone(tag, pos, radius, owner != nullptr ? owner->id : -1, power);
}

IxResult SimHarness::predict(const Agent& threat, const std::string& move_id, int tier, bool perfect, ActorState* by) {
	AgentRef c = Agent::of_move(w, by, move_id, tier, perfect);
	return Interactions::predict(w, threat, *c);
}

void SimHarness::begin_scope() {
	Saved s;
	s.moves = Moves::save_state();
	s.rules = Interactions::save_state();
	s.status = Status::specs().duplicate(true);
	s.ticks = Hooks::body_ticks();
	s.zones = Hooks::zone_effects();
	s.previews = Hooks::tech_previews();
	saved_ = std::move(s);
}

void SimHarness::end_scope() {
	if (!saved_) return;
	Moves::load_state(saved_->moves);
	Interactions::load_state(saved_->rules);
	Status::specs() = saved_->status;
	Hooks::body_ticks() = saved_->ticks;
	Hooks::zone_effects() = saved_->zones;
	Hooks::tech_previews() = saved_->previews;
	saved_.reset();
}

void SimHarness::legacy_bindings_only() { Moves::legacy_bindings_only(); }

}  // namespace fft
