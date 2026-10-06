// Fourfold core - port of game/combat/act_earth.gd: Earth stone shot (tap) / heavy heave (hold), seize-aim-throw
// technique; T2 Boulder / T3 Crag Breaker ladder (tier data in the live def) and T+A Split during the technique.
#include "Combat/Acts.h"

#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace ActEarth {

bool _stone_filter(MatBody& b) {
	// Legality through the engine (legacy cells stone* x grip_stone: reclaim).
	return b.form != Form::Wall && b.phase != Phase::Molten && b.form != Form::Wave && Interactions::allows(b, "grip_stone");
}

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	if (inst.id == "earth_attack") {
		if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
			_fizzle(w, a, inst, "focus");
			return;
		}
		// Prefer a loose stone at the feet (reuse material), else rip one from the ground.
		MatBody* loose = _loose_stone_near(w, a);
		if (loose != nullptr)
			w.take_control(a, *loose, 0.9, "acquire");
		else
			_rip(w, a, Sim::STONE_SHOT_MASS);
		w.emit("telegraph", D({{"actor", a.id}, {"move", "earth_attack"}, {"body", a.held_body}, {"time", inst.def.get("startup")}}));
		Verbs::fx(w, a, inst, "cast", D({{"body", a.held_body}}));
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "earth_attack") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		return w.attack_after_startup(a, inst, it);
	}
	return ActionPhase::Channel;
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Charge && inst.id == "earth_attack") {
		// Heavy: gather more stone from the ground (mass budget, extra Focus).
		MatBody* b = w.held(a);
		const double extra = dnum(inst.def, "heavy_mass") - (b != nullptr ? b->mass : 0.0);
		if (b != nullptr && extra > 0.0 && w.spend_focus(a, dnum(inst.def, "heavy_cost") - dnum(inst.def, "cost"))) {
			// Ground stone arrives at ambient: total heat is unchanged, so temperature dilutes.
			const double e = b->thermal_energy();
			b->mass += extra;
			w._set_energy(*b, e);
			b->update_radius();
			w.mass_ledger.ground_taken += extra;
			w.emit("acquire", D({{"actor", a.id}, {"body", b->id}, {"mass", extra}}));
		} else if (extra > 0.0) {
			// The heave can't be paid: it stays a light shot, mass-scaled as usual.
			inst.heavy = false;
			if (b != nullptr) w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "earth_heavy"}}));
		}
		if (inst.heavy)
			w.emit("telegraph", D({{"actor", a.id}, {"move", "earth_heavy"}, {"body", a.held_body}, {"time", inst.def.get("heavy_min")}}));
	} else if (p == ActionPhase::Active) {
		if (inst.id == "earth_tech" && inst.data.has("split")) {
			_throw_split(w, a, inst);
			return;
		}
		MatBody* b = w.held(a);
		if (b == nullptr) return;
		const bool heavy = inst.heavy;
		double spd = heavy ? vnum(inst.def.get("heavy_speed", inst.def.get("speed"))) : dnum(inst.def, "speed");
		double dmg = heavy ? vnum(inst.def.get("heavy_damage", inst.def.get("damage"))) : dnum(inst.def, "damage");
		double bal = heavy ? vnum(inst.def.get("heavy_balance", inst.def.get("balance"))) : dnum(inst.def, "balance");
		const double mass_scale = !heavy ? std::sqrt(b->mass / Sim::STONE_SHOT_MASS) : 1.0;
		const int tier = heavy ? inst.tier() : 0;
		if (tier >= 2) {
			spd = Charge::paramf(inst, "speed", spd);
			dmg = Charge::paramf(inst, "damage", dmg);
			bal = Charge::paramf(inst, "balance", bal);
		}
		const Vec3 target = _throw_target(w, a, inst);
		const Vec3 v = launch_vel(b->pos, target, spd);
		w.release_body(a, v, true, dmg * mass_scale, bal * mass_scale);
		if (tier >= 2) {
			b->tier = tier;
			b->residual_authority = Interactions::cohesion(tier);
		}
		if (tier >= 3) {
			b->tag = Charge::params(inst, "tag", "crag");
			b->props.set("on_impact", "shatter");
			b->props.set("impact_pieces", Charge::parami(inst, "pieces", 3));
			b->props.set("move", inst.id);
		}
		w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", spd}, {"heavy", heavy}, {"tier", tier}}));
		Verbs::fx(w, a, inst, "release", D({{"body", b->id}, {"pos", b->pos}, {"dir", v.normalized()}, {"power", b->mass * spd / 20.0}}));
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	MatBody* b = w.held(a);
	if (inst.id == "earth_attack") {
		if (!it.attack_held) inst.data.set("released", true);
		if (b != nullptr) {
			const double rise = clampf(inst.total / maxf(dnum(inst.def, "startup"), 0.01), 0.0, 1.0);
			b->hold_point = a.pos + a.forward() * 0.85 + V3(0.0, lerpf(0.35, 1.45, ease(rise, 0.5)), 0.0);
		}
		inst.data.set("face", w.aim_dir(a, it));
		if (inst.phase == ActionPhase::Charge) {
			if (b == nullptr) {
				w.finish_action(a, inst);
				return;
			}
			if (inst.heavy && inst.tier() >= 2) _ladder(w, a, inst, *b);
			if (dbool(inst.data, "released", false) && inst.total >= dnum(inst.def, "heavy_min")) w.set_phase(a, inst, ActionPhase::Active);
		}
	} else if (inst.id == "earth_tech") {
		if (it.tech_cancel && (inst.phase == ActionPhase::Startup || inst.phase == ActionPhase::Channel)) {
			// Honoured from the first frame: a cancel during startup never seizes or throws.
			_drop(w, a);
			w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		if (inst.phase != ActionPhase::Channel) return;
		inst.data.set("aim", w.aim_dir(a, it));
		inst.data.set("aim_active", it.aim_active);
		inst.data.set("face", inst.data.get("aim"));
		if (b == nullptr) {
			_seek(w, a, inst, it);
			if (inst.phase == ActionPhase::Channel && !it.tech_held && w.held(a) == nullptr) {
				w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
				w.set_phase(a, inst, ActionPhase::Recovery);
			}
			return;
		}
		const Vec3 dir = dvec(inst.data, "aim");
		// Held at chest height between the hands (earth_hold), nudged toward the aim.
		b->hold_point = a.pos + Vec3(0.0f, 1.2f, 0.0f) + a.forward() * (0.3 + b->radius) + dir * 0.15;
		if (it.attack_pressed && !dbool(inst.data, "shaped", false) && dstr(inst.def, "shape", "") == "split") _split(w, a, inst, *b);
		if (!it.tech_held) {
			w.set_phase(a, inst, ActionPhase::Active);
			_throw_held(w, a, inst);
		}
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& /*inst*/, const std::string& /*reason*/) { _drop(w, a); }

// ---------------------------------------------------------------------------

void _seek(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const double reach = dnum(inst.def, "reach");
	const int tid = dint(inst.data, "target", -1);
	MatBody* tb = w.get_body(tid);
	if (tb == nullptr || !tb->alive || !_stone_filter(*tb) || tb->controller == a.id) {
		tb = w.find_body(a, w.aim_dir(a, it), reach, 65.0, _stone_filter);
		if (tb != nullptr) {
			inst.data.set("target", tb->id);
			w.emit("target_body", D({{"actor", a.id}, {"body", tb->id}}));
		}
	}
	if (tb != nullptr) {
		if (tb->mass > a.max_control_mass) {
			// Too heavy: the technique visibly fails (whiff -> recovery).
			w.request_grip(a, *tb, 0.0, "seize");   // emits control_fail (mass)
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"body", tb->id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		const double s = w.grip_strength(a, *tb, 0.85, reach);
		w.request_grip(a, *tb, s, "seize");
		return;
	}
	// Nothing to seize: rip a stone out of the ground after a short pull.
	if (inst.t >= dnum(inst.def, "rip_time")) {
		if (w.spend_focus(a, dnum(inst.def, "cost"))) {
			_rip(w, a, Sim::STONE_SHOT_MASS);
		} else {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
	}
}

void _throw_held(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr) return;
	const double mass_scale = std::sqrt(b->mass / Sim::STONE_SHOT_MASS);
	const Vec3 target = _throw_target(w, a, inst);
	const double spd = dnum(inst.def, "speed") / maxf(1.0, mass_scale * 0.8);
	const Vec3 v = launch_vel(b->pos, target, spd);
	w.release_body(a, v, true, dnum(inst.def, "damage") * mass_scale, dnum(inst.def, "balance") * mass_scale);
	w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", spd}}));
}

void _ladder(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	const int t = inst.tier();
	if (t <= dint(inst.data, "grown", 1)) return;
	const double want = Charge::paramf(inst, "mass", b.mass);
	if (!w.spend_focus(a, Charge::paramf(inst, "cost_add", 0.0))) {
		inst.data.set("tier", t - 1);
		inst.data.set("charge_frozen", true);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "tier"}, {"tier", t}}));
		return;
	}
	inst.data.set("grown", t);
	if (want > b.mass) {
		const double extra = want - b.mass;
		const double e = b.thermal_energy();
		b.mass += extra;
		w._set_energy(b, e);
		b.update_radius();
		w.mass_ledger.ground_taken += extra;
		w.emit("acquire", D({{"actor", a.id}, {"body", b.id}, {"mass", extra}}));
	}
	w.emit("telegraph", D({{"actor", a.id}, {"move", t == 2 ? "earth_boulder" : "earth_crag"}, {"body", b.id}, {"time", 0.0}}));
}

