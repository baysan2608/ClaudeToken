// Fourfold core - port of game/core/outcomes.gd.
#include "Sim/Outcomes.h"

#include "Combat/Acts.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/MatBody.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace Outcomes {

bool apply(CombatWorld& w, const std::string& nm, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (const OutcomeFn* custom = Interactions::handler(nm)) return (*custom)(w, t, c, res, r, ctx);
	if (nm == "pass") return pass_(w, t, c, res, r, ctx);
	if (nm == "block") return block(w, t, c, res, r, ctx);
	if (nm == "deflect") return deflect(w, t, c, res, r, ctx);
	if (nm == "redirect") return redirect(w, t, c, res, r, ctx);
	if (nm == "reflect") return reflect(w, t, c, res, r, ctx);
	if (nm == "reclaim") return reclaim(w, t, c, res, r, ctx);
	if (nm == "capture") return capture(w, t, c, res, r, ctx);
	if (nm == "absorb") return absorb(w, t, c, res, r, ctx);
	if (nm == "transform") return transform(w, t, c, res, r, ctx);
	if (nm == "shatter") return shatter(w, t, c, res, r, ctx);
	if (nm == "sink") return sink(w, t, c, res, r, ctx);
	if (nm == "conduct") return conduct(w, t, c, res, r, ctx);
	if (nm == "ground") return ground(w, t, c, res, r, ctx);
	if (nm == "amplify") return amplify(w, t, c, res, r, ctx);
	if (nm == "extinguish") return extinguish(w, t, c, res, r, ctx);
	if (nm == "weaken") return weaken(w, t, c, res, r, ctx);
	if (nm == "bend") return bend(w, t, c, res, r, ctx);
	if (nm == "slow") return slow(w, t, c, res, r, ctx);
	if (nm == "overwhelm") return overwhelm(w, t, c, res, r, ctx);
	if (nm == "clash") return clash(w, t, c, res, r, ctx);
	if (nm == "disrupt") return disrupt(w, t, c, res, r, ctx);
	if (nm == "neutralize") return neutralize(w, t, c, res, r, ctx);
	if (nm == "heat") return heat(w, t, c, res, r, ctx);
	if (nm == "push") return push(w, t, c, res, r, ctx);
	if (nm == "disperse") return disperse(w, t, c, res, r, ctx);
	return false;   // Godot: push_warning("Outcomes: unknown outcome")
}

int _id(const Agent* x) { return (x != nullptr && x->actor != nullptr) ? x->actor->id : -1; }

double _left(const IxResult& res, double absorb_f) {
	const double tp = res.tp;
	if (tp <= 1e-6) return 0.0;
	return clampf((tp - absorb_f * res.cp_eff) / tp, 0.0, 1.0);
}

double draw_heat(Agent* src, double hu) {
	if (src == nullptr || hu <= 0.0) return 0.0;
	if (src->body != nullptr && src->body->alive) {
		const double take = minf(hu, maxf(0.0, src->body->thermal_energy()));
		const double got = -Thermal::heat(*src->body, -take);
		src->heat = maxf(0.0, src->heat - got);
		return got;
	}
	const double g = minf(hu, maxf(0.0, src->heat));
	src->heat -= g;
	return g;
}

double move_heat(CombatWorld& w, Agent* src, MatBody& dst, double hu) {
	const double got = draw_heat(src, hu);
	if (got <= 0.0) return 0.0;
	const double used = w.heat_body(dst, got);
	double left = got - used;
	if (left > 1e-9) {
		if (src->body != nullptr && src->body->alive) {
			left -= Thermal::heat(*src->body, left);
		} else {
			src->heat += left;
			left = 0.0;
		}
		if (left > 1e-9) w.ledger.spent += left;
	}
	return used;
}

bool pass_(CombatWorld&, Agent&, Agent&, IxResult& res, const Dict&, IxCtx&) {
	res.pass_scale = 1.0;
	return true;
}

// ------------------------------------------------------------------ block

