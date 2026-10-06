// Fourfold core - port of game/combat/kits/air/air_vacuum.gd (Air / Vacuum, sub 2; MOVESET §7.15): Pressure Palm / Air
// Cannon / Implode / Collapse, Suction Line, Pressure Mine, Null Bubble, Pressure Wave, Vacuum Well, Pressure Hop,
// Slipstream and the Vacuum column outcomes. A vacuum holds no fire, carries no sound and insulates against lightning;
// when a well collapses it crushes what it held and leaves an air inrush zone (tag inrush) Fire's detonations read.
#include "Combat/Kits/Air/Air.h"

#include "Combat/Kits/Air/AirUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/Outcomes.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

namespace ff {
namespace AirVacuum {

namespace {
void _collapse_held(CombatWorld& w, ActionInst& inst) {
	MatBody* z = w.get_body(dint(inst.data, "summon", -1));
	inst.data.erase("summon");
	if (z != nullptr && z->alive) {
		z->props.set("crush_balance", 20.0 + 0.4 * z->power);
		z->props.set("crush_damage", 4.0 + 0.4 * z->power);
		collapse(w, *z);
	}
}
}  // namespace

// ================================================================ strike: Pressure Palm / Air Cannon / Implode / Collapse

bool palm_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int tier = inst.tier();
	if (tier == 0) return false;   // the burst verb
	if (tier == 1) {
		VerbVolume::beam(w, a, inst);
		return true;
	}
	const Vec3 dir = AirUtil::aim_flat(a, inst);
	const double rng_m = Charge::paramf(inst, "range", 8.0);
	Vec3 p = a.pos + dir * f32(rng_m);
	ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr && !dbool(inst.data, "aim_active", false)) {
		Vec3 to = t->pos - a.pos;
		to.y = 0.0f;
		if (to.length() < rng_m + 2.0) p = a.pos + to.normalized() * f32(maxf(2.0, static_cast<double>(to.length()) - 0.5));
	}
	MatBody* z = spawn_well(w, a, inst, p, Charge::paramf(inst, "radius", 3.0), Charge::paramf(inst, "power", 22.0));
	z->props.set("collapse_at", tier == 2 ? 0.4 : 0.45);
	z->props.set("pull_speed", Charge::paramf(inst, "pull_speed", 7.5));
	z->props.set("crush_balance", Charge::paramf(inst, "balance", 40.0));
	z->props.set("crush_damage", Charge::paramf(inst, "damage", 12.0));
	inst.data.set("bodies", A({z->id}));
	return true;
}

// ================================================================ the Vacuum Well zone

MatBody* spawn_well(CombatWorld& w, ActorState& a, ActionInst& inst, Vec3 p, double radius, double power) {
	MatBody* z = AirUtil::zone(w, "vacuum_well", p, radius, a.id, power, -1.0, D({{"height", 4.0}, {"rate", 0.1}}), inst.tier(), SUB);
	Verbs::fx(w, a, inst, "ring", D({{"pos", z->pos}, {"radius", radius}, {"body", z->id}, {"power", power}}));
	return z;
}

// An implosion point collapses on schedule (props.collapse_at); a summoned well waits for its caster's release.
bool well_tick(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (z.form != Form::Zone || !z.alive) return false;
	const double at = dnum(z.props, "collapse_at", -1.0);
	if (at >= 0.0 && z.age >= at) {
		collapse(w, z);
		return true;
	}
	return false;
}

