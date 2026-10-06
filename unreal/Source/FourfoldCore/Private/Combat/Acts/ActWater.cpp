// Fourfold core - port of game/combat/act_water.gd: Water lash (tap) / ice lance (hold), draw-shape-release technique.
// Water must come from somewhere (pool, puddle, the 6 kg waterskin); quantities are tracked exactly. Kit additions:
// T2 Torrent (10 kg slug), T3 Maelstrom Lash (360 degrees); the technique condenses vapour and seizes enemy water in
// flight; an attack tap while holding water freezes it (T+A).
#include "Combat/Acts.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Agent.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace ActWater {

bool vapor_filter(MatBody& b) {
	if (!b.alive || b.controller >= 0 || b.mass < 0.05 || b.captured_by >= 0) return false;
	if (b.mat == Mat::Steam) return true;
	return b.is_water() && (b.form == Form::Cloud || b.form == Form::Zone) && b.phase != Phase::Frozen;
}

bool enemy_water(MatBody& b, const ActorState& a) {
	if (!b.alive || !b.is_water() || b.phase != Phase::Liquid || b.attack_id == 0 || b.controller >= 0) return false;
	if (b.attack_owner == a.id || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Zone || b.form == Form::Cloud ||
	    b.form == Form::Wall)
		return false;
	return b.mass >= 0.5;
}

