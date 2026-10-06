// Fourfold core - port of game/combat/kits/fire/fire_combustion.gd (Fire / Combustion, sub 3; MOVESET §7.12): fused air
// pockets (Pop -> Detonation, Chain Blasts, Fuse), Spark Mine / Scatter Charges embers, Smother Blast, Blast Jump and
// Afterglow. Paid heat waits in the pocket / ember and is spent by FireUtil::detonate.
#include "Combat/Kits/Fire/Fire.h"

#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/FxEvents.h"
#include "Util/GdUtil.h"

#include <cmath>

namespace ff {
namespace FireCombustion {

namespace {
constexpr int E = 2;
constexpr int SUB = 3;

Dict _prm(const ActionInst& inst) {
	return D({{"radius", Charge::paramf(inst, "radius", 2.0)}, {"power", Charge::paramf(inst, "power", 8.0)},
	          {"damage", Charge::paramf(inst, "damage", 8.0)}, {"balance", Charge::paramf(inst, "balance", 20.0)},
	          {"knock", Charge::paramf(inst, "knock", 6.0)}, {"lift", Charge::paramf(inst, "lift", 2.0)}, {"move", inst.id}, {"tier", inst.tier()}});
}

std::vector<MatBody*> _mines_of(CombatWorld& w, const ActorState& a) {
	std::vector<MatBody*> out;
	for (const BodyRef& b : w.bodies)
		if (b->alive && b->tag == "ember" && dbool(b->props, "mine", false) && dint(b->props, "mine_owner", -1) == a.id) out.push_back(b.get());
	return out;
}
}  // namespace

MatBody* pocket(CombatWorld& w, ActorState* a, Vec3 p, double delay, const Dict& prm, double heat, int tier, const std::string& mode) {
	const int aid = a != nullptr ? a->id : -1;
	MatBody* z = w.spawn_zone("fuse", p, 0.35, aid, dnum(prm, "power", 8.0), Mat::Air, 0.0, -1.0, "fuse:" + itos(aid));
	z->props.set("fuse", 1.0e9);   // the core fuse timer never fires it
	z->props.set("fire_fuse", delay);
	z->props.set("prm", prm);
	z->props.set("mode", mode);
	z->props.set("spare_owner", true);
	z->heat_payload = heat;   // paid heat waits in the pocket (counted by thermal_energy)
	z->tier = tier;
	z->sub = SUB;
	FxEvents::fx(w, "cast", "blast",
	             D({{"actor", aid}, {"pos", p}, {"dur", maxf(delay, 0.1)}, {"radius", dnum(prm, "radius", 2.0)}, {"power", dnum(prm, "power", 8.0)},
	                {"tier", tier}, {"element", E}, {"sub", SUB}, {"move", dstr(prm, "move", "")}, {"shape", "small"}}));
	return z;
}

Dict fire_pocket(CombatWorld& w, MatBody* z) {
	if (z == nullptr || !z->alive) return Dict();
	Dict prm = ddict(z->props, "prm").duplicate();
	prm.set("heat_hu", z->heat_payload);
	prm.set("tier", z->tier);
	z->heat_payload = 0.0;
	ActorState* owner = w.get_actor(z->owner);
	const Vec3 p = z->pos;
	const std::string mode = dstr(z->props, "mode", "strike");
	const int64_t inrush = z->props.has("inrush_tick") ? z->props.get("inrush_tick").as_int() : -1000000;
	w.close_zone(*z, "detonated");
	return FireUtil::detonate(w, owner, nullptr, p, prm, mode, inrush);
}

void fuse_zone_tick(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	if (!z.props.has("fire_fuse")) return;
	Dict seen = ddict(z.props, "vac");
	for (const std::string& id : seen.keys()) {
		MatBody* v = w.get_body(to_int(id));
		if (v == nullptr || !v->alive) {
			z.props.set("inrush_tick", w.tick);
			seen.erase(id);
		}
	}
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (b.alive && b.form == Form::Zone && &b != &z && Interactions::classify(b) == "vacuum" && (b.pos - z.pos).length() < b.zone_radius + 4.0)
			seen.set(itos(b.id), true);
	}
	z.props.set("vac", seen);
	const double delay = dnum(z.props, "fire_fuse");
	if (delay >= 0.0 && z.age >= delay - 1e-6) fire_pocket(w, &z);
}

bool pop_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double heat = Verbs::take_heat(inst);
	Dict prm = _prm(inst);
	const std::string at = Charge::params(inst, "at", "ahead");
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	Vec3 p = a.pos + dir * Charge::paramf(inst, "distance", 1.4) + V3(0, 1.0, 0);
	if (at == "aim") p = FireUtil::aim_ground(w, a, inst, Charge::paramf(inst, "range", 6.0)) + V3(0, 1.0, 0);
	const double fuse = Charge::paramf(inst, "fuse", 0.0);
	if (fuse > 0.0) {
		pocket(w, &a, p, fuse, prm, heat, inst.tier());
	} else {
		prm.set("heat_hu", heat);
		FireUtil::detonate(w, &a, &inst, p, prm);
	}
	return true;
}

