// Fourfold core - port of game/core/combat_world.gd: bodies (thermal, phases, ballistics, walls, waves, water, ledgers).
#include "Sim/CombatWorld.h"

#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Agent.h"
#include "Sim/FxEvents.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Materials.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace {
const char* const kWallThrough[] = {"weaken", "slow", "pass", "overwhelm", "bend"};
}  // namespace

void CombatWorld::_update_bodies() {
	const double dt = Sim::DT;
	for (size_t bi = 0; bi < bodies.size(); ++bi) {
		BodyRef keep = bodies[bi];
		MatBody& b = *keep;
		if (!b.alive) continue;
		b.age += dt;
		if (b.residual_authority > 0.0) {
			b.residual_authority = maxf(0.0, b.residual_authority - RESIDUAL_DECAY * dt);
			if (b.residual_authority <= 0.0) b.residual_owner = -1;
		}
		ActorState* held_by = b.controller >= 0 ? get_actor(b.controller) : nullptr;
		// --- thermal
		const bool insulated = held_by != nullptr && b.is_stone() && b.liquid > 0.0 && (held_by->has("magma") || held_by->has("heat_draw"));
		if (insulated) {
			if (!spend_focus(*held_by, Sim::HOLD_UPKEEP_FOCUS * dt)) {
				emit("control_lost", D({{"actor", held_by->id}, {"body", b.id}, {"reason", "focus"}}));
				release_body(*held_by, Vec3(), false);
				held_by = nullptr;
			}
		} else if (!b.static_body && b.form != Form::Cloud) {
			ledger.ambient += Thermal::ambient_step(b, dt);
		}
		const Phase old_phase = b.phase;
		if (Thermal::update_phase(b)) {
			emit("phase", D({{"body", b.id}, {"from", Sim::phase_name(old_phase)}, {"to", Sim::phase_name(b.phase)}}));
			_on_phase_changed(b, old_phase);
		}
		if (!b.alive) continue;
		if (static_cast<int>(b.mat) >= static_cast<int>(Mat::Metal)) {
			_material_tick(b, dt);
			if (!b.alive) continue;
		}
		// --- captured inside another body (vortex, wave carry): rides along
		if (b.captured_by >= 0) {
			MatBody* cap = get_body(b.captured_by);
			if (cap != nullptr && cap->alive) {
				Vec3 off = dvec(b.props, "capture_off");
				if (cap->spin != 0.0) {
					off = rotated(off, Vec3::Up(), cap->spin * dt);
					b.props.set("capture_off", off);
				}
				b.pos = cap->pos + off;
				b.vel = cap->vel;
				continue;
			}
			b.captured_by = -1;
		}
		// --- motion
		if (held_by != nullptr) {
			const Vec3 to = b.hold_point - b.pos;
			Vec3 v = to * 11.0f;
			if (v.length() > 22.0f) v = v.normalized() * 22.0f;
			b.vel = v;
			b.pos += v * dt;
			if (b.form == Form::Wave) b.form = Form::Chunk;
			if (held_by->chest().distance_to(b.pos) > 11.0f) {
				emit("control_lost", D({{"actor", held_by->id}, {"body", b.id}, {"reason", "range"}}));
				release_body(*held_by, b.vel, false);
			}
			continue;
		}
		// Kit body behaviours by tag (custom motion: return true to skip the default motion).
		if (!b.tag.empty()) {
			auto& ticks = Hooks::body_ticks();
			auto it = ticks.find(b.tag);
			if (it != ticks.end()) {
				const BodyTickFn cb = it->second;
				if (cb && cb(*this, b, dt)) continue;
				if (!b.alive) continue;
			}
		}
		switch (b.form) {
			case Form::Wave: _update_wave(b, dt); break;
			case Form::Wall: _update_wall(b, dt); break;
			case Form::Pool:
			case Form::Puddle: break;
			case Form::Zone: _update_zone(b, dt); break;
			case Form::Cloud:
				b.pos += (b.vel + Vec3(0, 0.6f, 0)) * dt;
				b.vel *= 0.96f;
				if (b.age > b.max_life) {
					if (b.mat == Mat::Steam || b.mat == Mat::Water) mass_ledger.vapor += b.mass;
					else _return_mass(b);
					release_captured(b);
					remove_body(b, "dissipated");
				}
				break;
			default: _update_ballistic(b, dt); break;
		}
		if (b.max_life > 0.0 && b.age > b.max_life && b.alive && b.form != Form::Cloud && b.form != Form::Zone && b.attack_id == 0)
			decay_body(b, "lifetime");
	}
}

