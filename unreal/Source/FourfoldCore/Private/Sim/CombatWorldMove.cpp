// Fourfold core - port of game/core/combat_world.gd: actor movement, surfaces, targeting, aim and resources.
#include "Sim/CombatWorld.h"

#include "Combat/Moves.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {

void CombatWorld::_move_actor(ActorState& a, const ActorIntent& it) {
	const double dt = Sim::DT;
	ActionRef inst = a.action;
	Vec3 want;
	double speed_scale = 1.0;
	double turn_scale = 1.0;
	bool controlled_motion = false;
	if (a.stun > 0.0) {
		speed_scale = 0.0;
		turn_scale = 0.0;
	} else if (inst != nullptr) {
		switch (inst->phase) {
			case ActionPhase::Startup:
				speed_scale = dnum(inst->def, "move_startup", 0.25);
				turn_scale = 0.6;
				break;
			case ActionPhase::Charge:
			case ActionPhase::Channel:
				speed_scale = vnum(inst->data.get("move_scale", inst->def.get("move_channel", Value(0.45))));
				turn_scale = 0.8;
				break;
			case ActionPhase::Active:
				speed_scale = inst->data.has("move_scale") ? dnum(inst->data, "move_scale", 0.0) : 0.0;
				turn_scale = speed_scale <= 0.0 ? 0.0 : 0.8;
				break;
			case ActionPhase::Recovery:
				speed_scale = 0.25;
				turn_scale = 0.3;
				break;
			default: break;
		}
		if (inst->id == "guard") {
			speed_scale = dnum(ddict(inst->data, "spec_def"), "move_channel", 0.35);
			turn_scale = 1.0;
		}
		if (dtruthy(inst->data, "controls_motion")) controlled_motion = true;
	}
	if (a.in_water) speed_scale *= 0.7;
	if (!a.status.empty()) {
		speed_scale *= Status::speed_mult(a);
		if (Status::rooted(a)) speed_scale = 0.0;
	}
	Vec3 mv = it.move;
	mv.y = 0.0f;
	if (mv.length() > 1.0f) mv = mv.normalized();
	const double m = mv.length();
	double gait_speed = 0.0;
	if (m > 0.05)
		gait_speed = m < RUN_STICK ? lerpf(0.0, WALK_MAX, m / RUN_STICK) : lerpf(RUN_MIN, RUN_SPEED, (m - RUN_STICK) / (1.0 - RUN_STICK));
	const bool running = m >= RUN_STICK && inst == nullptr && a.stun <= 0.0;
	want = (mv / maxf(m, 1e-4)) * gait_speed * speed_scale;
	if (a.gliding) {
		const double gs = inst != nullptr ? vnum(inst->data.get("glide_speed", Moves::defs().get("air_tech").get("glide_speed")))
		                                  : dnum(Moves::defs().get("air_tech"), "glide_speed");
		want = mv * gs;
	}
	const bool hover = inst != nullptr && inst->data.has("hover_height") && a.stun <= 0.0;
	Vec3 hv(a.vel.x, 0.0f, a.vel.z);
	if (!controlled_motion) {
		double air_ctl = a.grounded ? 1.0 : 0.35;
		if (a.gliding || hover) air_ctl = 0.8;
		double rate = (want.length() > hv.length() ? ACCEL : DECEL) * air_ctl;
		if (a.stun > 0.0) rate = 9.0;
		const double fr = _friction_at(a);
		if (fr != 1.0) rate *= fr;
		hv = move_toward(hv, want, rate * dt);
		a.vel.x = hv.x;
		a.vel.z = hv.z;
	}
	// Facing: actions face their aim; running faces the run direction; otherwise face the locked target and strafe.
	Vec3 face_dir;
	ActorState* tgt = get_actor(a.lock_target);
	if (running) face_dir = mv;
	else if (tgt != nullptr && a.pos.distance_to(tgt->pos) < 22.0f) face_dir = tgt->pos - a.pos;
	else if (mv.length() > 0.1f) face_dir = mv;
	if (inst != nullptr && inst->data.has("face")) {
		face_dir = dvec(inst->data, "face");
		if (inst->phase == ActionPhase::Startup) turn_scale = maxf(turn_scale, 1.4);
	}
	face_dir.y = 0.0f;
	if (face_dir.length() > 0.01f && turn_scale > 0.0) {
		const double target_yaw = std::atan2(static_cast<double>(face_dir.x), static_cast<double>(face_dir.z));
		const double diff = wrapf(target_yaw - a.facing, -kPi, kPi);
		const double max_turn = TURN_RATE * turn_scale * dt;
		a.facing = wrapf(a.facing + clampf(diff, -max_turn, max_turn), -kPi, kPi);
	}
	// Vertical
	if (hover) {
		const double hg = _ground_under(a.pos, a.pos.y + 3.0);
		const double target_y = hg + dnum(inst->data, "hover_height");
		a.grounded = false;
		a.vel.y = f32(clampf((target_y - a.pos.y) * 6.0, -4.0, 6.0));
	} else if (!a.grounded) {
		a.vel.y = f32(a.vel.y - Sim::GRAVITY * dt);
		if (a.gliding) {
			const double gf = inst != nullptr ? vnum(inst->data.get("glide_fall", Moves::defs().get("air_tech").get("glide_fall")))
			                                  : dnum(Moves::defs().get("air_tech"), "glide_fall");
			a.vel.y = f32(maxf(a.vel.y, -gf));
		}
	}
	Vec3 np = a.pos + a.vel * dt;
	np = arena.push_out(np, Sim::ACTOR_RADIUS);
	np = _push_out_walls(np, Sim::ACTOR_RADIUS);
	const double g = _ground_under(np, a.pos.y);
	if (np.y <= g + 0.001 && a.vel.y <= 0.0f) {
		if (!a.grounded) {
			a.grounded = true;
			a.gliding = false;
			emit("land", D({{"actor", a.id}, {"speed", -a.vel.y}}));
		}
		np.y = f32(g);
		a.vel.y = 0.0f;
	} else if (np.y > g + 0.05) {
		if (a.grounded && a.vel.y <= 0.0f && np.y - g < Sim::STEP_HEIGHT + 0.05 && g >= a.ground_y - Sim::STEP_HEIGHT) np.y = f32(g);
		else a.grounded = false;
	}
	a.ground_y = g;
	const double lim = arena.half_size - 0.5;
	np.x = f32(clampf(np.x, -lim, lim));
	np.z = f32(clampf(np.z, -lim, lim));
	a.pos = np;
	_update_surface(a);
}

