// Fourfold core - port of game/combat/kits/earth/earth_metal.gd (Earth / Metal, sub 1; MOVESET §7.2): the satchel and
// field ownership, Razor Disc, Iron Lance, Lodestone Line (caltrops), Chain Arc, Aegis Plate (guard spec), Magnet Glide,
// Lodestone Grip (Reforge / Recall / plate scrap), Rod Plant and the metal body tick.
#include "Combat/Kits/Earth/Earth.h"

#include "Combat/Kits/Earth/KitEarth.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ff {
namespace EarthMetal {

using KitEarthUtil::flat_dist2;
using KitEarthUtil::flatv;

namespace {

// GDScript static var _scrap_tick: "world|actor" -> tick of the last plate rip (cooldown).
std::unordered_map<std::string, int64_t>& scrap_ticks() {
	static std::unordered_map<std::string, int64_t> m;
	return m;
}
std::string scrap_key(const CombatWorld& w, const ActorState& a) { return std::to_string(w.instance_id()) + "|" + itos(a.id); }

const char* const kZoneProps[] = {"ccls", "barrier", "actor_status", "status_t", "dps", "ground_only", "height", "rate", "spare_owner", "life_end"};
const char* const REFORGE[] = {"lance", "shards", "disc"};

IxCtx site_ctx(const char* s) {
	IxCtx c;
	c.site = s;
	return c;
}

void _zone_expire(CombatWorld& w, MatBody& b) {
	FxEvents::zone(w, b, "close");
	const bool planted = b.tag == "rod";
	b.form = Form::Chunk;
	b.zone_radius = 0.0;
	b.power = 0.0;
	for (const char* k : kZoneProps) b.props.erase(k);
	b.tag = planted ? "rod" : "plate";
	b.static_body = planted;
	b.on_ground = true;
	b.update_radius();
	b.max_life = b.age + Sim::REMNANT_LIFETIME;
}

bool _orbit_step(CombatWorld& w, MatBody& b, double dt) {
	ActorState* a = w.get_actor(dint(b.props, "orbit_owner", -1));
	if (a == nullptr || b.attack_id == 0) {
		b.props.erase("orbit_until");
		return false;
	}
	const double ang = dnum(b.props, "orbit_ang", 0.0) + 8.0 * dt;
	b.props.set("orbit_ang", ang);
	const Vec3 p = a->chest() + V3(std::cos(ang), 0.0, std::sin(ang)) * ORBIT_R;
	b.vel = (p - b.pos) / dt;
	b.pos = p;
	b.spin = 40.0;
	if (b.age >= dnum(b.props, "orbit_until")) {
		b.props.erase("orbit_until");
		ActorState* t = w.get_actor(a->lock_target);
		const Vec3 to = (t != nullptr ? t->chest() : a->chest() + a->forward() * 14.0) - b.pos;
		b.vel = to.normalized() * dnum(b.props, "orbit_speed", 24.0);
		b.gravity_scale = 0.0;
		w.emit("launch", D({{"actor", a->id}, {"body", b.id}, {"speed", b.vel.length()}, {"kind", "disc_storm"}}));
	}
	return true;
}

bool _recall_step(CombatWorld& w, MatBody& b, double dt) {
	ActorState* a = w.get_actor(dint(b.props, "recall", -1));
	if (a == nullptr || a->health <= 0.0) {
		b.props.erase("recall");
		b.attack_id = 0;
		return false;
	}
	const Vec3 to = a->chest() - b.pos;
	if (to.length() <= 0.9) {
		to_satchel(w, *a, &b, "recalled");
		return true;
	}
	b.vel = to.normalized() * RECALL_SPEED;
	b.pos += b.vel * dt;
	b.spin = 30.0;
	return true;
}

// ---------------------------------------------------------------- Aegis Plate (guard spec)

void _guard_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	VerbBarrier::start(w, a, inst, it);
	MatBody* hb = w.get_body(dint(inst.data, "held_barrier", -1));
	if (hb != nullptr && hb->alive) {
		mark(hb, a);
		inst.data.set("shield", true);   // the guard's own held material: ActCommon keeps it (no drop)
	}
}

void _guard_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	VerbBarrier::tick(w, a, inst, it);
	MatBody* hb = w.get_body(dint(inst.data, "held_barrier", -1));
	if (hb == nullptr || !hb->alive || hb->controller != a.id) return;
	if (hb->temp >= PLATE_HOT_C) {
		// Red-hot: dropped (and it burns the hands that held it).
		w.release_body(a, a.forward() * 1.5 + V3(0, 1.0, 0), false);
		inst.data.set("held_barrier", -1);
		inst.data.erase("shield");
		if (!Status::immune(a, "burn")) {
			a.health = maxf(0.0, a.health - 3.0);
			w.emit("burn", D({{"actor", a.id}, {"body", hb->id}}));
		}
		w.emit("drop", D({{"actor", a.id}, {"body", hb->id}, {"why", "red_hot"}, {"temp", hb->temp}}));
	}
}

