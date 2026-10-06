// Fourfold core - port of game/combat/kits/water/water_util.gd (shared Water kit helpers, all booked).
#include "Combat/Kits/Water/WaterUtil.h"

#include "Sim/CombatWorld.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

namespace ff {
namespace WaterUtil {

// ------------------------------------------------------------------ water sources

double pool_dist(CombatWorld& w, Vec3 p) {
	const ArenaMap& ar = w.arena;
	const Vec2 q(static_cast<float>(clampf(p.x, ar.pool_min.x, ar.pool_max.x)), static_cast<float>(clampf(p.z, ar.pool_min.y, ar.pool_max.y)));
	return Vec2(p.x, p.z).distance_to(q);
}

MatBody* liquid_puddle_near(CombatWorld& w, Vec3 p, double reach) {
	MatBody* best = nullptr;
	double bd = kInf;
	for (const BodyRef& br : w.bodies) {
		MatBody* b = br.get();
		if (!b->alive || b->form != Form::Puddle || b->phase != Phase::Liquid || b->mass < 0.1) continue;
		const double d = static_cast<double>(Vec2(b->pos.x - p.x, b->pos.z - p.z).length()) - b->radius;
		if (d <= reach && d < bd) {
			bd = d;
			best = b;
		}
	}
	return best;
}

bool near_water(CombatWorld& w, Vec3 p, double reach) { return pool_dist(w, p) <= reach || liquid_puddle_near(w, p, reach) != nullptr; }

double available(CombatWorld& w, const ActorState& a, double reach) {
	double m = a.water_carried;
	if (a.in_water || pool_dist(w, a.pos) <= reach) m += w.pool->mass;
	MatBody* pd = liquid_puddle_near(w, a.pos, reach);
	if (pd != nullptr) m += pd->mass;
	return m;
}

double _src_energy(const MatBody& b, double kg) {
	return kg * (Sim::WATER_C * (b.temp - Sim::AMBIENT_C) - Sim::WATER_LATENT_FUSION * (1.0 - b.liquid));
}

double take(CombatWorld& w, ActorState& a, double kg, double reach) {
	double got = 0.0;
	const double m = minf(kg, a.water_carried);
	if (m > 0.0) {
		a.water_carried -= m;
		got += m;
	}
	if (got < kg - 1e-9 && (a.in_water || pool_dist(w, a.pos) <= reach)) {
		const double t = minf(kg - got, w.pool->mass);
		if (t > 0.0) {
			w.ledger.removed += _src_energy(*w.pool, t);
			w.pool->mass -= t;
			got += t;
		}
	}
	if (got < kg - 1e-9) {
		MatBody* pd = liquid_puddle_near(w, a.pos, reach);
		if (pd != nullptr) {
			const double t2 = minf(kg - got, pd->mass);
			w.ledger.removed += _src_energy(*pd, t2);
			pd->mass -= t2;
			got += t2;
			if (pd->mass <= 0.05)
				w.decay_body(*pd, "drained");
			else
				pd->update_radius_puddle();
		}
	}
	return got;
}

void give_back(CombatWorld& w, ActorState& a, double kg, Vec3 p) {
	if (kg <= 0.0) return;
	const double room = maxf(0.0, 6.0 - a.water_carried);
	const double back = minf(kg, room);
	a.water_carried += back;
	if (kg - back > 0.0) make_puddle(w, kg - back, p);
}

MatBody* make_puddle(CombatWorld& w, double kg, Vec3 p) {
	if (kg < 0.02) {
		w.mass_ledger.evaporated += kg;
		return nullptr;
	}
	MatBody* b = w.spawn_body(Mat::Water, Form::Stream, kg, Vec3(p.x, p.y + 0.3f, p.z), "spray");
	w._water_to_puddle(*b);
	return b->alive ? b : nullptr;
}

void disperse(CombatWorld& w, double kg) { w.mass_ledger.evaporated += maxf(kg, 0.0); }

// ------------------------------------------------------------------ phase changes

bool freeze_body(CombatWorld& w, MatBody* b) {
	if (b == nullptr || !b->alive || b->mat != Mat::Water || b->form == Form::Pool) return false;
	const double e0 = b->thermal_energy();
	b->liquid = 0.0;
	b->temp = minf(b->temp, -5.0);
	b->phase = Phase::Frozen;
	w.ledger.freeze_dump += b->thermal_energy() - e0;
	return true;
}

bool is_ice(const MatBody& b) { return b.mat == Mat::Water && b.phase == Phase::Frozen; }
bool is_liquid_water(const MatBody& b) { return b.mat == Mat::Water && b.phase == Phase::Liquid; }

double transfer_heat(CombatWorld& w, MatBody* src, MatBody* dst, double hu) {
	if (src == nullptr || dst == nullptr || !src->alive || !dst->alive || hu <= 0.0) return 0.0;
	const double give = minf(hu, maxf(0.0, src->thermal_energy() - src->heat_payload));
	if (give <= 0.0) return 0.0;
	const double got = -Thermal::heat(*src, -give);
	if (got <= 0.0) return 0.0;
	const double used = w.heat_body(*dst, got);
	if (used < got - 1e-9 && src->alive) Thermal::heat(*src, got - used);
	return used;
}

double make_steam(CombatWorld& w, ActorState& a, ActionInst* inst, double kg, Vec3 p) {
	const double got = take(w, a, kg);
	if (got < 0.05) return 0.0;
	MatBody* b = w.spawn_body(Mat::Water, Form::Blob, got, p, "steam:" + itos(a.id));
	const double need = Thermal::vapor_energy(got) + 0.01;
	const double have = inst != nullptr ? dnum(inst->data, "heat_paid", 0.0) : need;
	const double hu = minf(need, have);
	const double used = w.heat_body(*b, hu);
	if (inst != nullptr) inst->data.set("heat_paid", dnum(inst->data, "heat_paid", 0.0) - used);
	if (b->alive) {
		b->vel = Vec3();
		w._water_to_puddle(*b);
	}
	return used;
}

void wet(ActorState& a, double v) { a.wetness = maxf(a.wetness, v); }

// ------------------------------------------------------------------ evades

void evade_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it, const Dict& spec) {
	Vec3 dir = it.move;
	dir.y = 0.0f;
	if (dir.length() < 0.2f) dir = -a.forward();
	dir = dir.normalized();
	inst.data.set("dir", dir);
	inst.data.set("controls_motion", true);
	inst.data.set("face", a.forward());
	inst.data.set("evade_dist", dnum(spec, "dist", 2.8));
	inst.data.set("evade_dur", vnum(spec.get("active", inst.def.get("active"))));
	w.spend_focus(a, minf(a.focus, vnum(spec.get("cost", inst.def.get("cost", Value(0.0))))));
	a.iframes = maxf(a.iframes, dnum(spec, "iframes", 0.14));
	if (dbool(spec, "hidden", false))
		Status::apply(w, a, "concealed", vnum(spec.get("hidden_t", inst.data.get("evade_dur"))), 1.0, a.id);
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
	w.emit("evade", D({{"actor", a.id}, {"dir", dir}, {"side", side}, {"dash", false}, {"move", inst.id}}));
}

void evade_tick(CombatWorld& /*w*/, ActorState& a, ActionInst& inst) {
	if (inst.phase != ActionPhase::Active || !inst.data.has("dir")) return;
	const double dur = vnum(inst.data.get("evade_dur", inst.def.get("active")));
	const double dist = dnum(inst.data, "evade_dist", 2.8);
	const double x = clampf(inst.t / maxf(dur, 1e-3), 0.0, 1.0);
	const double spd = 2.0 * dist / maxf(dur, 1e-3) * (1.0 - x);
	const Vec3 dir = dvec(inst.data, "dir");
	a.vel.x = static_cast<float>(dir.x * spd);
	a.vel.z = static_cast<float>(dir.z * spd);
}

void evade_end(ActionInst& inst) { inst.data.set("controls_motion", false); }

// ------------------------------------------------------------------ zones

MatBody* zone(CombatWorld& w, const std::string& tag, Vec3 p, double radius, int owner_id, double life, const Dict& props, Mat mat,
              double mass, double power) {
	MatBody* z = w.spawn_zone(tag, p, radius, owner_id, power, mat, mass, life);
	for (const auto& kv : props) z->props.set(kv.first, kv.second);
	return z;
}

Vec3 ground_at(CombatWorld& w, Vec3 p) { return Vec3(p.x, static_cast<float>(w.arena.ground_height(p.x, p.z, p.y + 0.5)), p.z); }

bool hit_result_ok(const ActorState& t) { return t.last_result == "hit" || t.last_result == "knockdown"; }

// ------------------------------------------------------------------ ambient moisture, ice, vapour

double take_moisture(CombatWorld& w, double kg) {
	w.mass_ledger.moisture_taken += kg;
	return kg;
}

void give_back_moisture(CombatWorld& w, double kg) { w.mass_ledger.moisture_taken -= kg; }

bool freeze_any(CombatWorld& w, MatBody* b) {
	if (b == nullptr || !b->alive || b->form == Form::Pool) return false;
	if (b->mat == Mat::Steam) {
		const double e0 = b->thermal_energy();
		b->mat = Mat::Water;
		b->phase = Phase::Liquid;
		b->liquid = 1.0;
		b->temp = Sim::AMBIENT_C;
		w.ledger.removed += e0 - b->thermal_energy();
		if (b->form == Form::Cloud || b->form == Form::Zone) {
			b->form = Form::Shard;
			b->tag = "";
			b->max_life = Sim::REMNANT_LIFETIME;
		}
		b->update_radius();
	}
	if (!b->is_water()) return false;
	return freeze_body(w, b);
}

double absorb_into_skin(CombatWorld& w, ActorState* a, MatBody* b) {
	if (b == nullptr || !b->alive || a == nullptr) return 0.0;
	const double kg = b->mass;
	const double e = b->thermal_energy();
	b->mass = 0.0;
	w.ledger.removed += e;
	const double room = maxf(0.0, 6.0 - a->water_carried);
	const double into = minf(kg, room);
	a->water_carried += into;
	if (b->form == Form::Zone)
		w.close_zone(*b, "absorbed");
	else
		w.remove_body(*b, "absorbed");
	if (kg - into > 0.02)
		make_puddle(w, kg - into, a->pos);
	else if (kg - into > 0.0)
		disperse(w, kg - into);
	return into;
}

void rain_down(CombatWorld& w, MatBody& b, Vec3 p) {
	const double kg = b.mass;
	if (kg < 0.02) return;
	b.mass = 0.0;
	if (b.form == Form::Zone)
		w.close_zone(b, "rain");
	else
		w.remove_body(b, "rain");
	make_puddle(w, kg, p);
}

std::vector<ActorState*> foes_near(CombatWorld& w, const ActorState* owner, Vec3 p, double r) {
	std::vector<ActorState*> out;
	for (const auto& tp : w.actors) {
		ActorState* t = tp.get();
		if (t->health > 0.0 && (owner == nullptr || (t != owner && t->team != owner->team)) &&
		    static_cast<double>(Vec2(t->pos.x - p.x, t->pos.z - p.z).length()) <= r + Sim::ACTOR_RADIUS)
			out.push_back(t);
	}
	return out;
}

std::vector<MatBody*> zones_of(CombatWorld& w, const ActorState& a, const std::string& tag) {
	std::vector<MatBody*> out;
	for (const BodyRef& br : w.bodies)
		if (br->alive && br->form == Form::Zone && br->tag == tag && br->owner == a.id) out.push_back(br.get());
	return out;
}

Vec3 aim_flat(CombatWorld& /*w*/, const ActorState& a, const ActionInst& inst) {
	Vec3 d = vvec(inst.data.get("aim", inst.data.get("face", Value(a.forward()))));
	d.y = 0.0f;
	return d.length() > 0.01f ? d.normalized() : a.forward();
}

}  // namespace WaterUtil
}  // namespace ff
