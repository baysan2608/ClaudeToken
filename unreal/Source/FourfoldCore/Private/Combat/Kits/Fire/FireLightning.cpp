// Fourfold core - port of game/combat/kits/fire/fire_lightning.gd (Fire / Lightning, sub 2; MOVESET §7.11): Spark ->
// Bolt -> Storm Bolt -> Skybreak, Rail Arc, Ground Current / Storm Grid, Arc Fan, Static Ward, Static Burst, Grounding,
// Conductor's Hand / Arc Link (charged bodies), static fields, Arc Step and Overcharge. Lightning runs on Conduction.
#include "Combat/Kits/Fire/Fire.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace FireLightning {

namespace {
constexpr int E = 2;
constexpr int SUB = 2;

Dict _def_for(const ActionInst& inst, Vec3 aim) {
	Dict d;
	for (const char* k : {"range", "damage", "balance", "E", "conduct_budget", "max_hops", "forks"}) {
		const Value v = Charge::param(inst, k, Value());
		if (!v.is_nil()) d.set(k, v);
	}
	d.set("tier", inst.tier());
	d.set("meet_bodies", true);
	d.set("aim", aim);
	return d;
}

Vec3 path_last(const Dict& out) {
	const Array p = darr(out, "path");
	return p.empty() ? Vec3() : vvec(p[p.size() - 1]);
}

void _spark(CombatWorld& w, ActorState& a, ActionInst& inst, Dict def) {
	const double rng_m = dnum(def, "range");
	ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr && (t->chest() - a.hand_point()).length() <= rng_m + 0.5) {
		def.set("force_target", t->id);
	} else {
		const int aid = a.id;
		MatBody* b = w.find_body(a, dvec(inst.data, "aim", a.forward()), rng_m, 70.0, [aid](MatBody& x) {
			return x.controller != aid && (Materials::conducts(x) || (x.form == Form::Puddle && x.phase == Phase::Liquid)) && x.form != Form::Pool;
		});
		if (b != nullptr) def.set("aim", b->pos);
	}
	const Dict out = Conduction::discharge(w, a, dvec(def, "aim"), def, inst.attack_id, false);
	Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", rng_m}, {"power", dnum(def, "E")}, {"shape", "small"}}));
	const Vec3 hit_end = path_last(out);
	const Array hits = darr(out, "hits");
	for (const Value& hv : hits) {
		ActorState* ha = w.get_actor(vint(hv));
		if (ha != nullptr) Status::apply(w, *ha, "shocked", 0.35, 1.0, a.id);
	}
	// Chain 2 m: wet fighters and conductive bodies near the strike.
	const double cr = Charge::paramf(inst, "chain_r", 2.0);
	for (const auto& op : w.actors) {
		ActorState& o = *op;
		if (&o == &a || o.team == a.team || o.health <= 0.0 || arr_has_int(hits, o.id) || o.wetness <= Status::WET_AT) continue;
		if ((o.chest() - hit_end).length() <= cr + Sim::ACTOR_RADIUS && !Status::immune(o, "conduct")) {
			w.hit_actor(o, D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", dnum(def, "damage") * 0.5}, {"balance", 8.0},
			                  {"kind", "lightning"}, {"from", hit_end}, {"unblockable", true}}));
			Status::apply(w, o, "shocked", 0.3, 1.0, a.id);
			w.emit("lightning", D({{"actor", a.id}, {"path", A({Value(hit_end), Value(o.chest())})}, {"arcs", Array()}, {"blocked", false},
			                       {"hits", A({Value(o.id)})}, {"chain", true}}));
		}
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.mat == Mat::Metal && (b.pos - hit_end).length() <= cr + b.radius) charge_body(w, &b, dnum(def, "E") * 0.5, a.id);
	}
}

