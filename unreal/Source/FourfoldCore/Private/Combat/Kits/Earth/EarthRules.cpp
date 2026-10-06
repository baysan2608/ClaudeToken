// Fourfold core - port of the outcome handlers of game/combat/kits/earth/earth_rules.gd (the Earth column of the counter
// matrix). The cells themselves are data (Data/rules.json, registration order kept); handlers are resolved by name
// ("EarthRules._o_*") through Hooks. Every heat / mass change goes through the CombatWorld ledgers.
#include "Combat/Kits/Earth/Earth.h"

#include "Combat/Kits/Earth/KitEarth.h"
#include "Combat/Verbs.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/Outcomes.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

namespace ff {
namespace EarthRules {

namespace {
int _aid(const Agent& x) { return x.actor != nullptr ? x.actor->id : -1; }
}  // namespace

void _stop_body(MatBody& b) {
	b.vel = Vec3();
	b.attack_id = 0;
	b.on_ground = false;
}

double _left(const IxResult& res) {
	const double tp = res.tp;
	if (tp <= 1e-6) return 0.0;
	return clampf((tp - res.cp_eff) / tp, 0.0, 1.0);
}

void _to_glass(CombatWorld& w, MatBody& b, const std::string& why) {
	if (b.mat != Mat::Sand) return;
	w.convert_mat(b, Mat::Glass, "sand_to_glass");
	if (b.form == Form::Wall) {
		b.tag = "glass";
		b.props.erase("mud");
		b.props.erase("cp_bonus");
		b.hardness = -1.0;
	}
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "sand"}, {"to", "glass"}, {"why", why}}));
}

