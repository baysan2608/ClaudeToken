// Fourfold core - port of game/core/combat_world.gd: zones and contacts (projectiles / waves vs actors, clash pass,
// zone pass).
#include "Sim/CombatWorld.h"

#include "Combat/Verbs.h"
#include "Sim/Agent.h"
#include "Sim/FxEvents.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

MatBody* CombatWorld::spawn_zone(const std::string& tag, Vec3 p, double radius, int owner_id, double power, Mat mat, double mass, double life,
                                 const std::string& origin) {
	MatBody* z = spawn_body(mat, Form::Zone, mass, p, origin);
	z->tag = tag;
	z->zone_radius = radius;
	z->radius = radius;
	z->owner = owner_id;
	z->power = power;
	z->max_life = life;
	z->static_body = false;
	if (mat == Mat::Water) z->phase = Phase::Liquid;
	FxEvents::zone(*this, *z, "open");
	return z;
}

void CombatWorld::close_zone(MatBody& z, const std::string& why) {
	if (!z.alive) return;
	FxEvents::zone(*this, z, "close");
	decay_body(z, why);
}

void CombatWorld::_update_zone(MatBody& z, double dt) {
	const int att = dint(z.props, "attach", -1);
	if (att >= 0) {
		ActorState* a = get_actor(att);
		if (a == nullptr) {
			close_zone(z, "detached");
			return;
		}
		z.pos = a->pos + dvec(z.props, "attach_off");
		z.vel = a->vel;
	} else if (dnum(z.props, "walk_speed", 0.0) > 0.0) {
		ActorState* tgt = get_actor(dint(z.props, "walk_target", -1));
		if (tgt != nullptr) {
			Vec3 to = tgt->pos - z.pos;
			to.y = 0.0f;
			if (to.length() > 0.3f) {
				z.vel = to.normalized() * dnum(z.props, "walk_speed");
				z.pos += z.vel * dt;
			}
		}
	} else if (z.vel.length() > 0.01f) {
		z.pos += z.vel * dt;
		z.vel *= 1.0 - minf(1.0, dnum(z.props, "drag", 0.0) * dt);
	}
	if (z.max_life > 0.0 && z.age > z.max_life) close_zone(z, "expired");
}

void CombatWorld::_zone_pass() {
	const double dt = Sim::DT;
	const size_t n = bodies.size();
	for (size_t zi = 0; zi < n; ++zi) {
		MatBody& z = *bodies[zi];
		if (!z.alive || z.form != Form::Zone) continue;
		if (!z.tag.empty()) {
			auto& effects = Hooks::zone_effects();
			auto it = effects.find(z.tag);
			if (it != effects.end()) {
				const ZoneEffectFn cb = it->second;
				if (cb) cb(*this, z, dt);
				if (!z.alive) continue;
			}
		}
		if (z.props.has("actor_status") || z.props.has("dps")) {
			for (auto& ap : actors) {
				ActorState& a = *ap;
				if (a.health <= 0.0 || (dbool(z.props, "spare_owner", true) && a.id == z.owner)) continue;
				if (!_in_zone(z, a.pos + Vec3(0, 0.9f, 0), Sim::ACTOR_RADIUS)) continue;
				if (z.props.has("actor_status"))
					Status::apply(*this, a, dstr(z.props, "actor_status"), dnum(z.props, "status_t", 0.5), dnum(z.props, "status_mag", 1.0), z.owner);
				if (z.props.has("dps") && !(dbool(z.props, "ground_only", false) && Status::immune(a, "ground")))
					a.health = maxf(0.0, a.health - dnum(z.props, "dps") * dt);
			}
		}
		const int rate = static_cast<int>(maxf(1.0, dnum(z.props, "rate", 0.1) * Sim::HZ));
		for (size_t bi = 0; bi < n; ++bi) {
			MatBody& b = *bodies[bi];
			if (&b == &z || !b.alive || b.static_body || b.controller >= 0 || b.captured_by == z.id) continue;
			if (!_in_zone(z, b.pos, b.radius)) continue;
			const std::string key = itos(z.id) + "|" + itos(b.id);
			auto it = _zone_pairs.find(key);
			if (tick - (it == _zone_pairs.end() ? -100000 : it->second) < rate) continue;
			_zone_pairs[key] = tick;
			AgentRef counter = Agent::of_body(*this, z);
			counter->actor = get_actor(z.owner);
			AgentRef th = Agent::of_body(*this, b, counter->actor);
			IxCtx ctx;
			ctx.continuous = true;
			ctx.site = "zone";
			Interactions::resolve(*this, *th, *counter, ctx, &Interactions::PASS_RULE());
			if (!z.alive) break;
		}
	}
	if (tick % 120 == 0 && _zone_pairs.size() > 256) _zone_pairs.clear();
	if (tick % 120 == 0 && _partial_pairs.size() > 64) {
		std::unordered_map<std::string, int64_t> keep;
		for (const auto& kv : _partial_pairs)
			if (tick - kv.second <= Interactions::CONTACT_TICKS) keep[kv.first] = kv.second;
		_partial_pairs.swap(keep);
	}
}

