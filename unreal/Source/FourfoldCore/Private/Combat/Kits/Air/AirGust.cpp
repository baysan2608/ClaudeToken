// Fourfold core - port of game/combat/kits/air/air_gust.gd: Wind Crescent (+ its body tick), Crosswind, Downdraft,
// the Wind Grip context and the technique preview. Legacy T0 / T1 behaviour stays in Acts/ActAir.
#include "Combat/Kits/Air/AirGust.h"

#include "Combat/Kits/Air/Air.h"
#include "Combat/Kits/Air/AirUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

namespace ff {
namespace AirGust {

// One crescent per piece, all aimed at the same point so a pair crosses on the target. T2+ is a wide one.
bool crescent_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int count = maxi(1, static_cast<int>(Charge::paramf(inst, "count", 1.0)));
	const Vec3 dir = AirUtil::aim_flat(a, inst);
	const Vec3 target = Verbs::target_point(w, a, inst, 16.0);
	const Vec3 perp = dir.cross(Vec3::Up()).normalized();
	const double speed = Charge::paramf(inst, "speed", 22.0);
	const double radius = Charge::paramf(inst, "radius", 0.5);
	Array ids;
	for (int k = 0; k < count; ++k) {
		const std::vector<MatBody*> made = VerbProjectile::fire(w, a, inst, D({{"count", 1}}));
		if (made.empty()) continue;
		MatBody* b = made[0];
		const double off = count == 1 ? 0.0 : (k == 0 ? -0.7 : 0.7);
		b->pos += perp * f32(off);
		b->radius = radius;
		b->vel = (target - b->pos).normalized() * f32(speed);
		b->spin = 14.0;
		ids.push_back(b->id);
	}
	inst.data.set("bodies", ids);
	return true;
}

// A crescent meets what it passes: light shots are deflected / bent, vines (loose and walls) are cut (cells x|crescent).
bool crescent_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.attack_id == 0 || b.form == Form::Zone) return false;
	ActorState* owner = w.get_actor(b.attack_owner);
	AgentRef c;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !o.alive || o.captured_by >= 0 || o.controller >= 0) continue;
		const bool wallish = o.form == Form::Wall;
		if (!wallish && (o.static_body || o.form == Form::Zone || o.form == Form::Pool || o.form == Form::Puddle)) continue;
		const bool live_shot = o.is_projectile() && o.attack_owner != b.attack_owner && o.mat != Mat::Air;
		const bool vine = o.mat == Mat::Plant && o.attack_owner != b.attack_owner;
		if (!(live_shot || vine)) continue;
		const double reach = b.radius + (wallish ? maxf(o.wall_half.x, o.wall_half.z) : o.radius) + 0.3;
		if (b.pos.distance_to(o.pos) > reach) continue;
		const std::string key = itos(b.id) + "|" + itos(o.id);
		auto it = w._zone_pairs.find(key);
		const int64_t last = it == w._zone_pairs.end() ? -100000 : it->second;
		if (w.tick - last < 8) continue;
		w._zone_pairs[key] = w.tick;
		if (!c) {
			c = Agent::of_body(w, b);
			c->actor = owner;
		}
		AgentRef th = Agent::of_body(w, o, owner);
		IxCtx ctx;
		ctx.site = "crescent";
		Interactions::resolve(w, *th, *c, ctx, &Interactions::PASS_RULE());
		if (!b.alive) break;
	}
	return false;
}