// ---------------------------------------------------------------- Magnet Glide

void _glide_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (!Verbs::pay(w, a, inst, "start")) {
		inst.data.set("fizzle", true);
		return;
	}
	const double reach = dnum(inst.def, "reach", 8.0);
	MatBody* best = nullptr;
	double bd = kInf;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.mat != Mat::Metal || b.controller == a.id) continue;
		const double d = flat_dist2(b.pos, a.pos);
		if (d < 1.2 || d > reach) continue;
		if (d < bd - 1e-6 || (absf(d - bd) <= 1e-6 && best != nullptr && b.id < best->id)) {
			best = &b;
			bd = d;
		}
	}
	Dict spec = inst.def.duplicate();
	if (best != nullptr) {
		spec.set("distance", clampf(bd - 0.9, 1.0, 6.0));
		spec.set("dir", "aim");
		inst.data.set("aim", flatv(best->pos - a.pos).normalized());
		inst.data.set("glide_to", best->id);
	}
	inst.data.set("spec_def", spec);
	VerbMotion::dash_start(w, a, inst, it);
	Verbs::fx(w, a, inst, "trail",
	          D({{"dir", dvec(inst.data, "dir", a.forward())}, {"length", dnum(spec, "distance")}, {"body", best != nullptr ? best->id : -1}}));
}

// ---------------------------------------------------------------- Lodestone Grip / Reforge / Recall

bool _near_plate(CombatWorld& w, const ActorState& a) {
	const double mx = clampf(a.pos.x, w.arena.metal_min.x, w.arena.metal_max.x);
	const double mz = clampf(a.pos.z, w.arena.metal_min.y, w.arena.metal_max.y);
	return V2(mx - a.pos.x, mz - a.pos.z).length() <= VerbGrip::PLATE_REACH;
}

bool _scrap_ready(CombatWorld& w, const ActorState& a) {
	const std::string k = scrap_key(w, a);
	auto it = scrap_ticks().find(k);
	return it == scrap_ticks().end() || w.tick - it->second >= static_cast<int64_t>(SCRAP_COOLDOWN * Sim::HZ) || w.tick < it->second;
}

void _reforge(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	if (!w.spend_focus(a, dnum(inst.def, "shape_cost", 3.0))) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "shape"}}));
		return;
	}
	const std::string cur = dstr(inst.data, "reforge", "");
	int found = -1;
	for (int i = 0; i < 3; ++i)
		if (cur == REFORGE[i]) found = i;
	const std::string form = REFORGE[(found + 1) % 3];
	inst.data.set("reforge", form);
	if (form == "lance") {
		b.tag = "lance";
		inst.data.erase("split");
	} else if (form == "shards") {
		b.tag = "disc";
		inst.data.set("split", 3);
	} else {
		b.tag = "disc";
		inst.data.erase("split");
	}
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", "reforge"}, {"to", form}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", b.id}, {"shape", form == "lance" ? "lance" : "disc"}}));
}

