// Fourfold core - port of game/combat/kits/fire/fire_blue.gd (Fire / Blue, sub 1; MOVESET §7.10): the Blue Needle beam
// ladder (sustained pulses, White Core wall melting), Blue Furrow / Magma Rift, Corona, Kiln, Smelter preview and
// Afterburn. Blue costs 1.5x the heat of Flame and counts x1.5 in the counter rule.
#include "Combat/Kits/Fire/Fire.h"

#include "Combat/Acts.h"
#include "Combat/Kits/Fire/FireUtil.h"
#include "Combat/Verbs.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Sim/FxEvents.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace FireBlue {

namespace {
void _end_face(CombatWorld& w, ActorState& a, ActionInst& inst) {
	if (inst.data.has("face_id")) VerbHeat::end(w, a, inst);
}

void _melt_wall(CombatWorld& w, ActorState& a, ActionInst& inst, Agent& v, MatBody& wall) {
	MatBody* face = w.get_body(dint(inst.data, "face_id", -1));
	if (face == nullptr || !face->alive) face = VerbHeat::_split_face(w, a, inst, wall);
	const double give = minf(WALL_FACE_HU, v.heat);
	const double used = w.heat_body(*face, give);
	v.heat -= used;
	if (w.tick % 6 == 0)
		w.emit("heating", D({{"actor", a.id}, {"body", face->id}, {"liquid", face->liquid}, {"temp", face->temp}, {"wall", wall.id}}));
	if (face->liquid >= 0.5) {
		VerbHeat::_slump(w, a, inst, wall, *face);
		inst.data.erase("face_id");
	}
}

void _pulse(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	const Vec3 start = a.hand_point() + V3(0, 0.2, 0);
	const double budget = dnum(inst.data, "heat_paid", 0.0);
	inst.data.set("heat_paid", 0.0);
	AgentRef v = Agent::of_volume(&w, &a, &inst, "blue_fire", start, dir, D({{"H", budget / Interactions::HU_PER_PU}, {"heat_hu", budget}}));
	v->data.set("knock", Charge::paramf(inst, "knock", 1.5));
	Vec3 end = start + dir * rng_m;
	double stop_t = 1.0;
	for (const Conduction::BarrierHit& hb : Conduction::barriers_on(w, start, end)) {
		const double t = hb.t;
		if (hb.body == nullptr) {
			stop_t = t;
			break;
		}
		MatBody& wall = *hb.body;
		if (Charge::paramb(inst, "melt_walls", false) && wall.form == Form::Wall && wall.is_stone()) {
			_melt_wall(w, a, inst, *v, wall);
			stop_t = t;
			break;
		}
		const IxResult r = VerbVolume::meet_body(w, &a, *v, wall);
		if (r.stopped || r.pass_scale <= 0.0) {
			stop_t = t;
			break;
		}
	}
	end = start + (end - start) * stop_t;
	const Vec3 seg = end - start;
	const double seg_len = maxf(seg.length(), 0.01);
	const double width = Charge::paramf(inst, "width", 0.35);
	for (MatBody* bp : w.body_list()) {
		MatBody& b = *bp;
		if (!b.alive || b.static_body || b.controller == a.id || b.form == Form::Wall || b.form == Form::Pool || b.form == Form::Zone) continue;
		const double tb = clampf((b.pos - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tb - b.pos).length() <= width + b.radius + 0.15) VerbVolume::meet_body(w, &a, *v, b);
	}
	const bool pierce = Charge::paramb(inst, "pierce", false);
	std::vector<std::pair<double, ActorState*>> hits;
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0) continue;
		const double tt = clampf((t.chest() - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tt - t.chest()).length() <= width + Sim::ACTOR_RADIUS + 0.35) hits.emplace_back(tt, &t);
	}
	std::stable_sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) {
		return x.first < y.first || (x.first == y.first && x.second->id < y.second->id);
	});
	for (const auto& h : hits) {
		const std::string res = w.hit_actor(*h.second, VerbVolume::_hit_info(a, &inst, *v, start, dir), v);
		VerbVolume::_after_hit(w, a, inst, *h.second, res);
		if (!pierce) {
			end = start + dir * h.first;
			break;
		}
	}
	Verbs::fx(w, a, inst, "beam",
	          D({{"length", (start - end).length()}, {"path", A({Value(start), Value(end)})}, {"power", v->power}, {"dir", dir}, {"dur", PULSE * 1.5}}));
	// What the pulse did not use stays in the beam for the next pulse; the last pulse's rest is spent (Verbs RECOVERY).
	inst.data.set("heat_paid", maxf(0.0, v->heat));
}
}  // namespace

