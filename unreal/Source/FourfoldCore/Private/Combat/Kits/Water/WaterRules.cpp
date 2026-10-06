// Fourfold core - port of the code of game/combat/kits/water/water_rules.gd: the "water_*" outcome handlers (registered by
// name "WaterRules.o_*"), the ridge maker and the fog / ice floor / steam zone effects. The cells are data
// (Data/rules.json); statuses (fogbound, scalded, skating ...) come from hooks.json status_specs.
#include "Combat/Kits/Water/Water.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Verbs.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/Outcomes.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace WaterRules {

namespace {
void _report(IxResult& res, const std::string& outcome, const std::string& to = "") {
	res.outcome = outcome;
	if (!to.empty()) res.to = to;
}
const Dict& no_chip() {
	static const Dict d = D({{"chip", 0.0}, {"bal", 0.0}, {"knock", 0.0}});
	return d;
}
}  // namespace

bool o_carry(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (t.body == nullptr || c.body == nullptr || !t.body->alive || !c.body->alive) return false;
	MatBody& b = *t.body;
	MatBody& wave = *c.body;
	if (wave.form != Form::Wave) return false;
	if (dint(b.props, "no_carry_until", -1) > w.tick) {
		res.pass_scale = 1.0;
		return true;
	}
	const double spd = dnum(r, "release_speed", 14.0);
	const bool cap = Outcomes::capture(w, t, c, res, D({{"max_captured", dint(r, "max_captured", 4)}, {"release_speed", spd}}), ctx);
	if (!cap) return false;
	b.props.set("release_damage", dnum(r, "release_damage", 12.0));
	b.props.set("carried_by", wave.id);
	b.props.set("carry_from", b.attack_owner);
	_report(res, "capture");
	return true;
}

bool o_ridge(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || !b->is_water() || b->form != Form::Wave) return false;
	make_ridge(w, *b, c.actor, dnum(r, "stand", 6.0));
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "transform", "ridge");
	return true;
}

void make_ridge(CombatWorld& w, MatBody& b, ActorState* owner, double stand) {
	const Vec3 dir = b.wave_dir.length() > 0.01 ? b.wave_dir : Vec3(0.0f, 0.0f, -1.0f);
	WaterUtil::freeze_body(w, &b);
	w.release_captured(b);
	b.form = Form::Wall;
	b.tag = "ridge";
	b.wall_yaw = std::atan2(static_cast<double>(dir.x), static_cast<double>(dir.z)) + kPi * 0.5;
	b.wall_half = V3(maxf(b.wave_width * 0.5, 1.0), 0.75, 0.32);
	b.wall_rise = 0.0;
	b.wall_damage = 0.0;
	b.static_body = true;
	b.attack_id = 0;
	b.vel = Vec3();
	b.wave_path.clear();
	b.hardness = 0.44;
	b.props.set("rise_time", 0.22);
	b.props.set("standing", stand);
	b.props.set("source", "moisture");
	b.max_life = -1.0;
	b.age = 0.0;
	b.touch(owner != nullptr ? owner->id : -1, "wall", w.tick);
	b.pos.y = f32(w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3));
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "wave"}, {"to", "ridge"}, {"why", "frozen"}}));
	w.emit("wall", D({{"actor", owner != nullptr ? owner->id : -1}, {"body", b.id}, {"tag", "ridge"}}));
}

bool o_freeze(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) {
		// A water volume (jet, lash, spray): the heat leaves and the volume stops.
		res.stopped = true;
		res.pass_scale = 0.0;
		_report(res, "transform", "ice");
		return t.kind == "volume";
	}
	if (!WaterUtil::freeze_any(w, b)) return false;
	_report(res, "transform", "ice");
	if (b->form == Form::Stream || b->form == Form::Blob) {
		b->form = Form::Shard;
		b->update_radius();
	}
	b->attack_id = 0;
	b->vel = dbool(r, "drop", true) ? V3(0, -1.0, 0) : b->vel * 0.2;
	b->max_life = Sim::REMNANT_LIFETIME;
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", t.cls}, {"to", "ice"}, {"why", "frozen"}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	if (c.kind == "guard") res.result = w.guard_chip(*c.actor, ctx.info, no_chip(), &t);
	return true;
}

bool o_skin(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || c.actor == nullptr) {
		if (t.kind == "volume") {
			res.stopped = true;
			res.pass_scale = 0.0;
			w.ledger.spent += maxf(0.0, t.heat);   // the volume's heat leaves with it (booked)
			t.heat = 0.0;
			_report(res, "absorb");
			return true;
		}
		return false;
	}
	if (!(b->is_water() || b->mat == Mat::Steam) || b->form == Form::Pool) return false;
	WaterUtil::absorb_into_skin(w, c.actor, b);
	_report(res, "absorb");
	res.stopped = true;
	res.pass_scale = 0.0;
	if (c.kind == "guard") res.result = w.guard_chip(*c.actor, ctx.info, no_chip(), &t);
	return true;
}

