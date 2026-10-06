// Fourfold core - port of game/combat/kits/air/air_sound.gd (Air / Sound, sub 3; MOVESET §7.16): Clap / Shout / Roar /
// Resonance, Sound Lance (bank shots), Tremor Hum, Echo Ring, Thunder Step / Boom Step, Ground Ping, Flight, Hover and
// the Sound column outcomes. Sound is pressure with a medium: P >= the charge's cohesion (6 + 4 x tier) disrupts a charge
// or channel; stone and metal walls reflect it; a vacuum nullifies it. Nothing here creates matter or heat.
#include "Combat/Kits/Air/Air.h"

#include "Combat/Kits/Air/AirUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/Materials.h"
#include "Sim/Outcomes.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace AirSound {

namespace {
const char* const HIDDEN[] = {"concealed", "fogbound", "fogwalk", "veiled"};

IxCtx site(const char* s) {
	IxCtx c;
	c.site = s;
	return c;
}

struct Cand {
	double t = 0.0;
	int kind = 0;   // 0 arena, 1 wall, 2 zone, 3 body, 4 actor
	BodyRef body;
	ActorState* actor = nullptr;
	Vec3 n;
};
}  // namespace

// ================================================================ strike: Clap / Shout / Roar / Resonance

bool clap_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 3.0);
	const double half = Charge::paramf(inst, "angle", 30.0);
	const Vec3 dir = vvec(inst.data.get("face"), a.forward());
	const double drain = Charge::paramf(inst, "drain", 0.0);
	const std::vector<ActorState*> foes = w.actors_in_cone(a, dir, rng_m, half);
	// The pulse reaches a charge (or a non-guard channel) before it lands as a hit: P >= 6 + 4 x tier disrupts.
	// A held guard is not disrupted - the pulse meets the guard's counter cell through the cone hit instead.
	AgentRef vd = Agent::of_volume(&w, &a, &inst, "sound", a.chest(), dir, D({{"P", Charge::paramf(inst, "power", 8.0)}}));
	for (ActorState* t : foes)
		if (t->health > 0.0) disrupt(w, a, *t, *vd);
	VerbVolume::cone(w, a, inst);
	if (drain > 0.0) {
		for (ActorState* t : foes) {
			if (t->health > 0.0 && (t->last_result == "hit" || t->last_result == "knockdown")) {
				t->focus = maxf(0.0, t->focus - drain);
				t->focus_idle = 0.0;
				w.emit("focus_drain", D({{"actor", t->id}, {"by", a.id}, {"amount", drain}}));
			}
		}
	}
	return true;
}

// Sound against a charge / channel: the core `disrupt` outcome decides (P >= 6 + 4 x the charge's tier).
bool disrupt(CombatWorld& w, ActorState& a, ActorState& t, Agent& v) {
	(void)a;
	if (t.action == nullptr || !Outcomes::disruptable(t)) return false;
	Agent th;
	th.kind = "volume";
	th.cls = "charge";
	th.ccls = "charge";
	th.actor = &t;
	th.hostile = true;
	const ActionRef before = t.action;
	const IxResult res = Interactions::resolve(w, th, v, site("sound"));
	return t.action != before || res.outcome == "disrupt";
}

// ================================================================ thrust: Sound Lance (bank shots)

