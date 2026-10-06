// Fourfold core - port of game/core/combat_world.gd: intents and actions (press buffer, cancels, chains, morphs,
// phases, module dispatch).
#include "Sim/CombatWorld.h"

#include "Combat/Acts.h"
#include "Combat/Kits/Kits.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

namespace ff {

std::string CombatWorld::gesture_slot(int g) {
	switch (g) {
		case static_cast<int>(Gesture::Up): return "thrust";
		case static_cast<int>(Gesture::Down): return "ground";
		case static_cast<int>(Gesture::Side): return "sweep";
		default: return "";
	}
}

void CombatWorld::_process_intent(ActorState& a, const ActorIntent& it) {
	if (a.is_dummy) return;
	if (it.element_select >= 0 && it.element_select < 4 && a.elements[static_cast<size_t>(it.element_select)] &&
	    a.element != it.element_select) {
		a.element = it.element_select;
		emit("element", D({{"actor", a.id}, {"element", a.element}, {"sub", a.sub()}}));
	}
	if (it.sub_select >= 0 && it.sub_select < 4 && a.subs_unlocked[static_cast<size_t>(a.element)][static_cast<size_t>(it.sub_select)] &&
	    a.sub() != it.sub_select) {
		a.subs[static_cast<size_t>(a.element)] = it.sub_select;
		emit("element", D({{"actor", a.id}, {"element", a.element}, {"sub", a.sub()}}));
	}
	if (it.target_cycle) _cycle_target(a);
	if (a.lock_target < 0 || !_valid_target(a, a.lock_target)) a.lock_target = _auto_target(a);
	if (it.tech_cancel && a.buffered == "tech") a.buffered = "";
	std::string press;
	if (it.evade_pressed) press = "evade";
	else if (it.guard_pressed) press = "guard";
	else if (it.tech_pressed && !it.tech_cancel) press = "tech";
	else if (it.attack_pressed) press = "attack";
	const std::string slot = press == "attack" ? gesture_slot(it.attack_gesture) : "";
	if (!press.empty()) {
		if (_try_start(a, press, it, slot)) {
			a.buffered = "";
		} else {
			a.buffered = press;
			a.buffered_tick = tick;
			a.buffered_slot = slot;
		}
	} else if (!a.buffered.empty() && tick - a.buffered_tick <= static_cast<int64_t>(Moves::BUFFER_TIME * Sim::HZ)) {
		const std::string buf = a.buffered;
		const std::string bslot = a.buffered_slot;
		if (_try_start(a, buf, it, bslot)) a.buffered = "";
	} else {
		a.buffered = "";
	}
	if (it.guard_held && a.action == nullptr && a.stun <= 0.0) _try_start(a, "guard", it);
	if (a.action != nullptr && a.stun <= 0.0) {
		if (it.attack_gesture != 0 && press != "attack") _gesture_morph(a, it);
		if (it.guard_gesture == static_cast<int>(Gesture::Up) || it.guard_gesture == static_cast<int>(Gesture::Down)) {
			if (a.action != nullptr) _guard_gesture(a, it);
		}
		if (a.action != nullptr && a.action->slot == "evade") _evade_hold(a, it);
	}
}

void CombatWorld::_gesture_morph(ActorState& a, const ActorIntent& it) {
	ActionRef inst = a.action;
	if (dtruthy(inst->data, "morphed") || !Sim::is_attack_slot(inst->slot)) return;
	const std::string slot = gesture_slot(it.attack_gesture);
	if (slot.empty() || slot == inst->slot) return;
	const bool in_startup = inst->phase == ActionPhase::Startup && inst->total <= MORPH_WINDOW + 1e-6;
	const bool at_release = inst->phase == ActionPhase::Charge;
	if (!in_startup && !at_release) return;
	const std::string id = Moves::resolve(inst->element, inst->sub, slot);
	if (id.empty() || id == inst->id) return;
	morph_action(a, id, slot, it, at_release);
}

ActionRef CombatWorld::morph_action(ActorState& a, const std::string& id, const std::string& slot, const ActorIntent& it, bool at_release,
                                    const Dict& extra) {
	ActionRef inst = a.action;
	if (inst == nullptr || !Moves::defs().has(id)) return nullptr;
	const double paid_focus =
		maxf(0.0, dnum(inst->data, "focus0", a.focus) - a.focus - dnum(inst->data, "heat_paid", 0.0) / Sim::HU_PER_FOCUS) +
		dnum(inst->data, "paid_focus", 0.0);
	Dict pre = D({{"slot", slot},
	              {"morphed", true},
	              {"morph_from", inst->id},
	              {"element", inst->element},
	              {"sub", inst->sub},
	              {"paid_focus", paid_focus},
	              {"paid_hu", dnum(inst->data, "heat_paid", 0.0)},
	              {"heat_paid", dnum(inst->data, "heat_paid", 0.0)},
	              {"tier", inst->tier()},
	              {"charge_t", dnum(inst->data, "charge_t", inst->total)},
	              {"morph_body", a.held_body}});
	if (at_release) {
		pre.set("released", true);
		pre.set("morph_release", true);
	}
	pre.merge(extra, true);
	const double total = inst->total;
	inst->interrupted = true;
	module_interrupt(dstr(inst->def, "module"), a, *inst, "morph");
	a.action = nullptr;
	emit("morph", D({{"actor", a.id}, {"from", inst->id}, {"to", id}, {"slot", slot}, {"tier", inst->tier()},
	                 {"at", at_release ? "release" : "startup"}}));
	ActionRef n = start_action(a, id, it, pre);
	if (n != nullptr && a.action == n) {
		n->total = total;
		n->heavy = inst->heavy;
		if (at_release && n->phase == ActionPhase::Startup) _enter_after_startup(a, *n, it);
	}
	return n;
}

void CombatWorld::_guard_gesture(ActorState& a, const ActorIntent& it) {
	ActionRef inst = a.action;
	if (inst->id != "guard" || inst->phase != ActionPhase::Channel) return;
	const std::string slot = it.guard_gesture == static_cast<int>(Gesture::Up) ? "push" : "sink";
	const std::string id = Moves::resolve(inst->element, inst->sub, slot);
	if (id.empty() || !Moves::defs().has(id)) return;
	Dict pre = D({{"slot", slot},
	              {"from_guard", true},
	              {"guard_t", inst->total},
	              {"tier", inst->tier()},
	              {"guard_spec", inst->data.get("spec", Value(""))},
	              {"wall", a.wall_body},
	              {"keep_wall", a.wall_body},
	              {"held", a.held_body},
	              {"element", inst->element},
	              {"sub", inst->sub},
	              {"charge_t", dnum(inst->data, "charge_t", inst->total)},
	              {"charge_frozen", true}});
	inst->interrupted = true;
	module_interrupt(dstr(inst->def, "module"), a, *inst, slot);
	a.guarding = false;
	a.action = nullptr;
	emit("morph", D({{"actor", a.id}, {"from", "guard"}, {"to", id}, {"slot", slot}, {"tier", inst->tier()}, {"at", "guard"}}));
	start_action(a, id, it, pre);
}

void CombatWorld::_evade_hold(ActorState& a, const ActorIntent& it) {
	ActionRef inst = a.action;
	if (dtruthy(inst->data, "morphed") || dtruthy(inst->data, "evade_released")) return;
	if (!it.evade_held) {
		inst->data.set("evade_released", true);
		return;
	}
	if (inst->total + 1e-6 < EVADE_HOLD_TIME) return;
	inst->data.set("morphed", true);
	const std::string id = Moves::resolve(inst->element, inst->sub, "evade_hold");
	if (id.empty() || id == inst->id || !Moves::defs().has(id)) return;
	morph_action(a, id, "evade_hold", it);
}

bool CombatWorld::can_cancel(ActorState& a, const std::string& into, const std::string& slot) {
	if (a.stun > 0.0) return false;
	if (a.action == nullptr) return true;
	ActionRef inst = a.action;
	const Dict& d = inst->def;
	if (inst->id == "guard") return into != "guard";
	if (inst->phase == ActionPhase::Channel || inst->phase == ActionPhase::Charge) return into == "guard" || into == "evade";
	if (inst->phase == ActionPhase::Recovery) {
		const double rec = dnum(d, "recovery") * Status::recovery_mult(a);
		if (d.has("cancel") && (rec <= 0.0 || inst->t / rec >= dnum(d, "cancel")) && (into == "guard" || into == "evade")) return true;
		if (into == "attack" && d.has("chain") && _chain_ok(a, *inst, !slot.empty() ? slot : "strike", rec)) return true;
		if ((into == "tech" || into == "guard") && dtruthy(d, "counter_cancel") && _counter_cancel_ok(a, into)) return true;
	}
	return false;
}

bool CombatWorld::_chain_ok(ActorState& a, ActionInst& inst, const std::string& slot, double rec) {
	const Dict& d = inst.def;
	const double frac = rec > 0.0 ? inst.t / rec : 1.0;
	const bool contact = dtruthy(inst.data, "contact");
	const double need = contact ? dnum(d, "chain") : dnum(d, "cancel", 1.0);
	if (frac < need - 1e-6) return false;
	Dict& ch = a.chain;
	if (dint(ch, "n", 0) >= CHAIN_MAX || darr(ch, "slots").has(Value(slot))) return false;
	const bool weave = a.element != inst.element || a.sub() != inst.sub;
	if (weave) {
		if (dtruthy(ch, "weaved") || !contact || a.focus < WEAVE_COST) return false;
	}
	ch.set("pending", D({{"weave", weave}}));
	return true;
}

bool CombatWorld::_counter_cancel_ok(ActorState& a, const std::string& into) {
	if (a.focus < COUNTER_CANCEL_COST || incoming_threat(a, COUNTER_CANCEL_WINDOW) == nullptr) return false;
	if (into == "tech") {
		const Dict pv = tech_preview(a, aim_dir(a, _intent_for(a)));
		if (!dtruthy(pv, "ok")) return false;
	}
	a.chain.set("counter_cancel", true);
	return true;
}

MatBody* CombatWorld::incoming_threat(ActorState& a, double within) {
	MatBody* best = nullptr;
	double best_t = kInf;
	for (const BodyRef& bp : bodies) {
		MatBody& b = *bp;
		if (!b.alive || !b.is_projectile() || b.attack_owner == a.id || b.vel.length() < 0.5f) continue;
		const Vec3 rel = a.chest() - b.pos;
		const Vec3 v = b.vel;
		const double tc = clampf(rel.dot(v) / maxf(v.length_squared(), 1e-6), 0.0, within);
		if ((b.pos + v * tc).distance_to(a.chest()) < 1.2 + b.radius && tc < best_t) {
			best_t = tc;
			best = &b;
		}
	}
	return best;
}

bool CombatWorld::_try_start(ActorState& a, const std::string& press, const ActorIntent& it, const std::string& slot) {
	if (!can_cancel(a, press, slot)) return false;
	const Dict pending = ddict(a.chain, "pending");
	a.chain.erase("pending");
	const bool counter_cancel = dtruthy(a.chain, "counter_cancel");
	a.chain.erase("counter_cancel");
	if (a.action != nullptr) interrupt_action(a, "cancel:" + press);
	if (counter_cancel) {
		spend_focus(a, COUNTER_CANCEL_COST);
		emit("counter_cancel", D({{"actor", a.id}, {"into", press}}));
	}
	if (press == "evade") {
		std::string id = Moves::resolve(a.element, a.sub(), "evade");
		if (id.empty() || !Moves::defs().has(id)) id = "evade";
		if (a.focus < dnum(Moves::defs().get(id).as_dict(), "cost", 0.0) * 0.5) {
			emit("insufficient", D({{"actor", a.id}, {"what", "focus"}}));
			id = "evade";
		}
		start_action(a, id, it, D({{"slot", "evade"}}));
	} else if (press == "guard") {
		start_action(a, "guard", it, D({{"slot", "guard"}, {"spec", Moves::resolve(a.element, a.sub(), "guard")}}));
	} else if (press == "tech") {
		const std::string tid = Moves::resolve(a.element, a.sub(), "tech");
		if (!tid.empty() && Moves::defs().has(tid)) start_action(a, tid, it, D({{"slot", "tech"}}));
	} else if (press == "attack") {
		std::string sl = !slot.empty() ? slot : "strike";
		std::string aid = Moves::resolve(a.element, a.sub(), sl);
		if ((aid.empty() || !Moves::defs().has(aid)) && sl != "strike") {
			sl = "strike";
			aid = Moves::resolve(a.element, a.sub(), sl);
		}
		if (aid.empty() || !Moves::defs().has(aid)) return true;
		if (pending.empty()) {
			a.chain = D({{"n", 1}, {"slots", A({sl})}, {"weaved", false}});
		} else {
			a.chain.set("n", dint(a.chain, "n", 0) + 1);
			Array slots = darr(a.chain, "slots");
			if (!a.chain.has("slots")) a.chain.set("slots", slots);
			slots.append(sl);
			if (dtruthy(pending, "weave")) {
				a.chain.set("weaved", true);
				spend_focus(a, WEAVE_COST);
				emit("weave", D({{"actor", a.id}, {"element", a.element}, {"sub", a.sub()}}));
			}
			emit("chain", D({{"actor", a.id}, {"n", a.chain.get("n")}, {"slot", sl}}));
		}
		start_action(a, aid, it, D({{"slot", sl}}));
	}
	return true;
}

ActionRef CombatWorld::start_action(ActorState& a, const std::string& id, const ActorIntent& it, const Dict& pre) {
	ActionRef inst = std::make_shared<ActionInst>();
	inst->id = id;
	inst->def = Moves::defs().get(id).as_dict();
	inst->element = vint(pre.get("element", inst->def.get("element", Value(a.element))));
	inst->sub = vint(pre.get("sub", Value(a.sub_of(inst->element))));
	inst->slot = dstr(pre, "slot", "");
	inst->attack_id = new_attack_id();
	inst->data.merge(pre, true);
	if (!inst->data.has("focus0")) {
		inst->data.set("focus0", a.focus);
		inst->data.set("reserve0", a.heat_reserve);
	}
	if (pre.has("spec") && id == "guard") {
		const std::string sp = dstr(pre, "spec");
		const Dict spec_def = !sp.empty() ? Moves::defs().get(sp).as_dict() : Dict();
		inst->data.set("spec_def", spec_def);
		inst->data.set("spec_module", sp != id ? dstr(spec_def, "module", "common") : std::string("common"));
	}
	a.action = inst;
	a.attack_hold = 0.0;
	emit("action", D({{"actor", a.id}, {"move", id}, {"phase", "startup"}, {"sub", inst->sub}, {"slot", inst->slot}, {"tier", inst->tier()}}));
	module_start(dstr(inst->def, "module"), a, *inst, it);
	if (a.action == inst && inst->phase == ActionPhase::Startup && dnum(inst->def, "startup") <= 0.0) _enter_after_startup(a, *inst, it);
	return inst;
}

void CombatWorld::set_phase(ActorState& a, ActionInst& inst, ActionPhase p) {
	inst.phase = p;
	inst.t = 0.0;
	emit("action", D({{"actor", a.id}, {"move", inst.id}, {"phase", inst.phase_name()}, {"heavy", inst.heavy}, {"sub", inst.sub},
	                  {"slot", inst.slot}, {"tier", inst.tier()}}));
	if (p == ActionPhase::Done) {
		if (a.action.get() == &inst) a.action = nullptr;
		return;
	}
	module_phase(dstr(inst.def, "module"), a, inst, p);
}

void CombatWorld::finish_action(ActorState& a, ActionInst& inst) { set_phase(a, inst, ActionPhase::Done); }

void CombatWorld::interrupt_action(ActorState& a, const std::string& reason) {
	ActionRef inst = a.action;
	if (inst == nullptr) return;
	inst->interrupted = true;
	module_interrupt(dstr(inst->def, "module"), a, *inst, reason);
	a.guarding = false;
	a.gliding = false;
	emit("interrupt", D({{"actor", a.id}, {"move", inst->id}, {"reason", reason}}));
	a.action = nullptr;
}

void CombatWorld::_enter_after_startup(ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const ActionPhase nxt = module_after_startup(dstr(inst.def, "module"), a, inst, it);
	if (a.action.get() == &inst && nxt != ActionPhase::Startup) set_phase(a, inst, nxt);
}

void CombatWorld::_tick_action(ActorState& a, const ActorIntent& it) {
	ActionRef inst = a.action;
	if (inst == nullptr) return;
	inst->t += Sim::DT;
	inst->total += Sim::DT;
	if (it.attack_held) a.attack_hold += Sim::DT;
	const std::string module = dstr(inst->def, "module");
	switch (inst->phase) {
		case ActionPhase::Startup:
			module_tick(module, a, *inst, it);
			if (a.action == inst && inst->phase == ActionPhase::Startup && inst->total >= _startup_of(a, *inst) - 1e-6)
				_enter_after_startup(a, *inst, it);
			break;
		case ActionPhase::Charge:
		case ActionPhase::Channel:
			Charge::tick(*this, a, *inst, it);
			if (a.action == inst) module_tick(module, a, *inst, it);
			break;
		case ActionPhase::Active:
			module_tick(module, a, *inst, it);
			if (a.action == inst && inst->phase == ActionPhase::Active && inst->t >= _active_of(*inst)) set_phase(a, *inst, ActionPhase::Recovery);
			break;
		case ActionPhase::Recovery:
			module_tick(module, a, *inst, it);
			if (a.action == inst && inst->phase == ActionPhase::Recovery && inst->t >= dnum(inst->def, "recovery") * Status::recovery_mult(a))
				finish_action(a, *inst);
			break;
		default: break;
	}
}

double CombatWorld::_startup_of(const ActorState&, const ActionInst& inst) const {
	return dnum(inst.data, "startup", dnum(inst.def, "startup"));
}

double CombatWorld::_active_of(const ActionInst& inst) const { return dnum(inst.data, "active", dnum(inst.def, "active")); }

// ---- module dispatch (explicit, no reflection)

void CombatWorld::module_start(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (module == "common") ActCommon::on_start(*this, a, inst, it);
	else if (module == "earth") ActEarth::on_start(*this, a, inst, it);
	else if (module == "water") ActWater::on_start(*this, a, inst, it);
	else if (module == "fire") ActFire::on_start(*this, a, inst, it);
	else if (module == "air") ActAir::on_start(*this, a, inst, it);
	else if (module == "verbs") Verbs::on_start(*this, a, inst, it);
	else if (module == "kit_earth") KitEarth::on_start(*this, a, inst, it);
	else if (module == "kit_water") KitWater::on_start(*this, a, inst, it);
	else if (module == "kit_fire") KitFire::on_start(*this, a, inst, it);
	else if (module == "kit_air") KitAir::on_start(*this, a, inst, it);
}

ActionPhase CombatWorld::module_after_startup(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (module == "common") return ActCommon::after_startup(*this, a, inst, it);
	if (module == "earth") return ActEarth::after_startup(*this, a, inst, it);
	if (module == "water") return ActWater::after_startup(*this, a, inst, it);
	if (module == "fire") return ActFire::after_startup(*this, a, inst, it);
	if (module == "air") return ActAir::after_startup(*this, a, inst, it);
	if (module == "verbs") return Verbs::after_startup(*this, a, inst, it);
	if (module == "kit_earth") return KitEarth::after_startup(*this, a, inst, it);
	if (module == "kit_water") return KitWater::after_startup(*this, a, inst, it);
	if (module == "kit_fire") return KitFire::after_startup(*this, a, inst, it);
	if (module == "kit_air") return KitAir::after_startup(*this, a, inst, it);
	return ActionPhase::Active;
}

void CombatWorld::module_phase(const std::string& module, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (module == "common") ActCommon::on_phase(*this, a, inst, p);
	else if (module == "earth") ActEarth::on_phase(*this, a, inst, p);
	else if (module == "water") ActWater::on_phase(*this, a, inst, p);
	else if (module == "fire") ActFire::on_phase(*this, a, inst, p);
	else if (module == "air") ActAir::on_phase(*this, a, inst, p);
	else if (module == "verbs") Verbs::on_phase(*this, a, inst, p);
	else if (module == "kit_earth") KitEarth::on_phase(*this, a, inst, p);
	else if (module == "kit_water") KitWater::on_phase(*this, a, inst, p);
	else if (module == "kit_fire") KitFire::on_phase(*this, a, inst, p);
	else if (module == "kit_air") KitAir::on_phase(*this, a, inst, p);
}

void CombatWorld::module_tick(const std::string& module, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (module == "common") ActCommon::on_tick(*this, a, inst, it);
	else if (module == "earth") ActEarth::on_tick(*this, a, inst, it);
	else if (module == "water") ActWater::on_tick(*this, a, inst, it);
	else if (module == "fire") ActFire::on_tick(*this, a, inst, it);
	else if (module == "air") ActAir::on_tick(*this, a, inst, it);
	else if (module == "verbs") Verbs::on_tick(*this, a, inst, it);
	else if (module == "kit_earth") KitEarth::on_tick(*this, a, inst, it);
	else if (module == "kit_water") KitWater::on_tick(*this, a, inst, it);
	else if (module == "kit_fire") KitFire::on_tick(*this, a, inst, it);
	else if (module == "kit_air") KitAir::on_tick(*this, a, inst, it);
}

void CombatWorld::module_interrupt(const std::string& module, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (module == "common") ActCommon::on_interrupt(*this, a, inst, reason);
	else if (module == "earth") ActEarth::on_interrupt(*this, a, inst, reason);
	else if (module == "water") ActWater::on_interrupt(*this, a, inst, reason);
	else if (module == "fire") ActFire::on_interrupt(*this, a, inst, reason);
	else if (module == "air") ActAir::on_interrupt(*this, a, inst, reason);
	else if (module == "verbs") Verbs::on_interrupt(*this, a, inst, reason);
	else if (module == "kit_earth") KitEarth::on_interrupt(*this, a, inst, reason);
	else if (module == "kit_water") KitWater::on_interrupt(*this, a, inst, reason);
	else if (module == "kit_fire") KitFire::on_interrupt(*this, a, inst, reason);
	else if (module == "kit_air") KitAir::on_interrupt(*this, a, inst, reason);
}

ActionPhase CombatWorld::attack_after_startup(ActorState&, ActionInst& inst, const ActorIntent& it) {
	const bool decided_light = !it.attack_held || dtruthy(inst.data, "released");
	if (decided_light) {
		inst.heavy = false;
		return ActionPhase::Active;
	}
	if (inst.total < Moves::HOLD_THRESHOLD - 1e-6) {
		inst.data.set("startup", Moves::HOLD_THRESHOLD);
		return ActionPhase::Startup;
	}
	inst.heavy = true;
	return ActionPhase::Charge;
}

}  // namespace ff