void _skybreak(CombatWorld& w, ActorState& a, ActionInst& inst, const Dict& def) {
	ActorState* t = w.get_actor(a.lock_target);
	const Vec3 at = t != nullptr && (t->pos - a.pos).length() <= dnum(def, "range") + 2.0 ? t->pos : FireUtil::aim_ground(w, a, inst, dnum(def, "range"));
	MatBody* z = w.spawn_zone("static_field", at, 1.4, a.id, dnum(def, "E"), Mat::Air, 0.0, -1.0, "skybreak:" + itos(a.id));
	z->charge = 0.0;
	z->props.set("skybreak", true);
	z->props.set("delay", SKYBREAK_DELAY);
	z->props.set("target", t != nullptr ? t->id : -1);
	z->props.set("def", def);
	z->props.set("attack_id", inst.attack_id);
	z->props.set("deafen_r", Charge::paramf(inst, "deafen_r", 4.0));
	z->props.set("deafen_t", Charge::paramf(inst, "deafen_t", 0.3));
	z->props.set("spare_owner", true);
	w.emit("telegraph", D({{"actor", a.id}, {"move", "skybreak"}, {"pos", at}, {"time", SKYBREAK_DELAY}, {"body", z->id}}));
	FxEvents::fx_for(w, a, inst, "cast", "lightning", D({{"pos", at + V3(0, 0.05, 0)}, {"radius", 1.4}, {"dur", SKYBREAK_DELAY}, {"shape", "down"}}));
}

void _sky_strike(CombatWorld& w, MatBody& z) {
	ActorState* a = w.get_actor(z.owner);
	const Dict def = ddict(z.props, "def");
	w.close_zone(z, "struck");
	if (a == nullptr || def.empty()) return;
	ActorState* t = w.get_actor(dint(z.props, "target", -1));
	Vec3 at = z.pos;
	if (t != nullptr && t->health > 0.0 && (t->pos - z.pos).length() < 3.0) at = t->pos;   // the strike follows the mark within reach
	Dict d = def.duplicate();
	d.set("start", at + V3(0, 12.0, 0));
	d.set("range", 13.5);
	if (t != nullptr && Vec2(t->pos.x - at.x, t->pos.z - at.z).length() < 1.2) d.set("force_target", t->id);
	const int fallback_id = w.new_attack_id();   // GDScript evaluates the get() default eagerly (the id is consumed)
	const int aid = z.props.has("attack_id") ? dint(z.props, "attack_id") : fallback_id;
	const Dict out = Conduction::discharge(w, *a, at + V3(0, 1.0, 0), d, aid, false);
	FxEvents::fx(w, "beam", "lightning",
	             D({{"actor", a->id}, {"pos", d.get("start")}, {"path", out.get("path")}, {"length", 12.0}, {"power", dnum(d, "E")}, {"tier", 3},
	                {"move", "spark"}, {"element", E}, {"sub", SUB}, {"shape", "down"}}));
	const double deafen_r = dnum(z.props, "deafen_r", 4.0);
	FxEvents::fx(w, "burst", "lightning",
	             D({{"actor", a->id}, {"pos", at + V3(0, 0.2, 0)}, {"radius", deafen_r}, {"power", dnum(d, "E")}, {"tier", 3}, {"move", "spark"},
	                {"element", E}, {"sub", SUB}, {"shape", "ground"}}));
	for (const auto& op : w.actors) {
		ActorState& o = *op;
		if (o.health <= 0.0 || (o.pos - at).length() > deafen_r) continue;
		Status::apply(w, o, "deafened", dnum(z.props, "deafen_t", 0.3), 1.0, a->id);
	}
	w.emit("thunder", D({{"actor", a->id}, {"pos", at}, {"radius", deafen_r}}));
}

void _storm_grid(CombatWorld& w, MatBody& b, const std::string& node) {
	ActorState* owner = w.get_actor(b.attack_owner);
	if (owner == nullptr) return;
	std::vector<std::string> order;
	Conduction::bfs(Conduction::build_graph(w), {node}, 8, &order);
	int n = 0;
	Array nodes;
	for (const std::string& k : order) {
		nodes.append(k);
		const Vec3 p = Conduction::node_point(w, k, b.pos);
		if (n < 4 && (begins_with(k, "puddle:") || k == "metal")) {
			MatBody* z = w.spawn_zone("static_field", p, 1.6, owner->id, b.charge * 0.5, Mat::Air, 0.0, dnum(b.props, "grid_t", 1.5));
			z->charge = b.charge * 0.5;
			z->props.set("shock", true);
			++n;
		}
	}
	Dict out = D({{"hits", Array()}, {"arcs", Array()}});
	Conduction::_conduct_from(w, *owner, out, {node}, b.pos, b.charge, 8, b.attack_id);
	w.emit("storm_grid", D({{"actor", owner->id}, {"nodes", nodes}, {"victims", out.get("hits")}}));
}

