// Fourfold core - port of game/combat/kits/earth/earth_stone.gd (Earth / Stone, sub 0; MOVESET §7.1). The defs and the
// legacy extension (earth_attack T2/T3, earth_tech Split) are data (Data/moves.json); this file holds the code: Bulwark
// thickening (channel hook ""), Ram Wall, Swallow, Stone Skin / Burrow Step, Rising Fangs / Earthrise and the spike line.
#include "Combat/Kits/Earth/Earth.h"

#include "Combat/Kits/Earth/KitEarth.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace EarthStone {

using KitEarthUtil::flatv;

namespace {

IxCtx site_ctx(const char* s) {
	IxCtx c;
	c.site = s;
	return c;
}

double atan2d(Vec3 d) { return std::atan2(static_cast<double>(d.x), static_cast<double>(d.z)); }

// Burrow Step spec (merged over the stone_skin def).
Dict burrow_spec() {
	return D({{"distance", 3.5}, {"iframes", 0.24}, {"burrow", true}, {"dir", "stick"}, {"active", 14.0 / 60.0}, {"trail", ""}});
}

AgentRef _ram_agent(CombatWorld& w, ActorState& a, MatBody& wall, Vec3 dir, double spd) {
	AgentRef g = Agent::of_body(w, wall);
	g->kind = "body";
	g->ccls = "ram";
	g->actor = &a;
	g->dir = dir;
	g->power = wall.mass * spd / 20.0;
	return g;
}

bool _walls_touch(CombatWorld& w, MatBody& wall, MatBody& other, Vec3 dir) {
	const Vec3 front = wall.pos + dir * wall.wall_half.z + V3(0, 0.4, 0);
	for (double k : {-0.7, 0.0, 0.7}) {
		const Vec3 side = V3(std::cos(wall.wall_yaw), 0.0, -std::sin(wall.wall_yaw)) * wall.wall_half.x * k;
		if (w.point_in_wall(front + side, other, 0.15)) return true;
	}
	return w.point_in_wall(other.pos + V3(0, 0.4, 0), wall, other.wall_half.z + 0.1);
}

void _ram_crumble(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& wall, const std::string& why) {
	inst.data.set("keep_wall", -1);
	if (wall.alive) {
		wall.vel = Vec3();
		FxEvents::fx_for(w, a, inst, "burst", "stone", D({{"pos", wall.pos + V3(0, 0.6, 0)}, {"radius", 1.2}, {"body", wall.id}}));
		w.emit("ram_end", D({{"actor", a.id}, {"body", wall.id}, {"why", why}}));
		w._crumble_wall(wall);
	}
	if (inst.phase == ActionPhase::Active && a.action.get() == &inst) w.set_phase(a, inst, ActionPhase::Recovery);
}

void _ram_start(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "wall", a.wall_body));
	if (wall == nullptr || !wall->alive || wall->form != Form::Wall || wall->last_actor != a.id) {
		KitEarthUtil::fizzle(w, a, inst, "material");
		return;
	}
	if (!Verbs::pay(w, a, inst, "start")) {
		inst.data.set("fizzle", true);
		return;
	}
	if (wall->tag.empty() && wall->mat == Mat::Stone) thicken(w, a, *wall, dnum(inst.data, "guard_t", 0.0));
	inst.data.set("tier", wall->tier > 0 ? clampi(wall->tier - 1, 0, 2) : 0);
	const Vec3 dir = a.forward();
	inst.data.set("dir", dir);
	inst.data.set("ram", wall->id);
	inst.data.set("keep_wall", wall->id);
	inst.data.set("speed", RAM_SPEED);
	inst.data.set("hit", Dict());
	wall->props.set("ram", a.id);
	Verbs::fx(w, a, inst, "cast", D({{"body", wall->id}, {"pos", wall->pos}}));
}

