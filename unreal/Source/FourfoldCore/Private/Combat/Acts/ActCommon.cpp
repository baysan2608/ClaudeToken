// Fourfold core - port of game/combat/act_common.gd: evade, air dash and guard (Earth wall / Water shield variants).
// Guards keep the action id "guard"; a sub-element guard spec (inst.data.spec) is forwarded to its module.
#include "Combat/Acts.h"

#include "Combat/Moves.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

namespace ff {
namespace ActCommon {
namespace {

void common_raise_wall(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* old = w.get_body(a.wall_body);
	if (old != nullptr && old->alive) a.wall_body = -1;   // one wall at a time: the old one sinks
	if (!w.spend_focus(a, WALL_COST)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "wall"}}));
		return;
	}
	Vec3 p = a.pos + a.forward() * WALL_DIST;
	p.y = static_cast<float>(w.arena.ground_height(p.x, p.z, a.pos.y));
	if (absf(static_cast<double>(p.y) - static_cast<double>(a.pos.y)) > 0.3) return;   // no flat ground in front: plain guard
	MatBody* b = w.spawn_body(Mat::Stone, Form::Wall, Sim::WALL_MASS, p, "ground@" + ftos(p.x, 1) + "," + ftos(p.z, 1));
	w.mass_ledger.ground_taken += Sim::WALL_MASS;
	b->wall_yaw = a.facing;
	b->wall_half = Vec3(1.1f, 0.75f, 0.28f);
	b->wall_rise = 0.0;
	b->static_body = true;
	b->touch(a.id, "wall", w.tick);
	a.wall_body = b->id;
	inst.data.set("wall", b->id);
	w.emit("wall", D({{"actor", a.id}, {"body", b->id}}));
}

void common_water_shield(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr && a.water_carried >= 1.5) {
		b = w.spawn_body(Mat::Water, Form::Blob, a.water_carried, a.chest() + a.forward() * 0.75, "waterskin:" + itos(a.id));
		a.water_carried = 0.0;
		w.take_control(a, *b, 0.9, "shield");
	}
	if (b != nullptr && b->is_water() && b->phase == Phase::Liquid) {
		b->form = Form::Blob;
		inst.data.set("shield", true);
		w.emit("shield", D({{"actor", a.id}, {"body", b->id}}));
	}
}

std::string spec_module(const ActionInst& inst) { return dstr(inst.data, "spec_module"); }

}  // namespace

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "evade" || inst.id == "air_dash") {
		Vec3 dir = it.move;
		dir.y = 0.0f;
		if (dir.length() < 0.2f) dir = -a.forward();   // neutral evade = backstep
		dir = dir.normalized();
		inst.data.set("dir", dir);
		inst.data.set("controls_motion", true);
		inst.data.set("face", a.forward());
		w.spend_focus(a, minf(a.focus, dnum(inst.def, "cost")));
		a.iframes = dnum(inst.def, "iframes");
		const Vec3 f = a.forward();
		const Vec3 r = f.cross(Vec3::Up());
		const double fd = dir.dot(f);
		const double rd = dir.dot(r);
		std::string side = "back";
		if (absf(fd) >= absf(rd))
			side = fd > 0.0 ? "fwd" : "back";
		else
			side = rd < 0.0 ? "r" : "l";
		inst.data.set("side", side);
		w.emit("evade", D({{"actor", a.id}, {"dir", dir}, {"side", side}, {"dash", inst.id == "air_dash"}}));
	} else if (inst.id == "guard") {
		const double since = static_cast<double>(w.tick - a.guard_press_tick) * Sim::DT;
		a.guard_press_tick = w.tick;
		a.guarding = true;
		if (since < Moves::GUARD_MASH_LOCK) inst.data.set("mashed", true);   // mashing never opens a perfect window
		a.guard_tick = w.tick;
		if (_has_spec(inst)) {
			a.wall_body = -1;   // a sub-element barrier replaces the legacy wall: the old wall sinks
			w.module_start(spec_module(inst), a, inst, it);
		} else if (a.element == Sim::EARTH && a.grounded) {
			common_raise_wall(w, a, inst);
		} else {
			a.wall_body = -1;   // only a grounded Earth guard keeps a wall
			if (a.element == Sim::WATER) common_water_shield(w, a, inst);
		}
		// Held material that did not become the shield is dropped.
		if (w.held(a) != nullptr && !dbool(inst.data, "shield", false)) w.release_body(a, Vec3(0.0f, -1.0f, 0.0f), false);
		w.emit("guard", D({{"actor", a.id}, {"element", a.element}, {"wall", a.wall_body}, {"sub", inst.sub},
		                   {"spec", dstr(inst.data, "spec", "guard")}}));
	}
}