void CombatWorld::_material_tick(MatBody& b, double dt) {
	if (b.mat == Mat::Fire) {
		if (b.heat_payload < 0.5 && b.form != Form::Zone && b.attack_id == 0 && b.age > 0.1) decay_body(b, "burned_out");
	} else if (b.mat == Mat::Plant) {
		if (b.temp >= Materials::props(Mat::Plant).ignite) burn_plant(b, Materials::props(Mat::Plant).burn_rate * dt);
	}
}

void CombatWorld::_on_phase_changed(MatBody& b, Phase old_phase) {
	if (Materials::is_fusible(b.mat)) {
		const std::string solid_name = b.is_stone() ? "rock" : std::string(Sim::mat_name(b.mat));
		const std::string molten_name = b.is_stone() ? "molten" : "molten_" + std::string(Sim::mat_name(b.mat));
		if (b.phase == Phase::Solid && b.form == Form::Wave) {
			b.form = Form::Chunk;
			b.vel = Vec3();
			b.attack_id = 0;
			b.on_ground = true;
			b.max_life = Sim::REMNANT_LIFETIME;
			b.age = 0.0;
			release_captured(b);
			emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "wave"}, {"to", solid_name}, {"why", "cooled"}}));
		} else if (b.phase == Phase::Solid && b.form == Form::Blob) {
			b.form = Form::Chunk;
			b.max_life = Sim::REMNANT_LIFETIME;
			b.age = 0.0;
			emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", b.is_stone() ? std::string("lava") : molten_name}, {"to", solid_name},
			                     {"why", "cooled"}}));
		} else if (b.phase == Phase::Molten && old_phase != Phase::Molten && b.form == Form::Chunk) {
			b.form = Form::Blob;
			emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", b.is_stone() ? std::string("stone") : std::string(Sim::mat_name(b.mat))},
			                     {"to", molten_name}, {"why", "heated"}}));
		}
		if (b.mat == Mat::Sand && b.phase == Phase::Solid && old_phase != Phase::Solid) {
			convert_mat(b, Mat::Glass, "sand_to_glass");
			emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "sand"}, {"to", "glass"}, {"why", "cooled"}}));
		}
	} else if (b.is_water()) {
		if (b.phase == Phase::Liquid && old_phase == Phase::Frozen) {
			if (b.form == Form::Shard || b.form == Form::Chunk) {
				b.form = Form::Puddle;
				b.vel = Vec3();
				b.attack_id = 0;
				b.pos.y = f32(arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.2));
				b.update_radius_puddle();
				emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "ice"}, {"to", "water"}, {"why", "melted"}}));
				_merge_puddle(b);
			} else if (b.form == Form::Puddle) {
				emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "ice"}, {"to", "water"}, {"why", "melted"}}));
			}
		} else if (b.phase == Phase::Frozen) {
			emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "water"}, {"to", "ice"}, {"why", "frozen"}}));
		}
	}
}

