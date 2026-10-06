// Fourfold core - port of game/combat/kits/air/air_outcomes.gd: custom outcomes of the Gust column (prefix "air_").
//   air_cool    convective cooling of a hot body (booked ambient), then a core outcome per band (rule then_full/partial/fail)
//   air_cut     a crescent cuts vines: a loose vine is halved (the cut half returns to the plant ledger), a vine wall takes damage
//   air_split   a water wave struck by a hurricane splits into two narrower waves fanned apart
//   air_shrink  a partial against a zone (tornado, vacuum): it loses CP_eff of its power and shrinks
//   air_shatter the core shatter, once: fragments are marked (props.shattered) so a sweeping volume does not shatter them again
#include "Combat/Kits/Air/Air.h"

#include "Sim/CombatWorld.h"
#include "Sim/Materials.h"
#include "Sim/Outcomes.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

namespace ff {
namespace AirOutcomes {

void report(IxResult& res, const std::string& outcome, const std::string& to) {
	res.outcome = outcome;
	if (!to.empty()) res.to = to;
}

// Fraction of the threat left after the counter took `absorb` x CP_eff.
double left(const IxResult& res, double absorb) {
	const double tp = res.tp;
	if (tp <= 1e-6) return 0.0;
	return clampf((tp - absorb * res.cp_eff) / tp, 0.0, 1.0);
}

bool o_cool(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b != nullptr && b->alive && b->mat != Mat::Fire && b->thermal_energy() > 0.0) {
		const double want = res.cp_eff * Interactions::HU_PER_PU * dnum(r, "cool", 0.5);
		const double got = -Thermal::heat(*b, -minf(want, b->thermal_energy()));
		w.ledger.ambient -= got;   // convective cooling by wind: booked as ambient
		res.heat_used = res.heat_used + got;
	}
	const std::string then = dstr(r, "then_" + res.band, dstr(r, "then", "deflect"));
	const bool ok = Outcomes::apply(w, then, t, c, res, r, ctx);
	report(res, then);
	return ok;
}

bool o_cut(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mat != Mat::Plant) return false;
	if (b->form == Form::Wall) {
		const double cpw = maxf(1.0, b->mass * Materials::hardness(*b));
		b->wall_damage_add(res.cp_eff / cpw * dnum(r, "wall_k", 0.6));
		w.emit("wall_cut", D({{"body", b->id}, {"by", Outcomes::_id(&c)}, {"damage", b->wall_damage}}));
		if (b->wall_damage >= 1.0) {
			ActorState* owner = w.get_actor(b->last_actor);
			if (owner != nullptr && owner->wall_body == b->id) owner->wall_body = -1;
			w.decay_body(*b, "cut");   // the woven vines fall apart: plant_returned
		}
		res.stopped = false;
		res.pass_scale = 1.0;
	} else {
		MatBody* cut = w.split_body(*b, b->mass * 0.5, b->pos);
		cut->attack_id = 0;
		w.decay_body(*cut, "cut");   // the severed half returns to the plant ledger
		b->attack_id = 0;
		b->vel *= 0.3f;
		res.stopped = true;
		res.pass_scale = 0.0;
	}
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "vine"}, {"to", "cut"}, {"why", "cut"}}));
	report(res, "transform", "cut");
	return true;
}

bool o_split(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->form != Form::Wave || b->mass < 2.0) return false;
	const double ang = deg_to_rad(dnum(r, "angle", 28.0));
	MatBody* child = w.split_body(*b, b->mass * 0.5, b->pos);
	child->form = Form::Wave;
	child->props = b->props.duplicate(true);
	child->wave_dir = rotated(b->wave_dir, Vec3::Up(), ang);
	b->wave_dir = rotated(b->wave_dir, Vec3::Up(), -ang);
	child->wave_budget = b->wave_budget * 0.6;
	b->wave_budget *= 0.6;
	child->wave_width = b->wave_width * 0.7;
	b->wave_width *= 0.7;
	child->wave_path.assign(1, child->pos);
	child->max_life = -1.0;
	child->attack_id = w.new_attack_id();
	child->attack_owner = b->attack_owner;
	child->hit_set = b->hit_set;
	child->damage = b->damage * 0.6;
	child->balance_damage = b->balance_damage * 0.6;
	child->vel = child->wave_dir * b->vel.length();
	b->damage *= 0.6;
	b->balance_damage *= 0.6;
	res.stopped = false;
	res.pass_scale = 0.6;
	report(res, "weaken");
	w.emit("split_wave", D({{"body", b->id}, {"child", child->id}}));
	return true;
}

bool o_shrink(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)w;
	(void)c;
	(void)r;
	(void)ctx;
	const double f = left(res);
	res.pass_scale = f;
	res.stopped = false;
	MatBody* z = t.body;
	if (z != nullptr && z->alive && (z->form == Form::Zone || z->form == Form::Cloud)) {
		z->power *= f;
		if (z->zone_radius > 0.0) {
			z->zone_radius = maxf(0.8, z->zone_radius * std::sqrt(maxf(f, 0.05)));
			z->radius = z->zone_radius;
		}
	}
	report(res, "weaken");
	return true;
}

bool o_shatter(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) return Outcomes::shatter(w, t, c, res, r, ctx);
	if (dbool(b->props, "shattered", false)) return false;   // fragments are not shattered again
	const size_t n0 = w.bodies.size();
	BodyRef keep = b->shared_from_this();
	const bool ok = Outcomes::shatter(w, t, c, res, r, ctx);
	b->props.set("shattered", true);
	b->gravity_scale = 1.0;   // a stone that was flying (gravity 0) falls once it is broken
	for (size_t i = n0; i < w.bodies.size(); ++i) {
		w.bodies[i]->props.set("shattered", true);
		w.bodies[i]->gravity_scale = 1.0;
	}
	report(res, "shatter");
	return ok;
}

}  // namespace AirOutcomes
}  // namespace ff
