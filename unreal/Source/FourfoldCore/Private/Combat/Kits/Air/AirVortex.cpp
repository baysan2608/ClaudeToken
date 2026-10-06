// Fourfold core - port of game/combat/kits/air/air_vortex.gd (Air / Vortex, sub 1; MOVESET §7.14). A tornado is a ZONE
// body (tag tornado): it walks to the target, captures and orbits bodies <= 30 kg (cells x|tornado), lifts fighters and
// takes the character of what it carries (sand / fire / water / steam / spilled lava). A tornado infused by the enemy's
// material turns neutral. Nothing here creates matter or heat; wind cools what it carries through the ambient ledger.
#include "Combat/Kits/Air/Air.h"

#include "Combat/Kits/Air/AirUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Outcomes.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace AirVortex {

namespace {
void vec_erase(std::vector<int>& v, int id) {
	auto it = std::find(v.begin(), v.end(), id);
	if (it != v.end()) v.erase(it);
}

// INFUSION_STATUS
std::vector<const char*> infusion_status(const std::string& kind) {
	if (kind == "sand") return {"blinded", "sandblasted"};
	if (kind == "fire" || kind == "magma") return {"burning"};
	if (kind == "water") return {"wet"};
	if (kind == "steam") return {"scalded"};
	return {};
}

IxCtx site(const char* s) {
	IxCtx c;
	c.site = s;
	return c;
}

// Vortex zones the fighter can unleash / drop: the kept Vortex Wall first, then their own tornado nearby.
std::vector<MatBody*> _own_vortices(CombatWorld& w, const ActorState& a) {
	std::vector<MatBody*> out = AirUtil::zones_of(w, a.id, "vortex_wall");
	for (MatBody* z : AirUtil::zones_of(w, a.id, "tornado"))
		if (z->pos.distance_to(a.pos) <= 8.0f) out.push_back(z);
	return out;
}

void _whirl_end(CombatWorld& w, ActionInst& inst) {
	MatBody* z = w.get_body(dint(inst.data, "whirl", -1));
	inst.data.erase("whirl");
	if (z != nullptr && z->alive) w.close_zone(*z, "whirl_end");
}
}  // namespace

// ================================================================ strike: Twister / Tornado / Cyclone Fortress

bool twister_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (inst.tier() >= 2) {
		MatBody* z = VerbZone::spawn(w, a, inst);
		z->sub = SUB;
		z->props.set("lift", true);
		z->props.set("rate", 0.1);
		z->spin = SPIN;
		inst.data.set("bodies", A({z->id}));
		return true;
	}
	for (MatBody* b : VerbProjectile::fire(w, a, inst)) {
		b->radius = Charge::paramf(inst, "radius", 0.8);
		b->spin = 12.0;
	}
	return true;
}

// A twister (projectile) passes: it catches the small bodies it meets (cells x|tornado with the twister as the
// counter) and lifts the fighters it touches. It keeps flying through them.
bool twister_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.attack_id == 0 || b.form == Form::Zone) return false;
	b.spin = 12.0;
	ActorState* owner = w.get_actor(b.attack_owner);
	AgentRef c;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !AirUtil::carriable(o)) continue;
		if (o.attack_id != 0 && o.attack_owner == b.attack_owner) continue;
		if (o.mat == Mat::Air) continue;
		if (o.pos.distance_to(b.pos) > b.radius + o.radius + 0.4) continue;
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
		Interactions::resolve(w, *th, *c, site("twister"), &Interactions::PASS_RULE());
		if (!b.alive) return false;
	}
	if (owner == nullptr) return false;
	for (auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == owner || t.team == owner->team || t.health <= 0.0 || b.hit_set.has(t.id)) continue;
		if (b.pos.distance_to(t.chest()) > b.radius + Sim::ACTOR_RADIUS + 0.5) continue;
		b.hit_set.add(t.id);
		const double lift = 6.0 + 0.2 * b.power;
		AgentRef ag = Agent::of_body(w, b);
		ag->actor = owner;
		w.hit_actor(t,
		            D({{"attacker", owner->id}, {"attack_id", b.attack_id}, {"damage", b.damage}, {"balance", b.balance_damage},
		               {"knock", b.vel.normalized() * 3.0f + V3(0, lift, 0)}, {"kind", "air"}, {"from", b.pos}, {"power", b.power}, {"tier", b.tier},
		               {"mat", "vortex"}}),
		            ag);
	}
	return false;
}

