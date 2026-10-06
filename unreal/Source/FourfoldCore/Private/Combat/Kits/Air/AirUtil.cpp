// Fourfold core - port of game/combat/kits/air/air_util.gd.
#include "Combat/Kits/Air/AirUtil.h"

#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace AirUtil {

Vec3 aim_flat(const ActorState& a, const ActionInst& inst) {
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	dir.y = 0.0f;
	return dir.length() > 0.01f ? dir.normalized() : a.forward();
}

Vec3 ground_at(CombatWorld& w, Vec3 p) { return V3(p.x, w.arena.ground_height(p.x, p.z, p.y + 0.5), p.z); }

Vec3 aim_ground(CombatWorld& w, const ActorState& a, const ActionInst& inst, double rmax) {
	const Vec3 dir = aim_flat(a, inst);
	Vec3 ap = vvec(inst.data.get("aim_point", a.chest() + dir * rmax), a.chest() + dir * rmax);
	ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr && !dbool(inst.data, "aim_active", false)) ap = t->pos;
	Vec3 fl(ap.x - a.pos.x, 0.0f, ap.z - a.pos.z);
	if (fl.length() > rmax) fl = fl.normalized() * rmax;
	return ground_at(w, a.pos + fl);
}

bool in_cone(Vec3 origin, Vec3 dir, Vec3 p, double rng_m, double half_deg, double pad) {
	const Vec3 to = p - origin;
	const Vec3 fl(to.x, 0.0f, to.z);
	if (fl.length() > rng_m + pad) return false;
	if (fl.length() < 0.5f) return true;
	return fl.normalized().dot(dir) >= std::cos(deg_to_rad(half_deg));
}

MatBody* zone(CombatWorld& w, const std::string& tag, Vec3 pos, double radius, int owner_id, double power, double life, const Dict& props,
              int tier, int sub) {
	MatBody* z = w.spawn_zone(tag, ground_at(w, pos), radius, owner_id, power, Mat::Air, 0.0, life, "air:" + itos(owner_id));
	z->tier = tier;
	z->sub = sub;
	for (const auto& kv : props) z->props.set(kv.first, kv.second);
	return z;
}

std::vector<MatBody*> zones_of(CombatWorld& w, int owner_id, const std::string& tag) {
	std::vector<MatBody*> out;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.form == Form::Zone && b.owner == owner_id && b.tag == tag) out.push_back(&b);
	}
	return out;
}

AgentRef zone_counter(CombatWorld& w, MatBody& z, bool perfect) {
	AgentRef c = Agent::of_body(w, z);
	c->actor = w.get_actor(z.owner);
	c->perfect = perfect;
	return c;
}

void mark_pair(CombatWorld& w, const MatBody& z, const MatBody& b) { w._zone_pairs[itos(z.id) + "|" + itos(b.id)] = w.tick; }

bool pair_recent(CombatWorld& w, const MatBody& z, const MatBody& b, int ticks) {
	auto it = w._zone_pairs.find(itos(z.id) + "|" + itos(b.id));
	const int64_t t = it == w._zone_pairs.end() ? -100000 : it->second;
	return w.tick - t < ticks;
}

int source_owner(const MatBody& b) {
	if (b.attack_id != 0 && b.attack_owner >= 0) return b.attack_owner;
	if (b.owner >= 0) return b.owner;
	if (b.residual_owner >= 0) return b.residual_owner;
	return b.last_actor;
}

bool carriable(const MatBody& b, double max_mass) {
	if (!b.alive || b.static_body || b.controller >= 0 || b.captured_by >= 0) return false;
	if (b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Zone) return false;
	return b.mass <= max_mass;
}

std::vector<MatBody*> bodies_near(CombatWorld& w, Vec3 p, double r, const std::function<bool(MatBody&)>& filter) {
	std::vector<MatBody*> out;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.pos.distance_to(p) > r + b.radius) continue;
		if (filter && !filter(b)) continue;
		out.push_back(&b);
	}
	return out;
}

bool hostile_shot(const MatBody& b, const ActorState* to) {
	return b.alive && b.is_projectile() && b.controller < 0 && (to == nullptr || b.attack_owner != to->id);
}

std::vector<ActorState*> foes_near(CombatWorld& w, const ActorState& a, Vec3 p, double r) {
	std::vector<ActorState*> out;
	for (auto& tp : w.actors) {
		ActorState* t = tp.get();
		if (t == &a || t->team == a.team || t->health <= 0.0) continue;
		if (t->chest().distance_to(p) <= r + Sim::ACTOR_RADIUS) out.push_back(t);
	}
	std::stable_sort(out.begin(), out.end(), [p](const ActorState* x, const ActorState* y) {
		const double dx = x->chest().distance_to(p);
		const double dy = y->chest().distance_to(p);
		return dx < dy || (is_equal_approx(dx, dy) && x->id < y->id);
	});
	return out;
}

bool shove(ActorState& t, Vec3 v, const std::string& what) {
	if (Status::immune(t, what)) return false;
	t.vel += v;
	if (v.y > 0.0f) t.grounded = false;
	return true;
}