bool _water_filter(MatBody& b) {
	// Legality through the engine (legacy cell puddle x grip_water: reclaim).
	return b.is_water() && b.form == Form::Puddle && b.controller < 0 && b.phase == Phase::Liquid && b.mass > 0.5 &&
	       Interactions::allows(b, "grip_water");
}

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	if (inst.id == "water_attack") {
		MatBody* hb = w.held(a);
		const double have = a.water_carried + (hb != nullptr && hb->is_water() ? hb->mass : 0.0);
		if (have < 1.0 && !a.in_water) {
			inst.data.set("fizzle", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
			return;
		}
		if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
			inst.data.set("fizzle", true);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		}
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "water_attack") {
		if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
		return w.attack_after_startup(a, inst, it);
	}
	return ActionPhase::Channel;
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "water_attack" && p == ActionPhase::Active) {
		if (inst.heavy) {
			const int t = inst.tier();
			if (t == 3) _maelstrom(w, a, inst);
			else if (t == 2) _torrent(w, a, inst);
			else _ice_lance(w, a, inst);
		} else {
			_lash(w, a, inst);
		}
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "water_attack") {
		if (!it.attack_held) inst.data.set("released", true);
		inst.data.set("face", w.aim_dir(a, it));
		if (inst.phase == ActionPhase::Charge && dbool(inst.data, "released", false) && inst.total >= dnum(inst.def, "heavy_min")) {
			if (w.spend_focus(a, dnum(inst.def, "heavy_cost") - dnum(inst.def, "cost"))) {
				inst.data.set("tier", maxi(1, inst.tier()));
				w.set_phase(a, inst, ActionPhase::Active);
			} else {
				w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", "ice_lance"}}));
				inst.heavy = false;
				w.set_phase(a, inst, ActionPhase::Active);
			}
		}
	} else if (inst.id == "water_tech") {
		if (it.tech_cancel && (inst.phase == ActionPhase::Startup || inst.phase == ActionPhase::Channel)) {
			// Honoured from the first frame: a cancel during startup never draws or fires.
			if (w.held(a) != nullptr) w.release_body(a, Vec3(0, -1, 0), false);   // falls and becomes a puddle
			w.emit("cancel", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		if (inst.phase != ActionPhase::Channel) return;
		inst.data.set("aim", w.aim_dir(a, it));
		inst.data.set("aim_active", it.aim_active);
		inst.data.set("face", inst.data.get("aim"));
		_draw(w, a, inst, it);
		MatBody* b = w.held(a);
		if (b != nullptr && it.attack_pressed && !dbool(inst.data, "frozen", false) && b->is_water() && b->phase == Phase::Liquid)
			_freeze_held(w, a, inst, *b);
		if (b != nullptr) {
			const double sway = std::sin(inst.total * 5.0) * 0.25;
			const Vec3 side = a.forward().cross(Vec3::Up());
			b->hold_point = a.pos + V3(0, 1.3 + 0.15 * std::sin(inst.total * 3.0), 0) + a.forward() * 0.8 + side * sway;
			if (!it.tech_held) {
				w.set_phase(a, inst, ActionPhase::Active);
				_stream(w, a, inst);
			}
		} else if (!it.tech_held) {
			w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
		}
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	MatBody* b = w.held(a);
	if (b == nullptr || !b->is_water()) return;
	if (inst.id == "water_tech" && reason == "cancel:guard") return;   // the guard keeps the held water as a shield
	w.release_body(a, Vec3(0, -1, 0), false);
}

// ---------------------------------------------------------------------------

void _draw(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	MatBody* b = w.held(a);
	const double maxm = dnum(inst.def, "max_draw");
	if (b != nullptr && b->mass >= maxm - 0.01) return;
	const double rate = dnum(inst.def, "draw_rate") * Sim::DT;
	const double reach = dnum(inst.def, "reach");
	if (b != nullptr && b->phase == Phase::Frozen) return;   // a frozen block does not draw
	if (b == nullptr && _seize(w, a, inst, it, reach)) return;
	// Source priority: pool (if near), puddle in the aim cone, then the waterskin.
	MatBody* src = nullptr;
	Vec3 src_point;
	const Vec3 pn = V3(clampf(a.pos.x, w.arena.pool_min.x, w.arena.pool_max.x), w.arena.pool_level,
	                   clampf(a.pos.z, w.arena.pool_min.y, w.arena.pool_max.y));
	if (Vec2(pn.x - a.pos.x, pn.z - a.pos.z).length() < reach && w.pool != nullptr && w.pool->mass > rate) {
		src = w.pool;
		src_point = pn;
	} else {
		MatBody* pd = w.find_body(a, w.aim_dir(a, it), reach, 70.0, [](MatBody& x) { return _water_filter(x); });
		if (pd != nullptr) {
			src = pd;
			src_point = pd->pos;
		}
	}
	if (src != nullptr) {
		const double room = maxm - (b != nullptr ? b->mass : 0.0);
		const double take = minf(minf(rate, src->mass), room);
		// Drawn water carries the source's exact state (temperature and ice fraction).
		const double e_take = take * (Sim::WATER_C * (src->temp - Sim::AMBIENT_C) - Sim::WATER_LATENT_FUSION * (1.0 - src->liquid));
		if (b == nullptr) {
			b = w.spawn_body(Mat::Water, Form::Stream, take, src_point + V3(0, 0.2, 0), "draw:" + itos(src->id));
			b->temp = src->temp;
			b->liquid = src->liquid;
			b->lineage.push_back(src->id);
			w.take_control(a, *b, 0.9, "draw");
		} else {
			const double e = b->thermal_energy() + e_take;
			b->liquid = (b->liquid * b->mass + src->liquid * take) / (b->mass + take);
			b->mass += take;
			w._set_energy(*b, e);
			b->update_radius();
		}
		src->mass -= take;
		if (src->form == Form::Puddle) {
			src->update_radius_puddle();
			if (src->mass <= 0.05) w.decay_body(*src, "drained");
		}
		if (w.tick % 8 == 0) w.emit("draw_water", D({{"actor", a.id}, {"body", b->id}, {"from", src->id}, {"at", src_point}}));
		return;
	}
	MatBody* vp = w.find_body(a, w.aim_dir(a, it), reach, 70.0, [](MatBody& x) { return vapor_filter(x); });
	if (vp != nullptr) {
		_condense(w, a, inst, *vp, b, minf(dnum(inst.def, "draw_rate") * 0.8 * Sim::DT, maxm - (b != nullptr ? b->mass : 0.0)));
		return;
	}
	if (b == nullptr && a.water_carried >= 1.0) {
		b = w.spawn_body(Mat::Water, Form::Stream, a.water_carried, a.chest() + a.forward() * 0.6, "waterskin:" + itos(a.id));
		a.water_carried = 0.0;
		w.take_control(a, *b, 0.9, "draw");
	} else if (b == nullptr && inst.t > 0.3) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		w.set_phase(a, inst, ActionPhase::Recovery);
	}
}

void _stream(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.held(a);
	if (b == nullptr) return;
	const Vec3 target = ActEarth::_throw_target(w, a, inst);
	const Vec3 v = ActEarth::launch_vel(b->pos, target, dnum(inst.def, "speed"));
	const double s = clampf(b->mass / 8.0, 0.5, 1.5);
	b->form = b->phase == Phase::Frozen ? Form::Shard : Form::Stream;
	w.release_body(a, v, true, dnum(inst.def, "damage") * s, dnum(inst.def, "balance") * s);
	w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", v.length()}, {"kind", "stream"}}));
}

void _lash(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Vec3 dir = dvec(inst.data, "face");
	const Dict& d = inst.def;
	w.emit("lash", D({{"actor", a.id}, {"dir", dir}, {"range", d.get("range")}}));
	// The lash is a water volume: a threat to fighters, a counter (class "water_jet") to shots it meets.
	AgentRef lash = Agent::of_volume(&w, &a, &inst, "water", a.chest(), dir, D({{"P", dnum(d, "power", 8.0)}}));
	lash->ccls = "water_jet";
	FxEvents::fx_for(w, a, inst, "cone", "water", D({{"length", dnum(d, "range")}, {"angle", dnum(d, "arc") * 0.5}, {"power", lash->power}}));
	for (ActorState* t : w.actors_in_cone(a, dir, dnum(d, "range"), dnum(d, "arc") * 0.5)) {
		const std::string res = w.hit_actor(*t,
		                                    D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", d.get("damage")},
		                                       {"balance", d.get("balance")}, {"knock", dir * dnum(d, "knock")}, {"kind", "water"},
		                                       {"from", a.chest()}}),
		                                    lash);
		if (res != "dup" && res != "evaded") t->wetness = 1.0;
	}
	// The lash also knocks light incoming stones aside (legacy cell (*, water_jet): <= 25 kg).
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.is_projectile() && b.attack_owner != a.id) {
			const Vec3 to = b.pos - a.chest();
			if (to.length() < dnum(d, "range") && Vec3(to.x, 0, to.z).normalized().dot(dir) > std::cos(deg_to_rad(dnum(d, "arc") * 0.5))) {
				AgentRef th = Agent::of_body(w, b, &a);
				IxCtx ctx;
				ctx.site = "lash";
				Interactions::resolve(w, *th, *lash, ctx);
			}
		}
	}
}