void needle_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	if (p == ActionPhase::Active) {
		const double sus = Charge::paramf(inst, "sustain", 0.0);
		if (sus > 0.0) {
			Verbs::pay_dict(w, a, inst, D({{"heat", Charge::paramf(inst, "sustain_hu", 0.0)}}));
			inst.data.set("active", sus);
			inst.data.set("pulses_left", static_cast<int>(std::round(sus / PULSE)));
		}
		inst.data.set("pulse_t", 0.0);
		inst.data.set("budget_total", dnum(inst.data, "heat_paid", 0.0));
		_pulse(w, a, inst);
		return;
	}
	if (p == ActionPhase::Recovery) _end_face(w, a, inst);
	Verbs::on_phase(w, a, inst, p);
}

void needle_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	if (inst.phase == ActionPhase::Active) {
		// The sustained beam follows the aim slowly (a held beam can be dragged across a target).
		const Vec3 want = w.aim_dir(a, it);
		const Vec3 cur = dvec(inst.data, "face", a.forward());
		inst.data.set("face", want.length() > 0.1 ? slerp(cur, want, 0.06).normalized() : cur);
		inst.data.set("pulse_t", dnum(inst.data, "pulse_t", 0.0) + Sim::DT);
		if (dnum(inst.data, "pulse_t") >= PULSE - 1e-6 && dint(inst.data, "pulses_left", 0) > 0) {
			inst.data.set("pulse_t", 0.0);
			inst.data.set("pulses_left", dint(inst.data, "pulses_left") - 1);
			inst.attack_id = w.new_attack_id();
			_pulse(w, a, inst);
		}
		return;
	}
	Verbs::on_tick(w, a, inst, it);
}

void needle_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	_end_face(w, a, inst);
	Verbs::on_interrupt(w, a, inst, reason);
}

bool furrow_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double heat = Verbs::take_heat(inst);
	MatBody* b = VerbGroundLine::launch(w, a, inst, D({{"source", "ground"}, {"mat", "stone"}}));
	if (b == nullptr) {
		w.ledger.spent += heat;
		return true;
	}
	const double used = w.heat_body(*b, heat);
	w.ledger.spent += heat - used;
	b->props.set("viscous", false);
	b->props.set("own_walls_pass", true);
	b->update_radius();
	w.emit("magma_rift", D({{"actor", a.id}, {"body", b->id}, {"mass", b->mass}, {"liquid", b->liquid}, {"tier", inst.tier()}}));
	return true;
}

bool rift_tick(CombatWorld& w, MatBody& b, double dt) {
	(void)dt;
	if (b.form != Form::Wave || b.attack_id == 0) return false;
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		BodyRef keep = w.bodies[i];
		MatBody& o = *keep;
		if (&o == &b || !o.alive || o.form != Form::Wave || o.attack_owner == b.attack_owner || o.mat != Mat::Sand) continue;
		if (Vec2(o.pos.x - b.pos.x, o.pos.z - b.pos.z).length() > (o.wave_width + b.wave_width) * 0.5) continue;
		const std::string key = "met_" + itos(o.id);
		if (b.props.has(key) && b.props.get(key).as_int() > w.tick - 20) continue;
		b.props.set(key, w.tick);
		AgentRef counter = Agent::of_body(w, b);
		counter->actor = w.get_actor(b.attack_owner);
		AgentRef th = Agent::of_body(w, o, counter->actor);
		IxCtx ctx;
		ctx.site = "clash";
		Interactions::resolve(w, *th, *counter, ctx);
	}
	return false;
}

bool corona_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	MatBody* z = VerbZone::spawn(w, a, inst, D({{"pos", a.pos}}));
	z->props.set("blue", true);
	z->props.set("spare_owner", true);
	const double r = z->zone_radius;
	for (const auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0 || !w._in_zone(*z, t.pos + V3(0, 0.9, 0), Sim::ACTOR_RADIUS)) continue;
		Vec3 d = t.pos - a.pos;
		d.y = 0.0f;
		AgentRef v = Agent::of_volume(&w, &a, &inst, "blue_fire", a.chest(), d.normalized(), D({{"H", z->heat_payload / 20.0}}));
		w.hit_actor(t,
		            D({{"attacker", a.id}, {"attack_id", inst.attack_id}, {"damage", Charge::paramf(inst, "damage", 8.0)},
		               {"balance", Charge::paramf(inst, "balance", 16.0)}, {"knock", d.normalized() * 3.0 + V3(0, 1.0, 0)}, {"kind", "blue"},
		               {"from", a.chest()}, {"power", v->power}, {"tier", inst.tier()}, {"mat", "blue"}}),
		            v);
	}
	w.emit("corona", D({{"actor", a.id}, {"body", z->id}, {"radius", r}, {"tier", inst.tier()}}));
	return true;
}

