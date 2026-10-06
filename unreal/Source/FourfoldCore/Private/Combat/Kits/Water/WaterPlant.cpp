// Fourfold core - port of game/combat/kits/water/water_plant.gd (Water / Plant, sub 3; MOVESET §7.8): Bramble Lash, Burr
// Shot (snare seeds), Root Snare (root waves, groves), Thicket Fan, Living Lattice (booked growth), Lattice Roll, vine
// walls, Deep Roots, Vinegrip / Wrap / Hook, Vine Swing and Canopy. Water -> vine is booked (water_to_plant).
#include "Combat/Kits/Water/Water.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace WaterPlant {

namespace {
constexpr int SUB = 3;

// Water for a vine move: `kg` from the waterskin / pool / puddle (WaterUtil.take).
double _water(CombatWorld& w, ActorState& a, ActionInst& inst, double kg, double minimum = 0.4) {
	const double got = WaterUtil::take(w, a, kg);
	if (got < minimum) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return 0.0;
	}
	return got;
}

void _sync_booking(CombatWorld& w, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "wall", -1));
	if (wall == nullptr || !wall->alive) return;
	// Growth since the last look is booked (the barrier verb booked it as moisture); burning and damage only lower the mark.
	const double seen = dnum(wall->props, "plant_seen", 0.0);
	if (wall->mass > seen + 1e-9) w.mass_ledger.water_to_plant += wall->mass - seen;
	wall->props.set("plant_seen", wall->mass);
}

// The nearest anchor in the direction of travel: the closest point of an arena solid (wall, pillar, ledge) within reach.
struct Anchor {
	bool found = false;
	Vec3 point;
	double dist = 0.0, top = 0.0;
};
Anchor _anchor(CombatWorld& w, const ActorState& a, Vec3 dir, double reach) {
	Anchor best;
	double bd = reach;
	for (const ArenaSolid& s : w.arena.solids) {
		if (ends_with(s.name, "_wall") && s.name != "cover_wall") continue;   // the arena boundary is not an anchor
		const Vec3 q = V3(clampf(a.pos.x, s.min.x, s.max.x), 0.0, clampf(a.pos.z, s.min.z, s.max.z));
		Vec3 to = q - a.pos;
		to.y = 0.0f;
		const double d = to.length();
		if (d < 1.5 || d > bd || to.normalized().dot(dir) < 0.5) continue;
		bd = d;
		best.found = true;
		best.point = q;
		best.dist = d;
		best.top = s.max.y;
	}
	return best;
}
}  // namespace

MatBody* grow(CombatWorld& w, double kg, Vec3 p, double life) {
	w.mass_ledger.water_to_plant += kg;
	MatBody* b = w.spawn_body(Mat::Plant, Form::Chunk, kg, p, "vine");
	b->max_life = life;
	b->on_ground = true;
	return b;
}

MatBody* plant_zone(CombatWorld& w, const std::string& tag, Vec3 p, double radius, int owner_id, double life, double kg, const Dict& props, double power) {
	w.mass_ledger.water_to_plant += kg;
	MatBody* z = WaterUtil::zone(w, tag, p, radius, owner_id, life, props, Mat::Plant, kg, power);
	z->sub = SUB;
	return z;
}

bool lash_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double kg = _water(w, a, inst, Charge::paramf(inst, "vine_kg", 1.0));
	if (kg <= 0.0) return true;
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const bool around = Charge::paramb(inst, "around", false);
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const double yank = Charge::paramf(inst, "yank", 7.0);
	const double slam = Charge::paramf(inst, "slam", 0.0);
	// the whip itself: a vine volume (cells wall_vine / flame x vine in WaterRules)
	AgentRef v = VerbVolume::cone(w, a, inst);
	const double vpow = v != nullptr ? v->power : 0.0;
	if (around) FxEvents::fx_for(w, a, inst, "ring", "plant", D({{"radius", rng_m}, {"power", vpow}, {"pos", a.pos}}));
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.hits_taken.find(inst.attack_id) == t.hits_taken.end()) continue;
		if (!(t.last_result == "hit" || t.last_result == "knockdown")) continue;
		Vec3 to = a.pos - t.pos;
		to.y = 0.0f;
		const double d = to.length();
		if (!Status::immune(t, "pull") && d > 1.4) {
			t.vel += to.normalized() * minf(yank, d * 4.0);   // about 2 m toward you
			Status::apply(w, t, "hooked", 0.5, 1.0, a.id);
		}
		if (slam > 0.0) {
			t.balance = maxf(0.0, t.balance - slam);
			t.vel.y = std::max(t.vel.y, 3.0f);
			t.grounded = false;
			w.emit("slam", D({{"actor", t.id}, {"by", a.id}}));
		}
	}
	if (around) {
		for (const auto& tp : w.actors) {
			ActorState& t = *tp;
			if (&t == &a || t.team == a.team || t.health <= 0.0 || t.hits_taken.find(inst.attack_id) != t.hits_taken.end()) continue;
			if (Vec2(t.pos.x - a.pos.x, t.pos.z - a.pos.z).length() > rng_m + Sim::ACTOR_RADIUS) continue;
			Vec3 rd = t.pos - a.pos;
			rd.y = 0.0f;
			w.hit_actor(t,
			            D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "damage", 16.0)}, {"balance", 24.0},
			               {"knock", rd.normalized() * 4.0 + V3(0, 1.5, 0)}, {"kind", "plant"}, {"from", a.chest()}, {"power", vpow}, {"tier", 3},
			               {"mat", "plant"}}),
			            v);
		}
	}
	// the vine that did the work lies where the whip ended and withers (booked)
	const Vec3 end = WaterUtil::ground_at(w, a.pos + dir * rng_m * 0.7);
	grow(w, kg, end + V3(0, 0.1, 0), 2.5);
	return true;
}