void _ice_lance(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Dict& d = inst.def;
	const double want = dnum(d, "shard_mass");
	MatBody* b = w.held(a);
	MatBody* shard = nullptr;
	if (b != nullptr && b->is_water()) {
		shard = b->mass <= want + 0.01 ? b : w.split_body(*b, want, b->pos);
		if (shard == b) w.release_body(a, Vec3(), false);
	} else {
		const double m = minf(want, a.water_carried);
		if (m < 0.5) {
			w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", "ice_lance"}}));
			return;
		}
		a.water_carried -= m;
		shard = w.spawn_body(Mat::Water, Form::Shard, m, a.hand_point(), "waterskin:" + itos(a.id));
	}
	// Freezing dumps the water's heat into the environment (game rule: water technique).
	const double e0 = shard->thermal_energy();
	shard->liquid = 0.0;
	shard->temp = -5.0;
	shard->phase = Phase::Frozen;
	shard->form = Form::Shard;
	shard->update_radius();
	w.ledger.freeze_dump += shard->thermal_energy() - e0;
	w.emit("transform", D({{"body", shard->id}, {"at", shard->pos}, {"from", "water"}, {"to", "ice"}, {"why", "frozen"}}));
	shard->controller = -1;
	shard->pos = a.hand_point();
	const Vec3 target = ActEarth::_throw_target(w, a, inst);
	shard->vel = ActEarth::launch_vel(shard->pos, target, dnum(d, "shard_speed"));
	shard->attack_id = w.new_attack_id();
	shard->attack_owner = a.id;
	shard->hit_set.clear();
	shard->hit_set.add(a.id);
	shard->damage = dnum(d, "heavy_damage");
	shard->balance_damage = dnum(d, "heavy_balance");
	shard->max_life = Sim::REMNANT_LIFETIME;
	w.emit("launch", D({{"actor", a.id}, {"body", shard->id}, {"speed", d.get("shard_speed")}, {"kind", "ice"}}));
}