bool kiln_execute(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double heat = Verbs::take_heat(inst);
	MatBody* b = w.find_body(a, a.forward(), Charge::paramf(inst, "range", 2.5) + 0.5, 180.0, [](MatBody& x) {
		return x.controller < 0 && !x.static_body && x.form != Form::Zone && x.form != Form::Pool && x.form != Form::Puddle && x.form != Form::Wall &&
		       (x.is_stone() || x.mat == Mat::Metal || x.mat == Mat::Sand || x.mat == Mat::Glass);
	});
	if (b == nullptr) {
		w.ledger.spent += heat;
		w.emit("whiff", D({{"actor", a.id}, {"move", inst.id}}));
		return true;
	}
	AgentRef src = Agent::of_volume(&w, &a, &inst, "blue_fire", a.chest(), Vec3(), D({{"H", heat / 20.0}, {"heat_hu", heat}}));
	const double tt = Charge::paramf(inst, "target_temp", 600.0);
	const double need = maxf(0.0, tt + 110.0 - b->temp) * b->mass * Materials::c(b->mat);
	FireUtil::transfer(w, src.get(), b, minf(need, heat));
	w.ledger.spent += src->heat;
	if (b->mat == Mat::Sand) {
		w.convert_mat(*b, Mat::Glass, "sand_to_glass");
		w.emit("transform", D({{"body", b->id}, {"at", b->pos}, {"from", "sand"}, {"to", "glass"}, {"why", "kiln"}}));
	}
	MatBody* z = w.spawn_zone("kiln", b->pos, b->radius + 0.3, a.id, 0.0, Mat::Air, 0.0, 12.0, "kiln:" + itos(a.id));
	z->props.set("follow", b->id);
	z->props.set("spare_owner", true);
	b->props.set("kiln", a.id);
	Verbs::fx(w, a, inst, "beam",
	          D({{"pos", a.hand_point()}, {"body", b->id}, {"length", (a.chest() - b->pos).length()}, {"shape", "short"},
	             {"path", A({Value(a.hand_point()), Value(b->pos)})}}));
	w.emit("kiln", D({{"actor", a.id}, {"body", b->id}, {"temp", b->temp}}));
	return true;
}

void kiln_tick(CombatWorld& w, MatBody& z, double dt) {
	(void)dt;
	MatBody* b = w.get_body(dint(z.props, "follow", -1));
	if (b == nullptr || !b->alive) {
		w.close_zone(z, "kiln_gone");
		return;
	}
	z.pos = b->pos;
	if (b->controller >= 0 && b->controller != z.owner) {
		ActorState* h = w.get_actor(b->controller);
		if (h != nullptr) {
			w.release_body(*h, V3(0, -1, 0), false);
			w.hit_actor(*h, D({{"attacker", z.owner}, {"attack_id", w.new_attack_id()}, {"damage", 10.0}, {"balance", 24.0},
			                   {"knock", -h->forward() * 2.0 + V3(0, 1.0, 0)}, {"kind", "blue"}, {"from", b->pos}, {"unblockable", true}, {"mat", "blue"}}));
			Status::apply(w, *h, "kiln_burn", 1.5, 1.0, z.owner);
			w.emit("kiln_burn", D({{"actor", h->id}, {"body", b->id}, {"by", z.owner}}));
			FxEvents::fx(w, "burst", "blue",
			             D({{"actor", z.owner}, {"pos", b->pos}, {"radius", 1.0}, {"power", 8.0}, {"shape", "small"}, {"body", b->id}}));
		}
		b->props.erase("kiln");
		w.close_zone(z, "sprung");
		return;
	}
	if (b->temp < 300.0) {
		b->props.erase("kiln");
		w.close_zone(z, "cooled");
	}
}

Dict smelter_preview(CombatWorld& w, ActorState& a, Vec3 dir) {
	MatBody* b = ActFire::scorch_target(w, a, dir);
	if (b == nullptr) {
		const int aid = a.id;
		b = w.find_body(a, dir, 6.0, 40.0, [aid](MatBody& x) {
			return x.controller != aid && x.form != Form::Pool && x.form != Form::Zone && x.mass >= 0.5 && !x.static_body && x.mat != Mat::Fire &&
			       x.mat != Mat::Air && Interactions::allows(x, "heat_ranged");
		});
	}
	if (b == nullptr) return D({{"mode", "SMELT"}, {"body", -1}, {"ok", false}, {"reason", "target"}, {"label", "SMELT"}});
	return D({{"mode", "SMELT"}, {"body", b->id}, {"ok", true}, {"reason", ""}, {"label", "SMELT"}});
}

void afterburn_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	(void)it;
	if (inst.phase != ActionPhase::Channel) return;
	const Vec3 last = dvec(inst.data, "last_step", a.pos);
	if (!inst.data.has("last_step") || Vec2(a.pos.x - last.x, a.pos.z - last.z).length() >= 1.6) {
		inst.data.set("last_step", a.pos);
		const double paid = w.pay_heat(a, 8.0);
		if (paid > 0.0) {
			MatBody* z = FireUtil::spawn_field(w, a.id, a.pos, 0.55, 1.2, paid, true);
			z->props.set("dps", 3.0);
		}
	}
}

}  // namespace FireBlue
}  // namespace ff