bool o_hot_block(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* cb = c.body;
	if (cb != nullptr && cb->alive && (cb->is_water() || cb->mat == Mat::Plant)) {
		if (t.body != nullptr && t.body->alive) {
			const double hu = minf(dnum(r, "hu", 150.0), maxf(0.0, t.body->thermal_energy() - t.body->heat_payload));
			WaterUtil::transfer_heat(w, t.body, cb, hu);
		} else if (t.heat > 0.0) {
			const double used = w.heat_body(*cb, t.heat * dnum(r, "heat_share", 0.6));
			t.heat -= used;
			res.heat_used += used;
		}
	}
	_report(res, "block");
	if (t.kind == "body" && t.body != nullptr) return Outcomes::block(w, t, c, res, r, ctx);
	res.stopped = true;
	res.pass_scale = 0.0;
	if (c.kind == "guard") res.result = w.guard_chip(*c.actor, ctx.info, r, &t);
	return true;
}

bool o_dampen(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)ctx;
	const double k = dnum(r, "k", 0.5);
	_report(res, "weaken");
	res.pass_scale = 1.0 - k;
	res.stopped = false;
	MatBody* b = t.body;
	if (b != nullptr && b->alive) {
		b->vel *= 1.0 - k * 0.5;
		if (b->heat_payload > 0.0) {
			const double lost = b->heat_payload * k;
			b->heat_payload -= lost;
			w.ledger.ambient -= lost;
		} else if (b->thermal_energy() > 0.0 && b->mat != Mat::Water) {
			const double got = -Thermal::heat(*b, -b->thermal_energy() * k * 0.5);
			w.ledger.ambient -= got;
		}
	} else if (t.heat > 0.0) {
		const double lost2 = t.heat * k;
		t.heat -= lost2;
		w.ledger.spent += lost2;
	}
	return true;
}

bool o_condense_in(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	MatBody* cb = c.body;
	if (b != nullptr && b->alive && cb != nullptr && cb->alive && cb->is_water() && cb->form != Form::Pool && cb != b) {
		const double kg = b->mass;
		const double e = cb->thermal_energy();
		w.ledger.removed += b->thermal_energy();
		b->mass = 0.0;
		cb->liquid = (cb->liquid * cb->mass + kg) / (cb->mass + kg);
		cb->mass += kg;
		w._set_energy(*cb, e);
		cb->update_radius();
		if (b->form == Form::Zone) w.close_zone(*b, "condensed");
		else w.remove_body(*b, "condensed");
		res.stopped = true;
		res.pass_scale = 0.0;
		_report(res, "absorb");
		return true;
	}
	if (b == nullptr) {
		if (cb != nullptr && cb->alive && t.heat > 0.0) {
			const double used = w.heat_body(*cb, t.heat * dnum(r, "heat_share", 0.4));
			t.heat -= used;
			res.heat_used += used;
		}
		res.stopped = true;
		res.pass_scale = 0.0;
		_report(res, "absorb");
		if (c.kind == "guard") res.result = w.guard_chip(*c.actor, ctx.info, no_chip(), &t);
		return true;
	}
	return false;
}

bool o_brittle(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mat != Mat::Plant) return false;
	b->props.set("brittle", true);
	b->hardness = 0.08;
	const double e0 = b->thermal_energy();
	b->temp = minf(b->temp, 0.0);   // frost: the vine is cold now; the heat it lost goes to the environment (booked)
	w.ledger.freeze_dump += b->thermal_energy() - e0;
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "vine"}, {"to", "brittle"}, {"why", "frozen"}}));
	_report(res, "transform", "brittle");
	res.pass_scale = 1.0 - dnum(r, "slow", 0.0);
	return true;
}

bool o_feed(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	MatBody* cb = c.body;
	if (b == nullptr || !b->alive || cb == nullptr || !cb->alive || !b->is_water() || b->form == Form::Pool) {
		if (t.kind == "volume") {
			res.stopped = true;
			res.pass_scale = 0.0;
			return true;
		}
		return false;
	}
	const double take = minf(b->mass, dnum(r, "max_kg", 8.0));
	w.ledger.removed += b->thermal_energy() * take / maxf(b->mass, 1e-9);
	b->mass -= take;
	w.mass_ledger.water_to_plant += take;
	const double e = cb->thermal_energy();
	cb->mass += take;
	if (cb->props.has("plant_seen")) cb->props.set("plant_seen", dnum(cb->props, "plant_seen") + take);   // booked right here
	w._set_energy(*cb, e);
	if (cb->form != Form::Wall) cb->update_radius();
	if (b->mass <= 0.05) {
		w.decay_body(*b, "drunk");
	} else {
		b->update_radius();
		b->vel *= 0.3;
		b->attack_id = 0;
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "absorb");
	if (c.kind == "guard") res.result = w.guard_chip(*c.actor, ctx.info, no_chip(), &t);
	return true;
}