void _ram_step(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wallp = w.get_body(dint(inst.data, "ram", -1));
	if (wallp == nullptr || !wallp->alive || wallp->form != Form::Wall) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	MatBody& wall = *wallp;
	const Vec3 dir = dvec(inst.data, "dir");
	double spd = dnum(inst.data, "speed", RAM_SPEED);
	const Vec3 step = dir * spd * Sim::DT;
	Vec3 np = wall.pos + step;
	const Vec3 lead = wall.pos + dir * (static_cast<double>(wall.wall_half.z) + 0.1) + V3(0, 0.5, 0);
	if (w.arena.segment_hit(lead, lead + step + dir * 0.15, 0.05) >= 0.0) {
		_ram_crumble(w, a, inst, wall, "arena");
		return;
	}
	const double g = w.arena.ground_height(np.x, np.z, wall.pos.y + 0.35);
	if (absf(g - wall.pos.y) > 0.35) {
		_ram_crumble(w, a, inst, wall, "ledge");
		return;
	}
	np.y = f32(g);
	wall.pos = np;
	wall.vel = dir * spd;
	AgentRef ram = _ram_agent(w, a, wall, dir, spd);
	Dict hit = ddict(inst.data, "hit");
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (&b == &wall || !b.alive || b.controller >= 0 || b.captured_by >= 0 || &b == w.pool) continue;
		if (b.form == Form::Zone || b.form == Form::Pool || b.form == Form::Puddle) continue;
		if (b.form == Form::Wall) {
			if (b.last_actor != a.id && _walls_touch(w, wall, b, dir)) {
				AgentRef th = Agent::of_body(w, wall, w.get_actor(b.last_actor));
				AgentRef co = Agent::of_body(w, b);
				const IxResult res = Interactions::resolve(w, *th, *co, site_ctx("ram"));
				if (!wall.alive) {
					w.set_phase(a, inst, ActionPhase::Recovery);
					return;
				}
				if (res.counter_broken) {
					spd *= maxf(0.4, res.pass_scale);
				} else if (res.stopped || res.outcome == "earth_ram_both") {
					_ram_crumble(w, a, inst, wall, "contest");
					return;
				} else {
					spd *= maxf(0.3, res.pass_scale);
				}
				inst.data.set("speed", spd);
			}
			continue;
		}
		if (b.static_body) continue;
		bool touch = false;
		if (b.form == Form::Wave) touch = w.point_in_wall(b.pos + V3(0, 0.2, 0), wall, b.wave_width * 0.4 + 0.2);
		else touch = w.point_in_wall(b.pos, wall, b.radius + 0.25);
		if (!touch || hit.has(itos(b.id))) continue;
		hit.set(itos(b.id), true);
		ram->power = wall.mass * spd / 20.0;
		AgentRef th = Agent::of_body(w, b, &a);
		const IxResult r2 = Interactions::resolve(w, *th, *ram, site_ctx("ram"));
		if (!wall.alive) {
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		if (r2.outcome == "weaken") {
			spd *= 0.85;
			inst.data.set("speed", spd);
		}
	}
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0 || hit.has(itos(-t.id))) continue;
		if (w.point_in_wall(t.pos + V3(0, 0.9, 0), wall, Sim::ACTOR_RADIUS + 0.15)) {
			hit.set(itos(-t.id), true);
			w.hit_actor(t, D({{"attacker", a.id},
			                  {"attack_id", inst.attack_id},
			                  {"damage", dnum(inst.def, "damage")},
			                  {"balance", dnum(inst.def, "balance")},
			                  {"knock", dir * dnum(inst.def, "knock") + V3(0, 2.0, 0)},
			                  {"kind", "stone"},
			                  {"from", wall.pos - dir},
			                  {"power", wall.mass * spd / 20.0},
			                  {"mat", "stone"}}));
		}
	}
}

void _ram_end(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "ram", -1));
	if (wall != nullptr && wall->alive && wall->form == Form::Wall) _ram_crumble(w, a, inst, *wall, "spent");
	inst.data.set("keep_wall", -1);
}

void _swallow_start(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (!Verbs::pay(w, a, inst, "start")) {
		inst.data.set("fizzle", true);
		return;
	}
	inst.data.set("tier", KitEarthUtil::guard_tier(inst));
	inst.data.set("done", Dict());
	if (inst.tier() > 0) FxEvents::charge(w, a, inst, inst.tier(), inst.tier() >= 3);
	Verbs::fx(w, a, inst, "cast", D({{"shape", "open"}}));
}