// T2 Torrent: a heavy water slug (up to 10 kg from the waterskin / pool / puddle), a hard knock.
void _torrent(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double want = Charge::paramf(inst, "torrent_kg", 10.0);
	const double got = WaterUtil::take(w, a, want);
	if (got < 1.5) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", "water_attack"}}));
		return;
	}
	MatBody* b = w.spawn_body(Mat::Water, Form::Blob, got, a.hand_point(), "torrent:" + itos(a.id));
	b->tag = "slug";
	b->max_life = Sim::REMNANT_LIFETIME;
	b->gravity_scale = 0.35;
	const Vec3 target = ActEarth::_throw_target(w, a, inst);
	b->vel = Verbs::launch_vel(b->pos, target, Charge::paramf(inst, "torrent_speed", 20.0), 0.35);
	const double s = clampf(got / want, 0.4, 1.0);
	Verbs::arm(w, a, inst, *b, Charge::paramf(inst, "torrent_damage", 16.0) * s, Charge::paramf(inst, "torrent_balance", 34.0) * s);
	w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", b->vel.length()}, {"kind", "water"}, {"tier", 2}}));
	FxEvents::fx_for(w, a, inst, "release", "water",
	                 D({{"body", b->id}, {"pos", b->pos}, {"dir", b->vel.normalized()}, {"power", got * b->vel.length() / 20.0}, {"shape", ""}}));
}

// T3 Maelstrom Lash: a 360 degree whip, radius 5 m. Everything around the caster is hit and thrown outward, loose
// bodies meet the whip as a water_jet counter (cells in WaterRules), the spent water lands as a ring of puddles.
void _maelstrom(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "maelstrom_range", 5.0);
	const double kg = WaterUtil::take(w, a, Charge::paramf(inst, "maelstrom_kg", 2.0));
	const Vec3 dir = vvec(inst.data.get("face", a.forward()), a.forward());
	const double pw = Charge::paramf(inst, "maelstrom_power", 18.0);
	w.emit("lash", D({{"actor", a.id}, {"dir", dir}, {"range", rng_m}, {"around", true}}));
	AgentRef v = Agent::of_volume(&w, &a, &inst, "water", a.chest(), dir, D({{"P", pw}}));
	v->ccls = "water_jet";
	v->data.set("knock", Charge::paramf(inst, "maelstrom_knock", 8.0));
	FxEvents::fx_for(w, a, inst, "cone", "water", D({{"length", rng_m}, {"angle", 180.0}, {"power", pw}, {"shape", "fan"}}));
	FxEvents::fx_for(w, a, inst, "ring", "water", D({{"radius", rng_m}, {"power", pw}, {"pos", a.pos}}));
	for (size_t i = 0; i < w.actors.size(); ++i) {
		ActorState* t = w.actors[i].get();
		if (t == &a || t->team == a.team || t->health <= 0.0) continue;
		Vec3 rel = t->pos - a.pos;
		rel.y = 0.0f;
		if (rel.length() > rng_m + Sim::ACTOR_RADIUS) continue;
		const Vec3 rd = rel.length() > 0.05f ? rel.normalized() : dir;
		const std::string res = w.hit_actor(
		    *t,
		    D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "maelstrom_damage", 18.0)},
		       {"balance", Charge::paramf(inst, "maelstrom_balance", 40.0)}, {"knock", rd * dnum(v->data, "knock") + V3(0, 1.5, 0)},
		       {"kind", "water"}, {"from", a.chest()}, {"power", pw}, {"tier", 3}, {"mat", "water"}}),
		    v);
		if (res == "hit" || res == "knockdown" || res == "block") t->wetness = 1.0;
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.controller == a.id || b.static_body || b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Zone ||
		    b.form == Form::Puddle)
			continue;
		const Vec3 to = b.pos - a.chest();
		const Vec3 flat_to(to.x, 0, to.z);
		if (flat_to.length() > rng_m + b.radius) continue;
		v->dir = flat_to.length() > 0.05f ? flat_to.normalized() : dir;
		VerbVolume::meet_body(w, &a, *v, b);
	}
	if (kg > 0.0) {
		// The whip's spray falls around the caster.
		const int n = 3;
		for (int k = 0; k < n; ++k) {
			const double ang = kTau * double(k) / double(n) + double(w.tick % 7);
			WaterUtil::make_puddle(w, kg / double(n), WaterUtil::ground_at(w, a.pos + V3(std::cos(ang), 0, std::sin(ang)) * (rng_m * 0.7)));
		}
	}
}

