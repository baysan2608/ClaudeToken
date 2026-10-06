// Fourfold core - port of game/combat/verbs/verb_heat.gd: ranged heat (Scorch / Smelter / Kiln). Heat a wall or a body at
// range, paid like Fire; a wall's heated face slumps into a molten body (ready to be poured back).
#include "Combat/Verbs.h"

#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace VerbHeat {

void start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent&) {
	const double rng_m = Charge::paramf(inst, "range", 6.0);
	const Vec3 dir = dvec(inst.data, "aim", a.forward());
	const double cone = std::cos(deg_to_rad(Charge::paramf(inst, "cone", 40.0)));
	MatBody* best = nullptr;
	double bd = kInf;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (!b.alive || b.form != Form::Wall || b.wall_rise < 0.3) continue;
		Vec3 to = b.pos - a.pos;
		to.y = 0.0f;
		const double d = to.length();
		if (d > rng_m + b.wall_half.x || (d > 0.5 && to.normalized().dot(dir) < cone)) continue;
		if (d < bd || (is_equal_approx(d, bd) && best != nullptr && b.id < best->id)) {
			best = &b;
			bd = d;
		}
	}
	if (best == nullptr) {
		best = w.find_body(a, dir, rng_m, Charge::paramf(inst, "cone", 40.0), [&a](MatBody& b) {
			return b.controller != a.id && b.form != Form::Pool && b.form != Form::Zone && b.mass >= 0.5 && !b.static_body &&
			       Interactions::allows(b, "heat_ranged");
		});
	}
	if (best == nullptr) {
		w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
		inst.data.set("fizzle", true);
		return;
	}
	inst.data.set("target", best->id);
	w.emit("telegraph", D({{"actor", a.id}, {"move", inst.id}, {"body", best->id}, {"time", 0.0}}));
}

void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (it.tech_cancel || !Charge::held(inst, it)) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	MatBody* t = w.get_body(dint(inst.data, "target", -1));
	MatBody* face = w.get_body(dint(inst.data, "face_id", -1));
	if (t == nullptr || !t->alive) {
		if (face != nullptr && face->alive) face->static_body = false;
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	const double rng_m = Charge::paramf(inst, "range", 6.0);
	if (a.chest().distance_to(t->pos) > rng_m + 2.0) {
		w.emit("draw_break", D({{"actor", a.id}, {"body", t->id}, {"reason", "range"}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	Vec3 to = t->pos - a.pos;
	to.y = 0.0f;
	if (to.length() > 0.2f) inst.data.set("face", to.normalized());
	const double want = Charge::paramf(inst, "rate", 300.0) * Sim::DT;
	const double paid = w.pay_heat(a, want);
	if (paid < want * 0.5) {
		if (!dtruthy(inst.data, "starved")) {
			inst.data.set("starved", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		}
		if (paid <= 0.0) return;
	}
	if (t->form == Form::Wall) {
		if (face == nullptr || !face->alive) face = _split_face(w, a, inst, *t);
		const double used = w.heat_body(*face, paid);
		w.ledger.spent += paid - used;
		if (w.tick % 6 == 0)
			w.emit("heating", D({{"actor", a.id}, {"body", face->id}, {"liquid", face->liquid}, {"temp", face->temp}, {"wall", t->id}}));
		if (face->liquid >= Charge::paramf(inst, "slump_at", 0.5)) {
			_slump(w, a, inst, *t, *face);
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
		return;
	}
	const double used2 = w.heat_body(*t, paid);
	w.ledger.spent += paid - used2;
	t->touch(a.id, "heat", w.tick);
	if (w.tick % 6 == 0) w.emit("heating", D({{"actor", a.id}, {"body", t->id}, {"liquid", t->liquid}, {"temp", t->temp}}));
	const double tt = Charge::paramf(inst, "target_temp", 0.0);
	if (tt > 0.0 && t->temp >= tt) w.set_phase(a, inst, ActionPhase::Recovery);
}

MatBody* _split_face(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall) {
	Vec3 n = V3(std::sin(wall.wall_yaw), 0.0, std::cos(wall.wall_yaw));
	if (n.dot(a.pos - wall.pos) < 0.0f) n = -n;
	const double m = wall.mass * Charge::paramf(inst, "slump_fraction", 0.25);
	const Vec3 p = wall.pos + n * (wall.wall_half.z + 0.05f) + Vec3(0, wall.wall_half.y, 0);
	MatBody* f = w.split_body(wall, m, p);
	f->form = Form::Chunk;
	f->static_body = true;
	f->vel = Vec3();
	f->props.set("face_of", wall.id);
	f->props.set("face_n", n);
	f->update_radius();
	inst.data.set("face_id", f->id);
	w.emit("wall_face", D({{"actor", a.id}, {"wall", wall.id}, {"body", f->id}, {"mass", m}}));
	return f;
}

void _slump(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall, MatBody& face) {
	const Vec3 n = dvec(face.props, "face_n", Vec3(0, 0, -1.0f));
	face.static_body = false;
	face.form = face.liquid > 0.0 ? Form::Blob : Form::Chunk;
	face.pos = wall.pos + n * (wall.wall_half.z + 0.6f);
	face.pos.y = f32(w.arena.ground_height(face.pos.x, face.pos.z, wall.pos.y + 0.4) + 0.05);
	face.vel = Vec3();
	face.on_ground = true;
	face.max_life = Sim::REMNANT_LIFETIME;
	face.props.erase("face_of");
	inst.data.set("slumped", true);
	w.emit("slump", D({{"actor", a.id}, {"wall", wall.id}, {"body", face.id}, {"mass", face.mass}, {"liquid", face.liquid}}));
	w.emit("transform", D({{"body", face.id}, {"at", face.pos}, {"from", "wall"}, {"to", "molten"}, {"why", "slump"}}));
	Verbs::fx(w, a, inst, "splash", D({{"pos", face.pos}, {"body", face.id}}));
	w._crumble_wall(wall);
}

void end(CombatWorld& w, ActorState&, ActionInst& inst) {
	MatBody* face = w.get_body(dint(inst.data, "face_id", -1));
	inst.data.erase("face_id");
	if (face == nullptr || !face->alive || dtruthy(inst.data, "slumped")) return;
	MatBody* wall = w.get_body(dint(face->props, "face_of", -1));
	face->props.erase("face_of");
	if (wall != nullptr && wall->alive && face->mat == wall->mat) {
		w.merge_bodies(*wall, *face);
		wall->vel = Vec3();
	} else {
		face->static_body = false;
	}
}

}  // namespace VerbHeat
}  // namespace ff