bool lance_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double total = Charge::paramf(inst, "range", 14.0);
	double power = Charge::paramf(inst, "power", 10.0);
	const double width = Charge::paramf(inst, "width", 0.5);
	Vec3 dir = AirUtil::aim_flat(a, inst);
	Vec3 pos = a.hand_point() + V3(0, 0.2, 0);
	Array path = A({Value(pos)});
	double left = total;
	AgentRef v = Agent::of_volume(&w, &a, &inst, "sound", pos, dir, D({{"P", power}}));
	bool hit_someone = false;
	for (int seg = 0; seg < MAX_BOUNCES + 1; ++seg) {
		const Vec3 end = pos + dir * f32(left);
		Vec3 reflect_n;
		std::vector<Cand> cands;
		// barriers (arena solids, walls, barrier zones) nearest first
		const Dict ah = AirUtil::arena_hit(w, pos, end);
		if (!ah.empty()) {
			Cand c;
			c.t = dnum(ah, "t") * left;
			c.kind = 0;
			c.n = dvec(ah, "n");
			cands.push_back(c);
		}
		for (size_t i = 0; i < w.bodies.size(); ++i) {
			const BodyRef& bref = w.bodies[i];
			MatBody& b = *bref;
			if (!b.alive || &b == w.pool || (b.static_body && b.form != Form::Wall && b.form != Form::Zone)) continue;
			if (b.form == Form::Wall && b.wall_rise > 0.5) {
				const double tw = w.wall_segment_t(pos, end, b);
				if (tw >= 0.0) cands.push_back(Cand{tw * left, 1, bref, nullptr, Vec3()});
			} else if (b.form == Form::Zone && dbool(b.props, "barrier", false)) {
				const double tz = Conduction::_cyl_t(pos, end, b.pos, b.zone_radius, dnum(b.props, "height", 2.5));
				if (tz >= 0.0) cands.push_back(Cand{tz * left, 2, bref, nullptr, Vec3()});
			} else if (AirUtil::carriable(b, 80.0) && b.controller != a.id && b.mat != Mat::Air) {
				const double tb = (b.pos - pos).dot(dir);
				if (tb >= 0.0 && tb <= left && (pos + dir * f32(tb)).distance_to(b.pos) <= width + b.radius) cands.push_back(Cand{tb, 3, bref, nullptr, Vec3()});
			}
		}
		for (size_t i = 0; i < w.actors.size(); ++i) {
			ActorState* t = w.actors[i].get();
			if (t == &a || t->team == a.team || t->health <= 0.0) continue;
			const double tt = (t->chest() - pos).dot(dir);
			if (tt >= 0.0 && tt <= left && (pos + dir * f32(tt)).distance_to(t->chest()) <= width + Sim::ACTOR_RADIUS + 0.2)
				cands.push_back(Cand{tt, 4, nullptr, t, Vec3()});
		}
		std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.t < y.t; });
		bool ended = false;
		double hit_at = left;
		for (const Cand& c : cands) {
			const double at = c.t;
			if (c.kind == 4) {
				ActorState& t = *c.actor;
				const Dict info =
				    D({{"attacker", a.id}, {"attack_id", inst.attack_id},
				       {"damage", Charge::paramf(inst, "damage", 7.0) * power / maxf(Charge::paramf(inst, "power", 10.0), 1.0)},
				       {"balance", Charge::paramf(inst, "balance", 16.0)}, {"knock", dir * f32(Charge::paramf(inst, "knock", 3.0)) + V3(0, 0.4, 0)},
				       {"kind", "sound"}, {"from", pos}, {"power", power}, {"tier", v->tier}, {"mat", "sound"}});
				disrupt(w, a, t, *v);
				const std::string res = w.hit_actor(t, info, v);
				if (res == "hit" || res == "knockdown") hit_someone = true;
				hit_at = at;
				ended = true;
				break;
			}
			if (c.kind == 3) {
				AgentRef th = Agent::of_body(w, *c.body, &a);
				Interactions::resolve(w, *th, *v, site("lance"), &Interactions::PASS_RULE());
				continue;
			}
			if (c.kind == 0) {
				reflect_n = c.n;
				hit_at = at;
				break;
			}
			if (c.kind == 1) {
				MatBody& wall = *c.body;
				const bool hard = wall.mat == Mat::Stone || wall.mat == Mat::Metal || wall.mat == Mat::Glass;
				if (hard) {
					reflect_n = AirUtil::wall_normal(wall, pos);
					hit_at = at;
					break;
				}
				const IxResult r = VerbVolume::meet_body(w, &a, *v, wall);
				if (r.stopped || r.pass_scale <= 0.0) {
					hit_at = at;
					ended = true;
					break;
				}
				continue;
			}
			if (c.kind == 2) {
				const IxResult rz = VerbVolume::meet_body(w, &a, *v, *c.body);
				if (rz.stopped || rz.pass_scale <= 0.0) {
					hit_at = at;
					ended = true;
					break;
				}
				v->power *= rz.pass_scale;
			}
		}
		const Vec3 stop = pos + dir * f32(minf(hit_at, left));
		path.push_back(stop);
		left -= static_cast<double>(pos.distance_to(stop));
		if (ended || reflect_n == Vec3() || left < 0.5 || seg == MAX_BOUNCES) break;
		// reflect: the pulse goes on from the surface with the mirrored heading, a little weaker
		dir = AirUtil::reflect_dir(dir, reflect_n);
		pos = stop + reflect_n * 0.08f;
		v->power *= 0.8;
		power *= 0.8;
		w.emit("ricochet", D({{"pos", stop}, {"by", a.id}, {"dir", dir}, {"power", power}}));
	}
	Verbs::fx(w, a, inst, "beam", D({{"length", total - left}, {"path", path}, {"power", power}}));
	inst.data.set("bank", static_cast<int>(path.size()) - 2);
	inst.data.set("hit", hit_someone);
	return true;
}