// The technique reaches for a rival's stream / wave in flight: a grip contest.
bool _seize(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it, double reach) {
	const ActorState* ap = &a;
	MatBody* tb = w.find_body(a, w.aim_dir(a, it), reach, 70.0, [ap](MatBody& x) { return enemy_water(x, *ap); });
	if (tb == nullptr) return false;
	if (tb->mass > a.max_control_mass) {
		w.request_grip(a, *tb, 0.0, "seize");   // emits control_fail (mass)
		return true;
	}
	if (dint(inst.data, "seize_target", -1) != tb->id) {
		inst.data.set("seize_target", tb->id);
		w.emit("target_body", D({{"actor", a.id}, {"body", tb->id}}));
	}
	w.request_grip(a, *tb, w.grip_strength(a, *tb, 0.9, reach), "seize");
	return true;
}

// Condenses vapour (steam, mist, fog) into the held water: the mass moves from the vapour body into a water blob.
void _condense(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& vp, MatBody* b, double take) {
	(void)inst;
	take = minf(take, vp.mass);
	if (take < 0.01) return;
	if (b == nullptr) {
		b = w.spawn_body(Mat::Water, Form::Stream, take, vp.pos, "condense:" + itos(vp.id));
		b->lineage.push_back(vp.id);
		w.take_control(a, *b, 0.9, "draw");
	} else {
		const double e = b->thermal_energy();
		b->liquid = (b->liquid * b->mass + take) / (b->mass + take);
		b->mass += take;
		w._set_energy(*b, e);
		b->update_radius();
	}
	vp.mass -= take;
	if (vp.form == Form::Zone) {
		const double m0 = dnum(vp.props, "mass0", vp.mass + take);
		vp.props.set("mass0", m0);
		vp.zone_radius = maxf(0.6, dnum(vp.props, "radius0", vp.zone_radius) * std::sqrt(maxf(vp.mass, 0.0) / m0));
		vp.props.set("radius0", dnum(vp.props, "radius0", vp.zone_radius));
		vp.radius = vp.zone_radius;
	}
	if (vp.mass <= 0.05) {
		const double rest = vp.mass;
		vp.mass = 0.0;
		if (rest > 0.0) w.mass_ledger.vapor += rest;
		if (vp.form == Form::Zone) w.close_zone(vp, "condensed");
		else w.remove_body(vp, "condensed");
	}
	if (w.tick % 8 == 0) w.emit("draw_water", D({{"actor", a.id}, {"body", b->id}, {"from", vp.id}, {"at", vp.pos}, {"vapor", true}}));
}

// T+A: the held water freezes into an ice block (booked: the heat leaves through freeze_dump).
void _freeze_held(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	if (!w.spend_focus(a, 3.0)) return;
	inst.data.set("frozen", true);
	WaterUtil::freeze_body(w, &b);
	b.form = Form::Shard;
	b.update_radius();
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", "freeze"}}));
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "water"}, {"to", "ice"}, {"why", "frozen"}}));
	FxEvents::fx_for(w, a, inst, "cast", "ice", D({{"body", b.id}, {"shape", ""}}));
}

}  // namespace ActWater
}  // namespace ff