void _throw(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr) return;
	const std::string form = dstr(inst.data, "reforge", "");
	std::vector<int> before;
	before.reserve(w.bodies.size());
	for (const BodyRef& x : w.bodies) before.push_back(x->id);
	VerbGrip::throw_(w, a, inst);
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& x = *w.bodies[i];
		const bool is_new = std::find(before.begin(), before.end(), x.id) == before.end();
		if (x.alive && x.mat == Mat::Metal && (&x == b || is_new) && x.attack_owner == a.id) {
			mark(&x, a);
			if (form == "lance") {
				x.tag = "lance";
				x.gravity_scale = 0.15;
				x.props.set("on_impact", "stick");
				x.props.set("move", "iron_lance");
			} else if (form == "disc" || form == "shards") {
				x.tag = "disc";
				x.props.set("homing", 15.0);
			}
		}
	}
}

void _grip_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const Dict d = inst.def;
	if (it.tech_cancel) {
		VerbGrip::drop(w, a, inst);
		w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("face", inst.data.get("aim"));
	inst.data.set("aim_point", w.aim_point(a, it));
	MatBody* b = w.held(a);
	if (b != nullptr) {
		b->static_body = false;
		b->props.erase("embedded_in");
		if (b->form == Form::Zone) {
			_zone_expire(w, *b);
			b->static_body = false;
		}
		mark(b, a);
		const Vec3 dir = dvec(inst.data, "aim");
		b->hold_point = a.pos + V3(0, 1.2, 0) + a.forward() * (0.3 + b->radius) + dir * 0.15;
		if (it.attack_pressed) _reforge(w, a, inst, *b);
		if (!it.tech_held) {
			w.set_phase(a, inst, ActionPhase::Active);
			_throw(w, a, inst);
		}
		return;
	}
	// Nothing in hand: seek metal (incoming preferred), else Recall, else rip scrap from the plate.
	const double reach = dnum(d, "reach");
	const int aid = a.id;
	auto f = [aid](MatBody& x) {
		return x.controller != aid && x.mat == Mat::Metal && x.form != Form::Pool && x.captured_by < 0 && !x.props.has("recall") &&
		       Interactions::allows(x, "grip_metal");
	};
	MatBody* tb = w.get_body(dint(inst.data, "target", -1));
	if (tb == nullptr || !tb->alive || !f(*tb)) {
		tb = w.find_body(a, dvec(inst.data, "aim"), reach, dnum(d, "cone", 60.0), f);
		if (tb != nullptr) {
			inst.data.set("target", tb->id);
			w.emit("target_body", D({{"actor", a.id}, {"body", tb->id}}));
		}
	}
	if (tb != nullptr) {
		if (tb->mass > minf(a.max_control_mass, dnum(d, "max_mass", 80.0))) {
			w.request_grip(a, *tb, 0.0, "magnet");
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"body", tb->id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		const double s = w.grip_strength(a, *tb, dnum(d, "base", 0.85), reach) * dnum(d, "grip_mult", 1.3);
		w.request_grip(a, *tb, s, "magnet");
		return;
	}
	const bool early = inst.t < dnum(d, "rip_time", 0.28);
	if (dbool(inst.data, "ripped", false) || (early && it.tech_held)) {
		if (!it.tech_held) {
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
		return;
	}
	inst.data.set("ripped", true);
	const std::vector<MatBody*> mine = owned(w, a);
	if (!mine.empty()) {
		recall(w, a, inst, mine);
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	if (early) {
		w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"reason", "no_metal"}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	if (_near_plate(w, a) && _scrap_ready(w, a)) {
		scrap_ticks()[scrap_key(w, a)] = w.tick;
		w.mass_ledger.metal_taken += SCRAP_MASS;
		const Vec3 p = a.pos + a.forward() * 0.8 + V3(0, 0.3, 0);
		MatBody* sb = w.spawn_body(Mat::Metal, Form::Chunk, SCRAP_MASS, p, "plate");
		sb->tag = "plate";
		sb->max_life = Sim::REMNANT_LIFETIME;
		mark(sb, a);
		w.take_control(a, *sb, 0.9, "rip");
		w.emit("rip", D({{"actor", a.id}, {"body", sb->id}, {"source", "metal_plate"}}));
		Verbs::fx(w, a, inst, "erupt", D({{"pos", p}, {"body", sb->id}, {"shape", "plate"}}));
		return;
	}
	w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}, {"reason", "no_metal"}}));
	w.set_phase(a, inst, ActionPhase::Recovery);
}

}  // namespace