void mine_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (!_mines_of(w, a).empty()) {
		inst.data.set("detonate", true);
		inst.data.set("face", w.aim_dir(a, it));
		Verbs::fx(w, a, inst, "cast", D({{"shape", "small"}}));
		return;
	}
	Verbs::on_start(w, a, inst, it);
}

bool mine_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (dbool(inst.data, "detonate", false)) {
		for (MatBody* m : _mines_of(w, a)) pop_ember(w, m, "remote");
		w.emit("mine_detonate", D({{"actor", a.id}}));
		return true;
	}
	for (MatBody* b : VerbProjectile::fire(w, a, inst)) {
		b->props.set("mine_power", Charge::paramf(inst, "mine_power", 12.0));
		b->props.set("mine_radius", Charge::paramf(inst, "mine_radius", 1.8));
		b->props.set("mine_damage", Charge::paramf(inst, "mine_damage", 10.0));
		b->props.set("mine_owner", a.id);
	}
	return true;
}

bool mine_impact(CombatWorld& w, MatBody& b, const std::string& what) {
	if (what == "actor") {
		pop_ember(w, &b, "contact");
		return true;
	}
	b.vel = Vec3();
	b.on_ground = true;
	b.attack_id = 0;
	b.gravity_scale = 0.0;
	b.static_body = true;
	b.props.set("mine", true);
	b.max_life = -1.0;
	b.props.set("mine_until", b.age + MINE_LIFE);
	b.pos.y = f32(maxf(b.pos.y, w.arena.ground_height(b.pos.x, b.pos.z, b.pos.y + 0.3) + 0.08));
	w.emit("stick", D({{"body", b.id}, {"on", what}, {"mine", true}}));
	return true;
}

bool ember_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (!b.alive || b.mat != Mat::Fire) return false;
	if (b.props.has("pop_at") && b.age >= dnum(b.props, "pop_at")) {
		pop_ember(w, &b, "timer");
		return true;
	}
	if (dbool(b.props, "mine", false)) {
		const int owner = dint(b.props, "mine_owner", -1);
		ActorState* ow = w.get_actor(owner);
		for (const auto& ap : w.actors) {
			ActorState& a = *ap;
			if (a.health <= 0.0 || a.id == owner || (ow != nullptr && a.team == ow->team)) continue;
			if (Vec2(a.pos.x - b.pos.x, a.pos.z - b.pos.z).length() <= MINE_RANGE && absf(a.pos.y - b.pos.y) < 2.0) {
				pop_ember(w, &b, "proximity");
				return true;
			}
		}
		if (b.age >= dnum(b.props, "mine_until", MINE_LIFE)) {
			w.emit("extinguish", D({{"body", b.id}, {"by", "time"}}));
			w.decay_body(b, "fizzled");
		}
		return true;
	}
	return false;
}