// ================================================================ ground: Tremor Hum

// The tremor cracks every wall its front passes (once per wall): wall damage grows with its power; at 1.0 it crumbles.
bool tremor_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.form != Form::Wave) return false;
	Dict done = b.props.has("cracked") ? ddict(b.props, "cracked") : Dict();
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		const std::string key = itos(o.id);
		if (!o.alive || o.form != Form::Wall || o.wall_rise < 0.5 || done.has(key)) continue;
		const double reach = b.wave_width * 0.5 + maxf(o.wall_half.x, o.wall_half.z);
		if (Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() > reach) continue;
		done.set(key, true);
		const double cp = maxf(1.0, o.mass * Materials::hardness(o));
		o.wall_damage_add(clampf(b.power / cp, 0.1, 1.0) * 0.6);
		w.emit("wall_crack", D({{"body", o.id}, {"by", b.attack_owner}, {"damage", o.wall_damage}}));
		if (o.wall_damage >= 1.0) w._crumble_wall(o);
	}
	b.props.set("cracked", done);
	return false;
}

// ================================================================ sweep: Echo Ring

bool echo_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double r = Charge::paramf(inst, "radius", 4.0);
	const double power = Charge::paramf(inst, "power", 10.0);
	const Dict prm = D({{"radius", r}, {"power", power}, {"damage", Charge::paramf(inst, "damage", 3.0)}, {"balance", Charge::paramf(inst, "balance", 10.0)},
	                    {"knock", Charge::paramf(inst, "knock", 2.0)}, {"lift", Charge::paramf(inst, "lift", 0.3)}, {"cls", "sound"}, {"mat", "sound"},
	                    {"heat_hu", 0.0}});
	AgentRef v = Agent::of_volume(&w, &a, &inst, "sound", a.pos + V3(0, 1.0, 0), Vec3(), D({{"P", power}}));
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& t = *w.actors[i];
		if (&t == &a || t.team == a.team || t.health <= 0.0 || t.pos.distance_to(a.pos) > r + Sim::ACTOR_RADIUS) continue;
		if (reveal(w, t, a.id)) w.emit("revealed", D({{"actor", t.id}, {"by", a.id}}));
		disrupt(w, a, t, *v);
	}
	VerbVolume::burst_at(w, &a, &inst, a.pos + V3(0, 1.0, 0), prm);
	return true;
}

// Removes every hiding status from a fighter and marks them revealed for 2 s. True when something was hiding.
bool reveal(CombatWorld& w, ActorState& t, int by) {
	bool was = false;
	for (const char* st : HIDDEN) {
		if (t.status.has(st)) {
			Status::remove(w, t, st);
			was = true;
		}
	}
	if (was) Status::apply(w, t, "revealed", 2.0, 1.0, by);
	return was;
}

// ================================================================ push: Thunder Step / sink: Ground Ping