// ================================================================ satchel and ownership

void to_satchel(CombatWorld& w, ActorState& a, MatBody* b, const std::string& why) {
	if (b == nullptr || !b->alive || b->mat != Mat::Metal) return;
	const double room = maxf(0.0, SATCHEL_MAX - a.metal_carried);
	const double take = minf(room, b->mass);
	if (take < b->mass - 1e-6) {
		MatBody* rest = w.split_body(*b, b->mass - take, a.pos + a.forward() * 0.6 + V3(0, 0.3, 0));
		rest->form = Form::Chunk;
		rest->vel = Vec3();
		rest->attack_id = 0;
		rest->static_body = false;
		rest->props.erase("recall");
	}
	if (b->form == Form::Zone) FxEvents::zone(w, *b, "close");
	w.ledger.removed += b->thermal_energy();
	a.metal_carried += b->mass;
	w.emit("satchel", D({{"actor", a.id}, {"body", b->id}, {"mass", b->mass}, {"why", why}, {"carried", a.metal_carried}}));
	b->mass = 0.0;
	b->heat_payload = 0.0;
	w.release_captured(*b);
	w.remove_body(*b, why);
}

void mark(MatBody* b, const ActorState& a) {
	if (b != nullptr && b->mat == Mat::Metal) b->props.set("metal_owner", a.id);
}

std::vector<MatBody*> owned(CombatWorld& w, const ActorState& a) {
	std::vector<MatBody*> out;
	for (const BodyRef& bp : w.bodies) {
		MatBody& b = *bp;
		if (!b.alive || b.mat != Mat::Metal || b.controller == a.id || b.props.has("recall")) continue;
		if (dint(b.props, "metal_owner", -1) == a.id) out.push_back(&b);
	}
	return out;
}

bool owned_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	for (MatBody* b : VerbProjectile::fire(w, a, inst)) mark(b, a);
	return true;
}

// ================================================================ Razor Disc

bool disc_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const std::vector<MatBody*> bodies = VerbProjectile::fire(w, a, inst);
	const double orbit = Charge::paramf(inst, "orbit", 0.0);
	int k = 0;
	for (MatBody* b : bodies) {
		mark(b, a);
		b->spin = 40.0;
		if (orbit > 0.0) {
			b->props.set("orbit_until", b->age + orbit);
			b->props.set("orbit_owner", a.id);
			b->props.set("orbit_ang", kTau * static_cast<double>(k) / static_cast<double>(maxi(1, static_cast<int>(bodies.size()))));
			b->props.set("orbit_speed", Charge::paramf(inst, "speed", 24.0));
		}
		++k;
	}
	return true;
}

// ================================================================ Iron Lance

bool lance_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)w;
	(void)b;
	return what == "actor";
}

// ================================================================ Lodestone Line

bool line_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = VerbGroundLine::launch(w, a, inst);
	if (b != nullptr) {
		mark(b, a);
		b->props.set("zone_r", Charge::paramf(inst, "zone_r", 2.0));
		b->props.set("zone_life", Charge::paramf(inst, "zone_life", 4.0));
		b->props.set("zone_power", Charge::paramf(inst, "zone_power", 10.0));
	}
	return true;
}