void _split(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	if (!w.spend_focus(a, vnum(inst.def.get("shape_cost", Value(3.0))))) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "shape"}}));
		return;
	}
	inst.data.set("shaped", true);
	inst.data.set("split", vint(inst.def.get("pieces", Value(3))));
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", "split"}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", b.id}, {"shape", "spear"}}));
}

void _throw_split(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr) return;
	const int n = dint(inst.data, "split", 3);
	const double mass = b->mass / static_cast<double>(n);
	const Vec3 target = _throw_target(w, a, inst);
	const double spread = deg_to_rad(vnum(inst.def.get("spread", Value(30.0))));
	const double speed = vnum(inst.def.get("split_speed", Value(20.0)));
	a.held_body = -1;
	b->controller = -1;
	for (int k = 0; k < n; ++k) {
		MatBody* p = k == n - 1 ? b : w.split_body(*b, mass, b->pos);
		const double ang = lerpf(-spread * 0.5, spread * 0.5, static_cast<double>(k) / static_cast<double>(maxi(1, n - 1)));
		const Vec3 to = a.chest() + rotated(target - a.chest(), Vec3::Up(), ang);
		p->vel = launch_vel(p->pos, to, speed);
		p->tag = "spear";
		p->on_ground = false;
		const double sc = std::sqrt(p->mass / Sim::STONE_SHOT_MASS);
		Verbs::arm(w, a, inst, *p, dnum(inst.def, "damage") * sc, dnum(inst.def, "balance") * sc);
		w.emit("launch", D({{"actor", a.id}, {"body", p->id}, {"speed", speed}, {"kind", "split"}}));
		Verbs::fx(w, a, inst, "release", D({{"body", p->id}, {"dir", p->vel.normalized()}, {"shape", "spear"}}));
	}
}