void CombatWorld::_update_ballistic(MatBody& b, double dt) {
	if (b.static_body) return;
	if (b.on_ground && b.vel.length() < 0.05f) {
		b.rest_time += dt;
		_quench_contact(b, dt);
		return;
	}
	if (b.attack_id != 0 && b.props.has("homing")) _home(b, dt);
	b.vel.y = f32(b.vel.y - Sim::GRAVITY * b.gravity_scale * dt);
	if (b.vel.length() > b.max_speed) b.vel = b.vel.normalized() * b.max_speed;
	Vec3 np = b.pos + b.vel * dt;
	const double t = arena.segment_hit(b.pos, np, b.radius * 0.8);
	MatBody* hit_wall_body = nullptr;
	for (size_t i = 0; i < bodies.size(); ++i) {
		MatBody& w = *bodies[i];
		if (w.alive && w.form == Form::Wall && w.wall_rise > 0.3 && &w != &b && point_in_wall(np, w, b.radius * 0.7)) {
			if (b.attack_id != 0) {
				if (w.last_actor == b.attack_owner) continue;   // a fighter's own wall launches, never blocks, their shot
				hit_wall_body = &w;
				break;
			}
			np = _push_out_obb(np, b.radius, w);
		}
	}
	if (hit_wall_body != nullptr) {
		const IxResult wres = _body_hits_wall(b, *hit_wall_body);
		if (!b.alive || b.attack_id == 0) return;
		const bool stopped = wres.empty ? true : wres.stopped;
		const double pass_scale = wres.empty ? 0.0 : wres.pass_scale;
		const bool through = (!wres.empty && wres.counter_broken) || (!stopped && pass_scale > 0.0 && in_list(wres.outcome, kWallThrough));
		if (!through) return;
		np = b.pos + b.vel * dt;
	}
	if (t >= 0.0) {
		np = b.pos.lerp(np, f32(maxf(0.0, t - 0.02)));
		if (b.attack_id != 0 && dint(b.props, "ricochet", 0) > 0) {
			_ricochet(b, np);
		} else if (b.attack_id != 0) {
			// Arena solids are an environment counter (legacy: impact, the shot drops).
			AgentRef at = Agent::of_body(*this, b);
			b.pos = np;
			AgentRef env = Agent::of_env(this, "arena_wall", np);
			IxCtx ctx;
			ctx.site = "arena";
			Interactions::resolve(*this, *at, *env, ctx);
			if (!b.alive) return;
			np = b.pos;
		} else {
			_body_impact(b, "wall");
			b.vel = Vec3(-b.vel.x * 0.15f, f32(maxf(b.vel.y, 0.0) * 0.2), -b.vel.z * 0.15f);
		}
	}
	const double g = arena.ground_height(np.x, np.z, np.y + 0.3);
	const double bottom = b.radius * (b.form == Form::Blob ? 0.25 : 0.8);
	if (np.y - bottom <= g) {
		np.y = f32(g + bottom);
		if (b.vel.y < -2.0f && b.attack_id != 0) {
			_body_impact(b, "ground");
			if (!b.alive) return;
		}
		if (b.mat == Mat::Water && b.phase == Phase::Liquid) {
			b.pos = np;
			_water_to_puddle(b);
			return;
		}
		if (b.form == Form::Shard) {
			b.pos = np;
			_shatter(b);
			return;
		}
		b.vel.y = 0.0f;
		b.vel *= b.form != Form::Blob ? 0.82f : 0.3f;
		b.on_ground = true;
		if (b.vel.length() < 0.4f) {
			b.vel = Vec3();
			b.attack_id = 0;
		}
	} else {
		b.on_ground = false;
	}
	b.pos = np;
	_quench_contact(b, dt);
}

void CombatWorld::_home(MatBody& b, double dt) {
	ActorState* owner = get_actor(b.attack_owner);
	if (owner == nullptr) return;
	ActorState* tgt = get_actor(dint(b.props, "homing_target", owner->lock_target));
	if (tgt == nullptr) return;
	const Vec3 to = tgt->chest() - b.pos;
	const Vec3 fl(to.x, 0.0f, to.z);
	const Vec3 hv(b.vel.x, 0.0f, b.vel.z);
	if (fl.length() < 0.5f || hv.length() < 0.5f) return;
	double cur = std::atan2(static_cast<double>(hv.x), static_cast<double>(hv.z));
	const double want = std::atan2(static_cast<double>(fl.x), static_cast<double>(fl.z));
	const double diff = wrapf(want - cur, -kPi, kPi);
	const double mx = deg_to_rad(dnum(b.props, "homing")) * dt;
	cur += clampf(diff, -mx, mx);
	const double sp = hv.length();
	b.vel = V3(std::sin(cur) * sp, b.vel.y, std::cos(cur) * sp);
}

