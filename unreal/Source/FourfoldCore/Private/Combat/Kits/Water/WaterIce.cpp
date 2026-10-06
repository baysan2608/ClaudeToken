// Fourfold core - port of game/combat/kits/water/water_ice.gd (Water / Ice, sub 1; MOVESET §7.6): Ice Spear (T2 spike),
// Rime Path (ice floors, ridge), Hoarfrost Fan (freeze the wet), Ice Wall (+20 kg near water), wall / ridge body tick
// and Glacier Shove slide, Freeze-Draw, Ice Glide and Skate.
#include "Combat/Kits/Water/Water.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace WaterIce {

namespace {
constexpr int SUB = 1;

void _skate_end(CombatWorld& w, ActionInst& inst) {
	MatBody* z = w.get_body(dint(inst.data, "zone_id", -1));
	inst.data.erase("zone_id");
	if (z != nullptr && z->alive) w.close_zone(*z, "skate_end");
}
}  // namespace

bool spear_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	if (b.tier != 2) return false;
	b.vel = Vec3();
	b.on_ground = true;
	b.attack_id = 0;
	b.gravity_scale = 0.0;
	b.static_body = what != "actor";
	b.max_life = b.age + 3.0;
	b.pos.y = f32(maxf(b.pos.y, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.2));
	w.emit("stick", D({{"body", b.id}, {"on", what}}));
	return true;
}

bool rime_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = VerbGroundLine::launch(w, a, inst);
	if (b == nullptr) return true;
	WaterUtil::freeze_body(w, b);
	b->props.set("ice_life", Charge::paramf(inst, "ice_life", 3.0));
	b->props.set("spikes", Charge::paramb(inst, "spikes", false));
	b->props.set("ridge", Charge::paramb(inst, "ridge", false));
	b->props.set("last_zone", b->pos);
	return true;
}

bool rime_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (b.form != Form::Wave || b.attack_id == 0) return false;
	const Vec3 last = dvec(b.props, "last_zone", b.pos);
	if (Vec2(b.pos.x - last.x, b.pos.z - last.z).length() >= 1.0) {
		b.props.set("last_zone", b.pos);
		MatBody* z = ice_zone(w, b.pos, 0.95 + b.wave_width * 0.2, b.attack_owner, dnum(b.props, "ice_life", 3.0), b.power);
		z->tier = b.tier;
		if (dbool(b.props, "spikes", false)) {
			z->props.set("dps", 2.5);
			z->props.set("actor_status", "slowed");
			z->props.set("status_t", 0.4);
			z->props.set("ground_only", true);
		}
	}
	WaterWater::wave_contacts(w, b);
	if (!b.alive || b.form != Form::Wave) return false;
	if (dbool(b.props, "ridge", false) && b.wave_budget < 0.6) {
		WaterRules::make_ridge(w, b, w.get_actor(b.attack_owner), 8.0);
		return true;
	}
	return false;
}

MatBody* ice_zone(CombatWorld& w, Vec3 p, double radius, int owner_id, double life, double power) {
	MatBody* z = WaterUtil::zone(w, "ice_floor", p, radius, owner_id, life,
	                             D({{"friction", 0.12}, {"surface", "ice"}, {"walk_height", -static_cast<double>(p.y)}, {"spare_owner", true},
	                                {"height", 1.2}, {"rate", 0.25}}),
	                             Mat::Air, 0.0, power);
	z->sub = SUB;
	return z;
}

