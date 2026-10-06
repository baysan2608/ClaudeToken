// Fourfold core - port of game/combat/verbs/verb_barrier.gd: the sub-element guard spec (barrier = wall | held | aura |
// zone) and free-standing barriers (spikes, ridges).
#include "Combat/Verbs.h"

#include "Combat/Acts.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Util/GdUtil.h"

namespace ff {
namespace VerbBarrier {

Dict _spec(const ActionInst& inst) { return ddict(inst.data, "spec_def"); }

void start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent&) {
	const Dict spec = _spec(inst);
	if (!Verbs::pay(w, a, inst, "start")) {
		inst.data.set("barrier_failed", true);
		return;
	}
	const std::string kind = dstr(spec, "barrier", "aura");
	inst.data.set("bt", 0);
	if (kind == "wall") {
		if (a.grounded) _raise_wall(w, a, inst, spec);
	} else if (kind == "held") {
		_take_held(w, a, inst, spec);
	} else if (kind == "zone") {
		MatBody* z = w.spawn_zone(dstr(spec, "tag", "barrier"), a.pos, Charge::paramf(inst, "radius", 1.8), a.id, Charge::counter_power(spec, 0),
		                          Verbs::mat_id(spec.get("mat", Value("air"))), 0.0, -1.0);
		z->props.set("attach", a.id);
		z->props.set("barrier", dbool(spec, "stops_bolts", true));
		z->props.set("height", dnum(spec, "height", 2.4));
		if (spec.get("counter").has("cls")) z->props.set("ccls", vstr(spec.get("counter").get("cls")));
		inst.data.set("zone", z->id);
	}
	Verbs::fx(w, a, inst, "aura", D({{"on", true}}));
}

void _raise_wall(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& spec) {
	const double mass = Charge::paramf(inst, "mass", 100.0);
	Vec3 p = a.pos + a.forward() * dnum(spec, "dist", ActCommon::WALL_DIST);
	p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y));
	if (std::fabs(p.y - a.pos.y) > 0.3f) return;   // no flat ground in front: guard without a wall
	const Mat mat = Verbs::mat_id(spec.get("mat", Value("stone")));
	const std::string source = dstr(spec, "source", "ground");
	if (!_take_source(w, a, source, mass)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", source}, {"move", inst.data.get("spec", Value(""))}}));
		return;
	}
	MatBody* b = w.spawn_body(mat, Form::Wall, mass, p, source + "@" + ftos(p.x, 1) + "," + ftos(p.z, 1));
	if (mat == Mat::Water) {
		const double e0 = b->thermal_energy();
		b->liquid = 0.0;
		b->temp = -5.0;
		b->phase = Phase::Frozen;
		w.ledger.freeze_dump += b->thermal_energy() - e0;
	}
	b->tag = dstr(spec, "tag", "");
	b->wall_yaw = a.facing;
	b->wall_half = dvec(spec, "half", Vec3(1.1f, 0.75f, 0.28f));
	b->wall_rise = 0.0;
	b->static_body = true;
	b->sub = inst.sub;
	b->props.set("rise_time", dnum(spec, "rise", 0.14));
	b->props.set("source", source);
	if (spec.has("hardness")) b->hardness = dnum(spec, "hardness");
	if (spec.get("counter").has("cls")) b->props.set("ccls", vstr(spec.get("counter").get("cls")));
	b->touch(a.id, "wall", w.tick);
	a.wall_body = b->id;
	inst.data.set("wall", b->id);
	w.emit("wall", D({{"actor", a.id}, {"body", b->id}, {"tag", b->tag}}));
}

bool _take_source(CombatWorld& w, ActorState& a, const std::string& source, double mass) {
	if (source == "ground") {
		w.mass_ledger.ground_taken += mass;
	} else if (source == "waterskin") {
		if (a.water_carried + 1e-6 < mass) return false;
		a.water_carried -= mass;
	} else if (source == "metal") {
		if (a.metal_carried + 1e-6 < mass) return false;
		a.metal_carried -= mass;
	} else if (source == "moisture") {
		w.mass_ledger.moisture_taken += mass;
	}
	return true;
}

void _give_back(CombatWorld& w, ActorState& a, const std::string& source, MatBody& b, double kg) {
	if (source == "ground") w.mass_ledger.ground_returned += kg;
	else if (source == "waterskin") a.water_carried += kg;
	else if (source == "metal") a.metal_carried += kg;
	else if (source == "moisture") w.mass_ledger.moisture_taken -= kg;
	w.ledger.removed += b.thermal_energy() * kg / maxf(b.mass, 1e-9);
	b.mass -= kg;
}