MatBody* _hand_target(CombatWorld& w, ActorState& a, Vec3 dir) {
	const int aid = a.id;
	return w.find_body(a, dir, 10.0, 55.0, [aid](MatBody& b) {
		return b.form != Form::Pool && b.controller != aid && (Materials::conducts(b) || (b.form == Form::Puddle && b.phase == Phase::Liquid));
	});
}
}  // namespace

void spark_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p != ActionPhase::Active) {
		Verbs::on_phase(w, a, inst, p);
		return;
	}
	const int tier = inst.tier();
	const Vec3 aim = dvec(inst.data, "aim_point", a.chest() + a.forward() * 10.0);
	Dict def = _def_for(inst, aim);
	if (tier == 0) {
		_spark(w, a, inst, def);
	} else if (tier == 3) {
		_skybreak(w, a, inst, def);
	} else {
		const Dict out = Conduction::discharge(w, a, aim, def, inst.attack_id, true);
		Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", dnum(def, "range")}, {"power", dnum(out, "e", dnum(def, "E"))}}));
	}
}

bool rail_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr && !dbool(inst.data, "aim_active", false)) dir = (t->chest() - (a.hand_point() + V3(0, 0.25, 0))).normalized();
	const Dict def = _def_for(inst, Vec3());
	const Dict out = Conduction::rail(w, a, dir, def, inst.attack_id);
	Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", dnum(def, "range")}, {"power", dnum(out, "e", dnum(def, "E"))}}));
	return true;
}

bool current_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = VerbGroundLine::launch(w, a, inst, D({{"source", "none"}, {"mat", "air"}}));
	if (b == nullptr) return true;
	b->charge = Charge::paramf(inst, "E", 12.0);
	b->power = b->charge;
	b->props.set("channel", "E");
	b->props.set("dry", 0.0);
	b->props.set("dry_max", Charge::paramf(inst, "dry_max", 2.0));
	if (Charge::paramb(inst, "grid", false)) {
		b->props.set("grid", true);
		b->props.set("grid_t", Charge::paramf(inst, "grid_t", 1.5));
	}
	b->props.set("budget_hits", Charge::paramf(inst, "damage", 10.0));
	for (const auto& op : w.actors)
		if (Status::immune(*op, "conduct")) b->hit_set.add(op->id);
	return true;
}

bool current_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.form != Form::Wave || b.attack_id == 0) return false;
	for (const auto& op : w.actors)
		if (Status::immune(*op, "conduct") || op->flying) b.hit_set.add(op->id);
	std::string node = Conduction::surface_node_at(w, b.pos + V3(0, 0.05, 0));
	bool frozen = false;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& o = *w.bodies[i];
		if (!o.alive || &o == &b) continue;
		if (o.form == Form::Puddle && o.phase == Phase::Frozen && Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < o.radius) {
			frozen = true;
		} else if (o.form == Form::Zone && (o.tag == "ice_floor" || Materials::insulates(o)) && w._in_zone(o, b.pos, 0.1)) {
			frozen = true;
		} else if (node.empty() && o.form == Form::Zone && (Materials::conducts(o) || o.tag == "mud" || o.tag == "caltrops") && w._in_zone(o, b.pos, 0.1)) {
			node = "body:" + itos(o.id);
		} else if (node.empty() && o.form != Form::Zone && o.on_ground && Materials::conducts(o) &&
		           Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < o.radius + 0.3) {
			node = "body:" + itos(o.id);
		}
	}
	if (frozen && node.empty()) {
		w.emit("insulated", D({{"body", b.id}, {"at", b.pos}, {"by", "ice"}}));
		w.decay_body(b, "insulated");
		return true;
	}
	if (!node.empty()) {
		b.props.set("dry", 0.0);
		if (dbool(b.props, "grid", false) && !dbool(b.props, "gridded", false)) {
			b.props.set("gridded", true);
			_storm_grid(w, b, node);
		}
	} else {
		b.props.set("dry", dnum(b.props, "dry", 0.0) + dnum(b.props, "speed", 20.0) * dt);
		if (dnum(b.props, "dry") > dnum(b.props, "dry_max", 2.0)) {
			w.emit("current_grounded", D({{"body", b.id}, {"at", b.pos}}));
			w.decay_body(b, "grounded");
			return true;
		}
	}
	if (w.tick % 4 == 0)
		FxEvents::fx(w, "trail", "lightning",
		             D({{"actor", b.attack_owner}, {"body", b.id}, {"pos", b.pos}, {"dir", b.wave_dir}, {"length", 1.5}, {"power", b.charge},
		                {"shape", "ground"}, {"tier", b.tier}}));
	return false;
}