double CombatWorld::_ground_under(Vec3 p, double from_y) const {
	double g = arena.ground_height(p.x, p.z, from_y);
	for (const BodyRef& zp : bodies) {
		const MatBody& z = *zp;
		if (z.alive && z.form == Form::Zone && z.props.has("walk_height")) {
			if (Vec2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius) {
				const double top = z.pos.y + dnum(z.props, "walk_height");
				if (top > g && top <= from_y + Sim::STEP_HEIGHT) g = top;
			}
		}
	}
	return g;
}

MatBody* CombatWorld::zone_surface_at(Vec3 p) const {
	for (const BodyRef& zp : bodies) {
		MatBody& z = *zp;
		if (z.alive && z.form == Form::Zone && (z.props.has("walk_height") || z.props.has("friction"))) {
			if (Vec2(p.x - z.pos.x, p.z - z.pos.z).length() <= z.zone_radius &&
			    std::fabs(p.y - (z.pos.y + dnum(z.props, "walk_height", 0.0))) < 0.35)
				return &z;
		}
	}
	return nullptr;
}

double CombatWorld::_friction_at(const ActorState& a) const {
	double f = 1.0;
	if (!a.status.empty()) f *= Status::friction_mult(a);
	if (a.grounded && begins_with(a.surface, "zone:")) {
		MatBody* z = zone_surface_at(a.pos);
		if (z != nullptr) f *= dnum(z->props, "friction", 1.0);
	}
	return f;
}

void CombatWorld::_update_surface(ActorState& a) {
	MatBody* zs = nullptr;
	if (a.grounded) zs = zone_surface_at(a.pos);
	a.in_water = arena.in_pool(a.pos.x, a.pos.z) && a.pos.y < arena.pool_level && a.grounded && zs == nullptr;
	if (a.in_water) {
		a.wetness = 1.0;
		const double take = minf(6.0 - a.water_carried, pool->mass);
		if (take > 0.0) {
			ledger.removed += take * (Sim::WATER_C * (pool->temp - Sim::AMBIENT_C) - Sim::WATER_LATENT_FUSION * (1.0 - pool->liquid));
			a.water_carried += take;
			pool->mass -= take;
		}
	}
	a.surface = arena.surface_at(a.pos.x, a.pos.z, a.pos.y);
	if (zs != nullptr) {
		a.surface = "zone:" + dstr(zs->props, "surface", zs->tag);
		return;
	}
	if (a.surface == "stone" && a.grounded) {
		MatBody* pd = puddle_at(a.pos);
		if (pd != nullptr) {
			a.surface = "puddle";
			a.wetness = maxf(a.wetness, 0.5);
		}
	}
}

