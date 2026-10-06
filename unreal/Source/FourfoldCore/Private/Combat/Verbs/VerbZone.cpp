// Fourfold core - port of game/combat/verbs/verb_zone.gd: "zone" (a ZONE body at self / feet / ahead / aim) and
// "summon" (a zone at the aim point whose tier grows with the hold, steered while held; release lets it linger).
#include "Combat/Verbs.h"

#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

namespace ff {
namespace VerbZone {
namespace {
const char* const kZoneProps[] = {"height", "walk_height", "friction", "surface", "actor_status", "status_t", "status_mag", "dps",
                                  "ground_only", "rate", "barrier", "ccls", "cls", "walk_speed", "spare_owner", "drag", "channel"};
}  // namespace

Vec3 _point(CombatWorld& w, const ActorState& a, const ActionInst& inst) {
	const std::string at = Charge::params(inst, "at", "ahead");
	const Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", Value(a.forward()))));
	Vec3 p = a.pos;
	if (at == "ahead") {
		p = a.pos + dir * Charge::paramf(inst, "distance", 3.0);
	} else if (at == "aim") {
		const Vec3 ap = dvec(inst.data, "aim_point", a.chest() + dir * 8.0f);
		const double rmax = Charge::paramf(inst, "range", 10.0);
		Vec3 fl(ap.x - a.pos.x, 0.0f, ap.z - a.pos.z);
		if (fl.length() > rmax) fl = fl.normalized() * rmax;
		p = a.pos + fl;
	}
	p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y + 0.5));
	return p;
}

MatBody* spawn(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& over) {
	VerbParams P{&inst, over};
	const Vec3 p = over.has("pos") ? dvec(over, "pos") : _point(w, a, inst);
	const Mat mat = Verbs::mat_id(P.get("mat", Value("air")));
	double mass = P.f("mass", 0.0);
	const std::string source = P.s("source", "none");
	if (mass > 0.0) {
		if (source == "ground") {
			w.mass_ledger.ground_taken += mass;
		} else if (source == "waterskin") {
			mass = minf(mass, a.water_carried);
			a.water_carried -= mass;
		} else if (source == "moisture") {
			w.mass_ledger.moisture_taken += mass;
		} else if (source == "metal") {
			mass = minf(mass, a.metal_carried);
			a.metal_carried -= mass;
		} else if (mat != Mat::Air && mat != Mat::Fire) {
			mass = 0.0;   // material zones need a booked source
		}
	}
	MatBody* z = w.spawn_zone(P.s("tag", "zone"), p, P.f("radius", 2.0), a.id, P.f("power", 0.0), mat, mass, P.f("life", 3.0),
	                          source + ":" + itos(a.id));
	z->tier = inst.tier();
	z->sub = inst.sub;
	for (const char* k : kZoneProps) {
		const Value v = P.get(k, Value());
		if (!v.is_nil()) z->props.set(k, v);
	}
	if (P.b("attach", false)) {
		z->props.set("attach", a.id);
		z->props.set("attach_off", p - a.pos);
	}
	if (z->props.has("walk_speed")) z->props.set("walk_target", a.lock_target);
	if (mat == Mat::Fire) z->heat_payload = Verbs::take_heat(inst);
	Verbs::fx(w, a, inst, "ring", D({{"pos", p}, {"radius", z->zone_radius}, {"body", z->id}, {"power", z->power}}));
	inst.data.set("zone", z->id);
	return z;
}

void summon_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent&) {
	MatBody* z = spawn(w, a, inst);
	z->max_life = -1.0;
	inst.data.set("summon", z->id);
}

void summon_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	MatBody* z = w.get_body(dint(inst.data, "summon", -1));
	if (z == nullptr || !z->alive) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
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
	z->tier = inst.tier();
	z->zone_radius = Charge::paramf(inst, "radius", z->zone_radius);
	z->radius = z->zone_radius;
	z->power = Charge::paramf(inst, "power", z->power);
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_point", w.aim_point(a, it));
	const double steer = Charge::paramf(inst, "steer_speed", 0.0);
	if (steer > 0.0) {
		Vec3 to = _point(w, a, inst) - z->pos;
		to.y = 0.0f;
		if (to.length() > 0.1f) {
			z->vel = to.normalized() * minf(steer, to.length() / Sim::DT);
			z->pos += z->vel * Sim::DT;
			z->pos.y = f32(w.arena.ground_height(z->pos.x, z->pos.z, z->pos.y + 0.5));
		}
	}
}

void summon_release(CombatWorld& w, ActorState&, ActionInst& inst) {
	MatBody* z = w.get_body(dint(inst.data, "summon", -1));
	inst.data.erase("summon");
	if (z == nullptr || !z->alive) return;
	z->max_life = z->age + Charge::paramf(inst, "linger", 2.0);
	z->vel = Vec3();
	if (z->props.has("walk_speed")) z->props.set("walk_target", dint(z->props, "walk_target", -1));
	w.emit("summon_release", D({{"body", z->id}, {"tier", z->tier}}));
}

}  // namespace VerbZone
}  // namespace ff