bool CombatWorld::_in_zone(const MatBody& z, Vec3 p, double r) const {
	const double h = dnum(z.props, "height", 2.5);
	if (p.y < z.pos.y - 0.5 || p.y > z.pos.y + h) return false;
	return Vec2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius + r;
}

std::vector<MatBody*> CombatWorld::bodies_in_zone(const MatBody& z) const {
	std::vector<MatBody*> out;
	for (const BodyRef& bp : bodies) {
		MatBody& b = *bp;
		if (&b != &z && b.alive && _in_zone(z, b.pos, b.radius)) out.push_back(&b);
	}
	return out;
}

std::vector<ActorState*> CombatWorld::actors_in_zone(const MatBody& z) const {
	std::vector<ActorState*> out;
	for (const auto& ap : actors) {
		ActorState& a = *ap;
		if (a.health > 0.0 && _in_zone(z, a.pos + Vec3(0, 0.9f, 0), Sim::ACTOR_RADIUS)) out.push_back(&a);
	}
	return out;
}

// ============================================================== contacts

void CombatWorld::_contacts() {
	for (size_t bi = 0; bi < bodies.size(); ++bi) {
		MatBody& b = *bodies[bi];
		if (!b.alive) continue;
		if (b.is_projectile() && b.vel.length() > 2.0f) {
			for (auto& ap : actors) {
				ActorState& a = *ap;
				if (b.hit_set.has(a.id) || a.health <= 0.0) continue;
				if (_touches_actor(b, a, 0.0)) {
					_projectile_hits_actor(b, a);
					if (!b.alive || b.attack_id == 0) break;
				}
			}
		} else if (b.form == Form::Wave && b.attack_id != 0) {
			for (auto& ap : actors) {
				ActorState& a = *ap;
				if (b.hit_set.has(a.id) || a.health <= 0.0) continue;
				const double fl = Vec2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length();
				if (fl < b.wave_width * 0.5 + Sim::ACTOR_RADIUS && a.pos.y < b.pos.y + 0.45f) {
					if (!b.tag.empty() && Status::immune(a, "ground")) continue;
					b.hit_set.add(a.id);
					Vec3 kn = b.wave_dir * 4.0f + Vec3(0, 3.0f, 0);
					std::string kind = "lava";
					if (!b.tag.empty()) {
						kn = b.wave_dir * dnum(b.props, "knock", 4.0) + V3(0, dnum(b.props, "lift", 3.0), 0);
						kind = dstr(b.props, "kind", FxEvents::mat_of(b));
					}
					const double hs = hit_scale(b);
					hit_actor(a, D({{"attacker", b.attack_owner},
					                {"attack_id", b.attack_id},
					                {"damage", b.damage * hs},
					                {"balance", b.balance_damage * hs},
					                {"knock", kn * maxf(hs, 0.4)},
					                {"kind", kind},
					                {"from", b.pos - b.wave_dir},
					                {"body", b.id},
					                {"src_attack", dint(b.props, "src_attack", -1)}}));
					if (!b.tag.empty() && b.props.has("hit_status"))
						Status::apply(*this, a, dstr(b.props, "hit_status"), dnum(b.props, "hit_status_t", 1.0), 1.0, b.attack_owner);
				}
			}
		}
		if (b.is_stone() && b.temp >= Sim::HOT_ROCK_C && b.controller < 0 && b.form != Form::Wave) {
			for (auto& ap : actors) {
				ActorState& a = *ap;
				if (a.burn_cd <= 0.0 && _touches_actor(b, a, 0.15)) {
					if (Status::immune(a, "burn")) continue;
					a.burn_cd = 0.8;
					a.health = maxf(0.0, a.health - 3.0);
					emit("burn", D({{"actor", a.id}, {"body", b.id}}));
				}
			}
		}
	}
	_clash_pass();
	_zone_pass();
}