bool filings_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.form == Form::Zone) return metal_tick(w, b, dt);
	if (b.form == Form::Wave && b.wave_budget > 0.05) return false;
	if (b.props.has("recall")) return metal_tick(w, b, dt);
	const int owner = b.attack_owner >= 0 ? b.attack_owner : dint(b.props, "metal_owner", -1);
	const double r = dnum(b.props, "zone_r", 2.0);
	b.form = Form::Zone;
	b.tag = "caltrops";
	b.zone_radius = r;
	b.radius = r;
	b.owner = owner;
	b.power = dnum(b.props, "zone_power", 10.0);
	b.vel = Vec3();
	b.attack_id = 0;
	b.static_body = true;
	b.gravity_scale = 0.0;
	b.wave_path.clear();
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4));
	b.max_life = -1.0;
	b.props.set("life_end", b.age + dnum(b.props, "zone_life", 4.0));
	b.props.set("ccls", "caltrops");
	b.props.set("actor_status", "slowed");
	b.props.set("status_t", 0.3);
	b.props.set("dps", 2.0);
	b.props.set("ground_only", true);
	b.props.set("height", 1.0);
	b.props.set("rate", 1.0);
	b.props.set("spare_owner", true);
	w.release_captured(b);
	FxEvents::zone(w, b, "open");
	FxEvents::fx(w, "erupt", "metal", D({{"actor", owner}, {"body", b.id}, {"pos", b.pos}, {"radius", r}, {"element", 0}, {"sub", 1}}));
	return true;
}

// ================================================================ body behaviour (disc, lance, plate, rod, caltrops)

bool metal_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.mat != Mat::Metal) return false;
	if (b.props.has("recall")) return _recall_step(w, b, dt);
	if (b.props.has("orbit_until")) return _orbit_step(w, b, dt);
	if (b.form == Form::Zone) {
		if (b.age >= dnum(b.props, "life_end", kInf)) {
			_zone_expire(w, b);
			return true;
		}
		return false;
	}
	if (b.attack_id != 0) b.spin = b.tag == "disc" ? 40.0 : (b.tag == "plate" ? 25.0 : 0.0);
	else b.spin = 0.0;
	return false;
}

void recall(CombatWorld& w, ActorState& a, ActionInst& inst, const std::vector<MatBody*>& pieces) {
	for (MatBody* bp : pieces) {
		MatBody& b = *bp;
		if (b.form == Form::Zone) {
			FxEvents::zone(w, b, "close");
			b.form = Form::Chunk;
			b.zone_radius = 0.0;
			b.power = 0.0;
			for (const char* k : kZoneProps) b.props.erase(k);
			b.update_radius();
		}
		if (b.tag != "disc" && b.tag != "lance" && b.tag != "plate") b.tag = "plate";
		b.static_body = false;
		b.gravity_scale = 0.0;
		b.on_ground = false;
		b.props.erase("embedded_in");
		b.props.erase("on_impact");
		b.props.set("recall", a.id);
		Verbs::arm(w, a, inst, b, 8.0 * clampf(std::sqrt(b.mass / 6.0), 0.6, 1.5), 14.0);
		b.vel = (a.chest() - b.pos).normalized() * RECALL_SPEED;
		w.emit("recall", D({{"actor", a.id}, {"body", b.id}}));
		Verbs::fx(w, a, inst, "release", D({{"body", b.id}, {"pos", b.pos}, {"dir", b.vel.normalized()}, {"shape", b.tag}}));
	}
}

// ================================================================ Chain Arc