bool _o_embed(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || c.body == nullptr || t.body->mat != Mat::Metal) return false;
	MatBody& b = *t.body;
	_stop_body(b);
	b.static_body = true;
	b.gravity_scale = 0.0;
	b.tag = "rod";
	b.props.set("embedded_in", c.body->id);
	b.props.erase("on_impact");
	b.max_life = Sim::REMNANT_LIFETIME;
	c.body->wall_damage_add(b.mass * 4.0 / 900.0);
	w.emit("stick", D({{"body", b.id}, {"on", "wall"}, {"wall", c.body->id}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_stick(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || c.body == nullptr || !c.body->alive) return false;
	MatBody& b = *t.body;
	MatBody& wall = *c.body;
	const double hu = minf(dnum(r, "hu", 60.0), wall.heat_payload);
	if (hu > 0.0) {
		wall.heat_payload -= hu;
		const double used = w.heat_body(b, hu);
		wall.heat_payload += hu - used;
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	if (b.mat == wall.mat && b.mass <= 20.0 + 1e-6) {
		w.emit("stick", D({{"body", b.id}, {"on", "wall"}, {"wall", wall.id}}));
		b.attack_id = 0;
		w.merge_bodies(wall, b);
		if (c.perfect) wall.props.set("cp_bonus", dnum(wall.props, "cp_bonus", 0.0) + 5.0);   // the wall hardens
		return true;
	}
	_stop_body(b);
	b.vel = V3(0, -1.0, 0);
	w.emit("stick", D({{"body", b.id}, {"on", "wall"}, {"wall", wall.id}}));
	return true;
}

bool _o_face_heat(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || c.body == nullptr) return false;
	const double hu = minf(dnum(r, "hu", 120.0), c.body->heat_payload);
	c.body->heat_payload -= hu;
	const double used = w.heat_body(*t.body, hu);
	c.body->heat_payload += hu - used;
	if (t.body->alive) _stop_body(*t.body);
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_absorb_face(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || c.body == nullptr || !c.body->alive || t.body->mat != c.body->mat) return false;
	MatBody& b = *t.body;
	const double e = b.thermal_energy() - b.heat_payload;
	const double got = -Thermal::heat(b, -maxf(0.0, e));
	c.body->heat_payload += got;
	b.attack_id = 0;
	if (b.form == Form::Wave) w.emit("wave_blocked", D({{"body", b.id}, {"at", b.pos}}));
	w.emit("absorb", D({{"body", b.id}, {"into", c.body->id}, {"hu", got}}));
	w.merge_bodies(*c.body, b);
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_feed_face(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)w;
	(void)ctx;
	if (c.body == nullptr || !c.body->alive) return false;
	res.stopped = true;
	res.pass_scale = 0.0;
	double hu = 0.0;
	if (t.body != nullptr && t.body->alive && t.body->mat == Mat::Fire) {
		hu = t.body->heat_payload * dnum(r, "share", 1.0);
		t.body->heat_payload -= hu;
	} else if (t.heat > 0.0) {
		hu = t.heat * dnum(r, "share", 1.0);
		t.heat -= hu;
	}
	c.body->heat_payload += hu;
	res.heat_used += hu;
	return true;
}

bool _o_set(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* wall = c.body;
	if (wall == nullptr || !wall->alive) return false;
	MatBody* water = t.body;
	const double hu = wall->heat_payload;
	if (water != nullptr && water->alive && water->is_water() && hu > 0.0) {
		wall->heat_payload = 0.0;
		const double kg = w.boil_water(*water, hu, wall->pos + V3(0, 0.8, 0));
		w.emit("steam", D({{"body", wall->id}, {"water", water->id}, {"kg", kg}}));
		if (water->alive && water->mass <= 0.05) {
			w.decay_body(*water, "boiled");
		} else if (water->alive && water->form != Form::Puddle && water->form != Form::Pool) {
			_stop_body(*water);
			if (water->form == Form::Wave) w._settle_wave(*water, "blocked");
		}
	}
	if (!dtruthy(wall->props, "set")) {
		wall->props.set("set", true);
		wall->hardness = 0.38;
		w.emit("transform", D({{"body", wall->id}, {"at", wall->pos}, {"from", "molten_face"}, {"to", "obsidian"}, {"why", "quench"}}));
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_glass_beads(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	if (t.body == nullptr || !t.body->alive || t.body->mat != Mat::Sand) return false;
	MatBody& b = *t.body;
	const double hu = minf(c.body != nullptr ? c.body->heat_payload : 0.0, 40.0);
	if (c.body != nullptr) c.body->heat_payload -= hu;
	const double used = w.heat_body(b, hu);
	if (c.body != nullptr) c.body->heat_payload += hu - used;
	w.convert_mat(b, Mat::Glass, "sand_to_glass");
	_stop_body(b);
	b.vel = V3(0, -1.0, 0);
	w.emit("transform", D({{"body", b.id}, {"at", b.pos}, {"from", "sand"}, {"to", "glass"}, {"why", "molten_face"}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_crust(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* lava = t.body;
	MatBody* sand = c.body;
	if (dstr(r, "target", "") == "counter") {
		lava = c.body;
		sand = t.body;
	}
	if (lava == nullptr || !lava->alive || !lava->is_stone()) return false;
	double want = res.cp_eff * Interactions::HU_PER_PU * dnum(r, "crust", 1.0);
	want = minf(want, maxf(0.0, lava->thermal_energy() - lava->heat_payload));
	const double got = -Thermal::heat(*lava, -want);
	if (sand != nullptr && sand->alive && sand != lava && sand->mass > 0.0 && Materials::is_fusible(sand->mat)) {
		const double used = w.heat_body(*sand, got);
		w.ledger.ambient -= got - used;
	} else {
		w.ledger.ambient -= got;
	}
	res.heat_used += got;
	w.emit("transform", D({{"body", lava->id}, {"at", lava->pos}, {"from", "lava"}, {"to", "crust"}, {"why", "sand"}}));
	const bool stops = dbool(r, "stops", false) && res.band == "full";
	if (lava->form == Form::Wave) {
		if (stops || lava->liquid <= 0.05) {
			w.emit("wave_blocked", D({{"body", lava->id}, {"at", lava->pos}}));
			w._settle_wave(*lava, "crusted");
		} else {
			lava->wave_budget *= 0.6;
		}
	} else if (stops && lava == t.body) {
		_stop_body(*lava);
	}
	res.stopped = stops;
	res.pass_scale = stops ? 0.0 : _left(res);
	return true;
}

bool _o_mud(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* sand = nullptr;
	MatBody* water = nullptr;
	for (MatBody* b : {t.body, c.body}) {
		if (b == nullptr || !b->alive) continue;
		if (b->mat == Mat::Sand && sand == nullptr) sand = b;
		else if (b->is_water() && water == nullptr) water = b;
	}
	if (sand == nullptr) return false;
	if (!dtruthy(sand->props, "mud")) {
		sand->props.set("mud", true);
		sand->props.set("wet", true);
		sand->props.set("mud_tick", w.tick);
		if (sand->form == Form::Wall) {
			sand->tag = "mud";
			sand->props.set("cp_bonus", dnum(sand->props, "cp_bonus", 0.0) + 5.0);
		} else if (sand->form == Form::Zone) {
			sand->power *= 1.3;
			if (sand->max_life > 0.0) sand->max_life += 3.0;
		} else if (sand->form == Form::Wave) {
			sand->props.set("speed", dnum(sand->props, "speed", 9.0) * 0.75);
			sand->power += 5.0;
		}
		w.emit("transform", D({{"body", sand->id}, {"at", sand->pos}, {"from", "sand"}, {"to", "mud"}, {"why", "water"}}));
	}
	if (water != nullptr && water->form != Form::Pool) {
		w.mass_ledger.evaporated += water->mass;
		w.ledger.removed += water->thermal_energy();
		water->mass = 0.0;
		w.remove_body(*water, "soaked");
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_glassify(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	if (c.body == nullptr || !c.body->alive) return false;
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.heat > 0.0) {
		// Outcomes.move_heat takes the heat from the threat body's real energy (or a volume's budget).
		const double used = Outcomes::move_heat(w, &t, *c.body, t.heat * dnum(r, "heat_share", 0.5));
		res.heat_used += used;
	}
	_to_glass(w, *c.body, "blue_fire");
	return true;
}

bool _o_glass_ground(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)t;
	(void)r;
	(void)ctx;
	res.pass_scale = 0.0;
	res.stopped = true;
	w.emit("grounded", D({{"actor", _aid(c)}}));
	if (c.body != nullptr && c.body->alive) _to_glass(w, *c.body, "lightning");
	return true;
}

bool _o_plate_heat(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* plate = c.body;
	const double share = dnum(r, "share", 0.5);
	if (plate != nullptr && plate->alive) {
		if (t.body != nullptr && t.body->alive && t.body->mat != Mat::Fire) {
			const double take = maxf(0.0, t.body->thermal_energy()) * share;
			const double got = -Thermal::heat(*t.body, -take);
			const double used = w.heat_body(*plate, got);
			w.ledger.ambient -= got - used;
		} else if (t.body != nullptr && t.body->alive) {
			const double hp = t.body->heat_payload * share;
			t.body->heat_payload -= hp;
			const double used2 = w.heat_body(*plate, hp);
			w.ledger.ambient -= hp - used2;
		} else if (t.heat > 0.0) {
			const double used3 = w.heat_body(*plate, t.heat * share);
			t.heat -= used3;
			res.heat_used += used3;
		}
	}
	if (dtruthy(r, "melts")) {
		res.pass_scale = 0.6;
		res.stopped = false;
		res.knock_scale = 0.6;
		return true;
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	if (c.kind == "guard" && c.actor != nullptr) {
		res.result = w.guard_chip(*c.actor, ctx.info, r, &t);
	} else if (t.body != nullptr && t.body->alive && t.body->mat != Mat::Fire) {
		t.body->vel = -t.body->vel * 0.12 + V3(0, 1.0, 0);
		t.body->attack_id = 0;
	}
	return true;
}

bool _o_plate_bolt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	ActorState* a = c.actor;
	if (a == nullptr) return false;
	const std::string& s = a->surface;
	if (a->grounded && (s == "stone" || (begins_with(s, "zone:") && s.find("ice") == std::string::npos))) {
		res.stopped = true;
		res.pass_scale = 0.0;
		w.emit("grounded", D({{"actor", a->id}, {"via", "plate"}}));
		if (c.kind == "guard") {
			const Dict rr = D({{"chip", 0.0}, {"bal", 0.0}, {"knock", 0.0}, {"kind", "grounded"}});
			res.result = w.guard_chip(*a, ctx.info, rr, &t);
		}
		return true;
	}
	res.pass_scale = dnum(r, "conduct_mult", 1.2);
	res.stopped = false;
	res.extra.set("conduct", true);
	w.emit("conduct", D({{"actor", _aid(t)},
	                     {"nodes", A({Value("plate:" + itos(c.body != nullptr ? c.body->id : -1))})},
	                     {"victims", A({Value(a->id)})}}));
	return true;
}

bool _o_magnet_catch(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	ActorState* a = c.actor;
	if (a == nullptr || t.body == nullptr || !t.body->alive || t.body->mat != Mat::Metal) return false;
	MatBody* b = t.body;
	const int bid = b->id;
	EarthMetal::to_satchel(w, *a, b, "caught");
	w.emit("perfect_deflect", D({{"actor", a->id}, {"body", bid}, {"verb", "magnet"}, {"kind", "metal"}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	res.result = "perfect";
	return true;
}

bool _o_rod_ground(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	res.stopped = true;
	res.pass_scale = 0.0;
	MatBody* rod = c.body;
	w.emit("grounded", D({{"actor", _aid(c)}, {"via", "rod"}, {"body", rod != nullptr ? rod->id : -1}}));
	if (rod != nullptr && rod->alive) {
		rod->props.set("struck", dnum(rod->props, "struck", 0.0) + t.ch.E);
		FxEvents::fx(w, "burst", "lightning",
		             D({{"actor", _aid(c)}, {"body", rod->id}, {"pos", rod->pos + V3(0, 1.2, 0)}, {"radius", 0.8}, {"power", t.ch.E}, {"shape", "ground"}}));
		EarthMetal::rod_spread(w, t, rod, t.ch.E * 0.5);
	}
	return true;
}

bool _o_rod_melt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)t;
	(void)ctx;
	MatBody* rod = c.body;
	if (rod != nullptr && rod->alive) {
		w.emit("shatter", D({{"body", rod->id}, {"mass", rod->mass}, {"by", "lightning"}}));
		w.close_zone(*rod, "melted");   // the rod melts away (metal_returned)
	}
	res.counter_broken = true;
	const double tp = res.tp;
	res.pass_scale = clampf((tp - dnum(r, "absorb_on_fail", 0.5) * res.cp_eff) / maxf(tp, 1e-6), 0.0, 1.0);
	res.stopped = res.pass_scale <= 0.0;
	return true;
}

bool _o_melt_in(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* pit = c.body;
	MatBody* b = t.body;
	if (pit == nullptr || !pit->alive || b == nullptr || !b->alive) return false;
	const double hu = minf(pit->heat_payload, dnum(r, "rate_hu", 120.0));
	if (hu > 0.0) {
		pit->heat_payload -= hu;
		const double used = w.heat_body(*b, hu);
		pit->heat_payload += hu - used;
		res.heat_used += used;
	} else if (pit->is_stone() && pit->liquid > 0.0) {
		// A lava pool melts what lands in it with its own heat (exact transfer).
		const double take = minf(dnum(r, "rate_hu", 120.0),
		                         maxf(0.0, pit->thermal_energy() - pit->mass * Sim::STONE_C * (Sim::STONE_MELT_C - Sim::AMBIENT_C) * 0.5));
		if (take > 0.0) {
			const double got = -Thermal::heat(*pit, -take);
			const double used2 = w.heat_body(*b, got);
			w.ledger.ambient -= got - used2;
			res.heat_used += used2;
		}
	}
	if (b->alive && b->form != Form::Wave) {
		b->vel *= 0.2;
		b->attack_id = 0;
	} else if (b->alive && b->form == Form::Wave) {
		b->wave_budget = minf(b->wave_budget, 0.5);
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	if (pit->mat == Mat::Air && pit->heat_payload <= 0.5) w.close_zone(*pit, "spent");
	return true;
}

bool _o_bolt_grit(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)t;
	(void)r;
	(void)ctx;
	res.pass_scale = 0.5;
	res.stopped = false;
	MatBody* z = c.body;
	if (z != nullptr && z->alive && z->mat == Mat::Sand && z->mass > 1.5) {
		MatBody* g = w.split_body(*z, 1.0, z->pos + V3(0, 0.6, 0));
		g->form = Form::Chunk;
		g->zone_radius = 0.0;
		g->tag = "";
		g->vel = V3(0, -1.0, 0);
		g->max_life = Sim::REMNANT_LIFETIME;
		w.convert_mat(*g, Mat::Glass, "sand_to_glass");
		w.emit("transform", D({{"body", g->id}, {"at", g->pos}, {"from", "sand"}, {"to", "glass"}, {"why", "lightning"}}));
	}
	return true;
}

bool _o_quench(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* water = t.body;
	MatBody* lava = c.body;
	if (water == nullptr || lava == nullptr || !water->alive || !lava->alive || !water->is_water() || !lava->is_stone()) return false;
	w.quench_energy(*lava, *water, minf(lava->thermal_energy(), water->mass * 30.0));
	if (lava->alive && lava->form == Form::Wave && lava->liquid <= 0.4) w._settle_wave(*lava, "quenched");
	if (water->alive && water->form == Form::Wave) w._settle_wave(*water, "quenched");
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_smother(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)c;
	(void)r;
	(void)ctx;
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.body != nullptr && t.body->alive && t.body->mat == Mat::Fire) {
		w.emit("extinguish", D({{"body", t.body->id}, {"by", "sand"}}));
		if (t.body->form == Form::Zone) w.close_zone(*t.body, "smothered");
		else w.decay_body(*t.body, "smothered");
	}
	return true;
}

bool _o_ram_push(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || c.actor == nullptr) return false;
	ActorState& a = *c.actor;
	const Vec3 dir = c.dir;
	if (b->form == Form::Wave) {
		b->wave_dir = dir;
		b->wave_budget = maxf(b->wave_budget, 5.0);
		b->pos += dir * 0.4;
	} else {
		b->vel = dir * maxf(10.0, b->vel.length() * 0.6) + V3(0, 2.0, 0);
		b->on_ground = false;
	}
	if (b->attack_id != 0 || b->form == Form::Wave) {
		b->attack_id = w.new_attack_id();
		b->attack_owner = a.id;
		b->hit_set.clear();
		b->hit_set.add(a.id);
		b->damage = maxf(b->damage, 10.0);
		b->balance_damage = maxf(b->balance_damage, 25.0);
	}
	b->touch(a.id, "ram", w.tick);
	w.emit("deflect", D({{"actor", a.id}, {"body", b->id}, {"verb", "ram"}, {"kind", FxEvents::mat_of(*b)}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	res.result = "deflect";
	return true;
}

bool _o_ram_blocked(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	if (c.body != nullptr && c.body->alive && t.body != nullptr) {
		// The wall that held takes damage in proportion to how close the contest was.
		c.body->wall_damage_add(0.5 * res.tp / maxf(res.cp_eff, 1e-3));
		w.emit("block", D({{"actor", _aid(c)}, {"body", t.body->id}, {"kind", "ram"}, {"wall", c.body->id}, {"power", res.tp},
		                   {"mat", FxEvents::mat_of(*t.body)}, {"tier", t.tier}, {"dir", t.dir}}));
		if (c.body->wall_damage >= 1.0) w._crumble_wall(*c.body);
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_ram_both(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	if (c.body != nullptr && c.body->alive && c.body->form == Form::Wall) w._crumble_wall(*c.body);
	res.counter_broken = true;
	res.stopped = true;
	res.pass_scale = 0.0;
	if (t.body != nullptr) {
		w.emit("clash", D({{"a", t.body->id}, {"b", c.body != nullptr ? c.body->id : -1}, {"winner", -1}, {"pos", t.body->pos}, {"power", res.tp},
		                   {"mat", FxEvents::mat_of(*t.body)}}));
	}
	return true;
}

bool _o_wrap(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* b = t.body;
	ActorState* a = c.actor;
	if (b == nullptr || !b->alive || a == nullptr) return false;
	Vec3 land = a->pos + a->forward() * 0.9;
	land.y = f32(w.arena.ground_height(land.x, land.z, a->pos.y + 0.4) + b->radius);
	b->gravity_scale = 1.0;
	b->vel = Verbs::launch_vel(b->pos, land, maxf(2.0, KitEarthUtil::flat_dist2(b->pos, land) / 0.45), 1.0);
	b->attack_id = 0;
	b->on_ground = false;
	b->residual_owner = a->id;
	b->residual_authority = 0.9;
	b->touch(a->id, "wrap", w.tick);
	w.emit("capture", D({{"body", b->id}, {"by", -1}, {"actor", a->id}, {"verb", "chain"}}));
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_spike_stop(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)r;
	(void)ctx;
	MatBody* spike = nullptr;
	MatBody* other = nullptr;
	for (MatBody* x : {t.body, c.body}) {
		if (x == nullptr || !x->alive) continue;
		if (spike == nullptr && (x->tag == "spike_line" || x->tag == "spikes")) spike = x;
		else other = x;
	}
	if (spike == nullptr || other == nullptr) return false;
	if (spike->mat == Mat::Metal) {
		res.pass_scale = 1.0;
		return true;
	}
	if (other->form == Form::Wave) {
		w.emit("block", D({{"actor", spike->attack_owner >= 0 ? spike->attack_owner : spike->last_actor}, {"body", other->id}, {"kind", "spikes"},
		                   {"wall", spike->id}, {"power", res.tp}, {"mat", FxEvents::mat_of(*other)}, {"tier", other->tier}, {"dir", other->wave_dir}}));
		w.emit("wave_blocked", D({{"body", other->id}, {"at", other->pos}}));
		w._settle_wave(*other, "blocked");
	} else if (other->form == Form::Zone && other->mat == Mat::Fire) {
		w.close_zone(*other, "blocked");
	}
	if (spike->form == Form::Wave) EarthStone::_erupt(w, *spike);
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_glaze(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || b->mat != Mat::Sand) return false;
	_o_melt_in(w, t, c, res, r, ctx);
	if (b->alive && b->form == Form::Wave) w._settle_wave(*b, "glazed");
	if (b->alive && b->mat == Mat::Sand) {
		w.convert_mat(*b, Mat::Glass, "sand_to_glass");
		b->props.erase("settle");
		w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "sand"}, {"to", "glass"}, {"why", "melt_pit"}}));
	}
	res.stopped = true;
	res.pass_scale = 0.0;
	return true;
}

bool _o_drag(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx) {
	(void)ctx;
	MatBody* b = t.body;
	if (b == nullptr || !b->alive || c.body == nullptr) return false;
	const std::string key = "dragged_" + itos(c.body->id);
	res.pass_scale = 1.0;
	if (b->props.has(key)) return true;
	b->props.set(key, true);
	const double f = dnum(r, "factor", 0.7);
	b->vel *= f;
	res.pass_scale = f;
	w.emit("bend", D({{"actor", _aid(c)}, {"body", b->id}, {"verb", "grit"}}));
	return true;
}

}  // namespace EarthRules
}  // namespace ff
