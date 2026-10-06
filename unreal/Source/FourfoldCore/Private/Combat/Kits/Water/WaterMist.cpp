// Fourfold core - port of game/combat/kits/water/water_mist.gd (Water / Mist, sub 2; MOVESET §7.7): Scald Puff and its
// geysers, Fog Lance, Creeping Fog, Veil, Steam Blast, Dew, Vapor Draw (draw from fog zones), the steam ball, Mist Step
// and Fog Walk. The vapor ledger gets exactly the energy that vaporises water.
#include "Combat/Kits/Water/Water.h"

#include "Combat/Acts.h"
#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace WaterMist {

namespace {
constexpr int SUB = 2;

bool _geysers(CombatWorld& w, ActorState& a, ActionInst& inst, int count) {
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const double rng_m = Charge::paramf(inst, "range", 8.0);
	const Vec3 ap = dvec(inst.data, "aim_point", a.chest() + dir * rng_m);
	Vec3 flat_v(ap.x - a.pos.x, 0.0f, ap.z - a.pos.z);
	if (flat_v.length() > rng_m) flat_v = flat_v.normalized() * rng_m;
	const Vec3 centre = a.pos + flat_v;
	const double heat = Verbs::take_heat(inst);
	const double kg = Charge::paramf(inst, "steam_kg", 1.5);
	int made = 0;
	for (int k = 0; k < count; ++k) {
		const double off = (static_cast<double>(k) - static_cast<double>(count - 1) * 0.5) * 2.4;
		const Vec3 p = WaterUtil::ground_at(w, centre + dir * off);
		const double got = WaterUtil::take(w, a, kg);
		if (got < 0.3) {
			WaterUtil::give_back(w, a, got, a.pos);
			continue;
		}
		MatBody* z = w.spawn_zone("geyser", p, 1.3, a.id, Charge::paramf(inst, "power", 12.0), Mat::Steam, got, -1.0, "geyser:" + itos(a.id));
		z->props.set("fuse", Charge::paramf(inst, "fuse", 0.5) + 0.18 * static_cast<double>(k));
		z->props.set("lift", Charge::paramf(inst, "lift", 8.0));
		z->props.set("damage", Charge::paramf(inst, "damage", 14.0));
		z->props.set("knock", Charge::paramf(inst, "knock", 4.0));
		z->props.set("height", 3.0);
		z->tier = inst.tier();
		z->sub = SUB;
		z->heat_payload = heat / static_cast<double>(count);
		++made;
		Verbs::fx(w, a, inst, "ring", D({{"pos", p}, {"radius", 1.3}, {"body", z->id}, {"dur", dnum(z->props, "fuse")}}));
	}
	if (made == 0) {
		w.ledger.spent += heat;
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
	} else if (made < count) {
		w.ledger.spent += heat * static_cast<double>(count - made) / static_cast<double>(count);
	}
	return true;
}

MatBody* mist_cloak(CombatWorld& w, ActorState& a) {
	MatBody* z = WaterUtil::zone(w, "mist", a.pos, 1.3, a.id, 1.0, D({{"actor_status", "concealed"}, {"status_t", 0.3}, {"spare_owner", false}, {"height", 2.2}}),
	                             Mat::Air, 0.0, 5.0);
	z->sub = SUB;
	return z;
}

void _walk_end(CombatWorld& w, ActionInst& inst) {
	MatBody* z = w.get_body(dint(inst.data, "zone_id", -1));
	inst.data.erase("zone_id");
	if (z != nullptr && z->alive) w.close_zone(*z, "walk_end");
}
}  // namespace

bool puff_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int pillars = Charge::parami(inst, "pillars", 0);
	if (pillars > 0) return _geysers(w, a, inst, pillars);
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const double rng_m = Charge::paramf(inst, "range", 3.0);
	const Vec3 p = WaterUtil::ground_at(w, a.pos + dir * rng_m * 0.55) + V3(0, 0.8, 0);
	const double used = WaterUtil::make_steam(w, a, &inst, Charge::paramf(inst, "steam_kg", 0.5), p);
	if (used <= 0.0) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	VerbVolume::cone(w, a, inst);
	const double obscure = Charge::paramf(inst, "obscure", 0.5);
	MatBody* z = WaterUtil::zone(w, "steam", p - V3(0, 0.8, 0), 1.2 + 0.1 * static_cast<double>(inst.tier()), a.id, obscure + 0.4,
	                             D({{"actor_status", "blinded"}, {"status_t", obscure}, {"height", 2.0}, {"rate", 0.2}}), Mat::Air, 0.0, 3.0);
	z->tier = inst.tier();
	z->sub = SUB;
	return true;
}