// The boom at the end of a Thunder Step / Boom Step: a burst in front of the fighter.
void boom_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	Verbs::on_phase(w, a, inst, p);
	if (p != ActionPhase::Recovery || dbool(inst.data, "boomed", false)) return;
	inst.data.set("boomed", true);
	const bool ahead = inst.id == "sound_thunder_step";
	const Vec3 centre = a.pos + a.forward() * (ahead ? 1.2f : 0.0f) + V3(0, 1.0, 0);
	const Dict prm = D({{"radius", Charge::paramf(inst, "radius", 2.0)}, {"power", Charge::paramf(inst, "power", 8.0)},
	                    {"damage", Charge::paramf(inst, "damage", 4.0)}, {"balance", Charge::paramf(inst, "balance", 10.0)},
	                    {"knock", Charge::paramf(inst, "knock", 3.0)}, {"lift", 0.8}, {"cls", "sound"}, {"mat", "sound"}, {"heat_hu", 0.0}});
	VerbVolume::burst_at(w, &a, &inst, centre, prm);
}

bool ping_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double r = Charge::paramf(inst, "radius", 6.0);
	const double power = Charge::paramf(inst, "power", 12.0);
	Verbs::fx(w, a, inst, "ring", D({{"pos", a.pos}, {"radius", r}, {"power", power}, {"shape", "ground"}}));
	AgentRef c = Agent::of_move(&w, &a, "sound_ping", inst.tier(), false);
	c->pos = a.pos;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (!b.alive || b.form != Form::Wave || b.attack_owner == a.id || Vec2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length() > r) continue;
		AgentRef th = Agent::of_body(w, b, &a);
		Interactions::resolve(w, *th, *c, site("ping"), &Interactions::PASS_RULE());
	}
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& t = *w.actors[i];
		if (&t == &a || t.team == a.team || t.health <= 0.0 || t.pos.distance_to(a.pos) > r) continue;
		if (reveal(w, t, a.id)) w.emit("revealed", D({{"actor", t.id}, {"by", a.id}, {"ground", true}}));
	}
	return true;
}

// ================================================================ tech: Flight

MatBody* flight_zone(CombatWorld& w, const ActorState& a) {
	const std::vector<MatBody*> zs = AirUtil::zones_of(w, a.id, "flight_field");
	return zs.empty() ? nullptr : zs[0];
}

void flight_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	inst.data.set("move_scale", 1.0);
	if (flight_zone(w, a) != nullptr) {
		inst.data.set("land", true);   // pressing again: no cost, glide down
		return;
	}
	if (!Verbs::pay(w, a, inst, "start")) {
		inst.data.set("fizzle", true);
		return;
	}
	Verbs::fx(w, a, inst, "cast");
}

ActionPhase flight_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (dbool(inst.data, "land", false)) {
		end_flight(w, a, "landed");
		return ActionPhase::Recovery;
	}
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	MatBody* z = AirUtil::zone(w, "flight_field", a.pos, 0.5, a.id, 0.0, -1.0,
	                           D({{"attach", a.id}, {"attach_off", Vec3()}, {"height", 3.0}, {"rate", AirUtil::NO_PAIR}}), 0, SUB);
	z->props.set("height_target", Charge::paramf(inst, "height", FLIGHT_HEIGHT));
	a.flying = true;
	a.stance = "flight";
	a.gliding = false;
	Status::apply(w, a, "flight", -1.0, 1.0, a.id);
	w.emit("mode", D({{"actor", a.id}, {"kind", "flight"}, {"on", true}, {"move", inst.id}}));
	Verbs::fx(w, a, inst, "aura", D({{"on", true}, {"shape", "flight"}}));
	return ActionPhase::Recovery;
}

// Per tick of the flight field: hold 2.2 m above the ground, pay the upkeep, end on a knockdown, an empty Focus pool or death.
void flight_effect(CombatWorld& w, MatBody& z, double dt) {
	ActorState* a = w.get_actor(z.owner);
	if (a == nullptr || a->health <= 0.0 || a->stun_kind == "knockdown") {
		end_flight_zone(w, z, "down");
		return;
	}
	if (!w.spend_focus(*a, FLIGHT_UPKEEP * dt)) {
		w.emit("insufficient", D({{"actor", a->id}, {"what", "focus"}, {"move", "sound_flight"}}));
		end_flight(w, *a, "focus");
		return;
	}
	if (!Status::has(*a, "flight")) Status::apply(w, *a, "flight", -1.0, 1.0, a->id);
	a->flying = true;
	const double hg = w.arena.ground_height(a->pos.x, a->pos.z, a->pos.y + 3.0);
	const double target_y = hg + dnum(z.props, "height_target", FLIGHT_HEIGHT);
	a->grounded = false;
	a->vel.y = f32(clampf((target_y - a->pos.y) * 6.0, -4.0, 6.0));
}