bool block(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (c.kind == "guard") {
		res.result = w.guard_chip(*c.actor, ctx.info, r, &t, res.ratio);
		return true;
	}
	if (c.kind == "stance") {
		res.stopped = false;
		res.pass_scale = 1.0;
		res.knock_scale = 0.0;
		return true;
	}
	if (t.kind == "body" && t.body != nullptr) {
		MatBody& b = *t.body;
		if (c.kind == "env") {
			w._body_impact(b, "wall");
			b.vel = Vec3(-b.vel.x * 0.15f, f32(maxf(b.vel.y, 0.0) * 0.2), -b.vel.z * 0.15f);
			return true;
		}
		if (c.body != nullptr && c.body->form == Form::Wall) {
			MatBody& wall = *c.body;
			if (b.form == Form::Wave) {
				w.emit("block", D({{"actor", wall.last_actor}, {"body", b.id}, {"kind", "wave_wall"}, {"wall", wall.id},
				                   {"power", res.tp}, {"mat", FxEvents::mat_of(b)}, {"tier", b.tier}, {"dir", b.wave_dir}}));
				w.emit("wave_blocked", D({{"body", b.id}, {"at", b.pos}}));
				w._settle_wave(b, "blocked");
				return true;
			}
			ActorState* owner = w.get_actor(wall.controller >= 0 ? wall.controller : wall.last_actor);
			const double momentum = static_cast<double>(b.vel.length()) * b.mass;
			wall.wall_damage_add(momentum / dnum(r, "dmg_div", 900.0));
			const Vec3 d = b.vel.normalized();
			b.vel = -b.vel * 0.12f;
			b.vel.y = 1.0f;
			b.attack_id = 0;
			w.emit("block", D({{"actor", owner != nullptr ? owner->id : -1}, {"body", b.id}, {"kind", "wall"}, {"wall", wall.id},
			                   {"power", res.tp}, {"mat", FxEvents::mat_of(b)}, {"tier", b.tier}, {"dir", d}}));
			if (wall.wall_damage >= 1.0) w._crumble_wall(wall);
			return true;
		}
		// Other barriers (zones, held plates) and active volumes stop the body.
		const Vec3 d2 = b.vel.normalized();
		if (b.form == Form::Wave) {
			w.emit("wave_blocked", D({{"body", b.id}, {"at", b.pos}}));
			w._settle_wave(b, "blocked");
		} else {
			b.vel = -b.vel * dnum(r, "keep", 0.12);
			b.vel.y = f32(maxf(b.vel.y, 1.0));
			b.attack_id = 0;
		}
		w.emit("block", D({{"actor", _id(&c)}, {"body", b.id}, {"kind", dstr(r, "kind", c.ccls)}, {"power", res.tp},
		                   {"mat", FxEvents::mat_of(b)}, {"tier", b.tier}, {"dir", d2}}));
		return true;
	}
	// A volume stopped by a barrier: the barrier takes part of its heat.
	if (c.body != nullptr && t.heat > 0.0 && dnum(r, "heat_share", 0.0) > 0.0) {
		const double used = w.heat_body(*c.body, t.heat * dnum(r, "heat_share"));
		t.heat -= used;
		res.heat_used += used;
	}
	return true;
}

// ------------------------------------------------------------------ deflect / redirect / reflect

