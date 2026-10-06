// Fourfold core - port of the code of game/combat/kits/fire/fire_rules.gd: the channel hooks of fire bodies / currents
// and the "fire_*" outcome handlers (registered by name "FireRules.o_*"). The cells themselves are data (Data/rules.json,
// registration order kept); statuses (grounding, overcharged, kiln_burn) come from hooks.json status_specs.
#include "Combat/Kits/Fire/Fire.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/Outcomes.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

namespace ff {
namespace FireRules {

namespace {
// The interaction event carries a catalogued outcome name (FxEvents.OUTCOMES) and the result material in `to`.
void _report(IxResult& res, const std::string& outcome, const std::string& to = "") {
	res.outcome = outcome;
	if (!to.empty()) res.to = to;
}

// The heat source of a cell: the fire side (a volume with a paid budget, a FIRE body / field) - never a guard.
Agent* _fire_side(Agent& t, Agent& c) {
	for (Agent* g : {&c, &t}) {
		if (g->kind == "guard") continue;
		if (g->body != nullptr && g->body->alive && (g->body->mat == Mat::Fire || (g->body->mat == Mat::Air && g->body->heat_payload > 0.0))) return g;
		if (g->kind == "volume" && g->heat > 0.0) return g;
	}
	return nullptr;
}

MatBody* _other_body(Agent& t, const Agent* src) {
	if (&t != src && t.body != nullptr && t.body->alive) return t.body;
	return nullptr;
}

MatBody* _fire_body(Agent& c) { return c.body != nullptr && c.body->alive && c.body->mat == Mat::Fire ? c.body : nullptr; }

int _aid(const Agent& x) { return x.actor != nullptr ? x.actor->id : -1; }

bool _guard_clean(CombatWorld& w, Agent& c, const Dict& info, IxResult& res, Agent& t) {
	if (c.perfect) {
		w.emit("perfect_deflect", D({{"actor", c.actor->id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", t.cls}}));
		c.actor->last_result = "perfect";
		res.result = "perfect";
	} else {
		res.result = w.guard_chip(*c.actor, info, D({{"chip", 0.0}, {"bal", 0.1}, {"knock", 0.0}, {"kind", "reactive"}}), &t);
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}
}  // namespace

Dict plain_guard(const Dict& extra) {
	Dict r = D({{"bands", A({Value(A({Value(0.0), Value("overwhelm")})), Value(A({Value(0.25), Value("block")}))})},
	            {"full_at", 0.5},
	            {"partial_at", 0.25},
	            {"perfect", "deflect"},
	            {"chip", 0.12},
	            {"bal", 0.55},
	            {"knock", 0.35},
	            {"chip_scale", true},
	            {"perfect_balance", 18.0},
	            {"perfect_range", 3.0}});
	r.merge(extra, true);
	return r;
}

void _fire_channels(CombatWorld& w, MatBody& b, Agent& g) {
	(void)w;
	g.ch.H = maxf(0.0, b.heat_payload) / Interactions::HU_PER_PU;
	g.ch.P = 0.0;
	g.power = FireUtil::field_power(b);
	g.heat = maxf(0.0, b.heat_payload);
	if (dbool(b.props, "blue", false) || b.tag == "comet" || b.tag == "corona") g.power *= 1.5;   // blue fire: x1.5 (MOVESET §7.10)
}

void _current_channels(CombatWorld& w, MatBody& b, Agent& g) {
	(void)w;
	g.ch.E = maxf(b.charge, b.power);
	g.ch.P = 0.0;
	g.power = maxf(0.1, g.ch.E);
}

bool o_heat(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	Agent* src = _fire_side(t, c);
	MatBody* b = src != nullptr ? _other_body(t, src) : nullptr;
	if (b == nullptr && src != nullptr && src == &t) b = c.body;
	if (src == nullptr || b == nullptr) {
		res.pass_scale = 1.0;
		return true;
	}
	const double used = FireUtil::transfer(w, src, b, FireUtil::_src_avail(*src) * dnum(r, "share", 0.3));
	res.heat_used += used;
	res.pass_scale = 1.0;
	_report(res, "heat");
	return true;
}

bool o_evaporate(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	Agent* src = _fire_side(t, c);
	MatBody* b = t.body != nullptr && t.body->is_water() ? t.body : (c.body != nullptr && c.body->is_water() ? c.body : nullptr);
	if (src == nullptr || b == nullptr || !b->alive) return false;
	const double used = FireUtil::transfer(w, src, b, FireUtil::_src_avail(*src) * dnum(r, "share", 0.6));
	res.heat_used += used;
	if (b->alive && b->mass <= 0.1) {
		if (b->form == Form::Zone) w.close_zone(*b, "evaporated");
		else w.decay_body(*b, "evaporated");
	}
	res.pass_scale = 1.0;
	_report(res, "transform", "steam");
	return true;
}

bool o_burn(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	Agent* src = _fire_side(t, c);
	MatBody* b = t.body != nullptr && t.body->mat == Mat::Plant ? t.body : (c.body != nullptr && c.body->mat == Mat::Plant ? c.body : nullptr);
	if (b == nullptr || !b->alive) return false;
	if (src != nullptr) res.heat_used += FireUtil::transfer(w, src, b, FireUtil::_src_avail(*src) * dnum(r, "share", 0.8));
	if (b->alive && b->temp >= vnum(Materials::prop(static_cast<int>(Mat::Plant), "ignite", Value(250.0)), 250.0))
		w.burn_plant(*b, minf(b->mass, dnum(r, "burn_kg", 2.0)));
	res.pass_scale = 1.0;
	_report(res, "transform", "ash");
	return true;
}

bool o_melt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	Agent* src = _fire_side(t, c);
	MatBody* b = t.body;
	if (src == nullptr || b == nullptr || !b->alive || !Materials::is_fusible(b->mat)) return false;
	const double need = FireUtil::melt_need(*b, dnum(r, "to_liquid", 1.0));
	const double used = FireUtil::transfer(w, src, b, need);
	res.heat_used += used;
	if (b->alive && b->liquid > 0.15 && !b->on_ground) {
		const double keep = dnum(r, "keep", 0.35);
		b->vel = V3(b->vel.x * keep, minf(b->vel.y, 1.0), b->vel.z * keep);
		b->gravity_scale = maxf(b->gravity_scale, 1.0);
		w.emit("melt_in_flight", D({{"body", b->id}, {"liquid", b->liquid}, {"by", src->actor != nullptr ? src->actor->id : -1}}));
	}
	res.pass_scale = 1.0;
	res.stopped = false;
	_report(res, "transform", b->is_stone() ? "lava" : "molten_metal");
	return true;
}

bool o_snuffed(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* f = nullptr;
	for (Agent* g : {&c, &t}) {
		if (g->body != nullptr && g->body->alive && (g->body->mat == Mat::Fire || g->body->tag == "fire_field")) {
			f = g->body;
			break;
		}
	}
	if (f == nullptr) {
		if (t.kind == "volume") {
			w.ledger.spent += maxf(0.0, t.heat);   // the volume's unspent heat is lost to the air (booked)
			t.heat = 0.0;
			res.stopped = true;
			res.pass_scale = 0.0;
			_report(res, "extinguish");
			return true;
		}
		return false;
	}
	w.emit("extinguish", D({{"body", f->id}, {"by", f == c.body ? t.cls : c.ccls}}));
	if (f->form == Form::Zone) w.close_zone(*f, "snuffed");
	else w.decay_body(*f, "snuffed");
	res.pass_scale = f == c.body ? dnum(r, "pass", 1.0) : 0.0;
	res.stopped = f == t.body;
	_report(res, "extinguish");
	return true;
}

bool o_dampen(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)t;
	(void)r;
	(void)ctx;
	MatBody* f = _fire_body(c);
	if (f == nullptr) return false;
	const double k = clampf(res.tp / maxf(res.cp_eff, 0.01), 0.0, 1.0);
	const double got = -Thermal::heat(*f, -f->heat_payload * k);
	w.ledger.ambient -= got;
	res.pass_scale = 0.0;
	_report(res, "weaken");
	return true;
}

bool o_fanned(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* f = _fire_body(c);
	if (f == nullptr) return false;
	if (f->props.has("fanned_tick") && f->props.get("fanned_tick").as_int() > w.tick - 30) {
		res.pass_scale = 1.0;
		return true;
	}
	f->props.set("fanned_tick", w.tick);
	const double add = f->heat_payload * (dnum(r, "amp", 1.3) - 1.0);
	f->heat_payload += add;
	w.ledger.generated += add;
	if (f->form == Form::Zone) {
		f->zone_radius = minf(f->zone_radius + 1.0, 6.0);
		f->radius = f->zone_radius;
		if (t.dir.length() > 0.1) {
			f->vel = Vec3(t.dir.x, 0.0f, t.dir.z).normalized() * 1.5;
			f->props.set("drag", 1.0);
		}
	}
	res.pass_scale = 1.0;
	_report(res, "amplify");
	return true;
}

bool o_blown(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* f = _fire_body(c);
	if (f == nullptr) return false;
	Vec3 d = t.dir;
	if (d.length() < 0.1 && t.actor != nullptr) d = f->pos - t.actor->pos;
	d.y = 0.0f;
	if (d.length() > 0.01 && f->form == Form::Zone) {
		f->vel = d.normalized() * 6.0;
		f->props.set("drag", 3.0);
	} else if (d.length() > 0.01) {
		f->vel = d.normalized() * maxf(f->vel.length(), 8.0);
		f->attack_owner = t.actor != nullptr ? t.actor->id : f->attack_owner;
	}
	w.emit("deflect", D({{"actor", _aid(t)}, {"body", f->id}, {"verb", "wind"}, {"kind", "fire"}}));
	res.pass_scale = 1.0;
	_report(res, "deflect");
	return true;
}

bool o_tornado(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* tor = t.body;
	MatBody* f = _fire_body(c);
	if (tor == nullptr || f == nullptr || !tor->alive || f->form != Form::Zone) return false;
	if (dint(f->props, "follow", -1) == tor->id) {
		res.pass_scale = 1.0;
		return true;
	}
	f->props.set("follow", tor->id);
	f->props.set("spare_owner", false);   // neutral hazard: it burns everyone
	f->max_life = maxf(f->max_life, f->age + 3.0);
	tor->props.set("fire", true);
	tor->props.set("infused", "fire");
	w.emit("infuse", D({{"actor", _aid(c)}, {"body", tor->id}, {"with", "fire"}, {"field", f->id}}));
	res.pass_scale = 1.0;
	_report(res, "amplify", "fire_tornado");
	return true;
}

bool o_guard_absorb(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (c.kind != "guard" || c.actor == nullptr) {
		res.pass_scale = 1.0;
		return true;
	}
	const double share = dnum(r, "share", 0.5) * (c.perfect ? dnum(r, "perfect_share", 1.5) : 1.0);
	const double heat = t.kind == "volume" ? t.heat : (t.body != nullptr && t.body->mat == Mat::Fire ? t.body->heat_payload : 0.0);
	const double gain = clampf(heat * share, 0.0, Sim::RESERVE_MAX - c.actor->heat_reserve);
	if (gain > 0.0) {
		if (t.kind == "volume") t.heat -= gain;
		else if (t.body != nullptr) t.body->heat_payload -= gain;
		c.actor->heat_reserve += gain;
		res.absorbed = gain;
	}
	const Dict& info = ctx.info;
	if (c.perfect) {
		w.emit("perfect_deflect", D({{"actor", c.actor->id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", t.cls}}));
		c.actor->last_result = "perfect";
		res.result = "perfect";
	} else {
		res.result = w.guard_chip(*c.actor, info, D({{"chip", 0.0}, {"bal", dnum(r, "bal", 0.15)}, {"knock", 0.0}, {"kind", "heat_sink"}}), &t);
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	w.emit("heat_sink", D({{"actor", c.actor->id}, {"gain", gain}, {"cls", t.cls}}));
	_report(res, "absorb");
	return true;
}

bool o_aegis_melt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mass > dnum(r, "mass_max", 8.0)) return false;
	const double need = FireUtil::melt_need(*b, 1.0);
	double used = 0.0;
	if (c.kind == "guard" && c.actor != nullptr) {
		if (!w.can_pay_heat(*c.actor, need * 0.5)) return false;
		used = FireUtil::pay_into(w, *c.actor, b, need);
	} else if (c.body != nullptr && c.body->alive) {
		used = FireUtil::transfer(w, &c, b, need);
	}
	res.heat_used += used;
	if (b->alive) {
		b->vel *= 0.25;
		b->attack_id = 0;
		b->gravity_scale = 1.0;
	}
	w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", t.cls}, {"to", b->is_water() ? "water" : "molten_metal"}, {"why", "aegis"}}));
	if (c.kind == "guard")
		res.result = w.guard_chip(*c.actor, ctx.info, D({{"chip", 0.0}, {"bal", 0.0}, {"knock", 0.0}, {"kind", "aegis_melt"}}), &t);
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "transform", b->alive && b->is_water() ? "water" : "molten_metal");
	return true;
}

bool o_static(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (c.kind != "guard" || c.actor == nullptr) {
		res.pass_scale = 1.0;
		return true;
	}
	if (ctx.info.empty()) {
		res.pass_scale = 1.0;
		return true;
	}
	// The dedicated electric answer (MOVESET §8.6): the ward drinks up to `store_x` x its power of the bolt; static over
	// the 60 cap is bled into the ground. What it cannot take lands.
	const double e = t.ch.E;
	const double took = minf(e, res.cp_eff * dnum(r, "store_x", 2.5));
	const double share = e > 1e-6 ? took / e : 1.0;
	const double store = minf(took, FireLightning::STATIC_MAX - c.actor->static_charge);
	c.actor->static_charge += maxf(0.0, store);
	res.absorbed = store;
	res.pass_scale = clampf(1.0 - share, 0.0, 1.0);
	res.knock_scale = res.pass_scale;
	res.stopped = res.pass_scale <= 0.0;
	w.emit("static_absorb", D({{"actor", c.actor->id}, {"stored", store}, {"static", c.actor->static_charge}, {"perfect", false}}));
	FxEvents::fx(w, "aura", "lightning",
	             D({{"actor", c.actor->id}, {"pos", c.actor->chest()}, {"power", c.actor->static_charge}, {"on", true}, {"shape", "small"}}));
	_report(res, "absorb");
	return true;
}

bool o_static_full(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	if (c.kind != "guard" || c.actor == nullptr) return false;
	if (ctx.info.empty()) {
		res.pass_scale = 1.0;   // the redirect pre-check of a discharge: the hit itself is answered later
		return true;
	}
	const double e = t.ch.E;
	const double store = minf(e, FireLightning::STATIC_MAX - c.actor->static_charge);
	c.actor->static_charge += maxf(0.0, store);
	res.absorbed = store;
	const Dict& info = ctx.info;
	w.emit("perfect_deflect", D({{"actor", c.actor->id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", "lightning"}}));
	w.emit("static_absorb", D({{"actor", c.actor->id}, {"stored", store}, {"static", c.actor->static_charge}, {"perfect", true}}));
	c.actor->last_result = "perfect";
	res.result = "perfect";
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "absorb");
	return true;
}

bool o_reactive(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (c.kind != "guard" || c.actor == nullptr) return false;
	const Dict info = ctx.info;
	if (!w.spend_focus(*c.actor, FireCombustion::REACTIVE_COST)) {
		w.emit("insufficient", D({{"actor", c.actor->id}, {"what", "focus"}, {"move", "reactive_blast"}}));
		res.result = w.guard_chip(*c.actor, info, plain_guard(), &t, res.ratio);
		res.stopped = true;
		res.pass_scale = 0.0;
		_report(res, "block");
		return true;
	}
	FxEvents::fx(w, "burst", "blast",
	             D({{"actor", c.actor->id}, {"pos", c.actor->chest() + c.actor->forward() * 0.6}, {"radius", 1.6}, {"power", res.cp_eff},
	                {"move", "reactive_blast"}, {"element", FireUtil::E}, {"sub", 3}, {"shape", "small"}}));
	w.emit("reactive_blast", D({{"actor", c.actor->id}, {"cls", t.cls}, {"perfect", c.perfect}}));
	if (t.kind == "body" && t.body != nullptr && t.body->alive) {
		if (t.body->mat == Mat::Fire) return o_snuffed(w, t, c, res, Dict(), ctx) && _guard_clean(w, c, info, res, t);
		if (c.perfect && dstr(r, "perfect_body", "reflect") == "reflect" && t.body->mass <= 30.0) {
			Outcomes::reflect(w, t, c, res, D({{"speed_mult", 1.0}}), ctx);
			_report(res, "reflect");
			return true;
		}
		Outcomes::deflect(w, t, c, res, D({{"side", 0.8}, {"up", 3.0}}), ctx);
		_report(res, "deflect");
		return true;
	}
	// A volume: flames lose their oxygen, gusts and blasts are met by the counter-blast.
	w.ledger.spent += maxf(0.0, t.heat);   // its unspent heat is lost to the air (booked)
	t.heat = 0.0;
	_guard_clean(w, c, info, res, t);
	_report(res, (t.cls == "flame" || t.cls == "blue_fire") ? "extinguish" : "block");
	return true;
}

bool o_counter_blast(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	if (c.kind != "guard" || c.actor == nullptr) return false;
	const Dict info = ctx.info;
	if (!w.spend_focus(*c.actor, FireCombustion::REACTIVE_COST)) return false;
	_guard_clean(w, c, info, res, t);
	ActorState* att = w.get_actor(dint(info, "attacker", -1));
	if (att != nullptr && (att->pos - c.actor->pos).length() < 4.0) {
		Vec3 d = att->pos - c.actor->pos;
		d.y = 0.0f;
		att->vel += d.normalized() * 5.0 + V3(0, 2.0, 0);
		att->balance -= 12.0;
		att->balance_idle = 0.0;
	}
	FxEvents::fx(w, "burst", "blast",
	             D({{"actor", c.actor->id}, {"pos", c.actor->chest()}, {"radius", 2.0}, {"power", res.cp_eff}, {"move", "reactive_blast"},
	                {"element", FireUtil::E}, {"sub", 3}}));
	_report(res, "clash");
	return true;
}

bool o_body_burst(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)t;
	(void)r;
	(void)ctx;
	MatBody* f = _fire_body(c);
	if (f == nullptr) return false;
	if (f->tag == "ember") FireCombustion::pop_ember(w, f, "contact");
	else FireFlame::burst_fire_body(w, f, "clash");
	res.pass_scale = 1.0;
	res.stopped = false;
	_report(res, "clash");
	return true;
}

bool o_charge_body(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive) return false;
	FireLightning::charge_body(w, b, (c.power > 0.0 ? c.power : c.ch.E) * dnum(r, "share", 0.5), _aid(c));
	res.pass_scale = dnum(r, "pass", 1.0);
	_report(res, "conduct");
	return true;
}

bool o_disrupt_zone(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || t.body->form != Form::Zone) return false;
	w.emit("disrupt", D({{"body", t.body->id}, {"by", "blast"}}));
	w.close_zone(*t.body, "disrupted");
	res.pass_scale = 1.0;
	_report(res, "disrupt");
	return true;
}

bool o_fill_void(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	if (t.body == nullptr || !t.body->alive) return false;
	w.emit("fill_void", D({{"body", t.body->id}}));
	if (t.body->form == Form::Zone) w.close_zone(*t.body, "filled");
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "neutralize");
	return true;
}

bool o_suppressed(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)w;
	(void)t;
	(void)r;
	(void)ctx;
	c.heat = 0.0;   // the blast site books the heat it paid (FireCombustion), so nothing is booked here
	res.stopped = true;
	res.pass_scale = 0.0;
	_report(res, "extinguish");
	return true;
}

bool o_fulgurite(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	if (t.body == nullptr || !t.body->alive || t.body->mat != Mat::Sand) return false;
	Outcomes::transform(w, t, c, res, D({{"to", "glass"}}), ctx);
	res.pass_scale = dnum(r, "pass", 0.5);
	res.stopped = false;
	_report(res, "transform", "glass");
	return true;
}

bool o_glassify(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mat != Mat::Sand) return false;
	Agent* src = _fire_side(t, c);
	if (src != nullptr) res.heat_used += FireUtil::transfer(w, src, b, FireUtil::_src_avail(*src) * dnum(r, "share", 0.5));
	if (b->alive && b->mat == Mat::Sand) {
		w.convert_mat(*b, Mat::Glass, "sand_to_glass");
		w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "sand"}, {"to", "glass"}, {"why", "blue"}}));
	}
	res.pass_scale = 1.0;
	_report(res, "transform", "glass");
	return true;
}

bool o_conduct_owner(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)w;
	(void)c;
	(void)ctx;
	res.extra.set("conduct", true);
	res.pass_scale = dnum(r, "factor", 1.0);
	if (t.body != nullptr && t.body->alive && t.body->controller >= 0) res.extra.set("conduct_to", t.body->controller);
	_report(res, "conduct");
	return true;
}

bool o_smother(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) { return o_snuffed(w, t, c, res, r, ctx); }

}  // namespace FireRules
}  // namespace ff
