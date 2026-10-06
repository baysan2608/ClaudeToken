// Fourfold core - ports of game/combat/kits/water/water_water.gd (Water / Water, sub 0; MOVESET §7.5) and water_jet.gd
// (the connected Pressure / Cutting Jet of Water Bullet T2 / T3): Water Bullet, Tidal Rush (carry back, wave contacts),
// Spray Fan, Surge Orb, Slick, Riptide Step / Dive, Wave Ride and the technique preview.
#include "Combat/Kits/Water/Water.h"

#include "Combat/Acts.h"
#include "Combat/Kits/Water/WaterUtil.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/FxEvents.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {

// ================================================================ WaterJet

namespace WaterJet {
namespace {
void _pulse(CombatWorld& w, ActorState& a, ActionInst& inst, MatBody& jet) {
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	const double rng_m = Charge::paramf(inst, "jet_range", 10.0);
	const double width = Charge::paramf(inst, "jet_width", 0.5);
	const bool cut = Charge::paramb(inst, "cut", false);
	const Vec3 start = a.hand_point() + V3(0, 0.2, 0);
	AgentRef v = Agent::of_volume(&w, &a, &inst, "water", start, dir, D({{"P", Charge::paramf(inst, "jet_power", 10.0)}}));
	v->ccls = "water_jet";
	v->data.set("knock", Charge::paramf(inst, "jet_knock", 3.0));
	inst.attack_id = w.new_attack_id();   // every pulse is a new hit instance
	Vec3 end = start + dir * rng_m;
	double stop_t = 1.0;
	for (const Conduction::BarrierHit& hb : Conduction::barriers_on(w, start, end)) {
		const double t = hb.t;
		if (hb.body == nullptr) {
			stop_t = t;
			break;
		}
		MatBody& hbody = *hb.body;
		if (cut && hbody.form == Form::Wall && in_list(hbody.tag, {"sand", "mud", "vine"})) {
			hbody.wall_damage_add(0.3);   // a thin jet cuts soft walls
			if (hbody.wall_damage >= 1.0) {
				w._crumble_wall(hbody);
				continue;
			}
		}
		const IxResult r = VerbVolume::meet_body(w, &a, *v, hbody);
		if (r.stopped || r.pass_scale <= 0.0) {
			stop_t = t;
			break;
		}
		v->power *= r.pass_scale;
	}
	end = start + (end - start) * stop_t;
	const Vec3 seg = end - start;
	const double seg_len = maxf(seg.length(), 0.01);
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (!o.alive || &o == &jet || o.static_body || o.controller == a.id || o.form == Form::Wall || o.form == Form::Pool) continue;
		const double tb = clampf((o.pos - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tb - o.pos).length() <= width + o.radius + 0.1) VerbVolume::meet_body(w, &a, *v, o);
	}
	std::vector<std::pair<double, ActorState*>> hits;
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0) continue;
		const double tt = clampf((t.chest() - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tt - t.chest()).length() <= width + Sim::ACTOR_RADIUS + 0.4) hits.emplace_back(tt, &t);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) {
		return x.first < y.first || (x.first == y.first && x.second->id < y.second->id);
	});
	for (const auto& h : hits) {
		ActorState& tgt = *h.second;
		const std::string res = w.hit_actor(tgt,
		                                    D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "jet_dmg", 2.5)},
		                                       {"balance", Charge::paramf(inst, "jet_balance", 4.0)},
		                                       {"knock", dir * dnum(v->data, "knock") + V3(0, 0.4, 0)}, {"kind", "water"}, {"from", start},
		                                       {"power", v->power}, {"tier", inst.tier()}, {"mat", "water"}}),
		                                    v);
		if (res == "hit" || res == "knockdown" || res == "block") tgt.wetness = 1.0;
		end = start + dir * h.first;
		break;
	}
	inst.data.set("jet_end", end);
	jet.hold_point = (start + end) * 0.5;
	jet.pos = jet.hold_point;
	jet.radius = maxf(0.4, (start - end).length() * 0.5);
	Verbs::fx(w, a, inst, "beam", D({{"length", (start - end).length()}, {"path", A({Value(start), Value(end)})}, {"power", v->power}, {"dir", dir}}));
	if (v->heat > 0.0) {
		w.ledger.spent += v->heat;
		v->heat = 0.0;
	}
}
}  // namespace