bool chain_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng = Charge::paramf(inst, "range", 5.0);
	const double ang = Charge::paramf(inst, "angle", 60.0);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	AgentRef v = Agent::of_volume(&w, &a, &inst, "chain", a.chest(), dir, D({{"K", Charge::paramf(inst, "power", 10.0)}}));
	v->data.set("knock", Charge::paramf(inst, "knock", 4.0));
	Verbs::fx(w, a, inst, "cone", D({{"length", rng}, {"angle", ang}, {"power", v->power}}));
	const double cos_lim = std::cos(deg_to_rad(ang));
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || b.static_body || b.controller >= 0 || b.captured_by >= 0 || &b == w.pool) continue;
		if (b.form == Form::Wall || b.form == Form::Zone || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Wave) continue;
		const Vec3 to = flatv(b.pos - a.chest());
		if (to.length() > rng + b.radius || (to.length() > 0.5 && to.normalized().dot(dir) < cos_lim)) continue;
		AgentRef th = Agent::of_body(w, b, &a);
		Interactions::resolve(w, *th, *v, site_ctx("volume"), &Interactions::PASS_RULE());
	}
	for (ActorState* t : w.actors_in_cone(a, dir, rng, ang)) {
		const Vec3 to2 = flatv(t->pos - a.pos);
		const std::string res =
		    w.hit_actor(*t, VerbVolume::_hit_info(a, &inst, *v, a.chest(), to2.length() > 0.1 ? -to2.normalized() : -dir), v);
		VerbVolume::_after_hit(w, a, inst, *t, res);
	}
	return true;
}

// ================================================================ module lifecycle (aegis spec, lodestone_grip, magnet_glide)

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		_guard_start(w, a, inst, it);
		return;
	}
	if (inst.id == "magnet_glide") {
		_glide_start(w, a, inst, it);
	} else if (inst.id == "rod_plant") {
		// The plate kept from the guard goes back into the satchel first (the rod comes from there).
		MatBody* hb = w.held(a);
		if (hb != nullptr && hb->mat == Mat::Metal && hb->tag == "plate") to_satchel(w, a, hb, "returned");
		Verbs::on_start(w, a, inst, it);
	} else {
		Verbs::on_start(w, a, inst, it);
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	if (inst.id == "magnet_glide") return ActionPhase::Active;
	return Verbs::after_startup(w, a, inst, it);
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "guard") {
		if (p == ActionPhase::Recovery) VerbBarrier::end(w, a, inst, "release");
		return;
	}
	if (inst.id == "magnet_glide") {
		if (p == ActionPhase::Recovery) inst.data.set("controls_motion", false);
	} else {
		Verbs::on_phase(w, a, inst, p);
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "guard") {
		_guard_tick(w, a, inst, it);
		return;
	}
	if (inst.id == "magnet_glide") {
		if (inst.phase == ActionPhase::Active) VerbMotion::dash_tick(w, a, inst);
	} else if (inst.id == "lodestone_grip") {
		if (inst.phase == ActionPhase::Channel) _grip_tick(w, a, inst, it);
	} else {
		Verbs::on_tick(w, a, inst, it);
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (inst.id == "guard") {
		VerbBarrier::end(w, a, inst, reason);
		return;
	}
	if (inst.id == "magnet_glide") inst.data.set("controls_motion", false);
	else Verbs::on_interrupt(w, a, inst, reason);
}