// ================================================================ the tornado zone

// Per tick: spin, orbit and cool what it carries, take the character of its infusions, lift and swirl the fighters in it.
void tornado_effect(CombatWorld& w, MatBody& z, double dt) {
	z.spin = SPIN;
	const Dict inf = _scan_infusions(w, z);
	_orbit(w, z, dt);
	z.charge = inf.has("water") ? 0.02 : 0.0;   // a water tornado conducts (Materials.conducts: charged bodies)
	const double lift_k = clampf(z.power / 25.0, 0.4, 1.6);
	const bool neutral = dbool(z.props, "neutral", false);
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& a = *w.actors[i];
		if (a.health <= 0.0 || (a.id == z.owner && !neutral)) continue;
		if (!w._in_zone(z, a.pos + V3(0, 0.9, 0), Sim::ACTOR_RADIUS)) continue;
		Vec3 rel = a.pos - z.pos;
		rel.y = 0.0f;
		const double d = rel.length();
		const Vec3 tangent = d > 0.05 ? Vec3(-rel.z, 0.0f, rel.x).normalized() : Vec3(1.0f, 0.0f, 0.0f);
		if (!Status::immune(a, "pull")) {
			const Vec3 inward = d > 0.05 ? -rel.normalized() * f32(minf(d, 3.0)) * 1.2f : Vec3();
			a.vel += (tangent * 7.0f + inward) * f32(dt);
		}
		if (!Status::immune(a, "lift")) {
			const double height = a.pos.y - w.arena.ground_height(a.pos.x, a.pos.z, a.pos.y + 0.3);
			const double vy_t = height < 3.4 ? 5.0 * lift_k : 0.5;
			a.vel.y = f32(move_toward(a.vel.y, vy_t, 40.0 * dt));
			a.grounded = false;
			Status::apply(w, a, "windborne", 0.3, 1.0, z.owner);
			// the whirl wears balance down (x1.5 for fighters held up by wind: flight)
			a.balance = minf(a.balance, maxf(8.0, a.balance - (Status::has(a, "flight") ? 10.5 : 7.0) * dt));
			a.balance_idle = 0.0;
			if (Status::has(a, "flight")) AirSound::end_flight(w, a, "tornado");
		}
		for (const auto& kv : inf)
			for (const char* st : infusion_status(kv.first)) Status::apply(w, a, st, 0.4, 1.0, z.owner);
		if (z.props.has("dps") && dnum(z.props, "dps") > 0.0 && inf.size() > 0)
			a.health = maxf(0.0, a.health - 1.5 * static_cast<double>(inf.size()) * dt);
	}
}

// What the tornado carries / has soaked up, as infusion kinds -> count: sand, fire, water, steam, magma.
Dict _scan_infusions(CombatWorld& w, MatBody& z) {
	Dict out;
	for (int id : z.captured) {
		MatBody* b = w.get_body(id);
		if (b == nullptr || !b->alive || b->captured_by != z.id) continue;
		const std::string k = infusion_kind(*b);
		if (!k.empty()) out.set(k, dint(out, k, 0) + 1);
	}
	if (dnum(z.props, "spatter_until", -1.0) > z.age) out.set("magma", dint(out, "magma", 0) + 1);
	Dict seen = z.props.has("inf_seen") ? ddict(z.props, "inf_seen") : Dict();
	for (const auto& kv : out) {
		if (!seen.has(kv.first)) {
			seen.set(kv.first, true);
			w.emit("infuse", D({{"body", z.id}, {"with", kv.first}, {"owner", z.owner}, {"neutral", dbool(z.props, "neutral", false)}}));
		}
	}
	z.props.set("inf_seen", seen);
	std::string joined;
	for (const auto& kv : out) joined += (joined.empty() ? "" : ", ") + kv.first;
	z.props.set("infused", joined);
	return out;
}