bool burr_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const int count = Charge::parami(inst, "count", 3);
	const double each = Charge::paramf(inst, "burr_kg", 0.5);
	const double got = _water(w, a, inst, each * static_cast<double>(count), each);
	if (got <= 0.0) return true;
	const int n = maxi(1, static_cast<int>(std::floor(got / each + 1e-6)));
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const double spread = deg_to_rad(Charge::paramf(inst, "spread", 10.0));
	const double spd = Charge::paramf(inst, "speed", 25.0);
	const Vec3 target = Verbs::target_point(w, a, inst, 14.0);
	const double left = got - each * static_cast<double>(n);
	if (left > 0.0) WaterUtil::give_back(w, a, left, a.pos);
	for (int k = 0; k < n; ++k) {
		const double ang = n == 1 ? 0.0 : lerpf(-spread * 0.5, spread * 0.5, static_cast<double>(k) / static_cast<double>(n - 1));
		MatBody* b = grow(w, each, a.hand_point() + rotated(dir, Vec3::Up(), ang) * 0.4, -1.0);
		b->tag = "seed";
		b->on_ground = false;
		b->gravity_scale = 0.3;
		b->max_life = 6.0;
		const Vec3 aim_to = a.chest() + rotated(target - a.chest(), Vec3::Up(), ang);
		b->vel = Verbs::launch_vel(b->pos, aim_to, spd, 0.3);
		Verbs::arm(w, a, inst, *b, 4.0, 8.0);
		b->props.set("on_impact", "sprout");
		b->props.set("root_t", Charge::paramf(inst, "root_t", 1.0));
		b->props.set("impact_radius", Charge::paramf(inst, "radius", 1.0));
		b->props.set("impact_life", Charge::paramf(inst, "life", 4.0));
		w.emit("launch", D({{"actor", a.id}, {"body", b->id}, {"speed", spd}, {"kind", "seed"}, {"tier", inst.tier()}}));
	}
	Verbs::fx(w, a, inst, "release", D({{"pos", a.hand_point()}, {"dir", dir}, {"power", static_cast<double>(n)}}));
	return true;
}

bool seed_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)what;
	if (b.tag != "seed") return false;
	ActorState* owner = w.get_actor(b.attack_owner);
	const double r = dnum(b.props, "impact_radius", 1.0);
	b.form = Form::Zone;
	b.tag = "snare";
	b.zone_radius = r;
	b.radius = r;
	b.attack_id = 0;
	b.vel = Vec3();
	b.gravity_scale = 0.0;
	b.owner = owner != nullptr ? owner->id : -1;
	b.max_life = b.age + dnum(b.props, "impact_life", 4.0);
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3));
	b.props.set("actor_status", "rooted");
	b.props.set("status_t", dnum(b.props, "root_t", 1.0));
	b.props.set("spare_owner", true);
	b.props.set("height", 1.6);
	b.props.set("rate", 0.2);
	b.props.set("ccls", "briar");
	FxEvents::zone(w, b, "open");
	return true;
}