void CombatWorld::_ricochet(MatBody& b, Vec3 at) {
	b.props.set("ricochet", dint(b.props, "ricochet") - 1);
	Vec3 n;
	for (const ArenaSolid& s : arena.solids) {
		const float pad = f32(b.radius + 0.1);
		const Vec3 mn = s.min - Vec3(pad, pad, pad);
		const Vec3 mx = s.max + Vec3(pad, pad, pad);
		if (at.x >= mn.x && at.x <= mx.x && at.z >= mn.z && at.z <= mx.z && at.y <= mx.y) {
			const Vec3 c = (s.min + s.max) * 0.5f;
			const double hx = (s.max.x - s.min.x) * 0.5;
			const double hz = (s.max.z - s.min.z) * 0.5;
			const double dx = (at.x - c.x) / maxf(hx, 0.01);
			const double dz = (at.z - c.z) / maxf(hz, 0.01);
			n = std::fabs(dx) > std::fabs(dz) ? V3(signf(dx), 0, 0) : V3(0, 0, signf(dz));
			break;
		}
	}
	if (n == Vec3()) n = -Vec3(b.vel.x, 0.0f, b.vel.z).normalized();
	b.vel = b.vel - 2.0f * b.vel.dot(n) * n;
	b.vel *= 0.9f;
	b.pos = at + n * 0.05f;
	b.hit_set.clear();
	if (b.attack_owner >= 0) b.hit_set.add(b.attack_owner);
	emit("ricochet", D({{"body", b.id}, {"at", at}, {"dir", b.vel.normalized()}}));
}

void CombatWorld::_quench_contact(MatBody& b, double) {
	if (!Materials::is_fusible(b.mat) || b.liquid <= 0.0) return;
	if (arena.in_pool(b.pos.x, b.pos.z) && b.pos.y < arena.pool_level + 0.1) {
		AgentRef th = Agent::of_body(*this, b);
		AgentRef env = Agent::of_env(this, "pool", b.pos);
		IxCtx ctx;
		ctx.continuous = true;
		Interactions::resolve(*this, *th, *env, ctx);
		return;
	}
	MatBody* pd = puddle_at(b.pos);
	if (pd != nullptr) {
		AgentRef env = Agent::of_env(this, "puddle", b.pos);
		env->body = pd;
		AgentRef th = Agent::of_body(*this, b);
		IxCtx ctx;
		ctx.continuous = true;
		Interactions::resolve(*this, *th, *env, ctx);
	}
}

void CombatWorld::_body_impact(MatBody& b, const std::string& what) {
	emit("impact", D({{"body", b.id}, {"on", what}, {"speed", b.vel.length()}, {"mass", b.mass},
	                  {"power", b.mass * b.vel.length() / 20.0}, {"mat", FxEvents::mat_of(b)}, {"tier", b.tier}, {"dir", b.vel.normalized()}}));
	if (b.props.has("on_impact") && b.attack_id != 0) {
		Verbs::on_impact(*this, b, what);
		if (!b.alive) return;
	}
	if (b.form == Form::Shard) {
		_shatter(b);
		return;
	}
	b.attack_id = 0;
}

IxResult CombatWorld::_body_hits_wall(MatBody& b, MatBody& w) {
	IxResult none;
	none.empty = true;
	if (b.attack_id == 0) return none;
	ActorState* owner = get_actor(w.controller >= 0 ? w.controller : w.last_actor);
	AgentRef counter = Agent::of_body(*this, w);
	counter->actor = owner;
	counter->perfect = owner != nullptr && owner->guarding && owner->wall_body == w.id && perfect_guard(*owner);
	AgentRef th = Agent::of_body(*this, b, owner);
	IxCtx ctx;
	ctx.site = "wall";
	return Interactions::resolve(*this, *th, *counter, ctx);
}

void CombatWorld::_crumble_wall(MatBody& w) {
	if (!w.alive) return;
	emit("wall_crumble", D({{"body", w.id}}));
	ActorState* owner = get_actor(w.last_actor);
	if (owner != nullptr && owner->wall_body == w.id) owner->wall_body = -1;
	release_captured(w);
	// Rubble: two usable 20 kg stones split off, the rest sinks back into the ground.
	const double piece = minf(Sim::STONE_SHOT_MASS, w.mass * 0.25);
	for (int k = 0; k < 2; ++k) {
		if (w.mass <= piece * 0.5) break;
		const Vec3 off = V3(std::cos(w.wall_yaw), 0, -std::sin(w.wall_yaw)) * (k == 0 ? 0.5f : -0.5f);
		MatBody* c = split_body(w, piece, w.pos + off + Vec3(0, 0.4f, 0));
		c->form = Form::Chunk;
		c->vel = Vec3(0, 2.0f, 0);
		c->max_life = Sim::REMNANT_LIFETIME;
		c->update_radius();
	}
	_return_mass(w);
	ledger.removed += w.thermal_energy();
	w.mass = 0.0;
	remove_body(w, "crumbled");
}