bool start(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double kg = Charge::paramf(inst, "jet_kg", 3.0);
	const double got = WaterUtil::take(w, a, kg);
	if (got < 0.8) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		inst.data.set("active", 0.02);
		return true;
	}
	inst.data.set("active", Charge::paramf(inst, "jet_t", 0.8));
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	MatBody* b = w.spawn_body(Mat::Water, Form::Stream, got, a.chest() + dir * 1.0, "jet:" + itos(a.id));
	b->tag = "jet";
	b->max_life = -1.0;
	w.take_control(a, *b, 0.97, "jet");
	b->hold_point = b->pos;
	inst.data.set("jet", b->id);
	inst.data.set("jet_t", 0.0);
	inst.data.set("jet_end", a.chest() + dir * Charge::paramf(inst, "jet_range", 10.0));
	w.emit("jet", D({{"actor", a.id}, {"body", b->id}, {"tier", inst.tier()}, {"on", true}}));
	_pulse(w, a, inst, *b);
	return true;
}

void tick(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (!inst.data.has("jet") || inst.phase != ActionPhase::Active) return;
	MatBody* b = w.get_body(dint(inst.data, "jet"));
	if (b == nullptr || !b->alive || b->controller != a.id) {
		inst.data.erase("jet");
		return;
	}
	inst.data.set("jet_t", dnum(inst.data, "jet_t", 0.0) + Sim::DT);
	if (dnum(inst.data, "jet_t") >= PULSE - 1e-6) {
		inst.data.set("jet_t", 0.0);
		_pulse(w, a, inst, *b);
	}
}

void end(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* b = w.get_body(dint(inst.data, "jet", -1));
	inst.data.erase("jet");
	if (b == nullptr || !b->alive) return;
	if (a.held_body == b->id) a.held_body = -1;
	b->controller = -1;
	b->authority = 0.0;
	b->vel = Vec3();
	b->tag = "";
	const Vec3 e = dvec(inst.data, "jet_end", a.pos + a.forward() * 4.0);
	b->pos = WaterUtil::ground_at(w, Vec3(e.x, 0.0f, e.z)) + V3(0, 0.3, 0);
	b->update_radius();
	w.emit("jet", D({{"actor", a.id}, {"body", b->id}, {"tier", inst.tier()}, {"on", false}}));
	w._water_to_puddle(*b);
}
}  // namespace WaterJet

// ================================================================ WaterWater

namespace WaterWater {

namespace {
void _carry_check(CombatWorld& w, MatBody& b) {
	if (b.captured.empty()) return;
	ActorState* owner = w.get_actor(b.attack_owner);
	if (owner == nullptr) return;
	ActorState* tgt = nullptr;
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (t.team != owner->team && t.health > 0.0 && Vec2(t.pos.x - b.pos.x, t.pos.z - b.pos.z).length() < b.wave_width * 0.5 + 2.2) {
			tgt = &t;
			break;
		}
	}
	if (tgt == nullptr) return;   // no rival reached yet: the core releases the load where the wave ends
	// The core release (ownership, damage, momentum along the wave) then aimed at the rival it reached.
	const std::vector<int> ids = b.captured;
	w.release_captured(b);
	for (int id : ids) {
		MatBody* c = w.get_body(id);
		if (c == nullptr || !c->alive || c->attack_id == 0 || c->attack_owner != owner->id) continue;
		c->vel = Verbs::launch_vel(c->pos, tgt->chest(), maxf(dnum(c->props, "release_speed", 14.0), 12.0), 1.0);
		c->gravity_scale = 1.0;
		c->damage = maxf(c->damage, dnum(c->props, "release_damage", 12.0));
		c->props.set("no_carry_until", w.tick + 90);   // a thrown load is not caught again by the wave that threw it
		w._zone_pairs[itos(b.id) + "|" + itos(c->id)] = w.tick + 90;
	}
}
}  // namespace

bool bullet_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (inst.tier() < 2) return false;   // T0 / T1: the generic projectile verb (slug / triple)
	return WaterJet::start(w, a, inst);
}

void bullet_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() == &inst) WaterJet::tick(w, a, inst);
}

void bullet_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) WaterJet::end(w, a, inst);
	Verbs::on_phase(w, a, inst, p);
}