void pop_ember(CombatWorld& w, MatBody* bp, const std::string& why) {
	if (bp == nullptr || !bp->alive) return;
	MatBody& b = *bp;
	ActorState* owner = w.get_actor(dint(b.props, "mine_owner", b.attack_owner >= 0 ? b.attack_owner : b.residual_owner));
	const Vec3 p = b.pos + V3(0, 0.4, 0);
	const double heat = b.heat_payload;
	b.heat_payload = 0.0;
	const Dict prm = D({{"radius", dnum(b.props, "mine_radius", 1.8)}, {"power", dnum(b.props, "mine_power", 10.0)},
	                    {"damage", dnum(b.props, "mine_damage", 8.0)}, {"balance", 22.0}, {"knock", 6.0}, {"lift", 2.0}, {"heat_hu", heat},
	                    {"move", dstr(b.props, "move", "spark_mine")}, {"tier", b.tier}});
	b.props.erase("mine");
	b.attack_id = 0;
	w.decay_body(b, "detonated");
	FireUtil::detonate(w, owner, nullptr, p, prm);
	w.emit("ember_pop", D({{"body", b.id}, {"why", why}, {"pos", p}}));
}

bool chain_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double heat = Verbs::take_heat(inst);
	const int n = Charge::parami(inst, "steps", 3);
	const double step = Charge::paramf(inst, "step", 1.7);
	Vec3 dir = vvec(inst.data.get("aim", inst.data.get("face", a.forward())), a.forward());
	dir.y = 0.0f;
	dir = dir.length() > 0.01 ? dir.normalized() : a.forward();
	const Dict prm = _prm(inst);
	for (int k = 0; k < n; ++k) {
		Vec3 p = a.pos + dir * step * static_cast<double>(k + 1);
		p.y = f32(w.arena.ground_height(p.x, p.z, a.pos.y + 0.5) + 0.6);
		if (w.arena.segment_hit(a.chest(), p) >= 0.0) {
			w.ledger.spent += heat * static_cast<double>(n - k) / static_cast<double>(n);
			break;
		}
		pocket(w, &a, p, Charge::paramf(inst, "step_t", 0.15) * static_cast<double>(k), prm, heat / static_cast<double>(n), inst.tier());
	}
	return true;
}

bool scatter_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const std::vector<MatBody*> bodies = VerbProjectile::fire(w, a, inst);
	for (size_t k = 0; k < bodies.size(); ++k) {
		MatBody* b = bodies[k];
		b->props.set("pop_at", Charge::paramf(inst, "pop", 0.4) + 0.03 * static_cast<double>(k));
		b->props.set("mine_power", Charge::paramf(inst, "mine_power", 8.0));
		b->props.set("mine_radius", Charge::paramf(inst, "mine_radius", 1.6));
		b->props.set("mine_damage", Charge::paramf(inst, "mine_damage", 6.0));
		b->props.set("mine_owner", a.id);
		b->props.erase("on_impact");
	}
	return true;
}

bool smother_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	Dict prm = _prm(inst);
	prm.set("heat_hu", Verbs::take_heat(inst));
	FireUtil::detonate(w, &a, &inst, a.pos + V3(0, 0.3, 0), prm);
	a.vel.y = f32(std::sqrt(2.0 * Sim::GRAVITY * Charge::paramf(inst, "jump", 1.5)));
	a.grounded = false;
	w.emit("blast_jump", D({{"actor", a.id}, {"height", Charge::paramf(inst, "jump", 1.5)}}));
	return true;
}

Dict fuse_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	(void)w;
	(void)a;
	(void)dir;
	return D({{"mode", "FUSE"}, {"body", -1}, {"ok", true}, {"reason", ""}, {"label", "FUSE"}});
}

ActionPhase fuse_after(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (dbool(inst.data, "fizzle", false)) return ActionPhase::Recovery;
	const Vec3 p = FireUtil::aim_ground(w, a, inst, Charge::paramf(inst, "range", 10.0)) + V3(0, 1.0, 0);
	MatBody* z = pocket(w, &a, p, -1.0, _prm(inst), 0.0, 0, "fuse");
	inst.data.set("pocket", z->id);
	return ActionPhase::Channel;
}