void CombatWorld::_update_wall(MatBody& w, double dt) {
	ActorState* owner = get_actor(w.last_actor);
	bool keep = owner != nullptr && owner->wall_body == w.id && owner->guarding;
	if (!keep && owner != nullptr && owner->action != nullptr && dint(owner->action->data, "keep_wall", -1) == w.id) keep = true;
	if (!keep && dnum(w.props, "standing", 0.0) > 0.0) keep = w.age < dnum(w.props, "standing");
	if (keep) {
		w.wall_rise = minf(1.0, w.wall_rise + dt / dnum(w.props, "rise_time", 0.14));
		if (!w.props.has("standing")) w.age = 0.0;
	} else {
		w.wall_rise -= dt / 0.35;
		if (w.wall_rise <= 0.0) {
			_return_mass(w);
			ledger.removed += w.thermal_energy();
			if (owner != nullptr && owner->wall_body == w.id) owner->wall_body = -1;
			release_captured(w);
			remove_body(w, "sank");
		}
	}
}

void CombatWorld::_update_wave(MatBody& b, double dt) {
	if (b.mat == Mat::Stone && !b.props.has("liquid0")) b.props.set("liquid0", maxf(b.liquid, 0.01));
	double speed = 0.0;
	if (!b.tag.empty() && b.props.has("speed")) {
		speed = dnum(b.props, "speed") * (dtruthy(b.props, "viscous") ? Thermal::flow_factor(b) : 1.0);
	} else {
		speed = dnum(Moves::defs().get("pour").as_dict(), "wave_speed") * Thermal::flow_factor(b);
	}
	if (b.wave_budget <= 0.0 || speed < 0.35) {
		_settle_wave(b, b.wave_budget <= 0.0 ? "budget" : "viscous");
		return;
	}
	// The pouring fighter keeps bending the wave toward their target while it is fluid.
	ActorState* owner = get_actor(b.attack_owner);
	const double steer = !b.tag.empty() ? dnum(b.props, "steer", WAVE_TURN_RATE) : WAVE_TURN_RATE;
	const bool fluid = b.tag.empty() ? b.liquid > 0.4 : steer > 0.0;
	if (owner != nullptr && fluid) {
		ActorState* tgt = get_actor(owner->lock_target);
		if (tgt != nullptr) {
			Vec3 to = tgt->pos - b.pos;
			to.y = 0.0f;
			if (to.length() > 1.0f) {
				const double want = std::atan2(static_cast<double>(to.x), static_cast<double>(to.z));
				double cur = std::atan2(static_cast<double>(b.wave_dir.x), static_cast<double>(b.wave_dir.z));
				const double diff = wrapf(want - cur, -kPi, kPi);
				const double max_turn = deg_to_rad(steer) * dt * (b.tag.empty() ? Thermal::flow_factor(b) : 1.0);
				if (std::fabs(diff) < deg_to_rad(70.0)) {
					cur += clampf(diff, -max_turn, max_turn);
					b.wave_dir = V3(std::sin(cur), 0.0, std::cos(cur));
				}
			}
		}
	}
	// Godot (combat_world.gd `b.wave_dir * speed * dt`) multiplies left to right, rounding the Vector3 to float32 after
	// each product; folding speed * dt in double first drifted ice / fire wave segments by a tick.
	const Vec3 stepv = (b.wave_dir * static_cast<float>(speed)) * static_cast<float>(dt);
	Vec3 np = b.pos + stepv;
	const double g0 = b.pos.y;
	const double raw_top = arena.ground_height(np.x, np.z, g0, 100.0);
	bool blocked = raw_top > g0 + Sim::WAVE_STEP;
	for (size_t i = 0; i < bodies.size(); ++i) {
		MatBody& w = *bodies[i];
		if (w.alive && w.form == Form::Wall && w.wall_rise > 0.3 && &w != &b && point_in_wall(np + Vec3(0, 0.2f, 0), w, b.wave_width * 0.3)) {
			if (w.last_actor == b.attack_owner && !b.tag.empty() && dtruthy(b.props, "own_walls_pass")) continue;
			AgentRef counter = Agent::of_body(*this, w);
			counter->actor = get_actor(w.last_actor);
			AgentRef th = Agent::of_body(*this, b);
			IxCtx ctx;
			ctx.site = "wave_wall";
			const IxResult res = Interactions::resolve(*this, *th, *counter, ctx);
			if (!b.alive || b.form != Form::Wave) return;
			if (res.stopped) blocked = true;
			break;
		}
	}
	if (blocked) {
		emit("wave_blocked", D({{"body", b.id}, {"at", b.pos}}));
		_settle_wave(b, "blocked");
		return;
	}
	const double g1 = arena.ground_height(np.x, np.z, g0, Sim::WAVE_STEP);
	if (g1 < g0 - 0.1) {
		b.wave_budget -= 1.0;
		emit("wave_drop", D({{"body", b.id}, {"from", g0}, {"to", g1}}));
	}
	np.y = f32(g1);
	b.vel = stepv / dt;
	b.pos = np;
	b.wave_budget -= stepv.length();
	if (b.wave_path.empty() || b.wave_path.back().distance_to(np) > 0.35f) {
		b.wave_path.push_back(np);
		if (b.wave_path.size() > 28) b.wave_path.erase(b.wave_path.begin());
		if (b.props.has("trail_zone") && b.wave_path.size() % 4 == 0) Verbs::leave_trail(*this, b);
	}
	if (!b.tag.empty()) {
		_wave_sweep(b);
		if (!b.alive || b.form != Form::Wave) return;
	}
	if (arena.in_pool(np.x, np.z)) {
		AgentRef th = Agent::of_body(*this, b);
		AgentRef env = Agent::of_env(this, "pool", np);
		IxCtx ctx;
		ctx.continuous = true;
		Interactions::resolve(*this, *th, *env, ctx, &Interactions::PASS_RULE());
		if (!b.alive || b.form != Form::Wave) return;
	}
	MatBody* pd = puddle_at(np);
	if (pd != nullptr) {
		AgentRef env = Agent::of_env(this, "puddle", np);
		env->body = pd;
		AgentRef th = Agent::of_body(*this, b);
		IxCtx ctx;
		ctx.continuous = true;
		Interactions::resolve(*this, *th, *env, ctx, &Interactions::PASS_RULE());
	}
}