bool hoarfrost_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	VerbVolume::cone(w, a, inst);
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const double ang = Charge::paramf(inst, "angle", 45.0);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	for (ActorState* tp : w.actors_in_cone(a, dir, rng_m, ang)) {
		ActorState& t = *tp;
		if (t.hits_taken.find(inst.attack_id) == t.hits_taken.end()) continue;
		if ((t.last_result == "hit" || t.last_result == "knockdown") && t.wetness > Status::WET_AT) {
			// The water on them freezes: rooted until it cracks.
			Status::apply(w, t, "frozen", Charge::paramf(inst, "freeze_t", 0.8), 1.0, a.id);
			t.wetness = 0.2;
			w.emit("transform", D({{"actor", t.id}, {"at", t.pos}, {"from", "wet"}, {"to", "frozen"}, {"why", "frost"}}));
		}
	}
	// Puddles in the fan freeze (they are barriers to the volume code, so the cone never meets them as threats).
	const double cos_lim = std::cos(deg_to_rad(ang));
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.form == Form::Puddle && b.phase == Phase::Liquid) {
			const Vec3 to = b.pos - a.chest();
			const Vec3 flat_v(to.x, 0.0f, to.z);
			if (flat_v.length() <= rng_m + b.radius && (flat_v.length() < 0.5 || flat_v.normalized().dot(dir) >= cos_lim)) {
				WaterUtil::freeze_body(w, &b);
				w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "water"}, {"to", "ice"}, {"why", "frozen"}}));
			}
		}
	}
	return true;
}

void wall_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_start(w, a, inst, it);
	MatBody* wall = w.get_body(dint(inst.data, "wall", -1));
	if (wall != nullptr && wall->alive && WaterUtil::near_water(w, a.pos, 3.0)) {
		const double e = wall->thermal_energy();
		w.mass_ledger.moisture_taken += 20.0;
		wall->mass += 20.0;
		w._set_energy(*wall, e);
		w.emit("barrier_grow", D({{"actor", a.id}, {"body", wall->id}, {"mass", wall->mass}, {"tier", 0}, {"near_water", true}}));
	}
}

bool wall_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.form != Form::Wall) return false;
	if (b.is_water() && b.phase == Phase::Liquid && b.mass > 0.0) {
		ActorState* owner = w.get_actor(b.last_actor);
		if (owner != nullptr && owner->wall_body == b.id) owner->wall_body = -1;
		w.release_captured(b);
		b.static_body = false;
		b.wall_rise = 1.0;
		b.form = Form::Puddle;
		b.tag = "";
		b.hardness = -1.0;
		w._water_to_puddle(b);
		w.emit("wall_crumble", D({{"body", b.id}, {"melted", true}}));
		return true;
	}
	if (b.props.has("slide")) _slide(w, b, dt);
	return false;
}

void _slide(CombatWorld& w, MatBody& b, double dt) {
	Dict s = ddict(b.props, "slide");
	const Vec3 dir = dvec(s, "dir");
	const Vec3 step = dir * dnum(s, "speed") * dt;
	const Vec3 np = b.pos + step;
	const Vec3 up = V3(0, 0.6, 0);
	if (w.arena.segment_hit(b.pos + up, np + up, 0.5) >= 0.0 || absf(w.arena.ground_height(np.x, np.z, b.pos.y + 0.3) - b.pos.y) > 0.3) {
		b.props.erase("slide");
		return;
	}
	b.pos = np;
	s.set("left", dnum(s, "left") - step.length());
	ActorState* owner = w.get_actor(dint(s, "owner"));
	Dict hit = ddict(s, "hit");
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == owner || hit.has(itos(t.id)) || t.health <= 0.0 || (owner != nullptr && t.team == owner->team)) continue;
		if (w.point_in_wall(t.pos + V3(0, 0.9, 0), b, 0.5)) {
			hit.set(itos(t.id), true);
			const std::string res =
			    w.hit_actor(t, D({{"attacker", dint(s, "owner")}, {"attack_id", dint(s, "attack_id")}, {"damage", dnum(s, "damage", 10.0)},
			                      {"balance", dnum(s, "balance", 30.0)}, {"knock", dir * 6.0 + V3(0, 2.0, 0)}, {"kind", b.is_water() ? "ice" : "plant"},
			                      {"from", b.pos - dir}, {"power", dnum(s, "power", 22.0)}, {"tier", b.tier}, {"mat", b.is_water() ? "ice" : "plant"}}));
			if ((res == "hit" || res == "knockdown") && s.has("status"))
				Status::apply(w, t, dstr(s, "status"), dnum(s, "status_t", 0.8), 1.0, dint(s, "owner"));
		}
	}
	if (dnum(s, "left") <= 0.0) b.props.erase("slide");
}