bool roots_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double want = Charge::paramf(inst, "mass", 2.0);
	const double got = _water(w, a, inst, want, 1.0);
	if (got <= 0.0) return true;
	w.mass_ledger.water_to_plant += got;   // the booked water -> vine conversion of the root wave
	MatBody* b = VerbGroundLine::launch(w, a, inst, D({{"source", "none"}, {"mass", got}, {"mat", "plant"}}));
	if (b == nullptr) {
		w.mass_ledger.water_to_plant -= got;
		WaterUtil::give_back(w, a, got, a.pos);
		return true;
	}
	b->props.set("grove", Charge::paramf(inst, "grove", 0.0));
	b->props.set("root_t", Charge::paramf(inst, "hit_status_t", 1.2));
	return true;
}

bool roots_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (b.form != Form::Wave) return false;
	if (w.tick % 6 == 0)
		FxEvents::fx(w, "trail", "plant",
		             D({{"actor", b.attack_owner}, {"pos", b.pos}, {"dir", b.wave_dir}, {"length", 2.5}, {"shape", "ground"}, {"move", "root_snare"},
		                {"tier", b.tier}, {"body", b.id}}));
	const double grove = dnum(b.props, "grove", 0.0);
	if (grove > 0.0 && b.wave_budget < 0.5 && !dbool(b.props, "grove_made", false)) {
		b.props.set("grove_made", true);
		const double kg = minf(1.0, b.mass * 0.4);
		b.mass -= kg;
		// the grove's vine is part of the root wave's own booked mass: it moves, nothing new is grown
		MatBody* z = WaterUtil::zone(w, "briar", WaterUtil::ground_at(w, b.pos), grove, b.attack_owner, 4.0,
		                             D({{"actor_status", "rooted"}, {"status_t", dnum(b.props, "root_t", 1.5)}, {"spare_owner", true}, {"height", 2.0},
		                                {"rate", 0.2}, {"dps", 3.0}, {"ground_only", true}}),
		                             Mat::Plant, kg, 12.0);
		z->sub = SUB;
		z->tier = b.tier;
		w.emit("erupt", D({{"body", z->id}, {"at", z->pos}}));
	}
	return false;
}

bool thicket_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double kg = _water(w, a, inst, Charge::paramf(inst, "thicket_kg", 2.0), 0.8);
	if (kg <= 0.0) return true;
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	const double r = Charge::paramf(inst, "radius", 2.4);
	const Vec3 p = WaterUtil::ground_at(w, a.pos + dir * (r * 0.9 + 0.4));
	MatBody* z = plant_zone(w, "briar", p, r, a.id, Charge::paramf(inst, "life", 4.0), kg,
	                        D({{"actor_status", "slowed"}, {"status_t", 0.4}, {"status_mag", 1.0}, {"dps", Charge::paramf(inst, "dps", 1.5)},
	                           {"ground_only", true}, {"spare_owner", true}, {"height", 1.8}, {"rate", 0.15}}),
	                        Charge::paramf(inst, "power", 8.0));
	z->tier = inst.tier();
	Verbs::fx(w, a, inst, "ring", D({{"pos", p}, {"radius", r}, {"body", z->id}, {"dir", dir}}));
	return true;
}

void lattice_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_start(w, a, inst, it);
	_sync_booking(w, inst);
}

void lattice_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	_sync_booking(w, inst);
}

bool roll_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* wall = w.get_body(dint(inst.data, "wall", a.wall_body));
	if (wall == nullptr || !wall->alive || wall->form != Form::Wall || wall->tag != "vine") {
		w.emit("insufficient", D({{"actor", a.id}, {"what", "wall"}, {"move", inst.id}}));
		return true;
	}
	const Vec3 dir = WaterUtil::aim_flat(w, a, inst);
	wall->props.set("slide", D({{"dir", dir}, {"left", 7.0}, {"speed", 8.0}, {"owner", a.id}, {"hit", Dict()}, {"attack_id", w.new_attack_id()},
	                            {"status", "entangled"}, {"status_t", 0.8}, {"damage", 6.0}, {"balance", 18.0}, {"power", 10.0}}));
	wall->props.set("standing", 4.0);
	wall->age = 0.0;
	wall->tier = inst.tier();
	Verbs::fx(w, a, inst, "cast", D({{"body", wall->id}, {"dir", dir}, {"length", 7.0}}));
	return true;
}

bool vine_tick(CombatWorld& w, MatBody& b, double dt) {
	if (b.form != Form::Wall) return false;
	if (b.props.has("slide")) WaterIce::_slide(w, b, dt);
	if (dbool(b.props, "brittle", false) && b.wall_damage < 0.9) b.wall_damage = maxf(b.wall_damage, 0.6);   // any hit crumbles it
	return false;
}

