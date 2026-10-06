// Fourfold core - port of game/combat/kits/fire/fire_util.gd.
#include "Combat/Kits/Fire/FireUtil.h"

#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Interactions.h"
#include "Sim/Materials.h"
#include "Util/GdUtil.h"

namespace ff {
namespace FireUtil {

bool is_vapor_tag(std::string_view tag) { return in_list(tag, {"fog", "mist", "steam", "steam_screen", "geyser"}); }
bool is_gust_tag(std::string_view tag) { return in_list(tag, {"wind_guard", "gust", "wind_wall"}); }

void with_params(ActionInst& inst, const Dict& over) {
	Dict d = Charge::pdef(inst).duplicate(true);
	for (const auto& kv : over) d.set(kv.first, kv.second);
	const Dict tiers = ddict(d, "tiers");
	for (const auto& tk : tiers) {
		Dict td = tk.second.as_dict();
		for (const auto& kv : over) td.erase(kv.first);
	}
	inst.data.set("spec_def", d);
}

Vec3 aim_ground(CombatWorld& w, const ActorState& a, const ActionInst& inst, double rmax) {
	const Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	Vec3 ap = vvec(inst.data.get("aim_point", a.chest() + dir * rmax), a.chest() + dir * rmax);
	ActorState* t = w.get_actor(a.lock_target);
	if (t != nullptr && !dbool(inst.data, "aim_active", false)) ap = t->pos;
	Vec3 fl(ap.x - a.pos.x, 0.0f, ap.z - a.pos.z);
	if (fl.length() > rmax) fl = fl.normalized() * rmax;
	Vec3 p = a.pos + fl;
	p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y + 0.5));
	return p;
}

double _src_avail(const Agent& src) {
	if (src.body != nullptr && src.body->alive && (src.body->mat == Mat::Fire || src.body->mat == Mat::Air)) return maxf(0.0, src.body->heat_payload);
	return maxf(0.0, src.heat);
}

double transfer(CombatWorld& w, Agent* src, MatBody* dst, double hu, bool boil) {
	if (src == nullptr || dst == nullptr || !dst->alive || hu <= 0.0) return 0.0;
	const double want = minf(hu, _src_avail(*src));
	if (want <= 1e-6) return 0.0;
	double used = 0.0;
	if (boil && dst->is_water() && dst->phase == Phase::Liquid) {
		w.boil_water(*dst, want, dst->pos);   // what the water can't take is booked as ambient inside boil_water
		used = want;
		if (dst->mass <= 0.05 && dst->form != Form::Pool && dst->alive) w.decay_body(*dst, "boiled");
	} else {
		used = w.heat_body(*dst, want);
	}
	if (src->body != nullptr && src->body->alive && (src->body->mat == Mat::Fire || src->body->mat == Mat::Air))
		src->body->heat_payload = maxf(0.0, src->body->heat_payload - used);
	src->heat = maxf(0.0, src->heat - used);
	return used;
}

double melt_need(const MatBody& b, double to_liquid) {
	if (b.is_water())
		return maxf(0.0, -b.thermal_energy()) + b.mass * Sim::WATER_C * maxf(0.0, Sim::WATER_BOIL_C - maxf(b.temp, 0.0));
	if (!Materials::is_fusible(b.mat)) return 0.0;
	const double c = Materials::c(b.mat);
	const double sens = maxf(0.0, Materials::melt(b.mat) - b.temp) * b.mass * c;
	const double lat = maxf(0.0, to_liquid - b.liquid) * b.mass * Materials::latent(b.mat);
	return sens + lat;
}

double pay_into(CombatWorld& w, ActorState& a, MatBody* dst, double hu, bool allow_partial) {
	if (hu <= 0.0 || dst == nullptr || !dst->alive) return 0.0;
	const double paid = w.pay_heat(a, hu, allow_partial);
	if (paid <= 0.0) return 0.0;
	double used = 0.0;
	if (dst->is_water() && dst->phase == Phase::Liquid) {
		w.boil_water(*dst, paid, dst->pos);
		used = paid;
		if (dst->mass <= 0.05 && dst->alive && dst->form != Form::Pool) w.decay_body(*dst, "boiled");
	} else {
		used = w.heat_body(*dst, paid);
	}
	w.ledger.spent += paid - used;
	return used;
}