void _swallow_tick(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Vec3 dir = dvec(inst.data, "face");
	const double rng = dnum(inst.def, "range", 6.0);
	const double cos_lim = std::cos(deg_to_rad(dnum(inst.def, "angle", 30.0)));
	Dict done = ddict(inst.data, "done");
	AgentRef counter = Agent::of_move(&w, &a, "swallow", inst.tier(), false);
	counter->pos = KitEarthUtil::ground_point(w, a, dir, 2.0);
	counter->dir = dir;
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || b.static_body || b.controller >= 0 || b.captured_by >= 0 || done.has(itos(b.id)) || &b == w.pool) continue;
		if (b.form == Form::Wall || b.form == Form::Zone || b.form == Form::Pool || b.form == Form::Puddle || b.form == Form::Cloud) continue;
		if (b.attack_id != 0 && b.attack_owner == a.id) continue;
		const Vec3 to = flatv(b.pos - a.pos);
		const float d = to.length();
		if (d > rng + b.radius || (d > 0.6 && to.normalized().dot(dir) < cos_lim)) continue;
		if (b.pos.y > a.pos.y + 2.6) continue;
		done.set(itos(b.id), true);
		AgentRef th = Agent::of_body(w, b, &a);
		Interactions::resolve(w, *th, *counter, site_ctx("swallow"));
	}
}

void _skin_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Vec3 mv = it.move;
	mv.y = 0.0f;
	const bool ground_ok = a.grounded && a.surface == "stone" && !a.in_water;
	if (mv.length() > 0.3 && ground_ok) {
		inst.data.set("mode", "burrow");
		Dict spec = inst.def.duplicate();
		spec.merge(burrow_spec(), true);
		inst.data.set("spec_def", spec);
		inst.data.set("active", 14.0 / 60.0);
		if (!Verbs::pay_dict(w, a, inst, D({{"focus", 6.0}}))) {
			inst.data.set("fizzle", true);
			return;
		}
		FxEvents::fx_for(w, a, inst, "erupt", "stone", D({{"pos", a.pos}, {"shape", "small"}}));
		VerbMotion::dash_start(w, a, inst, it);
		return;
	}
	inst.data.set("mode", "skin");
	VerbMotion::stance_start(w, a, inst);
}

}  // namespace

// ================================================================ Bulwark thickening

void bulwark_channels(CombatWorld& w, MatBody& b, Agent& g) {
	if (b.form != Form::Wall || b.mat != Mat::Stone || !b.tag.empty()) return;
	ActorState* owner = w.get_actor(b.last_actor);
	if (owner == nullptr || owner->wall_body != b.id || !owner->guarding || owner->action == nullptr || owner->action->id != "guard") return;
	if (thicken(w, *owner, b, owner->action->total)) g.mass = b.mass;
}

bool thicken(CombatWorld& w, ActorState& a, MatBody& b, double held_s) {
	int tier = 0;
	if (held_s >= BULWARK_TIMES[1] - 1e-6) tier = 2;
	else if (held_s >= BULWARK_TIMES[0] - 1e-6) tier = 1;
	const double want = BULWARK_MASS[tier];
	if (b.mass >= want - 1e-6 || b.props.has("ram")) return false;
	const double extra = want - b.mass;
	const double e = b.thermal_energy();
	b.mass += extra;
	w._set_energy(b, e);
	w.mass_ledger.ground_taken += extra;
	b.tier = tier + 1;
	w.emit("barrier_grow", D({{"actor", a.id}, {"body", b.id}, {"mass", b.mass}, {"tier", tier + 1}}));
	w.emit("charge", D({{"actor", a.id}, {"move", "guard"}, {"element", Sim::EARTH}, {"sub", 0}, {"tier", tier + 1}, {"ready", tier >= 2},
	                     {"slot", "guard"}}));
	return true;
}