bool deflect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.kind == "body" && t.body != nullptr) {
		MatBody& b = *t.body;
		Vec3 side;
		double mult = 0.5;
		double up = 2.5;
		if (c.kind == "guard") {
			side = c.actor->forward().cross(Vec3::Up()).normalized();
		} else if (c.kind == "volume" || c.kind == "move") {
			side = c.dir.cross(Vec3::Up()).normalized();
			mult = dnum(r, "side", 0.45);
			up = dnum(r, "up", 2.0);
		} else {
			Vec3 n = b.pos - c.pos;
			n.y = 0.0f;
			side = n.length() > 1e-3f ? n.normalized() : Vec3Right();
		}
		if (r.has("side") && c.kind == "guard") mult = dnum(r, "side");
		if (r.has("up") && c.kind == "guard") up = dnum(r, "up");
		if (side.dot(b.vel) < 0.0f) side = -side;
		b.vel = side * (static_cast<double>(b.vel.length()) * mult) + V3(0, up, 0);
		b.attack_id = 0;
		const std::string ev = c.perfect ? "perfect_deflect" : "deflect";
		w.emit(ev, D({{"actor", _id(&c)}, {"body", b.id}, {"verb", dstr(r, "verb", "deflect")}, {"kind", dstr(r, "kind", "stone")}}));
		res.result = c.perfect ? "perfect" : "deflect";
		return true;
	}
	// A volume (melee / cone / bolt) met a guard.
	if (c.kind == "guard") {
		ActorState& tg = *c.actor;
		const Dict& info = ctx.info;
		const std::string kind = dstr(info, "kind", "");
		if (c.perfect) {
			w.emit("perfect_deflect", D({{"actor", tg.id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", kind}}));
			tg.last_result = "perfect";
			ActorState* att = w.get_actor(dint(info, "attacker", -1));
			const double rng_m = dnum(r, "perfect_range", 3.0);
			if (att != nullptr && dnum(r, "perfect_balance", 0.0) > 0.0 && att->pos.distance_to(tg.pos) < rng_m) {
				att->balance -= dnum(r, "perfect_balance");
				att->balance_idle = 0.0;
				if (att->balance <= 0.0) {
					w._stagger(*att, "knockdown", 1.1, info);
					att->balance = 45.0;
				}
			}
			if (dnum(r, "absorb_reserve", 0.0) > 0.0 && t.heat > 0.0) {
				double gain = minf(t.heat * dnum(r, "absorb_reserve"), Sim::RESERVE_MAX - tg.heat_reserve);
				gain = maxf(gain, 0.0);
				tg.heat_reserve += gain;
				t.heat -= gain;
				res.absorbed = gain;
			}
			res.result = "perfect";
		} else {
			w.emit("deflect", D({{"actor", tg.id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", kind},
			                     {"verb", dstr(r, "verb", "deflect")}}));
			tg.last_result = "deflect";
			res.result = "deflect";
		}
	}
	return true;
}

bool redirect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	const std::string req = dstr(r, "requires", "");
	if (req == "redirect_current") {
		if (t.cls != "lightning" || c.actor == nullptr || !c.actor->has("redirect_current") ||
		    w.guard_element(*c.actor) != Sim::FIRE || !ctx.allow_redirect)
			return false;
		if (ctx.redirect_cb) ctx.redirect_cb(dnum(r, "factor", 0.8));
		res.stopped = true;
		res.pass_scale = 0.0;
		res.result = "redirected";
		return true;
	}
	if (t.kind != "body" || t.body == nullptr || c.actor == nullptr) return false;
	MatBody& b = *t.body;
	ActorState& ca = *c.actor;
	if (req == "stone_control" && !(b.is_stone() && b.mass <= ca.max_control_mass && b.attack_owner != ca.id)) return false;
	res.stopped = true;
	res.pass_scale = 0.0;
	if (dstr(r, "aim", "sender") == "dir") {
		const double spd = b.vel.length();
		b.vel = (c.dir * (spd * dnum(r, "speed_mult", 0.8))) + V3(0, dnum(r, "up", 1.5), 0);
		b.attack_owner = ca.id;
		b.attack_id = w.new_attack_id();
		b.hit_set.clear();
		b.hit_set.add(ca.id);
		w.emit("deflect", D({{"actor", ca.id}, {"body", b.id}, {"verb", dstr(r, "verb", "gust")}, {"kind", "stone"}}));
		res.result = "deflect";
		return true;
	}
	ActorState* tgt = w.get_actor(b.attack_owner);
	const double spd2 = maxf(Vec2(b.vel.x, b.vel.z).length(), dnum(r, "min_speed", 12.0)) * dnum(r, "speed_mult", 1.05);
	const Vec3 fwd = ca.forward();
	if (c.body != nullptr && r.has("wall_push")) {
		const Vec3 dir = tgt != nullptr ? (tgt->chest() - b.pos).normalized() : fwd;
		b.vel = tgt != nullptr ? ActEarth::launch_vel(b.pos, tgt->chest(), spd2) : dir * spd2;
		b.attack_id = w.new_attack_id();
		b.attack_owner = ca.id;
		b.hit_set.clear();
		b.hit_set.add(ca.id);
		if (dtruthy(r, "residual")) {
			b.residual_owner = ca.id;
			b.residual_authority = Interactions::cohesion(b.tier);
		}
		b.pos += dir * dnum(r, "wall_push");
	} else {
		b.vel = tgt != nullptr ? ActEarth::launch_vel(b.pos, tgt->chest(), spd2) : fwd * spd2;
		b.attack_id = w.new_attack_id();
		b.attack_owner = ca.id;
		b.hit_set.clear();
		b.hit_set.add(ca.id);
	}
	b.touch(ca.id, "redirect", w.tick);
	w.emit("perfect_deflect", D({{"actor", ca.id}, {"body", b.id}, {"verb", "redirect"}, {"kind", "stone"}}));
	res.result = "perfect";
	return true;
}

bool reflect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.kind == "body" && t.body != nullptr) {
		MatBody& b = *t.body;
		ActorState* owner = c.actor;
		ActorState* tgt = w.get_actor(b.attack_owner);
		const double spd = maxf(b.vel.length() * dnum(r, "speed_mult", 1.0), 8.0);
		if (tgt != nullptr && tgt != owner) b.vel = ActEarth::launch_vel(b.pos, tgt->chest(), spd);
		else b.vel = -b.vel.normalized() * spd + Vec3(0, 1.0f, 0);
		if (owner != nullptr) {
			b.attack_id = w.new_attack_id();
			b.attack_owner = owner->id;
			b.hit_set.clear();
			b.hit_set.add(owner->id);
			b.residual_owner = owner->id;
			b.residual_authority = Interactions::cohesion(b.tier);
			b.touch(owner->id, "reflect", w.tick);
		}
		w.emit(c.perfect ? "perfect_deflect" : "deflect",
		       D({{"actor", _id(&c)}, {"body", b.id}, {"verb", "reflect"}, {"kind", FxEvents::mat_of(b)}}));
		res.result = c.perfect ? "perfect" : "deflect";
		return true;
	}
	res.extra.set("reflected", true);
	if (t.actor != nullptr && c.actor != nullptr && t.actor != c.actor) {
		const Dict& info = ctx.info;
		const double dmg = dnum(info, "damage", res.tp) * dnum(r, "factor", 1.0);
		ActorState* src = c.actor;
		if (src->pos.distance_to(t.actor->pos) <= dnum(r, "range", 14.0) && w.los(src->chest(), t.actor->chest())) {
			w.hit_actor(*t.actor, D({{"attacker", src->id},
			                         {"attack_id", w.new_attack_id()},
			                         {"damage", dmg},
			                         {"balance", dnum(info, "balance", dmg) * dnum(r, "factor", 1.0)},
			                         {"knock", (t.actor->pos - src->pos).normalized() * 3.0f},
			                         {"kind", dstr(info, "kind", t.cls)},
			                         {"from", src->chest()}}));
		}
	}
	w.emit("reflect", D({{"actor", _id(&c)}, {"to", _id(&t)}, {"cls", t.cls}}));
	res.result = c.perfect ? "perfect" : "deflect";
	return true;
}

// ------------------------------------------------------------------ control

bool reclaim(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	if (t.body == nullptr || c.actor == nullptr || !t.body->alive) return false;
	if (t.body->mass > c.actor->max_control_mass) return false;
	w.take_control(*c.actor, *t.body, dnum(r, "authority", 0.9), "reclaim");
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool capture(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (t.body == nullptr) return false;
	if (c.body == nullptr) return reclaim(w, t, c, res, r, ctx);
	MatBody& b = *t.body;
	if (b.captured_by == c.body->id) {
		res.stopped = true;
		return true;
	}
	if (static_cast<int>(c.body->captured.size()) >= dint(r, "max_captured", 6)) return false;
	c.body->captured.push_back(b.id);
	b.captured_by = c.body->id;
	b.props.set("capture_off", b.pos - c.body->pos);
	b.props.set("release_speed", dnum(r, "release_speed", 0.0));
	b.attack_id = 0;
	b.vel = c.body->vel;
	w.emit("capture", D({{"body", b.id}, {"by", c.body->id}, {"actor", _id(&c)}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool absorb(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.kind == "body" && t.body != nullptr && t.body->alive) {
		MatBody& b = *t.body;
		if (b.mass <= 1e-9) {
			w.decay_body(b, "absorbed");
			return true;
		}
		if (c.body != nullptr && c.body->alive && c.body != &b && c.body->mat == b.mat && b.form != Form::Pool) {
			if (c.body->form == Form::Puddle || c.body->form == Form::Pool || c.body->controller >= 0 || dbool(r, "merge", true)) {
				w.merge_bodies(*c.body, b);
				if (c.body->form == Form::Puddle) c.body->update_radius_puddle();
				return true;
			}
		}
		if (c.actor != nullptr && r.has("hu")) {
			const double room = Sim::RESERVE_MAX - c.actor->heat_reserve;
			const double take = minf(minf(dnum(r, "hu"), maxf(0.0, b.thermal_energy())), room);
			const double got = -Thermal::heat(b, -take);
			c.actor->heat_reserve += got;
			res.absorbed = got;
			res.stopped = false;
			return true;
		}
		w.decay_body(b, "absorbed");
		return true;
	}
	if (c.actor != nullptr && t.heat > 0.0) {
		const double gain = clampf(t.heat * dnum(r, "share", 1.0), 0.0, Sim::RESERVE_MAX - c.actor->heat_reserve);
		c.actor->heat_reserve += gain;
		t.heat -= gain;
		res.absorbed = gain;
	}
	return true;
}

// ------------------------------------------------------------------ material change

MatBody* _subject(Agent& t, Agent& c, const Dict& r) {
	const std::string tg = dstr(r, "target", "");
	if (tg == "counter") return c.body;
	if (tg == "threat") return t.body;
	return t.body != nullptr ? t.body : c.body;
}

Agent* _heat_src(Agent& t, Agent& c, const MatBody* subject) {
	if (t.body != subject && t.heat > 0.0) return &t;
	if (c.body != subject && c.heat > 0.0) return &c;
	return nullptr;
}

bool transform(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* bp = _subject(t, c, r);
	if (bp == nullptr || !bp->alive) return false;
	MatBody& b = *bp;
	const std::string to = dstr(r, "to", res.to);
	Agent* src = _heat_src(t, c, bp);
	double hu = 0.0;
	if (r.has("energy")) hu = dnum(r, "energy");
	else if (r.has("rate")) hu = dnum(r, "rate") * Sim::DT;
	else if (src != nullptr) hu = src->heat * dnum(r, "share", 1.0);
	else hu = res.cp_eff * dnum(r, "hu_per_pu", Interactions::HU_PER_PU);
	if (to == "steam") {
		if (!b.is_water()) return false;
		double share = hu;
		if (b.phase == Phase::Frozen) {
			const double used = src != nullptr ? move_heat(w, src, b, share) : w.heat_body(b, share);
			res.heat_used += used;
		} else {
			if (src != nullptr) share = draw_heat(src, share);
			w.boil_water(b, share, b.pos);
			res.heat_used += share;
			if (b.mass <= 0.05) w.decay_body(b, "boiled");
		}
		if (b.controller >= 0) res.extra.set("shielded", b.controller);
		if (r.has("event")) w.emit(dstr(r, "event"), D({{"actor", src == &t ? _id(&t) : _id(&c)}, {"body", b.id}}));
	} else if (to == "water") {
		if (!b.is_water()) return false;
		const double used2 = src != nullptr ? move_heat(w, src, b, hu) : w.heat_body(b, hu);
		res.heat_used += used2;
	} else if (to == "rock" || to == "obsidian" || to == "hot_rock") {
		if (dstr(r, "requires", "") == "liquid" && b.liquid <= 0.0) return false;
		MatBody* water = (c.body != nullptr && c.body->is_water()) ? c.body : nullptr;
		if (water != nullptr && water->mass > 0.0) {
			w.quench_energy(b, *water, hu);
		} else {
			double want = minf(hu, maxf(0.0, b.thermal_energy()));
			if (to == "hot_rock") want = minf(want, b.liquid * b.mass * Materials::latent(b.mat));
			const double got = -Thermal::heat(b, -want);
			w.ledger.ambient -= got;
			res.heat_used += got;
		}
		if (to == "obsidian" && b.liquid <= 0.0) b.tag = "obsidian";
	} else if (to == "lava" || to == "molten_metal") {
		double used3 = 0.0;
		if (src != nullptr) {
			used3 = move_heat(w, src, b, hu);
		} else {
			used3 = w.heat_body(b, hu);
			w.ledger.generated += used3;
		}
		res.heat_used += used3;
	} else if (to == "ice" || to == "snow") {
		if (!b.is_water()) return false;
		const double e0 = b.thermal_energy();
		b.liquid = 0.0;
		b.temp = minf(b.temp, -5.0);
		b.phase = Phase::Frozen;
		w.ledger.freeze_dump += b.thermal_energy() - e0;
		if (to == "snow") b.tag = "snow";
	} else if (to == "mist") {
		if (!b.is_water()) return false;
		b.form = Form::Cloud;
		b.tag = "mist";
		b.attack_id = 0;
		if (b.max_life < 0.0) b.max_life = b.age + 4.0;
	} else if (to == "glass") {
		if (b.mat != Mat::Sand) return false;
		w.convert_mat(b, Mat::Glass, "sand_to_glass");
	} else if (to == "sandstone") {
		if (b.mat != Mat::Sand) return false;
		w.convert_mat(b, Mat::Stone, "sand_to_sandstone");
		b.tag = "sandstone";
	} else if (to == "mud") {
		if (b.mat != Mat::Sand && !b.is_water()) return false;
		b.tag = "mud";
		b.props.set("wet", true);
	} else if (to == "ash") {
		if (b.mat != Mat::Plant) return false;
		w.burn_plant(b, b.mass);
	} else {
		return false;
	}
	res.stopped = dbool(r, "stops", false);
	res.pass_scale = res.stopped ? 0.0 : 1.0;
	if (c.kind == "guard" && r.has("guard_kind")) {
		const Dict& info = ctx.info;
		w.emit("block", D({{"actor", c.actor->id}, {"attacker", info.get("attacker", Value(_id(&t)))}, {"kind", dstr(r, "guard_kind")}}));
		res.result = "block";
		res.stopped = true;
		res.pass_scale = 0.0;
	}
	if (b.alive && in_list(to, {"ice", "snow", "mist", "mud", "obsidian", "sandstone", "glass", "ash"}))
		w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", t.cls}, {"to", to}, {"why", "interaction"}}));
	return true;
}

bool heat(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	MatBody* b = _subject(t, c, r);
	if (b == nullptr || !b->alive) return false;
	Agent* src = _heat_src(t, c, b);
	if (src == nullptr || src->heat <= 0.0) return true;
	const double used = move_heat(w, src, *b, src->heat * dnum(r, "share", 0.5));
	res.heat_used += used;
	return true;
}

bool shatter(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	if (t.kind != "body" && c.body != nullptr && c.body->alive) {
		_break_counter(w, c, res);
		res.pass_scale = _left(res, dnum(r, "absorb_on_fail", 0.5));
		res.stopped = res.pass_scale <= 0.0;
		w.emit("shatter", D({{"body", c.body->id}, {"mass", c.body->mass}, {"by", t.cls}}));
		return true;
	}
	if (t.body == nullptr || !t.body->alive) return false;
	MatBody& b = *t.body;
	const int n = dint(r, "pieces", 3);
	w.emit("shatter", D({{"body", b.id}, {"mass", b.mass}, {"by", c.ccls}}));
	const Vec3 v = b.vel;
	b.attack_id = 0;
	const double share = b.mass / static_cast<double>(n);
	for (int k = 0; k < n - 1; ++k) {
		if (b.mass <= share * 0.5) break;
		const double ang = kTau * static_cast<double>(k + 1) / static_cast<double>(n);
		const Vec3 off = V3(std::cos(ang), 0.2, std::sin(ang)) * 0.3f;
		MatBody* p = w.split_body(b, share, b.pos + off);
		p->form = p->form != Form::Shard ? Form::Chunk : Form::Shard;
		p->vel = v * 0.3f + off.normalized() * 3.0f + Vec3(0, 2.0f, 0);
		p->max_life = Sim::REMNANT_LIFETIME;
		p->attack_id = 0;
	}
	b.vel = v * 0.3f + Vec3(0, 2.0f, 0);
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool sink(CombatWorld& w, Agent& t, Agent&, IxResult& res, const Dict&, IxCtx&) {
	if (t.body == nullptr || !t.body->alive) return false;
	w.emit("sink", D({{"body", t.body->id}, {"mass", t.body->mass}, {"at", t.body->pos}}));
	w.decay_body(*t.body, "sunk");
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool conduct(CombatWorld&, Agent&, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	res.extra.set("conduct", true);
	res.pass_scale = dnum(r, "factor", 1.0);
	if (c.body != nullptr && c.body->controller >= 0) res.extra.set("conduct_to", c.body->controller);
	return true;
}

bool ground(CombatWorld& w, Agent&, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	res.pass_scale = dnum(r, "factor", 0.0);
	res.stopped = res.pass_scale <= 0.0;
	if (r.has("event")) w.emit(dstr(r, "event"), D({{"actor", _id(&c)}}));
	return true;
}

bool amplify(CombatWorld& w, Agent& t, Agent&, IxResult& res, const Dict& r, IxCtx&) {
	const double amp = dnum(r, "amp", 1.3);
	res.extra.set("amp", amp);
	res.pass_scale = amp;
	if (t.body != nullptr && t.body->alive && t.body->mat == Mat::Fire) {
		const double add = t.body->heat_payload * (amp - 1.0);
		t.body->heat_payload += add;
		w.ledger.generated += add;
	}
	return true;
}

bool extinguish(CombatWorld& w, Agent& t, Agent&, IxResult& res, const Dict&, IxCtx&) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.body != nullptr && t.body->alive && (t.body->mat == Mat::Fire || t.cls == "fire_field")) {
		w.emit("extinguish", D({{"body", t.body->id}}));
		w.decay_body(*t.body, "extinguished");
	} else if (t.body == nullptr && t.heat > 0.0) {
		w.ledger.spent += t.heat;
		t.heat = 0.0;
	}
	return true;
}

void scale_damage(MatBody* b, double f) {
	if (b == nullptr) return;
	b->props.set("dmg_scale", clampf(dnum(b->props, "dmg_scale", 1.0) * f, 0.0, 1.0));
}

double heat_capacity(const MatBody* b) {
	if (b == nullptr || !b->alive || b->mass <= 0.0) return kInf;
	switch (b->mat) {
		case Mat::Water:
		case Mat::Steam: {
			double per = Sim::WATER_LATENT_VAPOR + Sim::WATER_C * maxf(0.0, Sim::WATER_BOIL_C - minf(b->temp, Sim::WATER_BOIL_C));
			if (b->mat == Mat::Water && b->liquid < 1.0) per += Sim::WATER_LATENT_FUSION * (1.0 - b->liquid);
			return b->mass * per;
		}
		case Mat::Air:
		case Mat::Fire: return kInf;
		case Mat::Plant: return b->mass * Materials::c(b->mat) * 200.0;
		case Mat::Stone:
			return b->mass * Sim::STONE_C * maxf(0.0, Sim::STONE_MELT_C - b->temp) + b->mass * Sim::STONE_LATENT * (1.0 - b->liquid);
		default: break;
	}
	if (Materials::is_fusible(b->mat)) return b->mass * Materials::c(b->mat) * maxf(0.0, Materials::melt(b->mat) - b->temp);
	return kInf;
}

bool weaken(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	const double f = _left(res, 1.0);
	res.pass_scale = f;
	res.stopped = false;
	if (c.kind == "stance") {
		res.knock_scale = f;
		return true;
	}
	if (t.body != nullptr && t.body->alive) {
		MatBody& b = *t.body;
		scale_damage(&b, f);
		const Value& w_r = r.get("w");
		if (t.ch.K > 0.0 && vnum(w_r.get("K"), 1.0) > 0.0) {
			b.vel *= f;
			if (b.form == Form::Wave) b.wave_budget *= lerpf(1.0, f, 0.5);
		}
		if (t.ch.H > 0.0 && vnum(w_r.get("H"), 1.0) > 0.0 && b.thermal_energy() > 0.0) {
			double take = minf((1.0 - f) * t.ch.H * Interactions::HU_PER_PU * dnum(r, "heat_mult", 1.0), b.thermal_energy());
			take = minf(take, heat_capacity(c.body));
			if (c.body != nullptr && c.body->is_water() && c.body->mass > 0.0) {
				const double got = -Thermal::heat(b, -take);
				w.boil_water(*c.body, got, b.pos);
			} else {
				const double got2 = -Thermal::heat(b, -take);
				w.ledger.ambient -= got2;
			}
		}
	}
	return true;
}

bool bend(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	res.pass_scale = 1.0;
	if (t.body == nullptr || !t.body->alive) return true;
	MatBody& b = *t.body;
	if (r.has("bend_impulse")) {
		b.vel += c.dir * (dnum(r, "bend_impulse") / maxf(b.mass, 0.1));
	} else {
		const double f = clampf((res.ratio - 0.5) / 0.5, 0.0, 1.0);
		const double ang = deg_to_rad(dnum(r, "angle", 60.0)) * f;
		const Vec3 away = b.pos - (c.actor != nullptr ? c.actor->pos : c.pos);
		const Vec3 side = b.vel.cross(Vec3::Up());
		const double sgn = side.dot(away) <= 0.0f ? 1.0 : -1.0;
		b.vel = rotated(b.vel, Vec3::Up(), ang * sgn);
	}
	w.emit("bend", D({{"actor", _id(&c)}, {"body", b.id}}));
	return true;
}

bool slow(CombatWorld&, Agent& t, Agent&, IxResult& res, const Dict& r, IxCtx&) {
	const double k = dnum(r, "factor", 0.6);
	res.pass_scale = k;
	if (t.body != nullptr && t.body->alive) {
		scale_damage(t.body, k);
		t.body->vel *= k;
		if (t.body->form == Form::Wave) t.body->props.set("speed", dnum(t.body->props, "speed", 7.5) * k);
	}
	return true;
}

void _break_counter(CombatWorld& w, Agent& c, IxResult& res) {
	res.counter_broken = true;
	if (c.kind == "guard" && c.actor != nullptr) {
		w._stagger(*c.actor, "guard_break", 0.7, Dict());
		c.actor->balance = minf(c.actor->balance, 35.0);
		return;
	}
	MatBody* cb = c.body;
	if (cb == nullptr || !cb->alive || (cb->static_body && cb->form == Form::Pool)) return;
	switch (cb->form) {
		case Form::Wall: w._crumble_wall(*cb); break;
		case Form::Zone:
		case Form::Cloud: w.close_zone(*cb, "broken"); break;
		default:
			if (cb->controller >= 0) {
				ActorState* h = w.get_actor(cb->controller);
				if (h != nullptr) w.release_body(*h, Vec3(0, -1.0f, 0), false);
			}
			w.emit("counter_broken", D({{"body", cb->id}}));
			break;
	}
}

bool overwhelm(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	const double f = _left(res, dnum(r, "absorb_on_fail", 0.5));
	res.pass_scale = f;
	if (c.kind != "env" && c.kind != "volume" && c.kind != "move") _break_counter(w, c, res);
	if (t.body != nullptr && t.body->alive) {
		t.body->vel *= f;
		scale_damage(t.body, f);
	}
	w.emit("overwhelm", D({{"threat", t.cls}, {"counter", c.ccls}, {"left", f}}));
	return true;
}

bool clash(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	const double tp_a = res.tp;
	const double tp_b = Interactions::threat_power(c, r);
	MatBody* a = t.body;
	MatBody* b = c.body;
	if (a == nullptr || b == nullptr || !a->alive || !b->alive) return false;
	const double hi = maxf(tp_a, tp_b);
	const double lo = minf(tp_a, tp_b);
	const bool even = hi <= 1e-6 || lo / hi >= dnum(r, "even_at", 0.8);
	MatBody* win = tp_a >= tp_b ? a : b;
	MatBody* lose = win == a ? b : a;
	if (a->form == Form::Wave && b->form == Form::Wave) {
		if (a->mat == b->mat) {
			w.merge_bodies(*win, *lose);
		} else {
			w._settle_wave(*lose, "clash");
			win->wave_budget *= clampf(1.0 - lo / maxf(hi, 1e-6), 0.0, 1.0);
		}
	} else if (even) {
		for (MatBody* x : {a, b}) {
			x->vel = x->vel * -0.2f + Vec3(0, 2.0f, 0);
			x->attack_id = 0;
		}
	} else {
		Vec3 side = lose->vel.cross(Vec3::Up()).normalized();
		if (side.dot(lose->pos - win->pos) < 0.0f) side = -side;
		lose->vel = side * (lose->vel.length() * 0.4f) + Vec3(0, 2.5f, 0);
		lose->attack_id = 0;
		win->vel *= std::sqrt(clampf(1.0 - lo / hi, 0.05, 1.0));
	}
	res.stopped = true;
	res.result = even ? "even" : (win == a ? "win" : "lose");
	w.emit("clash", D({{"a", a->id}, {"b", b->id}, {"winner", even ? -1 : win->id}, {"pos", (a->pos + b->pos) * 0.5f},
	                   {"power", hi}, {"mat", FxEvents::mat_of(*win)}}));
	return true;
}

bool disruptable(const ActorState& who) {
	const ActionInst* inst = who.action.get();
	if (inst == nullptr) return false;
	if (inst->id == "guard" || who.guarding) return false;
	return inst->phase == ActionPhase::Charge || inst->phase == ActionPhase::Channel;
}

bool disrupt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict&, IxCtx&) {
	ActorState* who = t.actor;
	if (who == nullptr || who->action == nullptr) return false;
	ActionRef inst = who->action;
	if (!disruptable(*who)) return false;
	const double p = res.cp_eff;
	if (p < Interactions::disrupt_threshold(inst->tier())) return false;
	w.interrupt_action(*who, "disrupt");
	w.emit("disrupt", D({{"actor", who->id}, {"by", _id(&c)}, {"move", inst->id}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool neutralize(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict&, IxCtx&) {
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.body != nullptr && t.body->alive) {
		if (t.body->form == Form::Zone || t.body->form == Form::Cloud) w.close_zone(*t.body, "neutralized");
		else w.decay_body(*t.body, "neutralized");
	} else if (t.body == nullptr && t.heat > 0.0) {
		w.ledger.spent += t.heat;
		t.heat = 0.0;
	}
	if (c.body != nullptr && c.body->alive && (c.body->form == Form::Zone || c.body->form == Form::Cloud))
		w.close_zone(*c.body, "neutralized");
	w.emit("neutralize", D({{"threat", t.cls}, {"counter", c.ccls}}));
	return true;
}

bool push(CombatWorld&, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx&) {
	if (t.body == nullptr || !t.body->alive) return false;
	const double knock = dnum(c.data, "knock", 7.0);
	t.body->vel += c.dir * (knock * dnum(r, "push_mult", 12.0) / maxf(t.body->mass, 1.0)) + Vec3(0, 1.0f, 0);
	t.body->on_ground = false;
	res.pass_scale = 1.0;
	return true;
}

bool disperse(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict&, IxCtx&) {
	if (t.body == nullptr && t.kind == "volume") {
		w.ledger.spent += maxf(0.0, t.heat);
		t.heat = 0.0;
		w.emit("disperse", D({{"actor", _id(&c)}, {"body", -1}, {"cls", t.cls}}));
		res.stopped = true;
		res.pass_scale = 0.0;
		return true;
	}
	if (t.body == nullptr || !t.body->alive) return false;
	MatBody& b = *t.body;
	b.vel += c.dir * 9.0f;
	b.max_life = minf(b.max_life, b.age + 0.6);
	w.emit("disperse", D({{"actor", _id(&c)}, {"body", b.id}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

}  // namespace Outcomes
}  // namespace ff