// Pulls the projectiles, clouds and light bodies near it, drags the fighters (-40 % moving away); what enters the zone
// is met through the rules (captured, compressed, snuffed).
void well_effect(CombatWorld& w, MatBody& z, double dt) {
	z.spin = 0.0;
	const double reach = z.zone_radius * 1.8;
	const Vec3 centre = z.pos + V3(0, 1.0, 0);
	const double pull_a = 8.0 + 0.6 * z.power;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (&b == &z || !b.alive || b.static_body || b.controller >= 0 || b.captured_by >= 0 || b.mass > 80.0) continue;
		if (b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Zone || b.mat == Mat::Air) continue;
		const Vec3 to = centre - b.pos;
		const double d = to.length();
		if (d > reach || d < 0.3) continue;
		const double k = b.mass <= AirUtil::LIGHT ? 1.0 : 0.4;
		b.vel += to.normalized() * f32(pull_a) * f32(1.0 - d / reach) * f32(k) * f32(dt);
		b.on_ground = false;
	}
	const double inward_speed = dnum(z.props, "pull_speed", 3.5);
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& t = *w.actors[i];
		if (t.health <= 0.0 || t.id == z.owner) continue;
		Vec3 rel = z.pos - t.pos;
		rel.y = 0.0f;
		const double d2 = rel.length();
		if (d2 > reach || d2 < 0.2) continue;
		const Vec3 inward = rel.normalized();
		const double falloff = 1.0 - d2 / reach;
		const double vout = -t.vel.dot(inward);
		if (vout > 0.0) AirUtil::shove(t, inward * f32(vout) * 0.4f, "pull");   // -40 % moving away
		const double vin = t.vel.dot(inward);
		if (vin < inward_speed * falloff) AirUtil::shove(t, inward * f32(inward_speed * falloff - vin), "pull");
	}
}

// The well collapses: captured bodies drop, the air rushes back in (crush -balance, an inrush zone), the zone closes.
void collapse(CombatWorld& w, MatBody& z) {
	if (!z.alive) return;
	ActorState* owner = w.get_actor(z.owner);
	const double r = z.zone_radius;
	w.release_captured(z);
	const double bal = z.props.has("crush_balance") ? dnum(z.props, "crush_balance", 30.0) : 20.0 + 0.4 * z.power;
	const Dict prm = D({{"radius", r * 1.15}, {"power", z.power}, {"damage", dnum(z.props, "crush_damage", 8.0)}, {"balance", bal}, {"knock", 2.0},
	                    {"lift", 0.5}, {"cls", "blast"}, {"mat", "vacuum"}, {"heat_hu", 0.0}});
	VerbVolume::burst_at(w, owner, nullptr, z.pos + V3(0, 1.0, 0), prm);
	AirUtil::spawn_inrush(w, z.pos, r, z.power, z.owner);
	w.emit("collapse", D({{"body", z.id}, {"pos", z.pos}, {"radius", r}, {"power", z.power}, {"owner", z.owner}}));
	w.close_zone(z, "collapse");
}

// ================================================================ thrust: Suction Line