ActionPhase after_startup(CombatWorld& /*w*/, ActorState& /*a*/, ActionInst& inst, const ActorIntent& /*it*/) {
	if (inst.id == "guard") return ActionPhase::Channel;
	return ActionPhase::Active;
}

bool _has_spec(const ActionInst& inst) {
	const std::string sp = dstr(inst.data, "spec", "guard");
	return !sp.empty() && sp != "guard" && !ddict(inst.data, "spec_def").empty();
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "guard" && _has_spec(inst)) w.module_phase(spec_module(inst), a, inst, p);
	if (inst.id == "guard" && p == ActionPhase::Recovery) _end_guard(w, a, inst);
	if ((inst.id == "evade" || inst.id == "air_dash") && p == ActionPhase::Recovery) inst.data.set("controls_motion", false);
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "evade" || inst.id == "air_dash") {
		if (inst.phase == ActionPhase::Active) {
			const double dur = dnum(inst.def, "active");
			const double dist = dnum(inst.def, "distance");
			const double x = clampf(inst.t / dur, 0.0, 1.0);
			const double spd = 2.0 * dist / dur * (1.0 - x);   // ease-out profile, integrates to dist
			const Vec3 dir = dvec(inst.data, "dir");
			a.vel.x = static_cast<float>(dir.x * spd);
			a.vel.z = static_cast<float>(dir.z * spd);
		}
	} else if (inst.id == "guard") {
		if (_has_spec(inst)) {
			w.module_tick(spec_module(inst), a, inst, it);
			if (a.action.get() != &inst) return;
		}
		if (inst.phase == ActionPhase::Channel) {
			MatBody* shield = w.held(a);
			if (shield != nullptr) shield->hold_point = a.chest() + a.forward() * 0.75;
			if (!it.guard_held) w.set_phase(a, inst, ActionPhase::Recovery);
		}
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (inst.id == "guard") {
		if (_has_spec(inst)) w.module_interrupt(spec_module(inst), a, inst, reason);
		// A guard flick (push / sink) hands the guard's material on to the new move.
		_end_guard(w, a, inst, reason == "push" || reason == "sink");
	}
}

void raise_wall(CombatWorld& w, ActorState& a, ActionInst& inst) { common_raise_wall(w, a, inst); }
void water_shield(CombatWorld& w, ActorState& a, ActionInst& inst) { common_water_shield(w, a, inst); }

void _end_guard(CombatWorld& w, ActorState& a, ActionInst& inst, bool keep_material) {
	a.guarding = false;
	if (keep_material) return;
	if (dbool(inst.data, "shield", false)) {
		MatBody* b = w.held(a);
		if (b != nullptr && b->is_water()) {
			// Shield water returns to the waterskin; overflow falls as a puddle.
			const double back = minf(b->mass, 6.0 - a.water_carried);
			w.ledger.removed += back * (Sim::WATER_C * (b->temp - Sim::AMBIENT_C) - Sim::WATER_LATENT_FUSION * (1.0 - b->liquid));
			a.water_carried += back;
			b->mass -= back;
			if (b->mass <= 0.01) {
				a.held_body = -1;
				b->controller = -1;
				w.decay_body(*b, "absorbed");
			} else {
				w.release_body(a, Vec3(), false);
			}
		}
		inst.data.set("shield", false);
	}
}

}  // namespace ActCommon
}  // namespace ff