void bullet_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	WaterJet::end(w, a, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

double tidal_scale(CombatWorld& w, ActorState& a, int tier) {
	const Dict d = Moves::defs().get("tidal_rush").as_dict();
	const double want = Charge::pgetf(d, tier, "mass", 6.0);
	const double reach = Charge::pgetf(d, tier, "take_reach", 3.0);
	const double have = WaterUtil::available(w, a, reach);
	if (have < 3.0) return 0.0;
	return clampf(have / maxf(want, 0.1), 0.35, 1.0);
}

bool tidal_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double want = Charge::paramf(inst, "mass", 8.0);
	const double reach = Charge::paramf(inst, "take_reach", 3.0);
	const double got = WaterUtil::take(w, a, want, reach);
	if (got < 3.0) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	// The wave's counter power follows the water that is really in it (less water than the tier asks for: weaker).
	const double pw = Charge::paramf(inst, "power", 18.0) * clampf(got / maxf(want, 0.1), 0.35, 1.0);
	VerbGroundLine::launch(w, a, inst, D({{"source", "none"}, {"mass", got}, {"mat", "water"}, {"power", pw}}));
	return true;
}

bool wave_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (b.form != Form::Wave || b.attack_id == 0) return false;
	_carry_check(w, b);
	return wave_contacts(w, b);
}

bool wave_contacts(CombatWorld& w, MatBody& b) {
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !o.alive || o.attack_id == 0 || o.attack_owner == b.attack_owner) continue;
		bool close = false;
		if (o.form == Form::Wave) {
			if (o.mat == b.mat && o.tag == b.tag) continue;        // water vs water: the core clash merges them
			if (o.tag == "rime" && b.tag != "rime") continue;      // the rime wave resolves its own contacts (it is the counter)
			// A little look-ahead: our rule must see the pair before the core clash pass does (same tick, after the move).
			close = Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() <= (o.wave_width + b.wave_width) * 0.5 + 0.7;
		}
		if (!close) continue;
		AgentRef ctr = Agent::of_body(w, b);
		ctr->actor = w.get_actor(b.attack_owner);
		AgentRef th = Agent::of_body(w, o);
		IxCtx ctx;
		ctx.site = "clash";
		Interactions::resolve(w, *th, *ctr, ctx, &Interactions::CLASH_RULE());
		if (!b.alive || b.form != Form::Wave) return false;
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& z = *keep;
		if (z.alive && z.form == Form::Zone && z.owner != b.attack_owner && (z.tag == "tornado" || z.tag == "eddy") &&
		    Vec2(z.pos.x - b.pos.x, z.pos.z - b.pos.z).length() <= z.zone_radius + b.wave_width * 0.5) {
			const std::string key = itos(b.id) + "|" + itos(z.id) + "|drown";
			auto it = w._zone_pairs.find(key);
			const int64_t last = it == w._zone_pairs.end() ? -100000 : it->second;
			if (w.tick - last < 6) continue;
			w._zone_pairs[key] = w.tick;
			AgentRef ctr2 = Agent::of_body(w, b);
			ctr2->actor = w.get_actor(b.attack_owner);
			AgentRef th = Agent::of_body(w, z);
			IxCtx ctx;
			ctx.site = "wave";
			ctx.continuous = true;
			Interactions::resolve(w, *th, *ctr2, ctx, &Interactions::PASS_RULE());
			if (!b.alive || b.form != Form::Wave) return false;
		}
	}
	return false;
}

bool spray_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double kg = Charge::paramf(inst, "spray_kg", 1.0);
	const double got = WaterUtil::take(w, a, kg);
	if (got < 0.5) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	VerbVolume::cone(w, a, inst);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const Vec3 land = WaterUtil::ground_at(w, a.pos + dir * rng_m * 0.6);
	double rest = got;
	if (Charge::paramb(inst, "mist", false)) {
		const double mist_kg = minf(0.6, rest * 0.3);
		rest -= mist_kg;
		MatBody* z = WaterUtil::zone(w, "mist", land + V3(0, 0.1, 0), 2.2, a.id, 3.0,
		                             D({{"actor_status", "concealed"}, {"status_t", 0.4}, {"spare_owner", false}, {"height", 2.2}}), Mat::Steam, mist_kg);
		z->tier = inst.tier();
		z->sub = inst.sub;
	}
	WaterUtil::make_puddle(w, rest, land);
	return true;
}

