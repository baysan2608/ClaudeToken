// Fourfold core - port of game/combat/kits/earth/earth_sand.gd (Earth / Sand, sub 2; MOVESET §7.3): Grit Shot slugs
// (puff / cloud / sandstorm), Sandblast, Sand Surge, clouds and quicksand (zone effects), Dune Push, Sandform (gather /
// compress / seize a cloud) and the mud wall. Sand is ground grit (ground_taken); conversions go through convert_mat.
#include "Combat/Kits/Earth/Earth.h"

#include "Combat/Kits/Earth/KitEarth.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

namespace ff {
namespace EarthSand {

using KitEarthUtil::flat_dist2;
using KitEarthUtil::flatv;

namespace {

IxCtx site_ctx(const char* s) {
	IxCtx c;
	c.site = s;
	return c;
}

void _dune_push(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "dune", -1));
	if (wall == nullptr || !wall->alive || wall->form != Form::Wall) return;
	if (a.wall_body == wall->id) a.wall_body = -1;
	wall->static_body = false;
	wall->form = Form::Chunk;   // no longer a wall: the pour start isn't blocked by itself
	wall->wall_rise = 0.0;
	wall->props.erase("standing");
	w.release_captured(*wall);
	inst.data.set("morph_body", wall->id);
	inst.data.set("keep_wall", -1);
	static const double kTierMass[4] = {0.0, 115.0, 130.0, 160.0};
	int tier = 0;
	for (int k = 1; k <= 3; ++k)
		if (wall->mass >= kTierMass[k] - 1e-6) tier = k;
	inst.data.set("tier", tier);
	MatBody* b = VerbGroundLine::launch(w, a, inst,
	                                    D({{"source", "held"}, {"mat", "sand"}, {"tag", "sand_surge"}, {"width", 2.5 + 0.5 * tier},
	                                       {"budget", 10.0 + 1.0 * tier}}));
	if (b != nullptr) {
		b->props.erase("mud");
		w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "wall"}, {"to", "sand_surge"}, {"why", "dune_push"}}));
	}
}

MatBody* _cloud_in_reach(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const Vec3 dir = dvec(inst.data, "aim", a.forward());
	MatBody* best = nullptr;
	double bd = kInf;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& z = *w.bodies[i];
		if (!z.alive || z.form != Form::Zone || z.mat != Mat::Sand || z.tag == "quicksand") continue;
		const Vec3 to = flatv(z.pos - a.pos);
		const double d = to.length();
		if (d > dnum(inst.def, "reach") + z.zone_radius || (d > 0.6 && to.normalized().dot(dir) < 0.5)) continue;
		if (d < bd) {
			best = &z;
			bd = d;
		}
	}
	return best;
}

void _compress(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& b) {
	if (!w.spend_focus(a, dnum(inst.def, "shape_cost", 3.0))) {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}, {"reason", "shape"}}));
		return;
	}
	inst.data.set("shaped", true);
	w.convert_mat(b, Mat::Stone, "sand_to_sandstone");
	b.tag = "sandstone";
	w.emit("shape", D({{"actor", a.id}, {"body", b.id}, {"shape", "compress"}}));
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "sand"}, {"to", "sandstone"}, {"why", "compress"}}));
	Verbs::fx(w, a, inst, "cast", D({{"body", b.id}, {"shape", "small"}}));
}

void _sandform_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	MatBody* b = w.held(a);
	if (b == nullptr && !it.tech_cancel) {
		// Seize a sand cloud (zone) of the rival in the aim cone: it gathers into a held sand body.
		MatBody* z = _cloud_in_reach(w, a, inst);
		if (z != nullptr && inst.t >= 0.1) {
			const double s = w.grip_strength(a, *z, 0.85, dnum(inst.def, "reach")) * dnum(inst.def, "grip_mult", 1.2);
			const double auth = z->owner >= 0 && z->owner != a.id ? Interactions::cohesion(z->tier) : 0.0;
			if (s > auth + CombatWorld::GRIP_MARGIN || auth <= 0.0) {
				FxEvents::zone(w, *z, "close");
				z->form = Form::Blob;
				z->tag = "slug";
				z->zone_radius = 0.0;
				z->power = 0.0;
				z->max_life = -1.0;
				for (const char* k : {"barrier", "height", "drag", "channel"}) z->props.erase(k);
				z->update_radius();
				w.take_control(a, *z, 0.9, "seize");
				w.emit("reclaim", D({{"actor", a.id}, {"body", z->id}, {"what", "sand_cloud"}}));
				return;
			}
		}
	}
	if (b != nullptr && it.attack_pressed && !dbool(inst.data, "shaped", false) && b->mat == Mat::Sand) _compress(w, a, inst, *b);
	VerbGrip::tick(w, a, inst, it);
	b = w.held(a);
	if (b != nullptr && a.action.get() == &inst && inst.phase == ActionPhase::Channel && it.tech_held) {
		if (b->mat == Mat::Sand && b->mass < GATHER_MAX) {
			// Gather: more sand rises from the ground into the held body (booked ground_taken).
			if (w.spend_focus(a, GATHER_FOCUS * Sim::DT)) {
				const double add = minf(GATHER_RATE * Sim::DT, GATHER_MAX - b->mass);
				const double e = b->thermal_energy();
				b->mass += add;
				w._set_energy(*b, e);
				b->update_radius();
				w.mass_ledger.ground_taken += add;
			}
		}
		b->tag = b->mat == Mat::Stone ? "sandstone" : "slug";
	}
	if (b == nullptr && a.action.get() == &inst && inst.phase != ActionPhase::Channel) {
		// Released: the thrown slug bursts like a grit shot.
		for (size_t i = 0; i < w.bodies.size(); ++i) {
			MatBody& x = *w.bodies[i];
			if (x.alive && x.attack_owner == a.id && dint(x.props, "src_attack", -1) == inst.attack_id && x.mat == Mat::Sand) {
				x.props.set("on_impact", "burst");
				x.props.set("hit_status", "blinded");
				x.props.set("hit_status_t", 1.0);
				if (x.mass >= 20.0) {
					x.props.set("cloud_r", 2.0);
					x.props.set("cloud_life", 2.5);
				}
			}
		}
	}
}

}  // namespace