bool fan_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int n = Charge::parami(inst, "forks_n", 3);
	const double fan = deg_to_rad(Charge::paramf(inst, "fan", 60.0));
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	dir.y = 0.0f;
	dir = dir.normalized();
	const double rng_m = Charge::paramf(inst, "range", 6.0);
	std::vector<int> used;
	for (int k = 0; k < n; ++k) {
		const double ang = lerpf(-fan * 0.5, fan * 0.5, static_cast<double>(k) / static_cast<double>(maxi(1, n - 1)));
		const Vec3 fd = rotated(dir, Vec3::Up(), ang);
		Dict def = _def_for(inst, a.chest() + fd * rng_m + V3(0, -0.8, 0));
		def.set("meet_bodies", false);
		ActorState* best = nullptr;
		double bd = kInf;
		for (const auto& tp : w.actors) {
			ActorState& t = *tp;
			if (&t == &a || t.team == a.team || t.health <= 0.0 || std::find(used.begin(), used.end(), t.id) != used.end()) continue;
			Vec3 to = t.pos - a.pos;
			to.y = 0.0f;
			if (to.length() > rng_m + Sim::ACTOR_RADIUS || to.normalized().dot(fd) < std::cos(deg_to_rad(18.0))) continue;
			if (to.length() < bd) {
				bd = to.length();
				best = &t;
			}
		}
		if (best != nullptr) {
			used.push_back(best->id);
			def.set("force_target", best->id);
		}
		const Dict out = Conduction::discharge(w, a, dvec(def, "aim"), def, w.new_attack_id(), false);
		Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", rng_m}, {"power", dnum(def, "E")}, {"dir", fd}, {"shape", "fan"}}));
	}
	return true;
}

void ward_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() == &inst && inst.phase == ActionPhase::Channel && w.tick % 20 == 0)
		FxEvents::fx_for(w, a, inst, "aura", "lightning", D({{"on", true}, {"power", a.static_charge}, {"shape", "small"}}));
}

bool burst_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double e = a.static_charge;
	a.static_charge = 0.0;
	if (e < 2.0) FireUtil::with_params(inst, D({{"power", 2.0}, {"damage", 1.0}, {"balance", 14.0}, {"knock", 6.0}, {"status", ""}}));
	else FireUtil::with_params(inst, D({{"power", e}, {"damage", e * 0.55}, {"balance", 10.0 + e * 0.8}, {"knock", 3.0 + e * 0.05}}));
	VerbVolume::cone(w, a, inst);
	w.emit("static_burst", D({{"actor", a.id}, {"e", e}}));
	return true;
}

void grounding_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Active && inst.phase != ActionPhase::Channel) return;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.charge > 0.0 && b.form != Form::Wave && (b.pos - (a.pos + V3(0, 0.6, 0))).length() < b.radius + 1.0) {
			b.charge = 0.0;
			w.emit("discharge", D({{"actor", a.id}, {"body", b.id}}));
		}
	}
}

Dict hand_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	MatBody* b = _hand_target(w, a, dir);
	if (b == nullptr) return D({{"mode", "CHARGE"}, {"body", -1}, {"ok", false}, {"reason", "target"}, {"label", "CHARGE"}});
	return D({{"mode", "CHARGE"}, {"body", b->id}, {"ok", true}, {"reason", ""}, {"label", "ARC LINK"}});
}