void fuse_tick_action(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.phase != ActionPhase::Channel) {
		Verbs::on_tick(w, a, inst, it);
		return;
	}
	MatBody* z = w.get_body(dint(inst.data, "pocket", -1));
	if (it.tech_cancel || z == nullptr || !z->alive) {
		if (z != nullptr && z->alive) w.close_zone(*z, "cancelled");
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	inst.data.set("aim", w.aim_dir(a, it));
	inst.data.set("aim_active", it.aim_active);
	inst.data.set("aim_point", w.aim_point(a, it));
	const Vec3 want = FireUtil::aim_ground(w, a, inst, Charge::paramf(inst, "range", 10.0)) + V3(0, 1.0, 0);
	const Vec3 to = want - z->pos;
	const double sp = Charge::paramf(inst, "steer_speed", 8.0);
	if (to.length() > 0.05) z->pos += to.normalized() * minf(sp * Sim::DT, to.length());
	z->tier = inst.tier();
	z->props.set("prm", _prm(inst));
	if (w.tick % 15 == 0)
		FxEvents::fx(w, "aura", "blast",
		             D({{"actor", a.id}, {"pos", z->pos}, {"radius", Charge::paramf(inst, "radius", 2.2) * 0.3}, {"power", Charge::paramf(inst, "power", 14.0)},
		                {"tier", inst.tier()}, {"on", true}, {"shape", "small"}, {"body", z->id}}));
	if (!Charge::held(inst, it)) {
		inst.data.set("released", true);
		w.set_phase(a, inst, ActionPhase::Active);
	}
}

void fuse_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Active) {
		MatBody* z = w.get_body(dint(inst.data, "pocket", -1));
		if (z != nullptr && z->alive) {
			z->heat_payload = w.pay_heat(a, Charge::paramf(inst, "fuse_hu", 60.0));
			z->props.set("prm", _prm(inst));
			z->tier = inst.tier();
			const Dict out = fire_pocket(w, z);
			w.emit("fuse", D({{"actor", a.id}, {"tier", inst.tier()}, {"mods", out.get("mods", Array())}, {"suppressed", out.get("suppressed", false)}}));
		}
		return;
	}
	Verbs::on_phase(w, a, inst, p);
}

void fuse_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	MatBody* z = w.get_body(dint(inst.data, "pocket", -1));
	if (z != nullptr && z->alive && dint(z->props, "fire_fuse", 0) < 0) w.close_zone(*z, "interrupted");
	Verbs::on_interrupt(w, a, inst, reason);
}

void jump_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const bool neutral = V3(it.move.x, 0.0, it.move.z).length() < 0.2;
	if (neutral) FireUtil::with_params(inst, D({{"distance", 0.3}, {"up", 11.0}}));
	Verbs::on_start(w, a, inst, it);
	if (dbool(inst.data, "fizzle", false)) return;
	const Dict prm = D({{"radius", 1.3}, {"power", 5.0}, {"damage", 3.0}, {"balance", 14.0}, {"knock", 5.0}, {"lift", 1.0},
	                    {"heat_hu", Verbs::take_heat(inst)}, {"move", inst.id}, {"tier", 0}});
	FireUtil::detonate(w, &a, &inst, a.pos + V3(0, 0.2, 0), prm);
}

void afterglow_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	Verbs::on_tick(w, a, inst, it);
	if (a.action.get() != &inst || inst.phase != ActionPhase::Channel) return;
	if (inst.t >= Charge::paramf(inst, "hover_t", 0.8)) {
		w.set_phase(a, inst, ActionPhase::Recovery);
		return;
	}
	if (w.tick % 12 == 0)
		FxEvents::fx_for(w, a, inst, "burst", "blast", D({{"pos", a.pos + V3(0, -0.1, 0)}, {"radius", 0.5}, {"power", 2.0}, {"shape", "small"}}));
}

}  // namespace FireCombustion
}  // namespace ff