bool suction_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 10.0);
	const double width = Charge::paramf(inst, "width", 0.7);
	const Vec3 dir = AirUtil::aim_flat(a, inst);
	const Vec3 start = a.hand_point() + V3(0, 0.2, 0);
	const Vec3 end = start + dir * f32(rng_m);
	const double power = Charge::paramf(inst, "power", 8.0);
	AgentRef v = Agent::of_volume(&w, &a, &inst, "vacuum", start, dir, D({{"P", power}}));
	v->ccls = "suction";
	// the nearest of: a barrier / the arena (an anchor to be pulled to), a light body, a fighter
	double stop_t = 1.0;
	Vec3 anchor;
	bool has_anchor = false;
	if (const std::vector<Conduction::BarrierHit> barrier_hits = Conduction::barriers_on(w, start, end); !barrier_hits.empty()) {
		const Conduction::BarrierHit& hb = barrier_hits.front();
		stop_t = hb.t;
		anchor = start.lerp(end, f32(stop_t));
		has_anchor = true;
	}
	const double seg_len = rng_m * stop_t;
	double best_t = kInf;
	ActorState* best_actor = nullptr;
	MatBody* best_body = nullptr;
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState* t = w.actors[i].get();
		if (t == &a || t->team == a.team || t->health <= 0.0) continue;
		const double tt = (t->chest() - start).dot(dir);
		if (tt < 0.0 || tt > seg_len) continue;
		if ((start + dir * f32(tt)).distance_to(t->chest()) <= width + Sim::ACTOR_RADIUS + 0.3 && tt < best_t) {
			best_t = tt;
			best_actor = t;
			best_body = nullptr;
		}
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody* b = w.bodies[i].get();
		if (!AirUtil::carriable(*b, 80.0) || b->controller == a.id || b->mat == Mat::Air) continue;
		const double tb = (b->pos - start).dot(dir);
		if (tb < 0.0 || tb > seg_len) continue;
		if ((start + dir * f32(tb)).distance_to(b->pos) <= width + b->radius && tb < best_t) {
			best_t = tb;
			best_body = b;
			best_actor = nullptr;
		}
	}
	const Vec3 end_pt = start + dir * f32(best_t < kInf ? best_t : seg_len);
	Verbs::fx(w, a, inst, "beam", D({{"length", start.distance_to(end_pt)}, {"path", A({Value(start), Value(end_pt)})}, {"power", power}}));
	if (best_body != nullptr) {
		BodyRef keep = best_body->shared_from_this();
		AgentRef th = Agent::of_body(w, *best_body, &a);
		IxCtx ctx;
		ctx.site = "suction";
		Interactions::resolve(w, *th, *v, ctx, &Interactions::PASS_RULE());
	} else if (best_actor != nullptr) {
		Vec3 to_me = a.pos - best_actor->pos;
		to_me.y = 0.0f;
		const double pull = Charge::paramf(inst, "pull", 7.5);
		w.hit_actor(*best_actor,
		            D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "damage", 4.0)},
		               {"balance", Charge::paramf(inst, "balance", 14.0) * (Status::has(*best_actor, "flight") ? 1.5 : 1.0)},
		               {"knock", to_me.normalized() * f32(pull) + V3(0, 0.6, 0)}, {"kind", "vacuum"}, {"from", start}, {"power", power}, {"tier", v->tier},
		               {"mat", "vacuum"}}),
		            v);
	} else if (has_anchor && !Status::immune(a, "pull")) {
		const double d = Vec2(anchor.x - a.pos.x, anchor.z - a.pos.z).length();
		const double sp = clampf(std::sqrt(84.0 * maxf(d - 1.4, 0.0)), 6.0, 20.0);
		const Vec3 dv = Vec3(anchor.x - a.pos.x, 0.0f, anchor.z - a.pos.z).normalized() * f32(sp);
		a.vel.x = dv.x;
		a.vel.z = dv.z;
		w.emit("grapple", D({{"actor", a.id}, {"to", anchor}, {"speed", sp}}));
	}
	return true;
}

// ================================================================ ground: Pressure Mine

bool mine_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* z = VerbZone::spawn(w, a, inst);
	z->sub = SUB;
	z->props.set("lift", Charge::paramf(inst, "lift", 7.0));
	z->props.set("damage", Charge::paramf(inst, "damage", 8.0));
	z->props.set("balance", Charge::paramf(inst, "balance", 24.0));
	z->props.set("rate", AirUtil::NO_PAIR);
	inst.data.set("bodies", A({z->id}));
	return true;
}

// Armed after 0.4 s; the first fighter of another team within its radius sets it off (burst: lift 7 m, knock, balance).
void mine_effect(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (z.age < 0.4) return;
	ActorState* owner = w.get_actor(z.owner);
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& t = *w.actors[i];
		if (t.health <= 0.0 || t.id == z.owner || (owner != nullptr && t.team == owner->team)) continue;
		if (Vec2(t.pos.x - z.pos.x, t.pos.z - z.pos.z).length() > z.zone_radius + Sim::ACTOR_RADIUS || t.pos.y > z.pos.y + 1.2f) continue;
		const Dict prm = D({{"radius", z.zone_radius + 0.9}, {"power", z.power}, {"damage", dnum(z.props, "damage", 8.0)},
		                    {"balance", dnum(z.props, "balance", 24.0)}, {"knock", 3.0}, {"lift", dnum(z.props, "lift", 7.0)}, {"cls", "blast"},
		                    {"mat", "vacuum"}, {"heat_hu", 0.0}});
		VerbVolume::burst_at(w, owner, nullptr, z.pos + V3(0, 0.4, 0), prm);
		w.emit("mine_burst", D({{"body", z.id}, {"actor", t.id}, {"pos", z.pos}}));
		w.close_zone(z, "triggered");
		return;
	}
}

// ================================================================ guard: Null Bubble

void bubble_effect(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	z.spin = 0.0;
	AirUtil::guard_zone_effect(w, z);
}

// ================================================================ push: Pressure Wave

