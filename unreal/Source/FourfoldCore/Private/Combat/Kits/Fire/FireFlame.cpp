// Fourfold core - port of game/combat/kits/fire/fire_flame.gd (Fire / Flame, sub 0; MOVESET §7.9): fireball impacts and
// body behaviour, Fire Line / Fire Ring, the extended Heat Sink of Flame Guard, Backdraft, Ground Heat, Flare Dash and
// Rocket Hop. The legacy flare / blaze / thermal technique stay in ActFire (module "fire").
#include "Combat/Kits/Fire/Fire.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace FireFlame {

namespace {
void _ix(CombatWorld& w, MatBody& b, ActorState& a, const std::string& cls, const std::string& outcome, const std::string& to) {
	w.emit("interaction", D({{"threat", cls}, {"counter", "aura_flame"}, {"outcome", outcome}, {"band", "full"}, {"ratio", 1.5},
	                         {"tp", b.thermal_energy() / 20.0}, {"cp", 15.0}, {"perfect", true}, {"pos", b.pos}, {"dir", b.vel.normalized()},
	                         {"threat_actor", b.attack_owner}, {"counter_actor", a.id}, {"threat_body", b.id}, {"counter_body", -1}, {"to", to},
	                         {"rule", "fire_heat_sink"}, {"tier", b.tier}}));
}

void _fire_ring(CombatWorld& w, ActorState& a, ActionInst& inst, double heat) {
	const Vec3 c = FireUtil::aim_ground(w, a, inst, 11.0);
	const int n = Charge::parami(inst, "ring_n", 6);
	const double r = Charge::paramf(inst, "ring_r", 3.0);
	for (int k = 0; k < n; ++k) {
		const double ang = kTau * static_cast<double>(k) / static_cast<double>(n);
		const Vec3 p = c + V3(std::cos(ang), 0.0, std::sin(ang)) * r;
		MatBody* z = FireUtil::spawn_field(w, a.id, p, 1.25, Charge::paramf(inst, "ring_life", 3.0), heat / static_cast<double>(n), false, inst.tier());
		z->props.set("dps", 4.0);
	}
	Verbs::fx(w, a, inst, "ring", D({{"pos", c}, {"radius", r}, {"power", Charge::counter_power(inst.def, inst.tier())}, {"dur", 3.0}}));
	w.emit("fire_ring", D({{"actor", a.id}, {"pos", c}, {"radius", r}}));
}
}  // namespace

bool fireball_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)what;
	burst_fire_body(w, &b, "impact");
	return true;
}

void burst_fire_body(CombatWorld& w, MatBody* bp, const std::string& why) {
	if (bp == nullptr || !bp->alive) return;
	MatBody& b = *bp;
	ActorState* owner = w.get_actor(b.attack_owner >= 0 ? b.attack_owner : b.residual_owner);
	const Dict d = Moves::defs().get(dstr(b.props, "move", "")).as_dict();
	const int tier = b.tier;
	const bool blue = b.tag == "comet" || dbool(b.props, "blue", false);
	Vec3 p = b.pos;
	const double g = w.arena.ground_height(p.x, p.z, p.y + 0.5);
	if (p.y < g + 0.4) p.y = f32(g + 0.4);
	double heat = b.heat_payload;
	b.heat_payload = 0.0;
	const double fshare = Charge::pgetf(d, tier, "field_share", 0.0);
	if (fshare > 0.0 && heat > 0.0) {
		FireUtil::spawn_field(w, owner != nullptr ? owner->id : -1, p, Charge::pgetf(d, tier, "field_r", 3.0), Charge::pgetf(d, tier, "field_life", 3.0),
		                      heat * fshare, blue, tier);
		heat *= 1.0 - fshare;
	}
	b.props.erase("on_impact");
	b.attack_id = 0;
	VerbVolume::burst_at(w, owner, nullptr, p,
	                     D({{"radius", dnum(b.props, "impact_radius", 2.0)}, {"power", dnum(b.props, "impact_power", 6.0)},
	                        {"damage", dnum(b.props, "impact_damage", 6.0)}, {"balance", 18.0}, {"knock", 4.0}, {"lift", 1.5},
	                        {"cls", blue ? "blue_fire" : "flame"}, {"heat_hu", heat}, {"mat", blue ? "blue" : "flame"}}));
	w.emit("fire_burst", D({{"body", b.id}, {"why", why}, {"blue", blue}, {"pos", p}}));
	w.decay_body(b, "burst");
}