void roots_stance_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel || w.tick % 6 != 0) return;
	MatBody* pd = w.puddle_at(a.pos);
	if (pd != nullptr && pd->phase == Phase::Liquid && a.water_carried < 6.0 && pd->mass > 0.05) {
		const double take = minf(minf(0.8, pd->mass), 6.0 - a.water_carried);
		w.ledger.removed += pd->thermal_energy() * take / maxf(pd->mass, 1e-9);
		pd->mass -= take;
		a.water_carried += take;
		if (pd->mass <= 0.05) {
			const double rest = pd->mass;
			pd->mass = 0.0;
			a.water_carried = minf(6.0, a.water_carried + rest);
			w.ledger.removed += pd->thermal_energy();
			w.remove_body(*pd, "drunk");
		} else {
			pd->update_radius_puddle();
		}
	}
}

void vinegrip_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	// Hook: nothing to grip but a fighter in the cone -> yank them in (once).
	if (inst.phase == ActionPhase::Channel && w.held(a) == nullptr && !dbool(inst.data, "hooked", false) && inst.t >= 0.12) {
		const Vec3 dir = w.aim_dir(a, it);
		const double reach = Charge::paramf(inst, "hook_range", 9.0);
		const int aid = a.id;
		MatBody* body = w.find_body(a, dir, reach, Charge::paramf(inst, "cone", 50.0), [aid](MatBody& b) {
			return b.controller != aid && b.form != Form::Wall && b.form != Form::Pool && b.form != Form::Zone && Interactions::allows(b, "grip_vine");
		});
		if (body == nullptr) {
			for (ActorState* tp : w.actors_in_cone(a, dir, reach, 20.0)) {
				ActorState& t = *tp;
				if (t.health > 0.0 && t.team != a.team && w.los(a.chest(), t.chest())) {
					inst.data.set("hooked", true);
					Vec3 to = a.pos - t.pos;
					to.y = 0.0f;
					if (!Status::immune(t, "pull") && to.length() > 1.5) {
						t.vel += to.normalized() * Charge::paramf(inst, "hook_yank", 7.5);
						w._stagger(t, "light", 0.4, Dict());   // the yank slides them in (a stagger slide: v^2 / 18 = 3 m)
					}
					t.balance = maxf(0.0, t.balance - Charge::paramf(inst, "hook_balance", 20.0));
					t.balance_idle = 0.0;
					Status::apply(w, t, "hooked", 0.8, 1.0, a.id);
					w.emit("hook", D({{"actor", a.id}, {"target", t.id}}));
					FxEvents::fx_for(w, a, inst, "beam", "plant", D({{"pos", a.hand_point()}, {"length", (a.pos - t.pos).length()}, {"dir", dir}}));
					break;
				}
			}
		}
	}
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst) return;
	MatBody* held = w.held(a);
	if (held != nullptr && dbool(inst.data, "shaped", false) && !held->props.has("hit_status")) {
		held->props.set("hit_status", "rooted");   // Wrap: the body roots whoever it hits
		held->props.set("hit_status_t", Charge::paramf(inst, "wrap_t", 1.2));
	}
}

void swing_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Vec3 dir = it.move;
	dir.y = 0.0f;
	dir = dir.length() > 0.2 ? dir.normalized() : a.forward();
	const Anchor anchor = _anchor(w, a, dir, dnum(inst.def, "reach", 9.0));
	double dist = dnum(inst.def, "distance");
	Vec3 steer = dir;
	if (anchor.found) {
		Vec3 to = anchor.point - a.pos;
		to.y = 0.0f;
		dist = maxf(1.5, to.length() - 1.4);
		steer = to.normalized();
		inst.data.set("swing_anchor", anchor.point);
		a.vel.y = anchor.top > 0.5 ? 5.5f : 3.0f;
		a.grounded = false;
	}
	ActorIntent intent;
	intent.move = steer;
	WaterUtil::evade_start(w, a, inst, intent,
	                       D({{"dist", dist}, {"iframes", dnum(inst.def, "iframes")}, {"cost", dnum(inst.def, "cost")}, {"active", dnum(inst.def, "active")}}));
	Verbs::fx(w, a, inst, "beam", D({{"pos", a.hand_point()}, {"dir", steer}, {"length", dist}}));
	w.emit("swing", D({{"actor", a.id}, {"anchored", anchor.found}, {"dist", dist}}));
}

void swing_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) WaterUtil::evade_end(inst);
	Verbs::on_phase(w, a, inst, p);
}

void canopy_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() == &inst && inst.phase == ActionPhase::Channel && inst.t >= Charge::paramf(inst, "hang", 1.5))
		w.set_phase(a, inst, ActionPhase::Recovery);
}

}  // namespace WaterPlant
}  // namespace ff