// ================================================================ module lifecycle (ram_wall, swallow, stone_skin)

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	inst.data.set("aim", inst.data.get("face"));
	if (inst.id == "ram_wall") _ram_start(w, a, inst);
	else if (inst.id == "swallow") _swallow_start(w, a, inst);
	else if (inst.id == "stone_skin") _skin_start(w, a, inst, it);
	else Verbs::on_start(w, a, inst, it);
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	if (inst.id == "ram_wall" || inst.id == "swallow") return ActionPhase::Active;
	if (inst.id == "stone_skin") return dstr(inst.data, "mode", "") == "burrow" ? ActionPhase::Active : ActionPhase::Channel;
	return Verbs::after_startup(w, a, inst, it);
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "ram_wall") {
		if (p == ActionPhase::Active)
			Verbs::fx(w, a, inst, "release", D({{"dir", dvec(inst.data, "dir", a.forward())}, {"length", 6.0}, {"body", dint(inst.data, "ram", -1)}}));
		else if (p == ActionPhase::Recovery)
			_ram_end(w, a, inst);
	} else if (inst.id == "swallow") {
		if (p == ActionPhase::Active) {
			const Vec3 dir = dvec(inst.data, "face");
			Verbs::fx(w, a, inst, "release",
			          D({{"pos", KitEarthUtil::ground_point(w, a, dir, 3.0)}, {"dir", dir}, {"length", 6.0}, {"angle", 60.0},
			             {"power", Charge::counter_power(inst.def, inst.tier())}}));
		}
	} else if (inst.id == "stone_skin") {
		if (p == ActionPhase::Recovery) {
			if (dstr(inst.data, "mode", "") == "burrow") {
				inst.data.set("controls_motion", false);
				FxEvents::fx_for(w, a, inst, "erupt", "stone", D({{"pos", a.pos}, {"shape", "small"}}));
			} else {
				VerbMotion::stance_end(w, a, inst);
			}
		}
	} else {
		Verbs::on_phase(w, a, inst, p);
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "ram_wall") {
		if (inst.phase == ActionPhase::Active) _ram_step(w, a, inst);
	} else if (inst.id == "swallow") {
		if (inst.phase == ActionPhase::Active) _swallow_tick(w, a, inst);
	} else if (inst.id == "stone_skin") {
		if (dstr(inst.data, "mode", "") == "burrow") {
			if (inst.phase == ActionPhase::Active) VerbMotion::dash_tick(w, a, inst);
		} else if (inst.phase == ActionPhase::Channel) {
			VerbMotion::stance_tick(w, a, inst, it);
		}
	} else {
		Verbs::on_tick(w, a, inst, it);
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (inst.id == "ram_wall") {
		MatBody* wall = w.get_body(dint(inst.data, "ram", -1));
		if (wall != nullptr && wall->alive) wall->vel = Vec3();
		inst.data.set("keep_wall", -1);
	} else if (inst.id == "stone_skin") {
		if (dstr(inst.data, "mode", "") == "burrow") inst.data.set("controls_motion", false);
		else VerbMotion::stance_end(w, a, inst);
	} else if (inst.id == "swallow") {
	} else {
		Verbs::on_interrupt(w, a, inst, reason);
	}
}

// ================================================================ Rising Fangs / Earthrise