bool geyser_tick(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (z.form != Form::Zone) return false;
	if (z.age < dnum(z.props, "fuse", 0.5)) return true;
	ActorState* owner = w.get_actor(z.owner);
	const double kg = z.mass;
	const double ve = Thermal::vapor_energy(kg);
	const double use = minf(z.heat_payload, ve);
	const double f = ve > 1e-9 ? use / ve : 0.0;
	w.ledger.vapor += use;
	z.heat_payload -= use;
	z.mass = 0.0;
	const double steam = kg * f;
	if (steam > 0.02) w._spawn_steam(z.pos + V3(0, 0.5, 0), steam);
	if (kg - steam > 0.02) WaterUtil::make_puddle(w, kg - steam, z.pos);
	const double rest = z.heat_payload;
	z.heat_payload = 0.0;
	VerbVolume::burst_at(w, owner, nullptr, z.pos + V3(0, 0.9, 0),
	                     D({{"radius", 1.6}, {"power", z.power}, {"damage", dnum(z.props, "damage", 14.0)}, {"balance", 26.0},
	                        {"knock", dnum(z.props, "knock", 4.0)}, {"lift", dnum(z.props, "lift", 8.0)}, {"cls", "steam"}, {"heat_hu", rest},
	                        {"mat", "steam"}, {"status", "scalded"}, {"status_t", 1.5}}));
	FxEvents::fx(w, "erupt", "steam",
	             D({{"actor", z.owner}, {"pos", z.pos}, {"radius", 1.6}, {"height", 4.0}, {"power", z.power}, {"move", "scald_puff"}, {"tier", z.tier},
	                {"body", z.id}}));
	MatBody* sz = WaterUtil::zone(w, "steam", z.pos, 1.5, z.owner, 1.2, D({{"actor_status", "blinded"}, {"status_t", 0.6}, {"height", 3.0}, {"rate", 0.2}}),
	                              Mat::Air, 0.0, 6.0);
	sz->tier = z.tier;
	w.close_zone(z, "erupted");
	return true;
}

bool lance_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double kg = Charge::paramf(inst, "mist_kg", 0.5);
	const double got = WaterUtil::take(w, a, kg);
	if (got < 0.2) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	VerbVolume::beam(w, a, inst);
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const double rng_m = Charge::paramf(inst, "range", 12.0);
	Vec3 end = a.hand_point() + dir * rng_m;
	const double blind = Charge::paramf(inst, "blind_t", 0.8);
	double hit_end = -1.0;
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.hits_taken.find(inst.attack_id) == t.hits_taken.end()) continue;
		if (t.last_result == "hit" || t.last_result == "knockdown") {
			Status::apply(w, t, "blinded", blind, 1.0, a.id);
			t.wetness = 1.0;
			hit_end = Vec2(t.pos.x - a.pos.x, t.pos.z - a.pos.z).length();
		}
	}
	if (hit_end > 0.0) end = a.hand_point() + dir * hit_end;
	MatBody* z = WaterUtil::zone(w, "mist", WaterUtil::ground_at(w, end), 1.0, a.id, 1.4,
	                             D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 2.0}}), Mat::Steam, got);
	z->tier = inst.tier();
	z->sub = SUB;
	return true;
}

bool fog_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double want = Charge::paramf(inst, "fog_kg", 2.0);
	const double got = WaterUtil::take(w, a, want, 3.0);
	if (got < 0.6) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const Vec3 p = WaterUtil::ground_at(w, a.pos + dir * 2.0);
	MatBody* z = WaterUtil::zone(w, "fog", p, Charge::paramf(inst, "radius", 4.0), a.id, Charge::paramf(inst, "life", 6.0),
	                             D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 3.0}, {"rate", 0.15}, {"drag", 1.6}}),
	                             Mat::Steam, got, Charge::paramf(inst, "power", 6.0));
	z->vel = dir * Charge::paramf(inst, "speed", 5.0);
	z->tier = inst.tier();
	z->sub = SUB;
	Verbs::fx(w, a, inst, "ring", D({{"pos", p}, {"radius", z->zone_radius}, {"body", z->id}, {"dir", dir}}));
	return true;
}

bool veil_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double got = WaterUtil::take(w, a, Charge::paramf(inst, "veil_kg", 1.0));
	if (got < 0.4) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	MatBody* z = WaterUtil::zone(w, "mist", a.pos, Charge::paramf(inst, "radius", 2.2), a.id, Charge::paramf(inst, "life", 3.0),
	                             D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 2.6}, {"attach", a.id},
	                                {"attach_off", Vec3()}, {"rate", 0.15}}),
	                             Mat::Steam, got, 7.0);
	z->tier = inst.tier();
	z->sub = SUB;
	Verbs::fx(w, a, inst, "ring", D({{"pos", a.pos}, {"radius", z->zone_radius}, {"body", z->id}}));
	return true;
}

bool blast_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	for (MatBody* z : WaterUtil::zones_of(w, a, "steam_screen")) w.close_zone(*z, "burst");
	VerbVolume::cone(w, a, inst);
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	MatBody* z2 = WaterUtil::zone(w, "steam", a.pos + dir * 2.5, 1.6, a.id, 0.8, D({{"actor_status", "blinded"}, {"status_t", 0.4}, {"height", 2.2}}),
	                              Mat::Air, 0.0, 4.0);
	z2->tier = inst.tier();
	return true;
}