Dict preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	const Dict d = Moves::defs().get("lodestone_grip").as_dict();
	const int aid = a.id;
	auto f = [aid](MatBody& x) {
		return x.controller != aid && x.mat == Mat::Metal && x.form != Form::Pool && Interactions::allows(x, "grip_metal");
	};
	MatBody* b = w.find_body(a, dir, dnum(d, "reach", 10.0), dnum(d, "cone", 60.0), f);
	if (b != nullptr) {
		const bool ok = b->mass <= a.max_control_mass;
		return D({{"mode", "MAGNET"}, {"body", b->id}, {"ok", ok}, {"reason", ok ? "" : "mass"}});
	}
	if (!owned(w, a).empty()) return D({{"mode", "RECALL"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	if (_near_plate(w, a)) return D({{"mode", "RIP"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	return D({{"mode", "MAGNET"}, {"body", -1}, {"ok", false}, {"reason", "target"}});
}

// ================================================================ Rod Plant

bool rod_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	// A plate kept from the guard goes back into the satchel (the rod is planted with both hands).
	MatBody* hb = w.held(a);
	if (hb != nullptr && hb->mat == Mat::Metal && hb->tag == "plate") to_satchel(w, a, hb, "returned");
	const double mass = dnum(inst.data, "metal_paid", 0.0);
	if (mass <= 0.0) {
		KitEarthUtil::fizzle(w, a, inst, "metal");
		return true;
	}
	inst.data.set("metal_paid", 0.0);
	const Vec3 p = KitEarthUtil::ground_point(w, a, dvec(inst.data, "face", a.forward()), 1.5);
	MatBody* z = w.spawn_zone("rod", p, Charge::paramf(inst, "radius", 6.0), a.id, Charge::paramf(inst, "power", 60.0), Mat::Metal, mass, -1.0,
	                          "metal:" + itos(a.id));
	z->static_body = true;
	z->sub = 1;
	z->props.set("ccls", "rod");
	z->props.set("barrier", true);
	z->props.set("height", 3.5);
	z->props.set("rate", 1.0);
	z->props.set("life_end", Charge::paramf(inst, "life", 8.0));
	mark(z, a);
	Verbs::fx(w, a, inst, "release", D({{"pos", p}, {"body", z->id}, {"radius", z->zone_radius}}));
	inst.data.set("zone", z->id);
	return true;
}

void rod_spread(CombatWorld& w, Agent& t, MatBody* rod, double e) {
	if (rod == nullptr || !rod->alive || e <= 0.0) return;
	ActorState* src = t.actor;
	std::vector<ActorState*> victims;
	auto has = [&victims](ActorState* x) { return std::find(victims.begin(), victims.end(), x) != victims.end(); };
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& pd = *w.bodies[i];
		if (!pd.alive || pd.form != Form::Puddle || pd.phase != Phase::Liquid) continue;
		if (flat_dist2(pd.pos, rod->pos) > pd.radius + 0.6) continue;
		for (const auto& ap : w.actors) {
			ActorState* a = ap.get();
			if (a->health > 0.0 && !has(a) && w.puddle_at(a->pos) == &pd && !Status::immune(*a, "conduct")) victims.push_back(a);
		}
	}
	const Vec3 pool_pt = V3(clampf(rod->pos.x, w.arena.pool_min.x, w.arena.pool_max.x), 0.0,
	                        clampf(rod->pos.z, w.arena.pool_min.y, w.arena.pool_max.y));
	if (w.arena.in_pool(rod->pos.x, rod->pos.z) || flat_dist2(rod->pos, pool_pt) < 0.6) {
		for (const auto& ap : w.actors) {
			ActorState* a = ap.get();
			if (a->in_water && a->health > 0.0 && !has(a) && !Status::immune(*a, "conduct")) victims.push_back(a);
		}
	}
	if (victims.empty()) return;
	const int aid = w.new_attack_id();
	for (ActorState* a : victims) {
		w.hit_actor(*a, D({{"attacker", src != nullptr ? src->id : -1},
		                   {"attack_id", aid},
		                   {"damage", e * 0.6 / static_cast<double>(victims.size())},
		                   {"balance", 18.0},
		                   {"kind", "lightning"},
		                   {"from", rod->pos},
		                   {"unblockable", true},
		                   {"power", e}}));
	}
	Array ids;
	for (ActorState* a : victims) ids.append(a->id);
	w.emit("conduct", D({{"actor", src != nullptr ? src->id : -1}, {"nodes", A({Value("rod:" + itos(rod->id))})}, {"victims", ids}}));
}

}  // namespace EarthMetal
}  // namespace ff