bool orb_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (w.held(a) != nullptr || w.get_body(dint(inst.data, "held", -1)) != nullptr) return false;   // throw the shield itself
	const double got = WaterUtil::take(w, a, 2.0);
	if (got < 0.8) {
		WaterUtil::give_back(w, a, got, a.pos);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
		return true;
	}
	MatBody* b = w.spawn_body(Mat::Water, Form::Blob, got, a.hand_point(), "waterskin:" + itos(a.id));
	w.take_control(a, *b, 0.9, "orb");
	return false;
}

bool orb_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	(void)what;
	ActorState* owner = w.get_actor(b.attack_owner);
	const double r = 2.0;
	FxEvents::fx(w, "burst", "water",
	             D({{"actor", b.attack_owner}, {"pos", b.pos}, {"radius", r}, {"power", 5.0}, {"move", "surge_orb"}, {"tier", b.tier}, {"body", b.id}}));
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (t.health <= 0.0 || (owner != nullptr && (&t == owner || t.team == owner->team))) continue;
		if ((t.chest() - b.pos).length() > r + Sim::ACTOR_RADIUS) continue;
		Vec3 kd = t.pos - b.pos;
		kd.y = 0.0f;
		kd = kd.length() > 0.05 ? kd.normalized() : Vec3(0.0f, 0.0f, -1.0f);
		const std::string res = w.hit_actor(t, D({{"attacker", b.attack_owner}, {"attack_id", b.attack_id + 100000 + t.id}, {"damage", 5.0},
		                                          {"balance", 14.0}, {"knock", kd * 5.0 + V3(0, 1.0, 0)}, {"kind", "water"}, {"from", b.pos},
		                                          {"power", 5.0}, {"mat", "water"}, {"tier", b.tier}}));
		if (res == "hit" || res == "knockdown" || res == "block") t.wetness = 1.0;
	}
	b.vel = Vec3();
	b.attack_id = 0;
	b.form = Form::Stream;
	w._water_to_puddle(b);
	return true;
}

ActionPhase after_active(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)w;
	(void)a;
	(void)inst;
	(void)it;
	return ActionPhase::Active;
}

void slick_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	inst.data.set("face", w.aim_dir(a, it));
	if (!w.spend_focus(a, dnum(inst.def, "cost"))) {
		inst.data.set("fizzle", true);
		w.emit("insufficient", D({{"actor", a.id}, {"what", "focus"}, {"move", inst.id}}));
		return;
	}
	const Vec3 dir = dvec(inst.data, "face");
	const Vec3 p = WaterUtil::ground_at(w, a.pos + dir * 2.4);
	double kg = 0.0;
	MatBody* held = w.held(a);
	if (held == nullptr) held = w.get_body(dint(inst.data, "held", -1));
	if (held != nullptr && held->alive && held->is_water() && held->phase == Phase::Liquid) {
		kg = held->mass;
		if (a.held_body == held->id) a.held_body = -1;
		held->controller = -1;
		held->vel = Vec3();
		held->pos = p + V3(0, 0.3, 0);
		held->form = Form::Stream;
		w._water_to_puddle(*held);
	} else {
		kg = WaterUtil::take(w, a, 2.0);
		if (kg < 0.5) {
			WaterUtil::give_back(w, a, kg, a.pos);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
			inst.data.set("fizzle", true);
			return;
		}
		WaterUtil::make_puddle(w, kg, p);
	}
	MatBody* z = WaterUtil::zone(w, "slick", p + V3(0, 0.05, 0), dnum(inst.def, "zone_radius"), a.id, dnum(inst.def, "zone_life"),
	                             D({{"slip", dnum(inst.def, "slip")}, {"height", 1.4}, {"w_kind", "slick"}}));
	z->tier = inst.tier();
	z->sub = inst.sub;
	Verbs::fx(w, a, inst, "cast", D({{"pos", p}, {"radius", 1.25}, {"body", z->id}}));
	inst.data.set("zone", z->id);
}