bool dew_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double r = Charge::paramf(inst, "radius", 6.0);
	int n = 0;
	double kg = 0.0;
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || !ActWater::vapor_filter(b) || b.form == Form::Pool) continue;
		if (Vec2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length() > r + b.radius) continue;
		kg += b.mass;
		WaterUtil::rain_down(w, b, WaterUtil::ground_at(w, b.pos));
		++n;
	}
	Verbs::fx(w, a, inst, "burst", D({{"pos", a.pos + V3(0, 1.0, 0)}, {"radius", r}, {"power", kg}}));
	w.emit("dew", D({{"actor", a.id}, {"bodies", n}, {"kg", kg}}));
	return true;
}

void vapor_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel) return;
	MatBody* held = w.held(a);
	if (held != nullptr && held->mat != Mat::Steam) return;
	if (held != nullptr && !held->props.has("on_impact")) {
		held->props.set("on_impact", "burst");
		held->props.set("move", inst.id);
	}
	if (!it.tech_held) return;
	const double reach = Charge::paramf(inst, "reach", 7.5);
	const double maxm = Charge::paramf(inst, "max_draw", 8.0);
	if (held != nullptr && held->mass >= maxm - 0.01) return;
	MatBody* zone = w.find_body(a, w.aim_dir(a, it), reach, 70.0, [](MatBody& b) { return b.form == Form::Zone && ActWater::vapor_filter(b); });
	if (zone == nullptr) return;
	const double take = minf(minf(Charge::paramf(inst, "draw_rate", 6.0) * Sim::DT, zone->mass), maxm - (held != nullptr ? held->mass : 0.0));
	if (take < 0.005) return;
	if (held == nullptr) {
		held = w.spawn_body(Mat::Steam, Form::Cloud, take, zone->pos, "vapor:" + itos(zone->id));
		held->max_life = 6.0;
		held->lineage.push_back(zone->id);
		w.take_control(a, *held, 0.9, "draw");
		held->props.set("on_impact", "burst");
		held->props.set("move", inst.id);
	} else {
		held->mass += take;
	}
	zone->mass -= take;
	const double m0 = dnum(zone->props, "mass0", zone->mass + take);
	zone->props.set("mass0", m0);
	const double r0 = dnum(zone->props, "radius0", zone->zone_radius);
	zone->props.set("radius0", r0);
	zone->zone_radius = maxf(0.5, r0 * std::sqrt(maxf(zone->mass, 0.0) / m0));
	zone->radius = zone->zone_radius;
	if (zone->mass <= 0.04) {
		const double rest = zone->mass;
		zone->mass = 0.0;
		held->mass += rest;
		w.close_zone(*zone, "drawn");
	}
	if (w.tick % 8 == 0) w.emit("draw_water", D({{"actor", a.id}, {"body", held->id}, {"from", zone->id}, {"at", zone->pos}, {"vapor", true}}));
}

bool ball_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)what;
	ActorState* owner = w.get_actor(b.attack_owner);
	VerbVolume::burst_at(w, owner, nullptr, b.pos,
	                     D({{"radius", 2.5}, {"power", 6.0}, {"damage", 8.0}, {"balance", 18.0}, {"knock", 3.0}, {"lift", 1.0}, {"cls", "steam"},
	                        {"mat", "steam"}, {"status", "scalded"}, {"status_t", 1.5}}));
	MatBody* z = WaterUtil::zone(w, "steam", b.pos, 2.2, b.attack_owner, 1.5, D({{"actor_status", "blinded"}, {"status_t", 0.5}, {"height", 2.4}}),
	                             Mat::Air, 0.0, 4.0);
	z->tier = b.tier;
	b.vel = Vec3();
	b.attack_id = 0;
	return true;
}

void step_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	WaterUtil::evade_start(w, a, inst, it,
	                       D({{"dist", dnum(inst.def, "distance")}, {"iframes", dnum(inst.def, "iframes")}, {"cost", dnum(inst.def, "cost")},
	                          {"active", dnum(inst.def, "active")}, {"hidden", true}, {"hidden_t", dnum(inst.def, "active") + 0.2}}));
	mist_cloak(w, a);
	Verbs::fx(w, a, inst, "burst", D({{"pos", a.pos + V3(0, 1.0, 0)}, {"radius", 1.3}}));
}

void step_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) {
		WaterUtil::evade_end(inst);
		mist_cloak(w, a);
		Verbs::fx(w, a, inst, "burst", D({{"pos", a.pos + V3(0, 1.0, 0)}, {"radius", 1.3}}));
	}
}

ActionPhase walk_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const ActionPhase nxt = Verbs::after_startup(w, a, inst, it);
	if (nxt == ActionPhase::Channel && a.action.get() == &inst && !inst.data.has("zone_id")) {
		MatBody* z = WaterUtil::zone(w, "mist", a.pos, 1.5, a.id, -1.0,
		                             D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 2.4}, {"attach", a.id},
		                                {"attach_off", Vec3()}, {"rate", 0.15}}),
		                             Mat::Air, 0.0, 5.0);
		z->sub = SUB;
		inst.data.set("zone_id", z->id);
	}
	return nxt;
}

void walk_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) _walk_end(w, inst);
	Verbs::on_phase(w, a, inst, p);
}

void walk_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	_walk_end(w, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

}  // namespace WaterMist
}  // namespace ff