MatBody* spawn_field(CombatWorld& w, int owner_id, Vec3 pos, double radius, double life, double heat_hu, bool blue, int tier,
                     const std::string& tag) {
	Vec3 p = pos;
	p.y = f32(w.arena.ground_height(p.x, p.z, p.y + 0.5));
	MatBody* z = w.spawn_zone(tag, p, radius, owner_id, 0.0, Mat::Fire, 0.0, life, "fire:" + itos(owner_id));
	z->heat_payload = maxf(0.0, heat_hu);
	z->tier = tier;
	z->sub = blue ? 1 : 0;
	z->props.set("actor_status", "burning");
	z->props.set("status_t", FIELD_STATUS_T);
	z->props.set("status_mag", blue ? 1.5 : 1.0);
	z->props.set("height", 2.2);
	z->props.set("rate", 0.15);
	if (blue) z->props.set("blue", true);
	return z;
}

double field_power(const MatBody& b) { return maxf(0.1, b.heat_payload / Interactions::HU_PER_PU); }

void field_tick(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	const int fid = dint(z.props, "follow", -1);
	if (fid >= 0) {
		MatBody* f = w.get_body(fid);
		if (f == nullptr || !f->alive) {
			w.close_zone(z, "carrier_gone");
			return;
		}
		z.pos = V3(f->pos.x, w.arena.ground_height(f->pos.x, f->pos.z, f->pos.y + 0.5), f->pos.z);
		z.vel = f->vel;
		if (f->form == Form::Zone) {
			z.zone_radius = maxf(z.zone_radius, f->zone_radius * 0.8);
			z.radius = z.zone_radius;
		}
	}
	if (z.heat_payload < 2.0 && z.age > 0.2) w.close_zone(z, "burned_out");
}

std::vector<MatBody*> zones_at(CombatWorld& w, Vec3 p, const std::vector<std::string>& classes, double r) {
	std::vector<MatBody*> out;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.form != Form::Zone) continue;
		const std::string cls = Interactions::classify(b);
		bool match = false;
		for (const std::string& c : classes)
			if (c == cls || c == b.tag) match = true;
		if (!match) continue;
		if (w._in_zone(b, p, r)) out.push_back(&b);
	}
	return out;
}

bool in_vapor(CombatWorld& w, Vec3 p) {
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (!b.alive) continue;
		if (b.form == Form::Zone && (is_vapor_tag(b.tag) || b.mat == Mat::Steam) && w._in_zone(b, p, 0.3)) return true;
		if (b.form == Form::Cloud && (b.mat == Mat::Steam || b.is_water()) && b.pos.distance_to(p) < maxf(b.radius, 1.2) + 0.5) return true;
	}
	return false;
}

bool in_wind(CombatWorld& w, Vec3 p) {
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		const MatBody& b = *w.bodies[i];
		if (!b.alive || b.mat != Mat::Air) continue;
		if (Interactions::classify(b) != "gust") continue;
		if (b.form == Form::Zone) {
			if (w._in_zone(b, p, 0.3)) return true;
		} else if (b.pos.distance_to(p) < b.radius + 1.0) {
			return true;
		}
	}
	return false;
}

ActionRef blast_inst(CombatWorld& w, const std::string& move_id, int tier, int attack_id, int sub) {
	ActionRef fi = std::make_shared<ActionInst>();
	fi->id = move_id;
	fi->def = D({{"counter", D({{"cls", "blast"}})}, {"fx", D({{"mat", "blast"}})}, {"element", E}, {"sub", sub}});
	fi->element = E;
	fi->sub = sub;
	fi->attack_id = attack_id != 0 ? attack_id : w.new_attack_id();
	fi->data.set("tier", tier);
	fi->phase = ActionPhase::Active;
	return fi;
}