bool fireball_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.mat != Mat::Fire) return false;
	// Fed by wind: released from a wind grip (Air "grip_wind"), the fire takes a breath once.
	if (!dbool(b.props, "fed", false) && b.residual_owner >= 0 && b.controller < 0) {
		ActorState* ra = w.get_actor(b.residual_owner);
		if (ra != nullptr && ra->action != nullptr && dstr(Charge::pdef(*ra->action), "ccls", "") == "grip_wind") {
			b.props.set("fed", true);
			const double add = b.heat_payload * 0.2;
			b.heat_payload += add;
			w.ledger.generated += add;   // fantasy oxygen (like a fanned flame): booked as created heat
			w.emit("fed", D({{"body", b.id}, {"by", ra->id}, {"add", add}, {"payload", b.heat_payload}}));
		}
	}
	// No air, no fire: a vacuum snuffs it. A tornado swallows it and becomes a fire tornado (the heat rides the wind).
	if (!FireUtil::zones_at(w, b.pos, {"vacuum"}, b.radius).empty()) {
		w.emit("extinguish", D({{"body", b.id}, {"by", "vacuum"}}));
		w.decay_body(b, "snuffed");
		return true;
	}
	if (const std::vector<MatBody*> tornadoes = FireUtil::zones_at(w, b.pos, {"tornado"}, b.radius); !tornadoes.empty()) {
		MatBody* tor = tornadoes.front();
		MatBody* f = FireUtil::spawn_field(w, b.attack_owner, tor->pos, maxf(1.5, tor->zone_radius * 0.8),
		                                   maxf(2.5, tor->max_life > 0.0 ? tor->max_life - tor->age : 4.0), b.heat_payload, b.tag == "comet", b.tier);
		b.heat_payload = 0.0;
		f->props.set("follow", tor->id);
		f->props.set("spare_owner", false);
		tor->props.set("fire", true);
		tor->props.set("infused", "fire");
		w.emit("infuse", D({{"actor", b.attack_owner}, {"body", tor->id}, {"with", "fire"}, {"field", f->id}}));
		w.decay_body(b, "infused");
		return true;
	}
	// Quenched by water it touches (the pool, puddles, streams, shields, ice).
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !o.alive || !o.is_water() || o.form == Form::Cloud) continue;
		bool touch = false;
		if (o.form == Form::Pool) touch = w.arena.in_pool(b.pos.x, b.pos.z) && b.pos.y < w.arena.pool_level + b.radius + 0.1;
		else if (o.form == Form::Puddle)
			touch = Vec2(b.pos.x - o.pos.x, b.pos.z - o.pos.z).length() < o.radius && b.pos.y < o.pos.y + b.radius + 0.15;
		else touch = (b.pos - o.pos).length() < b.radius + o.radius + 0.1;
		if (!touch) continue;
		AgentRef src = Agent::of_body(w, b);
		const double used = FireUtil::transfer(w, src.get(), &o, b.heat_payload);
		w.emit("steam_block", D({{"actor", o.controller}, {"body", o.id}, {"fire", b.id}, {"hu", used}}));
		w.emit("interaction", D({{"threat", Interactions::classify(b)}, {"counter", Interactions::counter_class(o, &w)}, {"outcome", "extinguish"},
		                         {"band", "full"}, {"ratio", 1.0}, {"tp", src->ch.H}, {"cp", o.mass}, {"perfect", false}, {"pos", b.pos},
		                         {"dir", b.vel.normalized()}, {"threat_actor", b.attack_owner}, {"counter_actor", o.controller}, {"threat_body", b.id},
		                         {"counter_body", o.id}, {"to", "steam"}, {"rule", "fire_quench"}, {"tier", b.tier}}));
		if (b.heat_payload < 1.0) {
			w.emit("extinguish", D({{"body", b.id}, {"by", "water"}}));
			w.decay_body(b, "quenched");
			return true;
		}
	}
	return false;
}