bool fangs_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (!Charge::paramb(inst, "earthrise", false)) {
		MatBody* line = VerbGroundLine::launch(w, a, inst);
		if (line != nullptr) line->props.set("spike_h", Charge::paramf(inst, "spike_h", 0.45));
		return true;
	}
	const double r = Charge::paramf(inst, "radius", 3.5);
	const double mass = Charge::paramf(inst, "mass", 60.0);
	const Vec3 f = a.forward();
	const int pieces = 4;
	for (int k = 0; k < pieces; ++k) {
		const double ang = kTau * static_cast<double>(k) / static_cast<double>(pieces) + kPi / static_cast<double>(pieces);
		const Vec3 d = rotated(f, Vec3::Up(), ang);
		Vec3 p = a.pos + d * r;
		p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y + 0.4));
		MatBody* b = w.spawn_body(Mat::Stone, Form::Wall, mass / pieces, p, "ground@" + ftos(p.x, 1) + "," + ftos(p.z, 1));
		w.mass_ledger.ground_taken += mass / pieces;
		_make_spikes(w, a.id, *b, atan2d(d), 1.3, 0.7);
	}
	VerbVolume::burst_at(w, &a, &inst, a.pos + V3(0, 0.6, 0),
	                     D({{"radius", r + 0.6}, {"power", 40.0}, {"damage", Charge::paramf(inst, "damage", 16.0)}, {"balance", 50.0}, {"knock", 3.0},
	                        {"lift", 6.0}, {"cls", "blast"}, {"mat", "stone"}}));
	FxEvents::fx_for(w, a, inst, "ring", "stone", D({{"pos", a.pos}, {"radius", r}, {"power", 40.0}}));
	return true;
}

bool spike_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.mat == Mat::Metal) return EarthMetal::filings_tick(w, b, dt);
	if (b.form == Form::Wall) return false;
	if (b.form != Form::Wave) {
		_erupt(w, b);
		return true;
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !o.alive || o.form != Form::Wave || o.attack_owner == b.attack_owner || o.tag == "spike_line") continue;
		if (KitEarthUtil::flat_dist2(o.pos, b.pos) > (o.wave_width + b.wave_width) * 0.5 + 0.6) continue;
		AgentRef counter = Agent::of_body(w, b);
		counter->ccls = "spikes";
		counter->power = SPIKE_CP;
		counter->actor = w.get_actor(b.attack_owner);
		AgentRef th = Agent::of_body(w, o);
		const IxResult res = Interactions::resolve(w, *th, *counter, site_ctx("spikes"));
		if (res.stopped && b.alive) {
			_erupt(w, b);
			return true;
		}
	}
	if (b.wave_budget <= 0.05) {
		_erupt(w, b);
		return true;
	}
	return false;
}

void _erupt(CombatWorld& w, MatBody& b) {
	const int owner = b.attack_owner >= 0 ? b.attack_owner : b.last_actor;
	const Vec3 dir = b.wave_dir.length() > 0.1 ? b.wave_dir : Vec3(0.0f, 0.0f, -1.0f);
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.4));
	const double h = dnum(b.props, "spike_h", 0.45);
	_make_spikes(w, owner, b, atan2d(dir), maxf(0.8, b.wave_width * 0.6), h);
	FxEvents::fx(w, "erupt", "stone",
	             D({{"actor", owner}, {"body", b.id}, {"pos", b.pos}, {"dir", dir}, {"radius", b.wave_width}, {"element", 0}}));
}

void _make_spikes(CombatWorld& w, int owner, MatBody& b, double yaw, double half_x, double half_y) {
	release_hold(w, b);
	b.form = Form::Wall;
	b.tag = "spikes";
	b.vel = Vec3();
	b.attack_id = 0;
	b.wave_path.clear();
	b.wall_yaw = yaw;
	b.wall_half = V3(half_x, half_y, 0.3);
	b.wall_rise = 0.0;
	b.wall_damage = 0.0;
	b.static_body = true;
	b.on_ground = true;
	b.max_life = -1.0;
	b.age = 0.0;
	b.hardness = SPIKE_CP / maxf(b.mass, 1.0);
	b.props.set("standing", SPIKE_LIFE);
	b.props.set("rise_time", 0.08);
	b.props.set("source", "ground");
	b.last_actor = owner;
	w.release_captured(b);
	w.emit("wall", D({{"actor", owner}, {"body", b.id}, {"tag", "spikes"}}));
}

void release_hold(CombatWorld& w, MatBody& b) {
	if (b.controller >= 0) {
		ActorState* h = w.get_actor(b.controller);
		if (h != nullptr && h->held_body == b.id) h->held_body = -1;
		b.controller = -1;
	}
}

bool crag_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)w;
	(void)dt;
	if (b.attack_id != 0) b.spin = 6.0;
	return false;
}

}  // namespace EarthStone
}  // namespace ff