bool o_drown(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	Agent blank;
	const bool ok = Outcomes::neutralize(w, t, blank, res, r, ctx);
	_report(res, "neutralize");
	return ok;
}

bool o_melt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || !b->is_water() || b->phase != Phase::Frozen) return false;
	const double hu = dnum(r, "hu", 40.0);
	const double used = w.heat_body(*b, hu);
	w.ledger.generated += used;   // the screen's own steam heat (paid by its caster as spent heat) melts the ice
	res.heat_used += used;
	res.pass_scale = 0.8;
	_report(res, "transform", "water");
	return true;
}

bool o_quench(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	MatBody* cb = c.body;
	if (b == nullptr || !b->alive || cb == nullptr || !cb->alive || !cb->is_water()) return false;
	const double hu = minf(res.cp_eff * Interactions::HU_PER_PU * dnum(r, "share", 1.0), maxf(0.0, b->thermal_energy() - b->heat_payload));
	const double used = WaterUtil::transfer_heat(w, b, cb, hu);
	res.heat_used += used;
	const std::string to = dstr(r, "to", "rock");
	if (b->alive && b->liquid <= 1e-6 && Materials::is_fusible(b->mat)) {
		if (to == "obsidian") b->tag = "obsidian";
		_report(res, "transform", to);
	} else {
		_report(res, "weaken");
	}
	res.stopped = false;
	res.pass_scale = b->liquid > 0.0 ? 1.0 : 0.0;
	return true;
}

bool o_burn(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* cb = c.body;
	if (cb == nullptr || !cb->alive || cb->mat != Mat::Plant) return false;
	const double tp = res.tp;
	const double kg = clampf(dnum(r, "burn_kg", 3.0) * (1.0 + tp / 20.0), 0.5, cb->mass);
	w.burn_plant(*cb, kg);
	if (cb->alive && cb->form == Form::Wall) {
		cb->wall_damage_add(dnum(r, "wall_damage", 0.12));
		if (cb->wall_damage >= 1.0) w._crumble_wall(*cb);
	}
	MatBody* b = t.body;
	if (b != nullptr && b->alive) {
		if (b->heat_payload > 0.0) {
			const double lost = b->heat_payload * dnum(r, "k", 0.15);
			b->heat_payload -= lost;
			w.ledger.ambient -= lost;
		}
	} else if (t.heat > 0.0) {
		const double lost2 = t.heat * dnum(r, "k", 0.15);
		t.heat -= lost2;
		w.ledger.ambient -= lost2;   // heat spent burning the lattice (booked like the body branch)
	}
	res.pass_scale = dnum(r, "pass", 0.8);
	res.stopped = false;
	_report(res, "transform", "ash");
	return true;
}

bool o_sling(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || c.actor == nullptr) return false;
	ActorState* tgt = w.get_actor(b->attack_owner);
	ActorState* ca = c.actor;
	const double spd = maxf(Vec2(b->vel.x, b->vel.z).length() * dnum(r, "speed_mult", 0.9), dnum(r, "min_speed", 12.0));
	b->vel = Verbs::launch_vel(b->pos, tgt != nullptr && tgt != ca ? tgt->chest() : ca->chest() + ca->forward() * 8.0, spd, 0.6);
	b->attack_id = w.new_attack_id();
	b->attack_owner = ca->id;
	b->hit_set.clear();
	b->hit_set.add(ca->id);
	b->residual_owner = ca->id;
	b->residual_authority = Interactions::cohesion(b->tier);
	b->touch(ca->id, "sling", w.tick);
	res.stopped = true;
	res.pass_scale = 0.0;
	res.result = "perfect";
	_report(res, "redirect");
	w.emit("perfect_deflect", D({{"actor", ca->id}, {"body", b->id}, {"verb", "sling"}, {"kind", FxEvents::mat_of(*b)}}));
	return true;
}

void fog_zone(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (w.tick % 4 != 0) return;
	for (ActorState* a : w.actors_in_zone(z)) Status::apply(w, *a, "fogbound", 0.3, 1.0, z.owner);
}

void ice_floor_zone(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (w.tick % 3 != 0) return;
	for (ActorState* a : w.actors_in_zone(z))
		if (a->id == z.owner && !dbool(z.props, "skate", false)) Status::apply(w, *a, "icegrip", 0.12, 1.0, z.owner);
}

void steam_zone(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (w.tick % 6 != 0) return;
	for (ActorState* a : w.actors_in_zone(z))
		if (a->id != z.owner && !dbool(z.props, "harmless", false)) Status::apply(w, *a, "scalded", 0.4, 1.0, z.owner);
}

}  // namespace WaterRules
}  // namespace ff