bool line_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double heat = Verbs::take_heat(inst);
	if (Charge::paramb(inst, "ring", false)) {
		_fire_ring(w, a, inst, heat);
		return true;
	}
	const int n = maxi(1, Charge::parami(inst, "lines", 1));
	const double spread = deg_to_rad(Charge::paramf(inst, "spread", 0.0));
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	dir.y = 0.0f;
	dir = dir.length() > 0.01 ? dir.normalized() : a.forward();
	Array ids;
	for (int k = 0; k < n; ++k) {
		const double ang = n == 1 ? 0.0 : lerpf(-spread * 0.5, spread * 0.5, static_cast<double>(k) / static_cast<double>(n - 1));
		inst.data.set("aim", rotated(dir, Vec3::Up(), ang));
		inst.data.set("heat_paid", heat / static_cast<double>(n));
		MatBody* b = VerbGroundLine::launch(w, a, inst, D({{"source", "heat"}}));
		if (b != nullptr) {
			b->props.set("trail_every", Charge::paramf(inst, "trail_every", 2.2));
			b->props.set("trail_r", Charge::paramf(inst, "trail_r", 1.0));
			b->props.set("trail_life", Charge::paramf(inst, "trail_life", 2.5));
			b->props.set("trail_share", Charge::paramf(inst, "trail_share", 0.18));
			b->props.set("last_trail", b->pos);
			b->charge = 0.0;
			ids.append(b->id);
		}
	}
	inst.data.set("aim", dir);
	inst.data.set("bodies", ids);
	return true;
}

bool line_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.form != Form::Wave) return false;
	const bool wet = w.arena.in_pool(b.pos.x, b.pos.z) && b.pos.y < w.arena.pool_level + 0.4;
	MatBody* pd = w.puddle_at(b.pos);
	if (wet || (pd != nullptr && pd->phase == Phase::Liquid)) {
		MatBody* water = wet ? w.pool : pd;
		AgentRef src = Agent::of_body(w, b);
		FireUtil::transfer(w, src.get(), water, b.heat_payload);
		w.emit("extinguish", D({{"body", b.id}, {"by", "water"}}));
		w.emit("steam", D({{"body", b.id}, {"water", water != nullptr ? water->id : -1}, {"kg", 0.0}}));
		b.attack_id = 0;
		w.decay_body(b, "doused");
		return true;
	}
	const Vec3 last = dvec(b.props, "last_trail", b.pos);
	if (Vec2(b.pos.x - last.x, b.pos.z - last.z).length() >= dnum(b.props, "trail_every", 2.2) && b.heat_payload > 6.0) {
		b.props.set("last_trail", b.pos);
		const double share = b.heat_payload * dnum(b.props, "trail_share", 0.18);
		b.heat_payload -= share;
		MatBody* z = FireUtil::spawn_field(w, b.attack_owner, b.pos, dnum(b.props, "trail_r", 1.0), dnum(b.props, "trail_life", 2.5), share, false, b.tier);
		z->props.set("trail_of", b.id);
	}
	return false;
}

void guard_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel || !w.perfect_guard(a)) return;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& b = *keep;
		if (!b.alive || !b.is_projectile() || b.attack_owner == a.id || dbool(b.props, "heat_sunk", false)) continue;
		const Vec3 rel = a.chest() - b.pos;
		if (rel.length() > HEAT_SINK_REACH + b.radius || b.vel.dot(rel) <= 0.0f) continue;
		const std::string cls = Interactions::classify(b);
		if (cls == "magma" || cls == "hot_rock" || cls == "molten_metal") {
			b.props.set("heat_sunk", true);
			const double room = Sim::RESERVE_MAX - a.heat_reserve;
			const double take = minf(minf(HEAT_SINK_DRAW, room), maxf(0.0, b.thermal_energy()));
			const double got = -Thermal::heat(b, -take);
			a.heat_reserve += got;
			w.emit("heat_sink", D({{"actor", a.id}, {"body", b.id}, {"gain", got}, {"cls", cls}, {"perfect", true}}));
			_ix(w, b, a, cls, "absorb", b.is_stone() ? "hot_rock" : "");
			FxEvents::fx(w, "aura", "flame",
			             D({{"actor", a.id}, {"pos", a.chest()}, {"body", b.id}, {"power", got / 20.0}, {"on", true}, {"shape", "small"}}));
		} else if (cls == "ice") {
			b.props.set("heat_sunk", true);
			const double used = FireUtil::pay_into(w, a, &b, FireUtil::melt_need(b, 1.0) * 0.6);
			w.emit("heat_sink", D({{"actor", a.id}, {"body", b.id}, {"gain", -used}, {"cls", cls}, {"perfect", true}}));
			_ix(w, b, a, cls, "transform", "water");
		}
	}
}