void CombatWorld::_wave_sweep(MatBody& wv) {
	for (size_t i = 0; i < bodies.size(); ++i) {
		MatBody& o = *bodies[i];
		if (&o == &wv || !o.alive || o.static_body || o.controller >= 0 || o.captured_by >= 0) continue;
		if (o.form == Form::Wave || o.form == Form::Zone || o.form == Form::Wall || o.form == Form::Pool) continue;
		if (Vec2(o.pos.x - wv.pos.x, o.pos.z - wv.pos.z).length() > wv.wave_width * 0.5 + o.radius || o.pos.y > wv.pos.y + 2.0f) continue;
		const std::string key = itos(wv.id) + "|" + itos(o.id);
		auto it = _zone_pairs.find(key);
		if (tick - (it == _zone_pairs.end() ? -100000 : it->second) < 6) continue;
		_zone_pairs[key] = tick;
		AgentRef counter = Agent::of_body(*this, wv);
		counter->actor = get_actor(wv.attack_owner);
		AgentRef th = Agent::of_body(*this, o, counter->actor);
		IxCtx ctx;
		ctx.continuous = true;
		ctx.site = "wave";
		Interactions::resolve(*this, *th, *counter, ctx, &Interactions::PASS_RULE());
		if (!wv.alive || wv.form != Form::Wave) return;
	}
}

void CombatWorld::_settle_wave(MatBody& b, const std::string& why) {
	b.form = b.liquid > 0.0 ? Form::Blob : Form::Chunk;
	b.vel = Vec3();
	b.attack_id = 0;
	b.on_ground = true;
	b.max_life = Sim::REMNANT_LIFETIME;
	b.age = 0.0;
	emit("wave_settle", D({{"body", b.id}, {"why", why}}));
	release_captured(b);
	if (!b.tag.empty()) Verbs::on_wave_end(*this, b, why);
}