void CombatWorld::_separate_actors() {
	for (size_t i = 0; i < actors.size(); ++i) {
		for (size_t j = i + 1; j < actors.size(); ++j) {
			ActorState& a = *actors[i];
			ActorState& b = *actors[j];
			const Vec3 d(b.pos.x - a.pos.x, 0.0f, b.pos.z - a.pos.z);
			const double l = d.length();
			const double minl = Sim::ACTOR_RADIUS * 2.0;
			if (l < minl && std::fabs(a.pos.y - b.pos.y) < 1.5f) {
				const Vec3 n = l > 1e-4 ? d / l : Vec3(1.0f, 0.0f, 0.0f);
				const double push = (minl - l) * 0.5;
				a.pos -= n * push;
				b.pos += n * push;
			}
		}
	}
}

Vec3 CombatWorld::_push_out_walls(Vec3 p, double r) const {
	for (const BodyRef& bp : bodies) {
		const MatBody& b = *bp;
		if (b.alive && b.form == Form::Wall && b.wall_rise > 0.3) p = _push_out_obb(p, r, b);
	}
	return p;
}

Vec3 CombatWorld::_push_out_obb(Vec3 p, double r, const MatBody& w) const {
	const double c = std::cos(w.wall_yaw);
	const double s = std::sin(w.wall_yaw);
	const Vec3 rel = p - w.pos;
	double lx = rel.x * c - rel.z * s;
	double lz = rel.x * s + rel.z * c;
	const double hx = w.wall_half.x;
	const double hz = w.wall_half.z;
	const double cx = clampf(lx, -hx, hx);
	const double cz = clampf(lz, -hz, hz);
	const double dx = lx - cx;
	const double dz = lz - cz;
	const double d2 = dx * dx + dz * dz;
	if (d2 >= r * r) return p;
	if (d2 > 1e-8) {
		const double d = std::sqrt(d2);
		lx = cx + dx / d * r;
		lz = cz + dz / d * r;
	} else {
		lz = (hz + r) * (lz >= 0.0 ? 1.0 : -1.0);
	}
	return w.pos + V3(lx * c + lz * s, rel.y, -lx * s + lz * c);
}

bool CombatWorld::point_in_wall(Vec3 p, const MatBody& w, double pad) const {
	const double c = std::cos(w.wall_yaw);
	const double s = std::sin(w.wall_yaw);
	const Vec3 rel = p - w.pos;
	const double lx = rel.x * c - rel.z * s;
	const double lz = rel.x * s + rel.z * c;
	return std::fabs(lx) <= w.wall_half.x + pad && std::fabs(lz) <= w.wall_half.z + pad && rel.y >= -0.2f &&
	       rel.y <= w.wall_half.y * 2.0 * w.wall_rise + pad;
}

// ============================================================== targeting

bool CombatWorld::_valid_target(const ActorState& a, int id) const {
	const ActorState* t = get_actor(id);
	if (t == nullptr || t->team == a.team || t->health <= 0.0) return false;
	if (!t->status.empty() || !a.status.empty()) return _lockable(a, *t);
	return true;
}

bool CombatWorld::_lockable(const ActorState& a, const ActorState& t) const {
	if (Status::lock_blocked(a)) return false;
	if (Status::hidden(t) && a.pos.distance_to(t.pos) > Status::HIDDEN_RANGE) return false;
	return true;
}

int CombatWorld::_auto_target(const ActorState& a) const {
	int best = -1;
	double bd = kInf;
	for (const auto& tp : actors) {
		const ActorState& t = *tp;
		if (t.team == a.team || t.health <= 0.0) continue;
		if ((!t.status.empty() || !a.status.empty()) && !_lockable(a, t)) continue;
		const double d = a.pos.distance_to(t.pos);
		if (d < bd) {
			bd = d;
			best = t.id;
		}
	}
	return best;
}

void CombatWorld::_cycle_target(ActorState& a) {
	std::vector<int> ids;
	for (const auto& t : actors)
		if (t->team != a.team && t->health > 0.0) ids.push_back(t->id);
	if (ids.empty()) return;
	int i = -1;
	for (size_t k = 0; k < ids.size(); ++k)
		if (ids[k] == a.lock_target) i = static_cast<int>(k);
	a.lock_target = ids[static_cast<size_t>((i + 1) % static_cast<int>(ids.size()))];
	emit("target", D({{"actor", a.id}, {"target", a.lock_target}}));
}