bool airborne(const ActorState& t) { return t.flying || Status::has(t, "flight") || (!t.grounded && t.pos.y > t.ground_y + 0.6); }

void guard_zone_effect(CombatWorld& w, MatBody& z, const std::function<bool(MatBody&)>& skip) {
	ActorState* owner = w.get_actor(z.owner);
	if (owner == nullptr) return;
	const bool perfect = w.perfect_guard(*owner);
	AgentRef c;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (&b == &z || !b.alive || b.static_body || b.controller >= 0 || b.captured_by >= 0) continue;
		if (b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Puddle || (b.form == Form::Zone && b.owner == z.owner)) continue;
		if (skip && skip(b)) continue;
		if (!w._in_zone(z, b.pos, b.radius) || pair_recent(w, z, b, static_cast<int>(maxf(1.0, 0.1 * Sim::HZ)))) continue;
		mark_pair(w, z, b);
		if (!c) c = zone_counter(w, z, perfect);
		else c->perfect = perfect;
		AgentRef th = Agent::of_body(w, b, owner);
		IxCtx ctx;
		ctx.continuous = true;
		ctx.site = "zone";
		Interactions::resolve(w, *th, *c, ctx, &Interactions::PASS_RULE());
		if (!z.alive) return;
	}
}

Dict arena_hit(CombatWorld& w, Vec3 p0, Vec3 p1) {
	Dict best;
	const Vec3 d = p1 - p0;
	for (size_t i = 0; i < w.arena.solids.size(); ++i) {
		const ArenaSolid& s = w.arena.solids[i];
		Dict r = _slab_n(p0, d, s.min, s.max);
		if (dnum(r, "t") >= 0.0 && (best.empty() || dnum(r, "t") < dnum(best, "t"))) {
			best = r;
			best.set("solid", static_cast<int>(i));
		}
	}
	return best;
}

Dict _slab_n(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx) {
	double tmin = 0.0, tmax = 1.0;
	int axis = -1;
	double sgn = 0.0;
	for (int i = 0; i < 3; ++i) {
		const double oi = o[i];
		const double di = d[i];
		if (absf(di) < 1e-9) {
			if (oi < mn[i] || oi > mx[i]) return D({{"t", -1.0}});
		} else {
			const double ta = (mn[i] - oi) / di;
			const double tb = (mx[i] - oi) / di;
			const double enter = di > 0.0 ? ta : tb;
			const double leave = di > 0.0 ? tb : ta;
			if (enter > tmin) {
				tmin = enter;
				axis = i;
				sgn = di > 0.0 ? -1.0 : 1.0;
			}
			tmax = minf(tmax, leave);
			if (tmin > tmax) return D({{"t", -1.0}});
		}
	}
	if (axis < 0) return D({{"t", -1.0}});
	Vec3 n;
	n[axis] = f32(sgn);
	return D({{"t", tmin}, {"n", n}});
}

Vec3 wall_normal(const MatBody& wall, Vec3 p) {
	const Vec3 n = V3(std::sin(wall.wall_yaw), 0.0, std::cos(wall.wall_yaw));
	return n.dot(p - wall.pos) >= 0.0f ? n : -n;
}

Vec3 reflect_dir(Vec3 d, Vec3 n) { return (d - 2.0f * d.dot(n) * n).normalized(); }

bool condense(CombatWorld& w, MatBody& b) {
	if (!(b.mat == Mat::Steam || (b.is_water() && (b.form == Form::Cloud || b.form == Form::Zone)))) return false;
	const double e1 = b.thermal_energy();
	b.mat = Mat::Water;
	if (b.form == Form::Zone || b.form == Form::Cloud) b.form = Form::Blob;
	b.tag = "";
	b.phase = Phase::Liquid;
	b.liquid = 1.0;
	b.temp = Sim::AMBIENT_C;
	b.heat_payload = 0.0;
	w.ledger.removed += e1 - b.thermal_energy();
	b.max_life = -1.0;
	b.vel = Vec3(b.vel.x * 0.3f, -1.0f, b.vel.z * 0.3f);
	b.attack_id = 0;
	b.update_radius();
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "steam"}, {"to", "water"}, {"why", "collapsed"}}));
	return true;
}

MatBody* spawn_inrush(CombatWorld& w, Vec3 pos, double radius, double power, int owner_id) {
	MatBody* z = zone(w, "inrush", pos, radius, owner_id, power, INRUSH_LIFE, D({{"rate", NO_PAIR}, {"height", 3.0}}), 0, 2);
	w.emit("inrush", D({{"pos", pos}, {"radius", radius}, {"power", power}, {"owner", owner_id}, {"tick", w.tick}}));
	return z;
}

int64_t inrush_tick_at(CombatWorld& w, Vec3 p) {
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (b.alive && b.form == Form::Zone && b.tag == "inrush" && w._in_zone(b, p, 0.2)) return b.born_tick;
	}
	return -1000000;
}

}  // namespace AirUtil
}  // namespace ff