void end_flight_zone(CombatWorld& w, MatBody& z, const std::string& why) {
	ActorState* a = w.get_actor(z.owner);
	if (z.alive) w.close_zone(z, why);
	if (a != nullptr) _flight_off(w, *a, why);
}

// Ends the fighter's flight (a landing press, a downdraft, a tornado, no Focus): the hover field closes, a glide follows.
void end_flight(CombatWorld& w, ActorState& a, const std::string& why) {
	MatBody* z = flight_zone(w, a);
	if (z != nullptr) w.close_zone(*z, why);
	_flight_off(w, a, why);
}

void _flight_off(CombatWorld& w, ActorState& a, const std::string& why) {
	const bool was = a.flying || Status::has(a, "flight");
	if (Status::has(a, "flight")) Status::remove(w, a, "flight");
	if (a.stance == "flight") a.stance = "";
	if (a.stance != "hover" && (a.action == nullptr || !a.action->data.has("hover_height"))) a.flying = false;
	if (was) {
		if (!a.grounded && why != "slammed" && why != "down") a.gliding = true;   // release = glide down
		w.emit("mode", D({{"actor", a.id}, {"kind", "flight"}, {"on", false}, {"why", why}}));
	}
}

Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	(void)dir;
	if (flight_zone(w, a) != nullptr) return D({{"mode", "LAND"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	const bool ok = a.focus >= 6.0;
	return D({{"mode", "FLY"}, {"body", -1}, {"ok", ok}, {"reason", ok ? "" : "focus"}});
}

// ================================================================ evade: Boom Step / hold: Hover

// Hover holds the height the fighter has when the hold starts (at least 0.9 m, at most 3.5 m).
ActionPhase hover_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const ActionPhase phase = Verbs::after_startup(w, a, inst, it);
	if (phase == ActionPhase::Channel) {
		const double g = w.arena.ground_height(a.pos.x, a.pos.z, a.pos.y + 0.5);
		inst.data.set("hover_height", clampf(a.pos.y - g, 0.9, 3.5));
	}
	return phase;
}

// ================================================================ outcomes: the Sound column

// A loose stone on a tremor's path is popped up (vy ~ 3.5 + P / 6, less for heavy ones) and can be seized in the air.
bool o_pop(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) return false;
	const double vy = clampf((3.5 + res.cp_eff / 6.0) * std::pow(20.0 / maxf(b->mass, 4.0), 0.35), 1.5, 8.0);
	b->vel.y = f32(maxf(b->vel.y, vy));
	b->vel += c.dir * 1.5f;
	b->on_ground = false;
	b->rest_time = 0.0;
	b->touch(Outcomes::_id(&c), "pop", w.tick);
	w.emit("pop", D({{"body", b->id}, {"vy", vy}}));
	res.pass_scale = 1.0;
	AirOutcomes::report(res, "push");
	return true;
}

// A ground line that the Ground Ping out-powers is stilled on the spot (settles / fades through its own end rules).
bool o_still(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) return false;
	res.stopped = true;
	res.pass_scale = 0.0;
	if (b->form == Form::Wave) {
		w.emit("wave_disrupted", D({{"body", b->id}, {"by", "ping"}}));
		w._settle_wave(*b, "disrupted");
	} else if (b->form == Form::Zone) {
		w.close_zone(*b, "disrupted");
	} else {
		b->attack_id = 0;
		b->vel *= 0.2f;
	}
	AirOutcomes::report(res, "disrupt");
	return true;
}

}  // namespace AirSound
}  // namespace ff