Vec3 _throw_target(CombatWorld& w, ActorState& a, ActionInst& inst) {
	ActorState* t = w.get_actor(a.lock_target);
	if (dbool(inst.data, "aim_active", false)) {
		const Vec3 dir = dvec(inst.data, "aim");
		// Aim drag picks a direction; if the lock target is roughly there, still lead it.
		if (t != nullptr) {
			Vec3 to = t->pos - a.pos;
			to.y = 0.0f;
			if (to.normalized().dot(dir) > 0.97f) return t->chest() + t->vel * 0.25;
		}
		return a.chest() + dir * 14.0;
	}
	if (t != nullptr) return t->chest() + Vec3(t->vel.x, 0.0f, t->vel.z) * 0.2;
	return a.chest() + a.forward() * 14.0;
}

MatBody* _rip(CombatWorld& w, ActorState& a, double mass) {
	Vec3 p = a.pos + a.forward() * 0.85;
	p.y = static_cast<float>(w.arena.ground_height(p.x, p.z, a.pos.y) - 0.1);
	MatBody* b = w.spawn_body(Mat::Stone, Form::Chunk, mass, p, "ground@" + ftos(p.x, 1) + "," + ftos(p.z, 1));
	w.mass_ledger.ground_taken += mass;
	b->max_life = Sim::REMNANT_LIFETIME;
	w.take_control(a, *b, 0.9, "rip");
	w.emit("rip", D({{"actor", a.id}, {"body", b->id}}));
	return b;
}

MatBody* _loose_stone_near(CombatWorld& w, ActorState& a) {
	MatBody* best = nullptr;
	for (const BodyRef& br : w.bodies) {
		MatBody* b = br.get();
		if (!b->alive || !_stone_filter(*b) || b->controller >= 0 || b->attack_id != 0) continue;
		if (b->mass > 25.0 || !b->on_ground) continue;
		const double d = Vec2(b->pos.x - a.pos.x, b->pos.z - a.pos.z).length();
		if (d < LOOSE_RADIUS && (best == nullptr || b->id < best->id)) best = b;
	}
	return best;
}

void _drop(CombatWorld& w, ActorState& a) {
	if (w.held(a) != nullptr) w.release_body(a, Vec3(0.0f, -1.0f, 0.0f), false);
}

void _fizzle(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& what) {
	inst.data.set("fizzle", true);
	w.emit("insufficient", D({{"actor", a.id}, {"what", what}, {"move", inst.id}}));
}

Vec3 launch_vel(Vec3 from, Vec3 to, double h_speed) {
	const Vec3 d = to - from;
	const Vec3 fl(d.x, 0.0f, d.z);
	const double dist = fl.length();
	if (dist < 0.01) return V3(0.0, h_speed, 0.0);
	const double t = dist / h_speed;
	const double vy = static_cast<double>(d.y) / t + 0.5 * Sim::GRAVITY * t;
	return fl / dist * h_speed + V3(0.0, vy, 0.0);
}

}  // namespace ActEarth
}  // namespace ff