void _take_held(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& spec) {
	const Mat mat = Verbs::mat_id(spec.get("mat", Value("water")));
	const std::string source = dstr(spec, "source", "waterskin");
	MatBody* b = w.held(a);
	if (b != nullptr && b->mat != mat) {
		w.release_body(a, Vec3(0, -1.0f, 0), false);
		b = nullptr;
	}
	if (b == nullptr) {
		double mass = Charge::paramf(inst, "mass", 6.0);
		if (source == "waterskin") mass = minf(mass, a.water_carried);
		else if (source == "metal") mass = minf(mass, a.metal_carried);
		if (mass < 0.5 || !_take_source(w, a, source, mass)) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", source}, {"move", inst.data.get("spec", Value(""))}}));
			return;
		}
		b = w.spawn_body(mat, Form::Blob, mass, a.chest() + a.forward() * 0.75f, source + ":" + itos(a.id));
		w.take_control(a, *b, 0.9, "shield");
	}
	b->tag = dstr(spec, "tag", b->tag);
	if (spec.has("hardness")) b->hardness = dnum(spec, "hardness");
	b->props.set("source", source);
	if (spec.get("counter").has("cls")) b->props.set("ccls", vstr(spec.get("counter").get("cls")));
	inst.data.set("held_barrier", b->id);
	if (mat == Mat::Water) inst.data.set("shield", true);
	w.emit("shield", D({{"actor", a.id}, {"body", b->id}, {"tag", b->tag}}));
}

void tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent&) {
	if (inst.phase != ActionPhase::Channel) return;
	const Dict spec = _spec(inst);
	const double upkeep = Charge::paramf(inst, "upkeep", 0.0) * Sim::DT;
	if (upkeep > 0.0 && !w.spend_focus(a, upkeep)) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.data.get("spec", Value(""))}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	MatBody* hb = w.get_body(dint(inst.data, "held_barrier", -1));
	if (hb != nullptr && hb->alive && hb->controller == a.id) hb->hold_point = a.chest() + a.forward() * 0.75f;
	const int t = inst.tier();
	if (t > dint(inst.data, "bt", 0)) {
		inst.data.set("bt", t);
		const double want = Charge::paramf(inst, "mass", 0.0);
		MatBody* body = w.get_body(dint(inst.data, "wall", -1));
		if (body == nullptr) body = hb;
		if (body != nullptr && body->alive && want > body->mass) {
			const double extra = want - body->mass;
			const std::string src = dstr(body->props, "source", dstr(spec, "source", "ground"));
			if (_take_source(w, a, src, extra)) {
				const double e = body->thermal_energy();
				body->mass += extra;
				w._set_energy(*body, e);
				if (body->form != Form::Wall) body->update_radius();
				w.emit("barrier_grow", D({{"actor", a.id}, {"body", body->id}, {"mass", body->mass}, {"tier", t}}));
			}
		}
		MatBody* z = w.get_body(dint(inst.data, "zone", -1));
		if (z != nullptr && z->alive) {
			z->zone_radius = Charge::paramf(inst, "radius", z->zone_radius);
			z->radius = z->zone_radius;
			z->power = maxf(z->power, Charge::counter_power(spec, t));
		}
	}
}

void end(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (dtruthy(inst.data, "barrier_ended")) return;
	inst.data.set("barrier_ended", true);
	Verbs::fx(w, a, inst, "aura", D({{"on", false}}));
	w.ledger.spent += Verbs::take_heat(inst);
	const bool keep = reason == "push" || reason == "sink";
	MatBody* z = w.get_body(dint(inst.data, "zone", -1));
	if (z != nullptr && z->alive && !keep) w.close_zone(*z, "guard_end");
	MatBody* hb = w.get_body(dint(inst.data, "held_barrier", -1));
	if (hb != nullptr && hb->alive && hb->controller == a.id && !keep) {
		const std::string src = dstr(hb->props, "source", "waterskin");
		if (src == "waterskin" && hb->is_water()) {
			const double back = minf(hb->mass, 6.0 - a.water_carried);
			_give_back(w, a, src, *hb, back);
		} else if (src != "waterskin") {
			_give_back(w, a, src, *hb, hb->mass);
		}
		if (hb->mass <= 0.01) {
			a.held_body = -1;
			hb->controller = -1;
			w.decay_body(*hb, "returned");
		} else {
			w.release_body(a, Vec3(0, -1.0f, 0), false);
		}
		inst.data.set("shield", false);
	}
}

void raise_free(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Dict spec = inst.def;
	const double mass = Charge::paramf(inst, "mass", 60.0);
	Vec3 p = a.pos + a.forward() * Charge::paramf(inst, "dist", 2.0);
	p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y));
	const std::string source = dstr(spec, "source", "ground");
	if (!_take_source(w, a, source, mass)) return;
	MatBody* b = w.spawn_body(Verbs::mat_id(spec.get("mat", Value("stone"))), Form::Wall, mass, p, source + "@" + ftos(p.x, 1) + "," + ftos(p.z, 1));
	b->tag = Charge::params(inst, "tag", "spikes");
	b->wall_yaw = a.facing;
	b->wall_half = vvec(Charge::param(inst, "half", Value(Vec3(1.1f, 0.5f, 0.3f))), Vec3(1.1f, 0.5f, 0.3f));
	b->static_body = true;
	b->props.set("standing", Charge::paramf(inst, "life", 1.5));
	b->props.set("rise_time", Charge::paramf(inst, "rise", 0.1));
	b->props.set("source", source);
	if (spec.has("hardness")) b->hardness = dnum(spec, "hardness");
	b->touch(a.id, "wall", w.tick);
	Verbs::fx(w, a, inst, "erupt", D({{"pos", p}, {"body", b->id}}));
}

}  // namespace VerbBarrier
}  // namespace ff