ActionPhase hand_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	MatBody* b = _hand_target(w, a, w.aim_dir(a, it));
	if (b == nullptr) {
		w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
		return ActionPhase::Recovery;
	}
	inst.data.set("target", b->id);
	charge_body(w, b, Charge::paramf(inst, "E", 12.0), a.id);
	w.emit("telegraph", D({{"actor", a.id}, {"move", inst.id}, {"body", b->id}, {"time", 0.0}}));
	Verbs::fx(w, a, inst, "beam",
	          D({{"path", A({Value(a.hand_point()), Value(b->pos)})}, {"length", (a.hand_point() - b->pos).length()}, {"shape", "small"}}));
	return ActionPhase::Channel;
}

void hand_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.phase != ActionPhase::Channel) {
		Verbs::on_tick(w, a, inst, it);
		return;
	}
	MatBody* b = w.get_body(dint(inst.data, "target", -1));
	if (it.tech_cancel || b == nullptr || !b->alive || (a.chest() - b->pos).length() > 12.0) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	const double e = Charge::paramf(inst, "E", 12.0);
	if (b->charge < e) charge_body(w, b, e, a.id);
	if (w.tick % 10 == 0)
		Verbs::fx(w, a, inst, "beam",
		          D({{"path", A({Value(a.hand_point()), Value(b->pos)})}, {"length", (a.hand_point() - b->pos).length()}, {"power", b->charge},
		             {"shape", "small"}, {"dur", 0.2}}));
	if (!Charge::held(inst, it)) {
		inst.data.set("released", true);
		w.set_phase(a, inst, ActionPhase::Active);
	}
}

void hand_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Active) {
		MatBody* b = w.get_body(dint(inst.data, "target", -1));
		if (b != nullptr && b->alive) {
			Dict def = _def_for(inst, Vec3());
			def.set("E", maxf(dnum(def, "E"), b->charge));
			def.set("relay_range", Charge::paramf(inst, "relay_range", 8.0));
			const Dict out = Conduction::relay(w, a, {b}, def, inst.attack_id);
			b->charge = maxf(0.0, b->charge * 0.3);
			Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", 10.0}, {"power", dnum(out, "e", dnum(def, "E"))}}));
			w.emit("arc_link", D({{"actor", a.id}, {"body", b->id}, {"hits", out.get("hits")}, {"blocked", out.get("blocked")}}));
		}
		return;
	}
	Verbs::on_phase(w, a, inst, p);
}

void charge_body(CombatWorld& w, MatBody* b, double e, int owner_id) {
	if (b == nullptr || !b->alive || e <= 0.0) return;
	b->charge = maxf(b->charge, minf(e, 60.0));
	const int mark = dint(b->props, "charge_mark", -1);
	MatBody* z = w.get_body(mark);
	if (z == nullptr || !z->alive) {
		z = w.spawn_zone("static_field", b->pos, b->radius + 0.4, owner_id, b->charge, Mat::Air, 0.0, -1.0, "charge:" + itos(b->id));
		z->props.set("follow", b->id);
		z->props.set("spare_owner", true);
		b->props.set("charge_mark", z->id);
	}
	z->power = b->charge;
	z->owner = owner_id;
	w.emit("charged", D({{"body", b->id}, {"e", b->charge}, {"by", owner_id}}));
}