bool crosswind_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const double half = Charge::paramf(inst, "angle", 60.0);
	const Vec3 dir = vvec(inst.data.get("face"), a.forward());
	const double power = Charge::paramf(inst, "power", 8.0);
	// The volume is gust pressure to fighters and their guards; loose bodies meet it as the class "crosswind"
	// (cells x|crosswind: bend, by how much the wind out-powers the body).
	AgentRef v = Agent::of_volume(&w, &a, &inst, "gust", a.chest(), dir, D({{"P", power}}));
	v->ccls = "crosswind";
	v->data.set("knock", Charge::paramf(inst, "knock", 6.0));
	Verbs::fx(w, a, inst, "cone", D({{"length", rng_m}, {"angle", half}, {"power", v->power}}));
	const Vec3 side = dir.cross(Vec3::Up()).normalized();
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (!b.alive || &b == w.pool || b.controller == a.id || b.captured_by >= 0 || b.static_body) continue;
		if (b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Zone) continue;
		if (!AirUtil::in_cone(a.chest(), dir, b.pos, rng_m, half, b.radius)) continue;
		AgentRef th = Agent::of_body(w, b, &a);
		IxCtx ctx;
		ctx.site = "gust";
		Interactions::resolve(w, *th, *v, ctx, &Interactions::PASS_RULE());
	}
	for (ActorState* t : w.actors_in_cone(a, dir, rng_m, half)) {
		const Vec3 rel = t->pos - a.pos;
		const double sgn = rel.dot(side) >= 0.0f ? 1.0 : -1.0;
		const double bal = Charge::paramf(inst, "balance", 16.0) * (Status::has(*t, "flight") ? 1.5 : 1.0);
		const Vec3 knock = side * f32(sgn * dnum(v->data, "knock")) + V3(0, Charge::paramf(inst, "lift", 0.6), 0);
		const Dict info = D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "damage", 4.0)}, {"balance", bal},
		                     {"knock", knock}, {"kind", "air"}, {"from", a.chest()}, {"power", v->power}, {"tier", v->tier}, {"mat", "wind"}});
		w.hit_actor(*t, info, v);
	}
	return true;
}

bool downdraft_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double r = Charge::paramf(inst, "radius", 3.0);
	const Dict prm = D({{"radius", r}, {"power", Charge::paramf(inst, "power", 12.0)}, {"damage", Charge::paramf(inst, "damage", 5.0)},
	                    {"balance", Charge::paramf(inst, "balance", 18.0)}, {"knock", Charge::paramf(inst, "knock", 4.0)},
	                    {"lift", Charge::paramf(inst, "lift", -8.0)}, {"cls", "gust"}, {"mat", "wind"}, {"heat_hu", 0.0}});
	const Vec3 p = a.pos + V3(0, 0.6, 0);
	// Airborne enemies are slammed down before the blast reaches them (flight ends, the fall is fast).
	for (ActorState* t : AirUtil::foes_near(w, a, a.pos + V3(0, 1.0, 0), r + 1.2)) {
		if (AirUtil::airborne(*t) && !Status::immune(*t, "knockback")) {
			AirSound::end_flight(w, *t, "slammed");
			t->vel.y = -16.0f;
			t->grounded = false;
			t->balance = maxf(0.0, t->balance - 25.0);
			t->balance_idle = 0.0;
			w.emit("slam", D({{"actor", t->id}, {"by", a.id}}));
		}
	}
	VerbVolume::burst_at(w, &a, &inst, p, prm);
	return true;
}

MatBody* grip_target(CombatWorld& w, const ActorState& a, Vec3 dir) {
	const ActorState* ap = &a;
	return w.find_body(a, dir, GRIP_REACH, GRIP_CONE,
	                   [ap](MatBody& b) { return b.controller != ap->id && AirUtil::carriable(b) && Interactions::allows(b, "grip_wind"); });
}

// Each tick of a Wind Grip: a seized fireball is fed once (+20 % heat, booked as created: fantasy oxygen).
void grip_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	MatBody* b = w.held(a);
	if (b == nullptr || dbool(inst.data, "fed", false)) return;
	inst.data.set("fed", true);
	if (b->mat == Mat::Fire && b->heat_payload > 0.0 && !dbool(b->props, "fed", false)) {
		const double add = b->heat_payload * (FIRE_FEED - 1.0);
		b->heat_payload += add;
		w.ledger.generated += add;
		b->props.set("fed", true);
		w.emit("fed", D({{"body", b->id}, {"by", a.id}, {"add", add}, {"payload", b->heat_payload}}));
	}
}

Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	if (a.grounded) {
		MatBody* b = grip_target(w, a, dir);
		if (b != nullptr) return D({{"mode", "WIND GRIP"}, {"body", b->id}, {"ok", true}, {"reason", ""}});
		return D({{"mode", "UPDRAFT"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	}
	return D({{"mode", "GLIDE"}, {"body", -1}, {"ok", true}, {"reason", ""}});
}

}  // namespace AirGust
}  // namespace ff