std::string infusion_kind(const MatBody& b) {
	switch (b.mat) {
		case Mat::Sand: return "sand";
		case Mat::Fire: return b.heat_payload > 2.0 ? "fire" : "";
		case Mat::Water: return b.phase == Phase::Liquid && b.form != Form::Cloud ? "water" : "";
		case Mat::Steam: return "steam";
		case Mat::Stone: return b.liquid > 0.0 ? "magma" : "";
		default: return "";
	}
}

// Captured bodies orbit in a helix inside the zone; hot ones cool in the wind (convective cooling, booked ambient).
void _orbit(CombatWorld& w, MatBody& z, double dt) {
	int n = 0;
	const double radius = maxf(0.6, z.zone_radius * 0.6);
	const std::vector<int> ids = z.captured;
	for (int id : ids) {
		MatBody* b = w.get_body(id);
		if (b == nullptr || !b->alive || b->captured_by != z.id) {
			vec_erase(z.captured, id);
			continue;
		}
		const Vec3 off = dvec(b->props, "capture_off", Vec3());
		Vec3 flat(off.x, 0.0f, off.z);
		const double l = flat.length();
		const double want = radius * (0.7 + 0.3 * static_cast<double>(n % 3) / 2.0);
		flat = (l > 0.05 ? flat / f32(l) : Vec3(1.0f, 0.0f, 0.0f)) * f32(move_toward(l, want, 2.0 * dt));
		const double y = 0.9 + 0.3 * static_cast<double>(n % 4) + 0.35 * std::sin(static_cast<double>(w.tick) * dt * 3.0 + static_cast<double>(n) * 1.7);
		b->props.set("capture_off", Vec3(flat.x, f32(y), flat.z));
		if ((b->mat == Mat::Stone || b->mat == Mat::Metal || b->mat == Mat::Sand || b->mat == Mat::Glass) && b->thermal_energy() > 0.0) {
			const double rate = 40.0 * clampf(z.power / 25.0, 0.4, 2.0) * dt;
			const double got = -Thermal::heat(*b, -minf(rate, b->thermal_energy()));
			w.ledger.ambient -= got;
		}
		++n;
	}
}

// Tornado -> neutral: an infusion made of the enemy's material turns the whirl against both fighters.
void make_neutral(CombatWorld& w, MatBody& z, int src) {
	if (dbool(z.props, "neutral", false)) return;
	z.props.set("origin_owner", z.owner);
	z.props.set("neutral", true);
	z.props.set("spare_owner", false);
	z.owner = -1;
	w.emit("infuse", D({{"body", z.id}, {"with", "enemy"}, {"owner", -1}, {"neutral", true}, {"src", src}}));
}

// ================================================================ sweep: Eddy Ring

// The ring pushes the fighters inside it outward (light fighters: anchored ones stay).
void eddy_effect(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	z.spin = 10.0;
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState& t = *w.actors[i];
		if (t.health <= 0.0 || t.id == z.owner) continue;
		if (!w._in_zone(z, t.pos + V3(0, 0.9, 0), Sim::ACTOR_RADIUS)) continue;
		Vec3 rel = t.pos - z.pos;
		rel.y = 0.0f;
		const Vec3 out = rel.length() > 0.05f ? rel.normalized() : -t.forward();
		const double want = 5.0 + 0.2 * z.power;
		const double vout = t.vel.dot(out);
		if (vout < want) AirUtil::shove(t, out * f32(want - vout));
	}
}

// ================================================================ guard: Vortex Wall / Vortex Catch

// The Vortex Wall zone (attached to the guard): a body entering its radius is met through the rules with the guard's
// perfect timing (Vortex Catch); orbit management like the tornado's.
void wall_effect(CombatWorld& w, MatBody& z, double dt) {
	z.spin = 9.0;
	AirUtil::guard_zone_effect(w, z);
	if (z.alive) _orbit(w, z, dt);
}

// ================================================================ push: Unleash / sink: Funnel Down

