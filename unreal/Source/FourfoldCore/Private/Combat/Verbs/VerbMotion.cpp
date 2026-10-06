// Fourfold core - port of game/combat/verbs/verb_motion.gd: dash (evade), mode (sustained movement modes), stance.
#include "Combat/Verbs.h"

#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

namespace ff {
namespace VerbMotion {

void dash_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const std::string mode = Charge::params(inst, "dir", "stick");
	Vec3 dir = it.move;
	dir.y = 0.0f;
	if (mode == "aim") {
		dir = dvec(inst.data, "aim", a.forward());
	} else if (mode == "toward") {
		const ActorState* t = w.get_actor(a.lock_target);
		dir = t != nullptr ? (t->pos - a.pos) : a.forward();
	} else if (mode == "back") {
		dir = -a.forward();
	}
	dir.y = 0.0f;
	if (dir.length() < 0.2f) dir = mode == "stick" ? -a.forward() : a.forward();
	dir = dir.normalized();
	inst.data.set("dir", dir);
	inst.data.set("controls_motion", true);
	inst.data.set("face", a.forward());
	const double dur = Charge::paramf(inst, "active", dnum(inst.def, "active"));
	a.iframes = maxf(a.iframes, Charge::paramf(inst, "iframes", 0.12));
	if (Charge::paramb(inst, "burrow", false)) {
		a.iframes = maxf(a.iframes, dur);
		Status::apply(w, a, "concealed", dur, 1.0, a.id);
	} else if (Charge::paramb(inst, "hidden", false)) {
		Status::apply(w, a, "concealed", dur, 1.0, a.id);
	}
	const double up = Charge::paramf(inst, "up", 0.0);
	if (up > 0.0) {
		a.vel.y = f32(up);
		a.grounded = false;
	}
	const std::string trail = Charge::params(inst, "trail", "");
	if (!trail.empty()) {
		MatBody* z = w.spawn_zone(trail, a.pos, Charge::paramf(inst, "trail_radius", 0.8), a.id, Charge::paramf(inst, "power", 0.0), Mat::Air, 0.0,
		                          Charge::paramf(inst, "trail_life", 1.0));
		z->props.set("spare_owner", true);
	}
	w.emit("evade", D({{"actor", a.id}, {"dir", dir}, {"side", "fwd"}, {"dash", true}, {"move", inst.id}}));
	Verbs::fx(w, a, inst, "trail", D({{"dir", dir}, {"length", Charge::paramf(inst, "distance", 4.0)}}));
}

void dash_tick(CombatWorld&, ActorState& a, ActionInst& inst) {
	const double dur = Charge::paramf(inst, "active", dnum(inst.def, "active"));
	const double dist = Charge::paramf(inst, "distance", 4.0);
	const double x = clampf(inst.t / maxf(dur, 1e-3), 0.0, 1.0);
	const double spd = 2.0 * dist / maxf(dur, 1e-3) * (1.0 - x);
	const Vec3 dir = dvec(inst.data, "dir");
	a.vel.x = f32(dir.x * spd);
	a.vel.z = f32(dir.z * spd);
	if (inst.t + Sim::DT >= dur) inst.data.set("controls_motion", false);
}

void mode_start(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const std::string kind = Charge::params(inst, "kind", "run");
	a.stance = kind;
	inst.data.set("move_scale", Charge::paramf(inst, "speed_mult", 1.0));
	if (kind == "flight" || kind == "hover") {
		a.flying = true;
		inst.data.set("hover_height", Charge::paramf(inst, "height", kind == "flight" ? 2.2 : 1.5));
		Status::apply(w, a, "levitating", -1.0, 1.0, a.id);
	} else if (kind == "glide") {
		inst.data.set("glide_fall", Charge::paramf(inst, "glide_fall", 1.6));
		inst.data.set("glide_speed", Charge::paramf(inst, "glide_speed", 6.0));
	} else if (kind == "burrow") {
		Status::apply(w, a, "concealed", -1.0, 1.0, a.id);
	}
	const std::string st = Charge::params(inst, "status", "");
	if (!st.empty()) Status::apply(w, a, st, -1.0, Charge::paramf(inst, "status_mag", 1.0), a.id);
	w.emit("mode", D({{"actor", a.id}, {"kind", kind}, {"on", true}, {"move", inst.id}}));
	Verbs::fx(w, a, inst, "aura", D({{"on", true}, {"shape", kind}}));
}

void mode_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (it.tech_cancel || !Charge::held(inst, it)) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	const double upkeep = Charge::paramf(inst, "upkeep", 0.0) * Sim::DT;
	if (upkeep > 0.0 && !w.spend_focus(a, upkeep)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	if (a.stance == "glide" && !a.grounded && a.vel.y < 0.0f && !a.gliding) {
		a.gliding = true;
		w.emit("glide", D({{"actor", a.id}}));
	}
}

void mode_end(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (dtruthy(inst.data, "mode_ended")) return;
	inst.data.set("mode_ended", true);
	const std::string kind = a.stance;
	a.stance = "";
	a.flying = false;
	a.gliding = false;
	inst.data.erase("hover_height");
	for (const std::string& st : {std::string("levitating"), std::string("concealed"), Charge::params(inst, "status", "")}) {
		if (!st.empty() && a.status.has(st) && dint(a.status.get(st), "src", -1) == a.id) Status::remove(w, a, st);
	}
	w.emit("mode", D({{"actor", a.id}, {"kind", kind}, {"on", false}, {"move", inst.id}}));
	Verbs::fx(w, a, inst, "aura", D({{"on", false}}));
}

void stance_start(CombatWorld& w, ActorState& a, ActionInst& inst) {
	a.stance = Charge::params(inst, "stance", "stance");
	a.armor = Charge::paramf(inst, "armor", 0.0);
	if (Charge::paramb(inst, "anchored", false)) {
		a.anchored = true;
		Status::apply(w, a, "anchored", -1.0, Charge::paramf(inst, "anchor_cp", 30.0), a.id);
	}
	const std::string st = Charge::params(inst, "status", "");
	if (!st.empty()) Status::apply(w, a, st, -1.0, Charge::paramf(inst, "status_mag", 1.0), a.id);
	inst.data.set("move_scale", Charge::paramf(inst, "speed_mult", 0.4));
	w.emit("stance", D({{"actor", a.id}, {"stance", a.stance}, {"on", true}}));
	Verbs::fx(w, a, inst, "aura", D({{"on", true}, {"shape", a.stance}}));
}

void stance_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.phase == ActionPhase::Channel && (!Charge::held(inst, it) || it.tech_cancel)) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	const double upkeep = Charge::paramf(inst, "upkeep", 0.0) * Sim::DT;
	if (upkeep > 0.0 && !w.spend_focus(a, upkeep)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
	}
}

void stance_end(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (dtruthy(inst.data, "stance_ended")) return;
	inst.data.set("stance_ended", true);
	const std::string nm = a.stance;
	a.stance = "";
	a.armor = 0.0;
	a.anchored = false;
	for (const std::string& st : {std::string("anchored"), Charge::params(inst, "status", "")}) {
		if (!st.empty() && a.status.has(st) && dint(a.status.get(st), "src", -1) == a.id) Status::remove(w, a, st);
	}
	w.emit("stance", D({{"actor", a.id}, {"stance", nm}, {"on", false}}));
	Verbs::fx(w, a, inst, "aura", D({{"on", false}}));
}

}  // namespace VerbMotion
}  // namespace ff
