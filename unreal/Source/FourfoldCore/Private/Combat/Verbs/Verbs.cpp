// Fourfold core - port of game/combat/verbs/verbs.gd: verb lifecycle (module "verbs"), costs and shared helpers.
#include "Combat/Verbs.h"

#include "Combat/Moves.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Hooks.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

namespace ff {

// ------------------------------------------------------------------ VerbParams
Value VerbParams::get(std::string_view k, const Value& dflt) const {
	if (over.has(k)) return over.get(k);
	if (inst == nullptr) return dflt;
	return Charge::param(*inst, k, dflt);
}
int VerbParams::i(std::string_view k, int dflt) const { return vint(get(k, Value(dflt)), dflt); }
bool VerbParams::b(std::string_view k, bool dflt) const { return vbool(get(k, Value(dflt)), dflt); }
std::string VerbParams::s(std::string_view k, std::string_view dflt) const { return vstr(get(k, Value(std::string(dflt))), dflt); }

namespace Verbs {
namespace {
const char* const kChannelVerbs[] = {"grip", "summon", "ranged_heat", "mode"};
const char* const kDefaultMat[] = {"stone", "water", "flame", "wind"};
}  // namespace

bool is_channel_verb(std::string_view verb) { return in_list(verb, kChannelVerbs); }

void ensure_ready() {}

// ------------------------------------------------------------------ lifecycle

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		VerbBarrier::start(w, a, inst, it);
		return;
	}
	const Dict& d = inst.def;
	const std::string verb = dstr(d, "verb", "");
	inst.data.set("face", w.aim_dir(a, it));
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("aim_point", w.aim_point(a, it));
	if (!pay(w, a, inst, "start")) {
		inst.data.set("fizzle", true);
		return;
	}
	fx(w, a, inst, "cast");
	if (verb == "dash") VerbMotion::dash_start(w, a, inst, it);
	else if (verb == "stance") VerbMotion::stance_start(w, a, inst);
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") return ActionPhase::Channel;
	if (dtruthy(inst.data, "fizzle")) return ActionPhase::Recovery;
	const Dict& d = inst.def;
	const std::string verb = dstr(d, "verb", "");
	if (is_channel_verb(verb)) {
		if (verb == "summon") VerbZone::summon_start(w, a, inst, it);
		else if (verb == "ranged_heat") VerbHeat::start(w, a, inst, it);
		else if (verb == "mode") VerbMotion::mode_start(w, a, inst);
		return (a.action.get() == &inst && !dtruthy(inst.data, "fizzle")) ? ActionPhase::Channel : ActionPhase::Recovery;
	}
	if (verb == "stance") return dbool(d, "held", true) ? ActionPhase::Channel : ActionPhase::Active;
	if (verb == "dash") return ActionPhase::Active;
	if (dtruthy(inst.data, "morph_release")) return ActionPhase::Active;
	if (Sim::is_attack_slot(inst.slot) && Charge::max_tier(d) > 0 && !dtruthy(inst.data, "released"))
		return w.attack_after_startup(a, inst, it);
	return ActionPhase::Active;
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "guard") {
		if (p == ActionPhase::Recovery) VerbBarrier::end(w, a, inst, "release");
		return;
	}
	if (p == ActionPhase::Active) {
		execute(w, a, inst);
	} else if (p == ActionPhase::Recovery) {
		w.ledger.spent += take_heat(inst);
		const std::string verb = dstr(inst.def, "verb", "");
		if (verb == "mode") VerbMotion::mode_end(w, a, inst);
		else if (verb == "stance") VerbMotion::stance_end(w, a, inst);
		else if (verb == "summon") VerbZone::summon_release(w, a, inst);
		else if (verb == "ranged_heat") VerbHeat::end(w, a, inst);
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		VerbBarrier::tick(w, a, inst, it);
		return;
	}
	const Dict d = inst.def;
	if (const TickHook hook = Hooks::tick_of(d.get("hook_tick"))) {
		hook(w, a, inst, it);
		if (a.action.get() != &inst) return;
	}
	if (Sim::is_attack_slot(inst.slot)) {
		if (!it.attack_held) inst.data.set("released", true);
		if (inst.phase != ActionPhase::Active) {
			inst.data.set("face", w.aim_dir(a, it));
			inst.data.set("aim", inst.data.get("face"));
			inst.data.set("aim_point", w.aim_point(a, it));
		}
	}
	const std::string verb = dstr(d, "verb", "");
	switch (inst.phase) {
		case ActionPhase::Charge: {
			const double t1 = Charge::tier_times(d)[0];
			if (dtruthy(inst.data, "released") && inst.total >= t1 - 1e-6) {
				inst.data.set("tier", maxi(1, inst.tier()));
				if (!pay(w, a, inst, "release")) {
					inst.data.set("tier", 0);
					w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "tier"}}));
				}
				inst.heavy = inst.tier() > 0;
				w.set_phase(a, inst, ActionPhase::Active);
			}
			break;
		}
		case ActionPhase::Channel:
			if (verb == "grip") VerbGrip::tick(w, a, inst, it);
			else if (verb == "summon") VerbZone::summon_tick(w, a, inst, it);
			else if (verb == "ranged_heat") VerbHeat::tick(w, a, inst, it);
			else if (verb == "mode") VerbMotion::mode_tick(w, a, inst, it);
			else if (verb == "stance") VerbMotion::stance_tick(w, a, inst, it);
			break;
		case ActionPhase::Active:
			if (verb == "dash") VerbMotion::dash_tick(w, a, inst);
			else if (verb == "beam") VerbVolume::beam_tick(w, a, inst);
			else if (verb == "stance") VerbMotion::stance_tick(w, a, inst, it);
			break;
		default: break;
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (reason != "morph") w.ledger.spent += take_heat(inst);
	if (inst.id == "guard") {
		VerbBarrier::end(w, a, inst, reason);
		return;
	}
	const std::string verb = dstr(inst.def, "verb", "");
	if (verb == "grip") VerbGrip::drop(w, a, inst);
	else if (verb == "mode") VerbMotion::mode_end(w, a, inst);
	else if (verb == "stance") VerbMotion::stance_end(w, a, inst);
	else if (verb == "summon") VerbZone::summon_release(w, a, inst);
	else if (verb == "ranged_heat") VerbHeat::end(w, a, inst);
	else if (verb == "dash") inst.data.set("controls_motion", false);
}

void execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Dict d = inst.def;
	if (const ExecHook hook = Hooks::exec_of(d.get("hook_execute")))
		if (hook(w, a, inst)) return;
	const std::string verb = dstr(d, "verb", "");
	if (verb == "projectile") VerbProjectile::fire(w, a, inst);
	else if (verb == "cone") VerbVolume::cone(w, a, inst);
	else if (verb == "beam") VerbVolume::beam(w, a, inst);
	else if (verb == "burst") VerbVolume::burst(w, a, inst);
	else if (verb == "ground_line") VerbGroundLine::launch(w, a, inst);
	else if (verb == "zone") VerbZone::spawn(w, a, inst);
	else if (verb == "barrier") VerbBarrier::raise_free(w, a, inst);
}

// ------------------------------------------------------------------ costs

bool pay(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& stage) {
	const Dict d = Charge::pdef(inst);
	Dict need;
	if (stage == "start") {
		need = D({{"focus", Charge::pgetf(d, 0, "cost", 0.0)},
		          {"heat", Charge::pgetf(d, 0, "heat", 0.0)},
		          {"water", Charge::pgetf(d, 0, "water", 0.0)},
		          {"metal", Charge::pgetf(d, 0, "metal", 0.0)}});
	} else {
		const int tier = inst.tier();
		double add = 0.0;
		if (tier >= 1)
			add = Charge::paramf(inst, "cost_add", maxf(0.0, vnum(d.get("heavy_cost", d.get("cost", Value(0.0)))) - dnum(d, "cost", 0.0)));
		need = D({{"focus", add}, {"heat", Charge::paramf(inst, "heat_add", 0.0)}});
	}
	return pay_dict(w, a, inst, need);
}

bool pay_dict(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& need) {
	double focus = dnum(need, "focus", 0.0);
	if (inst.slot == "tech") focus *= Status::tech_cost_mult(a);
	const double credit = dnum(inst.data, "paid_focus", 0.0);
	const double use = minf(credit, focus);
	focus -= use;
	double hu = dnum(need, "heat", 0.0);
	const double credit_h = dnum(inst.data, "paid_hu", 0.0);
	const double use_h = minf(credit_h, hu);
	hu -= use_h;
	const double water = dnum(need, "water", 0.0);
	const double metal = dnum(need, "metal", 0.0);
	if (a.focus + 1e-6 < focus || (!w.can_pay_heat(a, hu + focus * Sim::HU_PER_FOCUS) && hu > 0.0)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		return false;
	}
	if (water > 0.0 && a.water_carried + 1e-6 < water && !a.in_water) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return false;
	}
	if (metal > 0.0 && a.metal_carried + 1e-6 < metal) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "metal"}, {"move", inst.id}}));
		return false;
	}
	inst.data.set("paid_focus", credit - use);
	inst.data.set("paid_hu", credit_h - use_h);
	w.spend_focus(a, focus);
	if (hu > 0.0) {
		const double paid = w.pay_heat(a, hu, false);
		inst.data.set("heat_paid", dnum(inst.data, "heat_paid", 0.0) + paid);
	}
	if (water > 0.0 && !a.in_water) {
		a.water_carried -= water;
		inst.data.set("water_paid", dnum(inst.data, "water_paid", 0.0) + water);
	} else if (water > 0.0) {
		_take_pool(w, a, water);
		inst.data.set("water_paid", dnum(inst.data, "water_paid", 0.0) + water);
	}
	if (metal > 0.0) {
		a.metal_carried -= metal;
		inst.data.set("metal_paid", dnum(inst.data, "metal_paid", 0.0) + metal);
	}
	return true;
}