// ================================================================ Grit Shot / slugs

bool slug_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	for (MatBody* b : VerbProjectile::fire(w, a, inst)) {
		b->props.set("cloud_r", Charge::paramf(inst, "cloud_r", 0.0));
		b->props.set("cloud_life", Charge::paramf(inst, "cloud_life", 2.5));
		b->props.set("storm", Charge::paramb(inst, "storm", false));
	}
	return true;
}

bool slug_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)what;
	if (b.mat != Mat::Sand || !b.alive) return false;
	ActorState* owner = w.get_actor(b.attack_owner);
	const double r = dnum(b.props, "cloud_r", 0.0);
	if (r > 0.0) {
		const bool storm = dbool(b.props, "storm", false);
		b.form = Form::Zone;
		b.tag = storm ? "sandstorm" : "sand_cloud";
		b.zone_radius = r;
		b.radius = r;
		b.owner = owner != nullptr ? owner->id : -1;
		b.power = storm ? 12.0 : 8.0;
		b.vel = Vec3();
		b.attack_id = 0;
		b.gravity_scale = 0.0;
		b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3));
		b.max_life = b.age + dnum(b.props, "cloud_life", 2.5);
		b.props.set("barrier", true);
		b.props.set("height", 3.0);
		b.props.set("drag", 6.0);
		b.props.set("channel", "P");
		FxEvents::zone(w, b, "open");
		FxEvents::fx(w, "burst", "sand",
		             D({{"actor", b.owner}, {"body", b.id}, {"pos", b.pos}, {"radius", r}, {"power", b.power}, {"tier", b.tier}, {"element", 0},
		                {"sub", 2}}));
		return true;
	}
	VerbVolume::burst_at(w, owner, nullptr, b.pos,
	                     D({{"radius", 1.2}, {"power", 4.0}, {"damage", 2.0}, {"balance", 6.0}, {"knock", 1.0}, {"lift", 0.5}, {"cls", "sand"},
	                        {"mat", "sand"}, {"status", "blinded"}, {"status_t", 1.0}}));
	b.attack_id = 0;
	b.vel *= 0.2;
	b.props.set("settle", true);
	b.max_life = b.age + SETTLE_TIME;
	return true;
}

bool slug_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)w;
	(void)dt;
	if (b.mat == Mat::Sand && b.attack_id == 0 && b.form != Form::Zone && b.controller < 0 && !b.props.has("settle") && b.on_ground) {
		b.props.set("settle", true);
		b.max_life = b.age + SETTLE_TIME;
	}
	return false;
}

// ================================================================ Sandblast

bool blast_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	(void)w;
	(void)a;
	const double at = Charge::paramf(inst, "active_t", 0.0);
	if (at > 0.0) inst.data.set("active", at);
	return false;
}

// ================================================================ Sand Surge

bool surge_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (b.mat != Mat::Sand) return false;
	if (b.form != Form::Wave) {
		if (!b.props.has("settle") && b.controller < 0 && b.form != Form::Zone) {
			b.props.set("settle", true);
			b.max_life = b.age + SETTLE_TIME;
		}
		return false;
	}
	AgentRef counter = Agent::of_body(w, b);
	counter->actor = w.get_actor(b.attack_owner);
	for (MatBody* zp : w.body_list()) {
		MatBody& z = *zp;
		if (!z.alive || &z == &b || z.form != Form::Zone || z.mat != Mat::Fire) continue;
		if (flat_dist2(z.pos, b.pos) > b.wave_width * 0.5 + z.zone_radius) continue;
		const std::string key = "smother_" + itos(z.id);
		if (b.props.has(key)) continue;
		b.props.set(key, true);
		AgentRef th = Agent::of_body(w, z);
		Interactions::resolve(w, *th, *counter, site_ctx("wave"));
	}
	MatBody* pd = w.puddle_at(b.pos);
	if (pd != nullptr && !b.props.has("mud_" + itos(pd->id))) {
		b.props.set("mud_" + itos(pd->id), true);
		AgentRef th = Agent::of_body(w, *pd);
		AgentRef co = Agent::of_body(w, b);
		Interactions::resolve(w, *th, *co, site_ctx("wave"));
	}
	return false;
}