void CombatWorld::_quench(MatBody& lava, MatBody& water, double dt) { quench_energy(lava, water, Sim::QUENCH_RATE * dt); }

void CombatWorld::quench_energy(MatBody& lava, MatBody& water, double q_max) {
	const double q = minf(q_max, lava.thermal_energy());
	if (q <= 0.0 || water.mass <= 0.0) return;
	const double applied = -Thermal::heat(lava, -q);
	const double kg = boil_water(water, applied, lava.pos + Vec3(0, 0.3f, 0));
	if (water.form == Form::Puddle && water.mass <= 0.05) decay_body(water, "boiled");
	if (tick % 6 == 0) emit("steam", D({{"body", lava.id}, {"water", water.id}, {"kg", kg}}));
}

double CombatWorld::heat_body(MatBody& b, double energy) {
	const double applied = Thermal::heat(b, energy);
	const double kg = Thermal::last_vapor();
	if (kg > 0.0) {
		ledger.vapor += Thermal::vapor_energy(kg);
		_spawn_steam(b.pos + Vec3(0, 0.3f, 0), kg);
		if (b.mass <= 0.05 && b.form != Form::Pool) decay_body(b, "boiled");
	}
	return applied;
}

double CombatWorld::boil_water(MatBody& water, double energy, Vec3 at) {
	const double kg = Thermal::boil(water, energy);
	ledger.vapor += Thermal::vapor_energy(kg);
	ledger.ambient -= energy - Thermal::last_used();
	_spawn_steam(at, kg);
	return kg;
}

void CombatWorld::_spawn_steam(Vec3 p, double kg) {
	if (kg <= 0.0) return;
	for (const BodyRef& cp : bodies) {
		MatBody& c = *cp;
		if (c.alive && c.form == Form::Cloud && c.mat == Mat::Steam && c.pos.distance_to(p) < 2.0f && c.age < 1.5) {
			c.mass += kg;
			return;
		}
	}
	MatBody* c = spawn_body(Mat::Steam, Form::Cloud, kg, p, "steam");
	c->max_life = 2.6;
	c->phase = Phase::Gas;
}

void CombatWorld::_water_to_puddle(MatBody& b) {
	b.form = Form::Puddle;
	b.vel = Vec3();
	b.attack_id = 0;
	b.on_ground = true;
	b.pos.y = f32(arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3));
	if (arena.in_pool(b.pos.x, b.pos.z)) {
		merge_bodies(*pool, b);
		return;
	}
	b.update_radius_puddle();
	emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "stream"}, {"to", "puddle"}, {"why", "landed"}}));
	_merge_puddle(b);
}

void CombatWorld::_merge_puddle(MatBody& b) {
	if (!b.alive) return;
	if (arena.in_pool(b.pos.x, b.pos.z)) {
		merge_bodies(*pool, b);
		return;
	}
	for (size_t i = 0; i < bodies.size(); ++i) {
		MatBody& o = *bodies[i];
		if (&o != &b && o.alive && o.form == Form::Puddle && o.phase == b.phase) {
			if (Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() < (o.radius + b.radius) * 0.8) {
				MatBody& into = o.mass >= b.mass ? o : b;
				MatBody& other = &into == &o ? b : o;
				merge_bodies(into, other);
				into.update_radius_puddle();
				return;
			}
		}
	}
	std::vector<MatBody*> puddles;
	for (const BodyRef& op : bodies)
		if (op->alive && op->form == Form::Puddle) puddles.push_back(op.get());
	if (static_cast<int>(puddles.size()) > Sim::MAX_PUDDLES) {
		MatBody* oldest = puddles[0];
		mass_ledger.evaporated += oldest->mass;
		ledger.removed += oldest->thermal_energy();
		remove_body(*oldest, "evaporated");
	}
}

void CombatWorld::_shatter(MatBody& b) {
	emit("shatter", D({{"body", b.id}, {"mass", b.mass}}));
	b.attack_id = 0;
	b.form = Form::Chunk;
	b.vel = Vec3();
	b.on_ground = true;
	b.pos.y = f32(arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.05);
	if (b.mass >= 1.0) {
		MatBody* c = split_body(b, b.mass * 0.5, b.pos + Vec3(0.35f, 0, 0.2f));
		c->form = Form::Chunk;
		c->on_ground = true;
		c->max_life = Sim::REMNANT_LIFETIME;
	}
	b.max_life = Sim::REMNANT_LIFETIME;
}