bool wave_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	Vec3 p = a.pos + V3(0, 1.0, 0);
	for (MatBody* z : AirUtil::zones_of(w, a.id, "null_bubble")) {
		p = z->pos + V3(0, 1.0, 0);
		w.close_zone(*z, "pressure_wave");
	}
	const Dict prm = D({{"radius", Charge::paramf(inst, "radius", 3.0)}, {"power", Charge::paramf(inst, "power", 16.0)},
	                    {"damage", Charge::paramf(inst, "damage", 6.0)}, {"balance", Charge::paramf(inst, "balance", 24.0)},
	                    {"knock", Charge::paramf(inst, "knock", 9.0)}, {"lift", Charge::paramf(inst, "lift", 1.0)}, {"cls", "blast"}, {"mat", "vacuum"},
	                    {"heat_hu", 0.0}});
	VerbVolume::burst_at(w, &a, &inst, p, prm);
	return true;
}

// ================================================================ tech: Vacuum Well

void well_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) _collapse_held(w, inst);
	Verbs::on_phase(w, a, inst, p);
}

void well_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	_collapse_held(w, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	(void)w;
	(void)dir;
	const bool ok = a.focus >= 8.0;
	return D({{"mode", "WELL"}, {"body", -1}, {"ok", ok}, {"reason", ok ? "" : "focus"}});
}

// ================================================================ evade: Pressure Hop / hold: Slipstream

// The hop lands softly: still airborne at the end of the dash, the fighter floats down (no landing lag).
void hop_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	Verbs::on_phase(w, a, inst, p);
	if (p == ActionPhase::Recovery && !a.grounded) a.gliding = true;
}

// Projectiles behind the fighter are slowed (the wake): x0.96 per tick inside 3.5 m behind the direction of travel.
void slipstream_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (inst.phase != ActionPhase::Channel) return;
	const Vec3 mv(a.vel.x, 0.0f, a.vel.z);
	if (mv.length() < 1.0f) return;
	const Vec3 back = -mv.normalized();
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!AirUtil::hostile_shot(b, &a) || b.mat == Mat::Air) continue;
		const Vec3 rel = b.pos - a.chest();
		if (rel.length() <= 3.5f && rel.dot(back) > 0.0f) b.vel *= 0.96f;
	}
}

// ================================================================ outcomes: the Vacuum column

bool o_compress(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) return false;
	if (b->mat == Mat::Sand) {
		w.convert_mat(*b, Mat::Stone, "sand_to_sandstone");
		b->tag = "sandstone";
		b->form = Form::Chunk;
		b->max_life = Sim::REMNANT_LIFETIME;
		b->attack_id = 0;
		b->vel *= 0.3f;
		AirOutcomes::report(res, "transform", "sandstone");
	} else if (AirUtil::condense(w, *b)) {
		AirOutcomes::report(res, "transform", "water");
	} else {
		return false;
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

// Fire / blasts inside a vacuum go out: bodies and zones are put out (their heat leaves through the ledger), volumes lose
// their heat budget.
bool o_snuff(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	res.stopped = true;
	res.pass_scale = 0.0;
	MatBody* b = t.body;
	if (b != nullptr && b->alive) {
		if (b->form == Form::Zone) {
			w.close_zone(*b, "extinguished");
		} else {
			w.emit("extinguish", D({{"body", b->id}}));
			w.decay_body(*b, "extinguished");
		}
	} else if (t.body == nullptr) {
		w.ledger.spent += maxf(0.0, t.heat);   // the snuffed volume's heat is booked as spent
		t.heat = 0.0;
	}
	AirOutcomes::report(res, "extinguish");
	return true;
}

// Vacuum Catch: a perfect bubble catches a light projectile (<= 30 kg) and spits it straight back as the caster's attack.
bool o_spit(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mass > AirUtil::LIGHT || !t.hostile || c.actor == nullptr) return false;
	const bool ok = Outcomes::reflect(w, t, c, res, r, ctx);
	if (ok) {
		w.emit("vacuum_catch", D({{"actor", c.actor->id}, {"body", b->id}}));
		AirOutcomes::report(res, "reflect");
	}
	return ok;
}

}  // namespace AirVacuum
}  // namespace ff