Dict detonate(CombatWorld& w, ActorState* a, ActionInst* inst, Vec3 p, const Dict& prm, const std::string& mode, int64_t inrush_tick) {
	Array mods;
	Dict out = D({{"suppressed", false}, {"power", dnum(prm, "power", 8.0)}, {"radius", dnum(prm, "radius", 2.0)}, {"mods", mods}, {"tornado", -1}});
	double power = dnum(prm, "power", 8.0);
	const double heat = dnum(prm, "heat_hu", 0.0);
	const int tier = inst != nullptr ? inst->tier() : dint(prm, "tier", 0);
	const int aid = a != nullptr ? a->id : -1;
	// The blast volume carries its tier and move (rule cells by tier) but its own power (modifiers apply to it).
	ActionRef bi = blast_inst(w, inst != nullptr ? inst->id : dstr(prm, "move", ""), tier, inst != nullptr ? inst->attack_id : 0);
	AgentRef vol = Agent::of_volume(&w, a, bi.get(), "blast", p, Vec3(), D({{"P", power}, {"heat_hu", heat}}));
	vol->power = power;
	vol->tier = tier;
	// 1 vacuum: no air, no blast.
	for (MatBody* z : zones_at(w, p, {"vacuum"})) {
		AgentRef th = Agent::of_body(w, *z);
		IxCtx ctx;
		ctx.site = "detonation";
		const IxResult res = Interactions::resolve(w, *th, *vol, ctx);
		out.set("suppressed", true);
		mods.append("vacuum:" + res.outcome);
		FxEvents::fx(w, "burst", "vacuum",
		             D({{"actor", aid}, {"pos", p}, {"radius", dnum(out, "radius") * 0.4}, {"power", power}, {"shape", "small"}, {"tier", tier}}));
		w.ledger.spent += heat;
		w.emit("blast_suppressed", D({{"actor", aid}, {"pos", p}, {"by", z->id}, {"outcome", res.outcome}}));
		return out;
	}
	// 2 tornado: disrupted by a strong enough blast, or set alight by a fuse.
	for (MatBody* z : zones_at(w, p, {"tornado"})) {
		if (mode == "fuse") {
			MatBody* f = spawn_field(w, aid, z->pos, maxf(1.5, z->zone_radius * 0.8),
			                         maxf(2.0, z->max_life > 0.0 ? z->max_life - z->age : 4.0), heat, false, tier);
			f->props.set("follow", z->id);
			f->props.set("spare_owner", true);
			z->props.set("fire", true);
			z->props.set("infused", "fire");
			out.set("tornado", z->id);
			mods.append("fire_tornado");
			w.emit("infuse", D({{"actor", aid}, {"body", z->id}, {"with", "fire"}, {"field", f->id}}));
			FxEvents::fx(w, "burst", "flame", D({{"actor", aid}, {"pos", p}, {"radius", z->zone_radius}, {"power", power}, {"tier", tier}}));
			return out;
		}
		AgentRef th = Agent::of_body(w, *z);
		IxCtx ctx;
		ctx.site = "detonation";
		const IxResult res2 = Interactions::resolve(w, *th, *vol, ctx);
		mods.append("tornado:" + res2.outcome);
	}
	// 3 modifiers.
	if (w.tick - inrush_tick <= INRUSH_TICKS) {
		power *= INRUSH;
		mods.append("inrush");
	}
	if (in_vapor(w, p)) {
		power *= DAMPEN;
		mods.append("vapor");
	}
	double r = dnum(prm, "radius", 2.0);
	if (in_wind(w, p)) {
		r *= FAN;
		mods.append("wind");
	}
	Dict q = prm.duplicate();
	q.set("power", power);
	q.set("radius", r);
	q.set("cls", "blast");
	q.set("mat", "blast");
	const double k = power / maxf(dnum(prm, "power", 8.0), 0.01);
	q.set("damage", dnum(prm, "damage", 8.0) * k);
	q.set("balance", dnum(prm, "balance", 20.0) * k);
	VerbVolume::burst_at(w, a, bi.get(), p, q);
	out.set("power", power);
	out.set("radius", r);
	return out;
}

}  // namespace FireUtil
}  // namespace ff