bool shove_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "wall", a.wall_body));
	if (wall == nullptr || !wall->alive || wall->form != Form::Wall || (wall->tag != "ice" && wall->tag != "ridge")) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "wall"}, {"move", inst.id}}));
		return true;
	}
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	wall->props.set("slide", D({{"dir", dir}, {"left", 6.0}, {"speed", 10.0}, {"owner", a.id}, {"hit", Dict()}, {"attack_id", w.new_attack_id()}}));
	wall->props.set("standing", 4.0);
	wall->age = 0.0;
	wall->tier = inst.tier();
	Verbs::fx(w, a, inst, "cast", D({{"body", wall->id}, {"dir", dir}, {"length", 6.0}}));
	return true;
}

void freeze_draw_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel) return;
	MatBody* b = w.held(a);
	if (b == nullptr || !b->is_water()) return;
	if (b->phase == Phase::Liquid) {
		WaterUtil::freeze_body(w, b);
		b->form = Form::Shard;
		b->update_radius();
		w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "water"}, {"to", "ice"}, {"why", "frozen"}}));
	}
	const double maxm = Charge::paramf(inst, "max_draw", 12.0);
	if (b->phase == Phase::Frozen && b->mass < maxm && !dbool(inst.data, "shaped", false)) {
		const double dm = minf(Charge::paramf(inst, "grow_rate", 4.0) * Sim::DT, maxm - b->mass);
		w.mass_ledger.moisture_taken += dm;
		const double e_before = b->thermal_energy();
		b->mass += dm;   // the new kg arrives as ice at the block's own state: the dump is the energy it gains
		w.ledger.freeze_dump += b->thermal_energy() - e_before;
		b->update_radius();
	}
}

void glide_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	WaterUtil::evade_start(w, a, inst, it,
	                       D({{"dist", dnum(inst.def, "distance")}, {"iframes", dnum(inst.def, "iframes")}, {"cost", dnum(inst.def, "cost")},
	                          {"active", dnum(inst.def, "active")}}));
	const Vec3 dir = dvec(inst.data, "dir");
	for (int k = 0; k < 3; ++k) {
		MatBody* z = ice_zone(w, WaterUtil::ground_at(w, a.pos + dir * (1.4 * static_cast<double>(k + 1) - 0.4)), 1.2, a.id, 1.6, 8.0);
		z->tier = 0;
	}
	Verbs::fx(w, a, inst, "trail", D({{"dir", dir}, {"length", dnum(inst.def, "distance")}}));
}

ActionPhase skate_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const ActionPhase nxt = Verbs::after_startup(w, a, inst, it);
	if (nxt == ActionPhase::Channel && a.action.get() == &inst && !dtruthy(inst.data, "zone_id")) {
		MatBody* z = WaterUtil::zone(w, "ice_floor", a.pos, 1.5, a.id, -1.0,
		                             D({{"friction", 0.3}, {"surface", "ice"}, {"walk_height", 0.0}, {"spare_owner", true}, {"height", 1.4},
		                                {"attach", a.id}, {"attach_off", Vec3()}, {"skate", true}, {"rate", 0.25}}),
		                             Mat::Air, 0.0, 10.0);
		z->sub = SUB;
		inst.data.set("zone_id", z->id);
	}
	return nxt;
}

void skate_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) _skate_end(w, inst);
	Verbs::on_phase(w, a, inst, p);
}

void skate_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	_skate_end(w, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

}  // namespace WaterIce
}  // namespace ff