bool backdraft_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double hu = minf(a.heat_reserve, 500.0);
	a.heat_reserve -= hu;
	inst.data.set("heat_paid", dnum(inst.data, "heat_paid", 0.0) + hu);
	const double p = clampf(hu / Interactions::HU_PER_PU, 1.0, 25.0);
	FireUtil::with_params(inst, D({{"power", p}, {"damage", 3.0 + p * 0.9}, {"balance", 8.0 + p * 1.6}, {"knock", 2.0 + p * 0.25}}));
	VerbVolume::cone(w, a, inst);
	w.emit("backdraft", D({{"actor", a.id}, {"hu", hu}, {"power", p}}));
	return true;
}

bool ground_heat_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double r = Charge::paramf(inst, "radius", 2.0);
	int boiled = 0;
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || !b.is_water()) continue;
		const double flat_d = Vec2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length();
		if (flat_d > r + b.radius) continue;
		if (b.form == Form::Puddle || (b.form == Form::Zone && b.tag == "ice_floor") || (b.form == Form::Chunk && b.on_ground)) {
			const double need = FireUtil::melt_need(b) + b.mass * Sim::WATER_LATENT_VAPOR;
			const double from_res = minf(a.heat_reserve, need);
			a.heat_reserve -= from_res;
			const double rest = need - from_res;
			const double paid = from_res + (rest > 0.0 ? w.pay_heat(a, minf(rest, 60.0)) : 0.0);
			double used = b.phase == Phase::Frozen ? w.heat_body(b, paid) : 0.0;
			if (b.alive && b.is_water() && paid - used > 0.0) {
				w.boil_water(b, paid - used, b.pos);
				used = paid;
			}
			w.ledger.spent += paid - used;
			if (b.alive && b.mass <= 0.05) {
				if (b.form == Form::Zone) w.close_zone(b, "boiled");
				else w.decay_body(b, "boiled");
			}
			++boiled;
		}
	}
	const double vented = a.heat_reserve;
	a.heat_reserve = 0.0;
	w.ledger.vented += vented;
	w.emit("vent", D({{"actor", a.id}, {"amount", vented}, {"ground", true}, {"boiled", boiled}}));
	Verbs::fx(w, a, inst, "burst", D({{"pos", a.pos + V3(0, 0.1, 0)}, {"radius", r}, {"power", vented / 20.0}, {"shape", "ground"}}));
	return true;
}

void dash_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Active) return;
	const int n = dint(inst.data, "trail_n", 0);
	if (n >= 3 || inst.t < static_cast<double>(n) * 0.07) return;
	inst.data.set("trail_n", n + 1);
	const double hu = minf(5.0, dnum(inst.data, "heat_paid", 0.0));
	inst.data.set("heat_paid", dnum(inst.data, "heat_paid", 0.0) - hu);
	MatBody* z = FireUtil::spawn_field(w, a.id, a.pos, 0.7, 1.0, hu);
	z->props.set("dps", 2.0);
}

void hop_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() == &inst && inst.phase == ActionPhase::Channel && inst.t >= Charge::paramf(inst, "hover_t", 0.85))
		w.set_phase(a, inst, ActionPhase::Recovery);
	if (a.action.get() == &inst && inst.phase == ActionPhase::Channel && w.tick % 8 == 0)
		Verbs::fx(w, a, inst, "trail", D({{"pos", a.pos}, {"dir", V3(0, -1, 0)}, {"length", 1.2}}));
}

}  // namespace FireFlame
}  // namespace ff