bool unleash_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Vec3 target = Verbs::target_point(w, a, inst, 16.0);
	const double speed = Charge::paramf(inst, "speed", 20.0);
	int n = 0;
	for (MatBody* z : _own_vortices(w, a)) {
		const std::vector<int> ids = z->captured;
		for (int id : ids) {
			MatBody* b = w.get_body(id);
			if (b == nullptr || !b->alive || b->captured_by != z->id) continue;
			vec_erase(z->captured, id);
			b->captured_by = -1;
			b->props.erase("capture_off");
			b->pos = b->pos + V3(0, 0.1, 0);
			const double spd = speed * (dbool(b->props, "caught_perfect", false) ? 1.25 : 1.0);
			b->gravity_scale = 0.3;
			b->vel = Verbs::launch_vel(b->pos, target + V3(0, 0.0, 0), spd, 0.3);
			b->on_ground = false;
			const double dmg = Charge::paramf(inst, "damage", 8.0) + 0.4 * b->mass;
			Verbs::arm(w, a, inst, *b, dmg, Charge::paramf(inst, "balance", 20.0) + 0.5 * b->mass);
			b->touch(a.id, "unleash", w.tick);
			w.emit("unleash", D({{"actor", a.id}, {"body", b->id}, {"speed", spd}, {"tier", inst.tier()}}));
			Verbs::fx(w, a, inst, "release", D({{"body", b->id}, {"pos", b->pos}, {"dir", b->vel.normalized()}, {"power", b->mass * spd / 20.0}}));
			++n;
		}
		if (z->tag == "vortex_wall") w.close_zone(*z, "unleashed");
	}
	inst.data.set("unleashed", n);
	if (n == 0) w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
	return true;
}

bool funnel_down_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	int dropped = 0;
	for (MatBody* z : _own_vortices(w, a)) {
		int k = 0;
		const std::vector<int> ids = z->captured;
		for (int id : ids) {
			MatBody* b = w.get_body(id);
			if (b == nullptr || !b->alive || b->captured_by != z->id) continue;
			vec_erase(z->captured, id);
			b->captured_by = -1;
			b->props.erase("capture_off");
			const double ang = kTau * static_cast<double>(k) / maxf(1.0, static_cast<double>(z->captured.size() + 1));
			b->pos = a.pos + V3(std::cos(ang), 0.0, std::sin(ang)) * 0.9f + V3(0, 0.6, 0);
			b->vel = V3(0, -2.0, 0);
			b->attack_id = 0;
			b->on_ground = false;
			++dropped;
			++k;
		}
		if (z->tag == "vortex_wall") w.close_zone(*z, "funnel_down");
	}
	inst.data.set("dropped", dropped);
	const Dict prm = D({{"radius", Charge::paramf(inst, "radius", 2.2)}, {"power", Charge::paramf(inst, "power", 10.0)},
	                    {"damage", Charge::paramf(inst, "damage", 3.0)}, {"balance", Charge::paramf(inst, "balance", 12.0)},
	                    {"knock", Charge::paramf(inst, "knock", 3.0)}, {"lift", Charge::paramf(inst, "lift", 1.0)}, {"cls", "gust"}, {"mat", "vortex"},
	                    {"heat_hu", 0.0}});
	VerbVolume::burst_at(w, &a, &inst, a.pos + V3(0, 0.2, 0), prm);
	Verbs::fx(w, a, inst, "burst", D({{"pos", a.pos}, {"radius", 2.2}, {"shape", "ground"}, {"mat", "sand"}}));
	return true;
}

// ================================================================ tech: Eye of the Storm

MatBody* _enemy_tornado_near(CombatWorld& w, const ActorState& a, Vec3 p, double r) {
	MatBody* best = nullptr;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody* z = w.bodies[i].get();
		if (!z->alive || z->form != Form::Zone || z->tag != "tornado" || z->owner == a.id) continue;
		if (z->owner >= 0) {
			ActorState* o = w.get_actor(z->owner);
			if (o != nullptr && o->team == a.team) continue;
		}
		if (Vec2(z->pos.x - p.x, z->pos.z - p.z).length() <= r + z->zone_radius && (best == nullptr || z->id < best->id)) best = z;
	}
	return best;
}