void CombatWorld::_clash_pass() {
	std::vector<MatBody*> movers;
	for (const BodyRef& bp : bodies) {
		MatBody& b = *bp;
		if (b.alive && b.attack_id != 0 && b.controller < 0 && (b.form == Form::Wave || (b.is_projectile() && b.vel.length() > 2.0f)))
			movers.push_back(&b);
	}
	if (movers.size() < 2) return;
	for (size_t i = 0; i < movers.size(); ++i) {
		MatBody& a = *movers[i];
		for (size_t j = i + 1; j < movers.size(); ++j) {
			MatBody& b = *movers[j];
			if (!a.alive || !b.alive || a.attack_id == 0 || b.attack_id == 0) continue;
			if (a.attack_owner == b.attack_owner || (a.form == Form::Wave) != (b.form == Form::Wave)) continue;
			const double reach = a.form == Form::Wave ? (a.wave_width + b.wave_width) * 0.5 : a.radius + b.radius;
			const double d = a.form != Form::Wave ? static_cast<double>(a.pos.distance_to(b.pos)) : static_cast<double>(Vec2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length());
			if (d > reach) continue;
			AgentRef ta = Agent::of_body(*this, a);
			AgentRef tb = Agent::of_body(*this, b);
			tb->power = tb->total();
			IxCtx ctx;
			ctx.site = "clash";
			Interactions::resolve(*this, *ta, *tb, ctx, &Interactions::CLASH_RULE());
		}
	}
}

double CombatWorld::hit_scale(const MatBody& b) const {
	double sc = clampf(dnum(b.props, "dmg_scale", 1.0), 0.0, 1.0);
	if (b.form == Form::Wave && b.mat == Mat::Stone && b.tag.empty()) {
		const double l0 = dnum(b.props, "liquid0", maxf(b.liquid, 0.01));
		sc *= clampf(b.liquid / l0 / 0.6, 0.3, 1.0);
	}
	return sc;
}

bool CombatWorld::_touches_actor(const MatBody& b, const ActorState& a, double pad) const {
	const double lo = a.pos.y + 0.2;
	const double hi = a.pos.y + Sim::ACTOR_HEIGHT;
	const double cy = clampf(b.pos.y, lo, hi);
	const double d = V3(b.pos.x - a.pos.x, b.pos.y - cy, b.pos.z - a.pos.z).length();
	return d < b.radius + Sim::ACTOR_RADIUS + pad;
}

void CombatWorld::_projectile_hits_actor(MatBody& b, ActorState& a) {
	b.hit_set.add(a.id);
	if (a.iframes > 0.0) {
		emit("evaded", D({{"actor", a.id}, {"body", b.id}}));
		return;
	}
	Vec3 to_src = -b.vel;
	to_src.y = 0.0f;
	const bool facing_vel = to_src.length() > 0.01f ? a.forward().dot(to_src.normalized()) > -0.15f : true;
	const double hs = hit_scale(b);
	const std::string kind = b.is_stone() ? "stone" : (b.is_water() ? "water" : FxEvents::mat_of(b));
	AgentRef ag = Agent::of_body(*this, b, &a);
	const std::string res = hit_actor(a,
	                                  D({{"attacker", b.attack_owner},
	                                     {"attack_id", b.attack_id},
	                                     {"damage", b.damage * hs},
	                                     {"facing_vel", facing_vel},
	                                     {"balance", b.balance_damage * hs},
	                                     {"knock", b.vel.normalized() * minf(b.mass * b.vel.length() / 70.0, 9.0)},
	                                     {"kind", kind},
	                                     {"from", b.pos - b.vel.normalized()},
	                                     {"body", b.id},
	                                     {"src_attack", dint(b.props, "src_attack", -1)}}),
	                                  ag);
	if (res == "perfect" || res == "deflect" || res == "redirected") return;
	if (b.is_water() && b.phase == Phase::Liquid) a.wetness = 1.0;
	if (b.is_stone() && b.temp >= Sim::HOT_ROCK_C && res != "block") {
		a.health = maxf(0.0, a.health - 4.0);
		emit("burn", D({{"actor", a.id}, {"body", b.id}}));
	}
	if (b.props.has("hit_status") && (res == "hit" || res == "knockdown"))
		Status::apply(*this, a, dstr(b.props, "hit_status"), dnum(b.props, "hit_status_t", 1.0), 1.0, b.attack_owner);
	if ((res == "hit" || res == "knockdown") && dint(b.props, "pierce", 0) > 0) {
		b.props.set("pierce", dint(b.props, "pierce") - 1);
		emit("pierce", D({{"body", b.id}, {"actor", a.id}}));
		return;
	}
	if (res == "block" || res == "hit" || res == "knockdown" || res == "guard_break") {
		if (b.props.has("on_impact")) {
			Verbs::on_impact(*this, b, "actor");
			if (!b.alive) return;
		}
		b.vel = -b.vel * 0.15f + Vec3(0, 1.5f, 0);
		b.attack_id = 0;
		if (b.form == Form::Shard) _shatter(b);
		else if (b.is_water() && b.phase == Phase::Liquid) _water_to_puddle(b);
	}
}

}  // namespace ff