void CombatWorld::_return_mass(MatBody& b) {
	switch (b.mat) {
		case Mat::Stone:
		case Mat::Sand:
		case Mat::Glass: mass_ledger.ground_returned += b.mass; break;
		case Mat::Water: mass_ledger.evaporated += b.mass; break;
		case Mat::Steam: mass_ledger.vapor += b.mass; break;
		case Mat::Metal: mass_ledger.metal_returned += b.mass; break;
		case Mat::Plant: mass_ledger.plant_returned += b.mass; break;
		default: break;
	}
}

void CombatWorld::decay_body(MatBody& b, const std::string& why) {
	if (!b.alive) return;
	_return_mass(b);
	ledger.removed += b.thermal_energy();
	release_captured(b);
	remove_body(b, why);
}

void CombatWorld::convert_mat(MatBody& b, Mat new_mat, const std::string& ledger_key) {
	const double e = b.thermal_energy();
	b.mat = new_mat;
	if (!Materials::is_fusible(new_mat)) b.liquid = 0.0;
	_set_energy(b, e);
	Thermal::update_phase(b);
	b.update_radius();
	if (!ledger_key.empty()) {
		if (double* f = mass_ledger.field(ledger_key)) *f += b.mass;
	}
	emit("convert", D({{"body", b.id}, {"to", Sim::mat_name(new_mat)}, {"mass", b.mass}}));
}

MatBody* CombatWorld::grow_plant(MatBody* src, double kg, Vec3 p, ActorState* a) {
	double take = kg;
	if (src != nullptr) {
		take = minf(kg, src->mass);
		const double e = src->thermal_energy() * take / maxf(src->mass, 1e-9);
		ledger.removed += e;
		src->mass -= take;
		if (src->form == Form::Puddle) src->update_radius_puddle();
		if (src->mass <= 0.01) decay_body(*src, "grown");
	} else if (a != nullptr) {
		take = minf(kg, a->water_carried);
		a->water_carried -= take;
	}
	if (take <= 0.0) return nullptr;
	mass_ledger.water_to_plant += take;
	return spawn_body(Mat::Plant, Form::Chunk, take, p, "grow");
}

void CombatWorld::burn_plant(MatBody& b, double kg) {
	const double m = minf(kg, b.mass);
	if (m <= 0.0) return;
	const double e_share = b.thermal_energy() * m / maxf(b.mass, 1e-9);
	ledger.removed += e_share;
	b.mass -= m;
	mass_ledger.burned += m;
	if (tick % 10 == 0) emit("burn_plant", D({{"body", b.id}, {"kg", m}}));
	if (b.mass <= 0.05) {
		mass_ledger.burned += b.mass;
		ledger.removed += b.thermal_energy();
		b.mass = 0.0;
		release_captured(b);
		remove_body(b, "burned");
	} else {
		b.update_radius();
	}
}

void CombatWorld::release_captured(MatBody& captor) {
	if (captor.captured.empty()) return;
	const Vec3 dir = captor.vel.length() > 0.1f ? captor.vel.normalized() : captor.wave_dir;
	const std::vector<int> ids = captor.captured;
	for (int id : ids) {
		MatBody* c = get_body(id);
		if (c == nullptr || !c->alive || c->captured_by != captor.id) continue;
		c->captured_by = -1;
		const double spd = dnum(c->props, "release_speed", 0.0);
		c->vel = spd > 0.0 ? dir * spd + Vec3(0, 1.5f, 0) : captor.vel;
		const int own = captor.attack_owner >= 0 ? captor.attack_owner : captor.owner;
		if (spd > 0.0 && own >= 0) {
			c->attack_id = new_attack_id();
			c->attack_owner = own;
			c->hit_set.clear();
			c->hit_set.add(own);
			c->damage = maxf(c->damage, dnum(c->props, "release_damage", 8.0));
			c->balance_damage = maxf(c->balance_damage, 20.0);
		}
		c->on_ground = false;
		emit("release_captured", D({{"body", c->id}, {"by", captor.id}}));
	}
	captor.captured.clear();
}

}  // namespace ff