// A tornado already stands at the aim point: contest it. Strong enough (the Eye's power >= 90 % of it), the caster takes
// it over and steers it; otherwise the new tornado is summoned beside it and the two meet through the rules.
ActionPhase eye_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	const Vec3 ap = AirUtil::aim_ground(w, a, inst, 10.0);
	MatBody* enemy = _enemy_tornado_near(w, a, ap, 1.0);
	if (enemy != nullptr && Charge::paramf(inst, "power", 25.0) >= enemy->power * 0.9) {
		const int from = enemy->owner;
		enemy->props.set("origin_owner", from);
		enemy->owner = a.id;
		enemy->props.set("neutral", false);
		enemy->props.set("spare_owner", true);
		enemy->props.erase("walk_speed");
		enemy->props.erase("walk_target");
		enemy->max_life = -1.0;
		inst.data.set("summon", enemy->id);
		inst.data.set("took_over", true);
		w.emit("tornado_taken", D({{"body", enemy->id}, {"by", a.id}, {"from", from}}));
		FxEvents::fx_for(w, a, inst, "ring", "vortex", D({{"pos", enemy->pos}, {"radius", enemy->zone_radius}, {"body", enemy->id}, {"power", enemy->power}}));
		return ActionPhase::Channel;
	}
	const ActionPhase phase = Verbs::after_startup(w, a, inst, it);
	MatBody* z = w.get_body(dint(inst.data, "summon", -1));
	if (z != nullptr) {
		z->sub = SUB;
		z->spin = SPIN;
	}
	return phase;
}

void eye_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	MatBody* z = w.get_body(dint(inst.data, "summon", -1));
	if (z != nullptr && z->alive && dbool(inst.data, "took_over", false)) z->owner = a.id;
}

Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	const Vec3 p = a.pos + dir * 8.0f;
	if (_enemy_tornado_near(w, a, p, 1.0) != nullptr) return D({{"mode", "CONTEST"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	return D({{"mode", "STORM"}, {"body", -1}, {"ok", a.focus >= 10.0}, {"reason", a.focus >= 10.0 ? "" : "focus"}});
}

// ================================================================ evade: Spin Step / Whirl Lift

// While the sidestep spins, light hostile projectiles near the fighter are met by the eddy cells (curved / deflected).
void spin_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Active) return;
	AgentRef c = Agent::of_move(&w, &a, "vortex_spin_step", inst.tier(), false);
	c->pos = a.chest();
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (!AirUtil::hostile_shot(b, &a) || b.pos.distance_to(a.chest()) > 1.6 + b.radius) continue;
		const std::string key = itos(b.id);
		if (ddict(inst.data, "spun").has(key)) continue;
		Dict done = inst.data.has("spun") ? ddict(inst.data, "spun") : Dict();
		done.set(key, true);
		inst.data.set("spun", done);
		c->dir = a.forward();
		AgentRef th = Agent::of_body(w, b, &a);
		Interactions::resolve(w, *th, *c, site("spin"), &Interactions::PASS_RULE());
	}
}

ActionPhase whirl_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const ActionPhase phase = Verbs::after_startup(w, a, inst, it);
	if (phase == ActionPhase::Channel) {
		MatBody* z = AirUtil::zone(w, "eddy", a.pos, 1.3, a.id, 10.0, -1.0,
		                           D({{"attach", a.id}, {"attach_off", Vec3()}, {"height", 2.4}, {"rate", 0.1}}), 0, SUB);
		z->spin = 12.0;
		inst.data.set("whirl", z->id);
	}
	return phase;
}

void whirl_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) _whirl_end(w, inst);
	Verbs::on_phase(w, a, inst, p);
}