void static_field_tick(CombatWorld& w, MatBody& z, double dt) {
	if (dbool(z.props, "skybreak", false)) {
		if (z.age >= dnum(z.props, "delay", SKYBREAK_DELAY)) _sky_strike(w, z);
		return;
	}
	const int fid = dint(z.props, "follow", -1);
	if (fid >= 0) {
		MatBody* b = w.get_body(fid);
		if (b == nullptr || !b->alive) {
			w.close_zone(z, "carrier_gone");
			return;
		}
		z.pos = b->pos;
		b->charge = maxf(0.0, b->charge - CHARGE_DECAY * dt);
		z.power = b->charge;
		if (b->charge <= 0.5) {
			b->charge = 0.0;
			b->props.erase("charge_mark");
			w.close_zone(z, "discharged");
			return;
		}
		for (const auto& ap : w.actors) {
			ActorState& a = *ap;
			if (a.health <= 0.0 || a.id == z.owner || Status::immune(a, "conduct")) continue;
			if (b->controller == a.id || (a.chest() - b->pos).length() < b->radius + 0.6 || (a.pos - b->pos).length() < b->radius + 0.4) {
				const std::string k = "shock_" + itos(a.id);
				if (z.props.has(k) && z.props.get(k).as_int() > w.tick - 30) continue;
				z.props.set(k, w.tick);
				w.hit_actor(a, D({{"attacker", z.owner}, {"attack_id", w.new_attack_id()}, {"damage", b->charge * 0.3}, {"balance", 10.0 + b->charge * 0.3},
				                  {"kind", "lightning"}, {"from", b->pos}, {"unblockable", true}}));
				Status::apply(w, a, "shocked", 0.3, 1.0, z.owner);
				b->charge *= 0.5;
			}
		}
		return;
	}
	if (dbool(z.props, "shock", false)) {
		for (const auto& ap : w.actors) {
			ActorState& a = *ap;
			if (a.health <= 0.0 || a.id == z.owner || Status::immune(a, "conduct") || !a.grounded) continue;
			const std::string k = "shocked_" + itos(a.id);
			if (!w._in_zone(z, a.pos + V3(0, 0.3, 0), Sim::ACTOR_RADIUS) || z.props.has(k)) continue;
			z.props.set(k, true);
			w.hit_actor(a, D({{"attacker", z.owner}, {"attack_id", w.new_attack_id()}, {"damage", z.power * 0.4}, {"balance", 18.0}, {"kind", "lightning"},
			                  {"from", z.pos}, {"unblockable", true}}));
			Status::apply(w, a, "shocked", 0.4, 1.0, z.owner);
		}
	}
}

void arc_step_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Active) return;
	if (!inst.data.has("from")) inst.data.set("from", a.pos);
	const double dur = Charge::paramf(inst, "active", dnum(inst.def, "active"));
	if (inst.t + Sim::DT < dur - 1e-6 || dbool(inst.data, "shocked", false)) return;
	inst.data.set("shocked", true);
	const Vec3 p0 = dvec(inst.data, "from");
	const Vec3 p1 = a.pos + dvec(inst.data, "dir") * 0.6;
	Vec3 seg = p1 - p0;
	seg.y = 0.0f;
	const double ln = maxf(seg.length(), 0.01);
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0 || Status::immune(t, "conduct")) continue;
		const double tt = clampf((t.pos - p0).dot(seg / ln), 0.0, ln);
		const Vec3 q = p0 + seg / ln * tt;
		if (Vec2(t.pos.x - q.x, t.pos.z - q.z).length() <= Sim::ACTOR_RADIUS + 0.6) {
			AgentRef v = Agent::of_volume(&w, &a, &inst, "lightning", q + V3(0, 1.0, 0), seg / ln, D({{"E", Charge::paramf(inst, "E", 6.0)}}));
			w.hit_actor(t,
			            D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "damage", 6.0)}, {"balance", 12.0},
			               {"knock", V3(0, 0.5, 0)}, {"kind", "lightning"}, {"from", q}, {"power", v->power}}),
			            v);
			Status::apply(w, t, "shocked", 0.3, 1.0, a.id);
		}
	}
	w.emit("lightning", D({{"actor", a.id}, {"path", A({Value(p0 + V3(0, 1.0, 0)), Value(a.pos + V3(0, 1.0, 0))})}, {"arcs", Array()}, {"blocked", false},
	                       {"hits", Array()}, {"trail", true}}));
}

void overcharge_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel) return;
	if (a.wetness > 0.5 && !dbool(inst.data, "self_shock", false)) {
		inst.data.set("self_shock", true);
		w.hit_actor(a, D({{"attacker", -1}, {"attack_id", w.new_attack_id()}, {"damage", 10.0}, {"balance", 30.0}, {"knock", Vec3()}, {"kind", "lightning"},
		                  {"from", a.chest()}, {"unblockable", true}}));
		Status::apply(w, a, "shocked", 0.6, 1.0, a.id);
		w.emit("overcharge_short", D({{"actor", a.id}}));
		if (a.action.get() == &inst) w.set_phase(a, inst, ActionPhase::Recovery);
	} else if (w.tick % 12 == 0) {
		Verbs::fx(w, a, inst, "aura", D({{"on", true}, {"shape", "small"}, {"power", 6.0}}));
	}
}

}  // namespace FireLightning
}  // namespace ff