Vec3 CombatWorld::aim_dir(const ActorState& a, const ActorIntent& it) const {
	if (it.aim_active && it.aim_dir.length() > 0.1f) return Vec3(it.aim_dir.x, 0.0f, it.aim_dir.z).normalized();
	const ActorState* t = get_actor(a.lock_target);
	if (t != nullptr) {
		Vec3 d = t->pos - a.pos;
		d.y = 0.0f;
		if (d.length() > 0.1f) return d.normalized();
	}
	return a.forward();
}

Vec3 CombatWorld::aim_point(const ActorState& a, const ActorIntent& it) const {
	const ActorState* t = get_actor(a.lock_target);
	if (t != nullptr && !it.aim_active) return t->chest();
	return a.chest() + aim_dir(a, it) * 12.0f;
}

MatBody* CombatWorld::find_body(const ActorState& a, Vec3 dir, double reach, double cone_deg, const BodyFilter& filter) const {
	MatBody* best = nullptr;
	double best_score = kInf;
	const double cos_lim = std::cos(deg_to_rad(cone_deg));
	for (size_t i = 0; i < bodies.size(); ++i) {
		MatBody& b = *bodies[i];
		if (!b.alive || !filter(b)) continue;
		const Vec3 to = b.pos - a.chest();
		const Vec3 fl(to.x, 0.0f, to.z);
		const double d = fl.length();
		if (d > reach) continue;
		if (d > 0.6 && fl.normalized().dot(dir) < cos_lim) continue;
		double score = d;
		if (b.is_projectile() && b.attack_owner != a.id && b.vel.dot(-to) > 0.0f) score -= 6.0;
		if (score < best_score - 1e-6 || (std::fabs(score - best_score) <= 1e-6 && best != nullptr && b.id < best->id)) {
			best = &b;
			best_score = score;
		}
	}
	return best;
}

MatBody* CombatWorld::puddle_at(Vec3 p) const {
	for (const BodyRef& bp : bodies) {
		MatBody& b = *bp;
		if (b.alive && b.form == Form::Puddle)
			if (Vec2(p.x - b.pos.x, p.z - b.pos.z).length() < b.radius && std::fabs(p.y - b.pos.y) < 0.3f) return &b;
	}
	return nullptr;
}

// ============================================================== resources

double CombatWorld::pay_heat(ActorState& a, double hu, bool allow_partial) {
	const double from_res = minf(a.heat_reserve, hu);
	double rest = hu - from_res;
	double focus_need = rest / Sim::HU_PER_FOCUS;
	if (focus_need > a.focus + 1e-6) {
		if (!allow_partial) return 0.0;
		focus_need = a.focus;
		rest = focus_need * Sim::HU_PER_FOCUS;
	}
	a.heat_reserve -= from_res;
	spend_focus(a, focus_need);
	ledger.generated += rest;
	return from_res + rest;
}

bool CombatWorld::can_pay_heat(const ActorState& a, double hu) const { return a.heat_reserve + a.focus * Sim::HU_PER_FOCUS >= hu - 1e-6; }

bool CombatWorld::spend_focus(ActorState& a, double f) {
	if (f <= 0.0) return true;
	if (a.focus + 1e-6 < f) return false;
	a.focus = maxf(0.0, a.focus - f);
	a.focus_idle = 0.0;
	return true;
}

void CombatWorld::_regen(ActorState& a) {
	a.focus_idle += Sim::DT;
	a.balance_idle += Sim::DT;
	if (a.focus_idle > Sim::FOCUS_REGEN_DELAY && a.action == nullptr) a.focus = minf(Sim::FOCUS_MAX, a.focus + Sim::FOCUS_REGEN * Sim::DT);
	else if (a.focus_idle > Sim::FOCUS_REGEN_DELAY) a.focus = minf(Sim::FOCUS_MAX, a.focus + Sim::FOCUS_REGEN * 0.4 * Sim::DT);
	if (a.balance_idle > Sim::BALANCE_REGEN_DELAY) a.balance = minf(Sim::BALANCE_MAX, a.balance + Sim::BALANCE_REGEN * Sim::DT);
	if (a.heat_reserve > 0.0) {
		const double d = minf(a.heat_reserve, Sim::RESERVE_DISSIPATE * Sim::DT);
		a.heat_reserve -= d;
		ledger.reserve_dissipated += d;
	}
}

}  // namespace ff