void whirl_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	_whirl_end(w, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

// ================================================================ outcomes: the Vortex column

// Capture into a vortex (zone, twister, funnel wave): the body orbits, harmless, until the vortex ends or the caster
// unleashes it. A tornado takes the character of what it catches; enemy material turns it neutral.
bool o_infuse(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	MatBody* cb = c.body;
	if (b == nullptr || cb == nullptr || !b->alive || !cb->alive || cb == b) return false;
	if (!AirUtil::carriable(*b, 1.0e9)) return false;
	if (b->mass > dnum(r, "max_mass", AirUtil::LIGHT)) {
		// too heavy to catch: a hostile shot is slowed and bent, a resting heavy body is left alone
		if (t.hostile) return o_slow_bend(w, t, c, res, r, ctx);
		res.pass_scale = 1.0;
		AirOutcomes::report(res, "pass");
		return true;
	}
	double spd = 0.0;
	if (cb->form == Form::Wave) spd = 16.0;
	else if (cb->form != Form::Zone) spd = 12.0;
	const Dict rule = D({{"max_captured", dint(r, "max_captured", MAX_CAPTURED)}, {"release_speed", spd}});
	const int src = AirUtil::source_owner(*b);   // before the capture makes the body harmless
	if (!Outcomes::capture(w, t, c, res, rule, ctx)) return false;
	b->props.set("release_damage", 8.0 + 0.3 * b->mass);
	b->props.set("caught_by", cb->id);
	if (c.perfect) b->props.set("caught_perfect", true);
	const std::string kind = infusion_kind(*b);
	if (cb->form == Form::Zone && cb->tag == "tornado" && !kind.empty()) {
		ActorState* caster = w.get_actor(cb->owner);
		ActorState* srca = w.get_actor(src);
		if (cb->owner >= 0 && src >= 0 && src != cb->owner && srca != nullptr && caster != nullptr && srca->team != caster->team) make_neutral(w, *cb, src);
	}
	AirOutcomes::report(res, "capture", kind);
	return true;
}

// Vortex Catch (a perfect Vortex Wall): catches shots up to 45 kg (not only 30) and marks them for a faster Unleash.
bool o_catch(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	Dict rr = r.duplicate();
	rr.set("max_mass", 45.0);
	MatBody* b = t.body;
	if (b != nullptr && b->alive && b->mass > 45.0) return false;
	const bool ok = o_infuse(w, t, c, res, rr, ctx);
	if (ok) w.emit("vortex_catch", D({{"actor", Outcomes::_id(&c)}, {"body", b->id}, {"mass", b->mass}}));
	return ok;
}

// A body too heavy to catch: it is slowed and bent by the whirl.
bool o_slow_bend(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	Outcomes::slow(w, t, c, res, D({{"factor", 0.6}}), ctx);
	Outcomes::bend(w, t, c, res, D({{"bend_impulse", dnum(r, "bend_impulse", 60.0)}}), ctx);
	AirOutcomes::report(res, "slow");
	return true;
}

// A partial against lava: the wind cools it (heat_mult, booked ambient) and picks up spatter: a magma vortex for 4 s.
bool o_spatter(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	// the crust keeps a floor: wind alone does not set a lava wave that a tornado only out-pushes by < 20 %
	if (t.body == nullptr || t.body->liquid > dnum(r, "liquid_floor", 0.0)) Outcomes::weaken(w, t, c, res, r, ctx);
	if (c.body != nullptr && c.body->alive && c.body->form == Form::Zone) c.body->props.set("spatter_until", c.body->age + 4.0);
	AirOutcomes::report(res, "amplify");
	return true;
}

// Two whirls meet: the stronger absorbs the weaker (power, radius); equal powers: the lower id keeps going.
bool o_contest(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* tz = t.body;
	MatBody* cz = c.body;
	if (tz == nullptr || cz == nullptr || !tz->alive || !cz->alive || tz == cz) return false;
	const double tp = maxf(tz->power, 0.001);
	const double cp = maxf(cz->power, 0.001);
	MatBody* win = (cp > tp || (is_equal_approx(cp, tp) && cz->id < tz->id)) ? cz : tz;
	MatBody* lose = win == cz ? tz : cz;
	win->power = minf(45.0, win->power + 0.3 * lose->power);
	win->zone_radius = minf(5.0, win->zone_radius + 0.3);
	win->radius = win->zone_radius;
	const std::vector<int> ids = lose->captured;
	for (int id : ids) {
		MatBody* cap = w.get_body(id);
		if (cap != nullptr && cap->alive && cap->captured_by == lose->id) {
			cap->captured_by = win->id;
			win->captured.push_back(id);
			vec_erase(lose->captured, id);
		}
	}
	if (lose->form == Form::Zone) w.close_zone(*lose, "absorbed");
	else w.decay_body(*lose, "absorbed");
	res.stopped = true;
	res.pass_scale = 0.0;
	w.emit("tornado_contest", D({{"winner", win->id}, {"loser", lose->id}}));
	AirOutcomes::report(res, "absorb");
	return true;
}

}  // namespace AirVortex
}  // namespace ff