void _take_pool(CombatWorld& w, ActorState& a, double kg) {
	const double take = minf(kg, w.pool->mass);
	w.ledger.removed += take * (Sim::WATER_C * (w.pool->temp - Sim::AMBIENT_C) - Sim::WATER_LATENT_FUSION * (1.0 - w.pool->liquid));
	w.pool->mass -= take;
	a.water_carried += take;
}

double take_heat(ActionInst& inst) {
	const double hu = dnum(inst.data, "heat_paid", 0.0);
	inst.data.set("heat_paid", 0.0);
	return hu;
}

// ------------------------------------------------------------------ helpers

Mat mat_id(const Value& name) {
	if (name.is_int()) return static_cast<Mat>(name.as_int());
	const int i = Sim::mat_index(vstr(name));
	return i >= 0 ? static_cast<Mat>(i) : Mat::Stone;
}

std::string fx_mat(const ActionInst& inst) {
	const Value f = Charge::pdef(inst).get("fx");
	if (f.has("mat")) return vstr(f.get("mat"));
	return kDefaultMat[clampi(inst.element, 0, 3)];
}

void fx(CombatWorld& w, const ActorState& a, const ActionInst& inst, const std::string& key, const Dict& extra) {
	const Value f = Charge::pdef(inst).get("fx");
	const std::string fx_key = vstr(f.get(key, Value(key)));
	if (fx_key.empty() || !FxEvents::is_known("fx", fx_key)) return;
	Dict d = D({{"shape", vstr(f.get("shape", Value("")))}});
	d.merge(extra, true);
	FxEvents::fx_for(w, a, inst, fx_key, fx_mat(inst), d);
}

Vec3 target_point(CombatWorld& w, const ActorState& a, const ActionInst& inst, double reach) {
	const Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", Value(a.forward()))));
	const ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr) {
		Vec3 to = t->pos - a.pos;
		to.y = 0.0f;
		if (to.length() < 0.1f || to.normalized().dot(dir) > 0.9f || !dtruthy(inst.data, "aim_active"))
			return t->chest() + Vec3(t->vel.x, 0.0f, t->vel.z) * 0.2f;
	}
	return a.chest() + dir * reach;
}

Vec3 launch_vel(Vec3 from, Vec3 to, double h_speed, double gscale) {
	const Vec3 d = to - from;
	const Vec3 fl(d.x, 0.0f, d.z);
	const double dist = fl.length();
	if (dist < 0.01) return V3(0, h_speed, 0);
	const double t = dist / h_speed;
	const double vy = d.y / t + 0.5 * Sim::GRAVITY * gscale * t;
	return fl / dist * h_speed + V3(0, vy, 0);
}

void arm(CombatWorld& w, const ActorState& a, const ActionInst& inst, MatBody& b, double damage, double balance) {
	b.controller = -1;
	b.authority = 0.0;
	b.attack_id = w.new_attack_id();
	b.attack_owner = a.id;
	b.hit_set.clear();
	b.hit_set.add(a.id);
	b.damage = damage;
	b.balance_damage = balance;
	b.tier = inst.tier();
	b.sub = inst.sub;
	b.residual_owner = a.id;
	b.residual_authority = Interactions::cohesion(b.tier);
	b.props.set("src_attack", inst.attack_id);
	b.props.set("move", inst.id);
	b.touch(a.id, "release", w.tick);
}

void on_impact(CombatWorld& w, MatBody& b, const std::string& what) { VerbProjectile::on_impact(w, b, what); }
void on_wave_end(CombatWorld& w, MatBody& b, const std::string& why) { VerbGroundLine::on_end(w, b, why); }
void leave_trail(CombatWorld& w, MatBody& b) { VerbGroundLine::leave_trail(w, b); }
Dict grip_preview(CombatWorld& w, ActorState& a, const Dict& d, Vec3 dir) { return VerbGrip::preview(w, a, d, dir); }

}  // namespace Verbs
}  // namespace ff