void slick_zone(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (!z.props.has("w_kind")) return;
	for (ActorState* tp : w.actors_in_zone(z)) {
		ActorState& t = *tp;
		if (t.id == z.owner || !t.grounded) continue;
		Status::apply(w, t, "slick", 0.25, 1.0, z.owner);
		t.wetness = maxf(t.wetness, 0.6);
		const double spd = Vec2(t.vel.x, t.vel.z).length();
		const std::string key = "slip" + itos(t.id);
		const int64_t last = z.props.has(key) ? z.props.get(key).as_int() : -1000;
		if (spd > 3.0 && w.tick - last > 90) {
			z.props.set(key, w.tick);
			t.balance = maxf(0.0, t.balance - dnum(z.props, "slip", 20.0));
			t.balance_idle = 0.0;
			w.emit("slip", D({{"actor", t.id}, {"zone", z.id}}));
			if (t.balance <= 0.0) {
				w._stagger(t, "knockdown", 1.1, Dict());
				t.balance = 45.0;
			}
		}
	}
}

void riptide_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const bool dive = a.in_water;
	const Dict spec = D({{"dist", dnum(inst.def, dive ? "dive_distance" : "distance", 4.0)},
	                     {"iframes", dnum(inst.def, dive ? "dive_iframes" : "iframes", 0.15)},
	                     {"cost", dnum(inst.def, "cost")},
	                     {"hidden", dive},
	                     {"active", dnum(inst.def, "active")}});
	WaterUtil::evade_start(w, a, inst, it, spec);
	inst.data.set("dive", dive);
	if (dive) {
		w.emit("dive", D({{"actor", a.id}, {"on", true}}));
		Verbs::fx(w, a, inst, "splash", D({{"pos", a.pos}, {"radius", 1.0}}));
	} else {
		// The water film: a thin slippery patch behind the step (spares the owner).
		MatBody* z = WaterUtil::zone(w, "slick", a.pos, 0.8, a.id, 1.4, D({{"slip", 10.0}, {"height", 1.4}, {"w_kind", "slick"}}));
		z->tier = 0;
		Verbs::fx(w, a, inst, "trail", D({{"dir", inst.data.get("dir")}, {"length", dnum(spec, "dist")}}));
	}
}

void evade_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	WaterUtil::evade_tick(w, a, inst);
}

void evade_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Recovery) {
		WaterUtil::evade_end(inst);
		if (dbool(inst.data, "dive", false)) {
			w.emit("dive", D({{"actor", a.id}, {"on", false}}));
			Verbs::fx(w, a, inst, "splash", D({{"pos", a.pos}, {"radius", 1.0}}));
		}
	}
}

void ride_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Channel) return;
	a.wetness = 1.0;
	const double acc = dnum(inst.data, "ride_acc", 0.0) + Charge::paramf(inst, "ride_kg", 2.0) * Sim::DT;
	if (acc >= 0.5) {
		const double got = WaterUtil::take(w, a, acc);
		inst.data.set("ride_acc", 0.0);
		if (got < acc * 0.9) {
			WaterUtil::make_puddle(w, got, a.pos);
			w.emit("insufficient", D({{"actor", a.id}, {"what", "water"}, {"move", inst.id}}));
			w.set_phase(a, inst, ActionPhase::Recovery);
			return;
		}
		WaterUtil::make_puddle(w, got, a.vel.length() > 0.5 ? a.pos - a.vel.normalized() * 0.8 : a.pos);
	} else {
		inst.data.set("ride_acc", acc);
	}
}

Dict tech_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	MatBody* held = w.held(a);
	if (held != nullptr && held->is_water()) return D({{"mode", "SHAPE"}, {"body", held->id}, {"ok", true}, {"reason", ""}});
	const double reach = dnum(Moves::defs().get("water_tech"), "reach");
	MatBody* vap = w.find_body(a, dir, reach, 70.0, [](MatBody& b) { return ActWater::vapor_filter(b); });
	if (vap != nullptr) return D({{"mode", "CONDENSE"}, {"body", vap->id}, {"ok", true}, {"reason", ""}});
	const ActorState* ap = &a;
	MatBody* en = w.find_body(a, dir, reach, 70.0, [ap](MatBody& b) { return ActWater::enemy_water(b, *ap); });
	if (en != nullptr) return D({{"mode", "SEIZE"}, {"body", en->id}, {"ok", true}, {"reason", ""}});
	if (WaterUtil::available(w, a, reach) >= 1.0) return D({{"mode", "DRAW"}, {"body", -1}, {"ok", true}, {"reason", ""}});
	return D({{"mode", "DRAW"}, {"body", -1}, {"ok", false}, {"reason", "water"}});
}

}  // namespace WaterWater
}  // namespace ff