// ================================================================ clouds and pits (zone effects)

void cloud_effect(CombatWorld& w, MatBody& z, double dt) {
	for (ActorState* a : w.actors_in_zone(z)) {
		if (a->id == z.owner) {
			Status::apply(w, *a, "concealed", 0.25, 1.0, a->id);
			continue;
		}
		Status::apply(w, *a, "blinded", 0.35, 1.0, z.owner);
		if (z.tag == "sandstorm") a->health = maxf(0.0, a->health - 2.0 * dt);
	}
}

void quicksand_effect(CombatWorld& w, MatBody& z, double dt) {
	const bool mud = dbool(z.props, "mud", false);
	for (const auto& ap : w.actors) {
		ActorState& a = *ap;
		const std::string k = "in_" + itos(a.id);
		if (a.health <= 0.0 || a.id == z.owner || !a.grounded || a.flying || Status::immune(a, "ground")) {
			z.props.erase(k);
			continue;
		}
		if (flat_dist2(a.pos, z.pos) > z.zone_radius + Sim::ACTOR_RADIUS * 0.5 || absf(a.pos.y - z.pos.y) > 0.6) {
			z.props.erase(k);
			continue;
		}
		Status::apply(w, a, "mired", 0.2, 1.0, z.owner);
		double t = dnum(z.props, k, 0.0) + dt;
		if (t >= (mud ? 0.6 : 1.0)) {
			Status::apply(w, a, "rooted", mud ? 0.9 : 0.6, 1.0, z.owner);
			t = -0.6;
		}
		z.props.set(k, t);
	}
}

bool pit_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	inst.data.set("tier", KitEarthUtil::guard_tier(inst));
	MatBody* z = VerbZone::spawn(w, a, inst);
	z->static_body = true;
	z->props.set("spare_owner", true);
	return true;
}

bool mud_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	const int64_t mt = b.props.has("mud_tick") ? b.props.get("mud_tick").as_int() : w.tick;
	if (b.form == Form::Wall && dtruthy(b.props, "mud") && static_cast<double>(w.tick - mt) * Sim::DT >= 4.0) {
		w._crumble_wall(b);
		return true;
	}
	return false;
}

// ================================================================ module lifecycle (dune_push, sandform)

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "dune_push") {
		inst.data.set("face", w.aim_dir(a, it));
		inst.data.set("aim", inst.data.get("face"));
		MatBody* wall = w.get_body(dint(inst.data, "wall", a.wall_body));
		if (wall == nullptr || !wall->alive || wall->form != Form::Wall || wall->mat != Mat::Sand || wall->last_actor != a.id) {
			KitEarthUtil::fizzle(w, a, inst, "material");
			return;
		}
		if (!Verbs::pay(w, a, inst, "start")) {
			inst.data.set("fizzle", true);
			return;
		}
		inst.data.set("dune", wall->id);
		inst.data.set("keep_wall", wall->id);
		Verbs::fx(w, a, inst, "cast", D({{"body", wall->id}, {"pos", wall->pos}}));
	} else {
		Verbs::on_start(w, a, inst, it);
	}
}

ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	if (inst.id == "dune_push") return ActionPhase::Active;
	return Verbs::after_startup(w, a, inst, it);
}

void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (inst.id == "dune_push") {
		if (p == ActionPhase::Active) _dune_push(w, a, inst);
		else if (p == ActionPhase::Recovery) inst.data.set("keep_wall", -1);
	} else {
		Verbs::on_phase(w, a, inst, p);
	}
}

void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.id == "dune_push") {
	} else if (inst.id == "sandform") {
		if (inst.phase == ActionPhase::Channel) _sandform_tick(w, a, inst, it);
	} else {
		Verbs::on_tick(w, a, inst, it);
	}
}

void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	if (inst.id == "dune_push") inst.data.set("keep_wall", -1);
	else Verbs::on_interrupt(w, a, inst, reason);
}

Dict preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	const Dict d = Moves::defs().get("sandform").as_dict();
	Dict pv = VerbGrip::preview(w, a, d, dir);
	pv.set("mode", dint(pv, "body", -1) >= 0 ? "SAND" : "GATHER");
	pv.set("ok", true);
	return pv;
}

}  // namespace EarthSand
}  // namespace ff
