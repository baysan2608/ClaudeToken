// Fourfold core - port of game/combat/verbs/verb_volume.gd: instant volumes (cone, beam, burst). A volume is an Agent:
// a threat to the fighters it reaches and to barrier bodies, a counter to the loose bodies it meets.
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
namespace VerbVolume {

AgentRef _volume(CombatWorld& w, ActorState& a, ActionInst& inst, Vec3 pos, Vec3 dir) {
	const std::string cls = Charge::params(inst, "cls", _default_cls(inst));
	const std::string chn = Charge::params(inst, "channel", Interactions::class_channel(cls, "P"));
	const double heat = Verbs::take_heat(inst);
	const double power = Charge::paramf(inst, "power", chn == "H" ? heat / Interactions::HU_PER_PU : 8.0);
	Dict ch;
	ch.set(chn, power);
	if (heat > 0.0) {
		ch.set("heat_hu", heat);
		if (chn != "H") ch.set("H", heat / Interactions::HU_PER_PU);
	}
	AgentRef v = Agent::of_volume(&w, &a, &inst, cls, pos, dir, ch);
	v->data.set("knock", Charge::paramf(inst, "knock", 3.0));
	return v;
}

std::string _default_cls(const ActionInst& inst) {
	const std::string m = Verbs::fx_mat(inst);
	if (m == "flame" || m == "magma") return "flame";
	if (m == "blue") return "blue_fire";
	if (m == "lightning") return "lightning";
	if (m == "blast") return "blast";
	if (m == "water") return "water";
	if (m == "sand") return "sand";
	if (m == "steam" || m == "mist") return "steam";
	if (m == "vacuum") return "vacuum";
	if (m == "sound") return "sound";
	if (m == "ice") return "frost";
	return "gust";
}

Dict _hit_info(const ActorState& a, const ActionInst* inst, const Agent& v, Vec3 from, Vec3 knock_dir) {
	const double lift = inst != nullptr ? Charge::paramf(*inst, "lift", 1.0) : 1.0;
	return D({{"attacker", a.id},
	          {"attack_id", inst != nullptr ? inst->attack_id : 0},
	          {"damage", inst != nullptr ? Charge::paramf(*inst, "damage", 6.0) : 6.0},
	          {"balance", inst != nullptr ? Charge::paramf(*inst, "balance", 14.0) : 14.0},
	          {"knock", knock_dir * dnum(v.data, "knock", 3.0) + V3(0, lift, 0)},
	          {"kind", v.cls},
	          {"from", from},
	          {"power", v.power},
	          {"tier", v.tier},
	          {"mat", inst != nullptr ? Verbs::fx_mat(*inst) : std::string()}});
}

void _after_hit(CombatWorld& w, ActorState& a, ActionInst& inst, ActorState& t, const std::string& res) {
	if (res == "hit" || res == "knockdown") {
		const std::string st = Charge::params(inst, "status", "");
		if (!st.empty())
			Status::apply(w, t, st, Charge::paramf(inst, "status_t", 1.0), Charge::paramf(inst, "status_mag", 1.0), a.id);
		if (Charge::paramb(inst, "wet", false)) t.wetness = 1.0;
	}
}

IxResult meet_body(CombatWorld& w, ActorState* a, Agent& v, MatBody& b) {
	IxCtx ctx;
	ctx.site = "volume";
	if (Interactions::is_barrier(w, b)) {
		AgentRef c = Agent::of_body(w, b);
		c->actor = w.get_actor(b.form == Form::Wall ? b.last_actor : (b.controller >= 0 ? b.controller : b.owner));
		return Interactions::resolve(w, v, *c, ctx, &Interactions::PASS_RULE());
	}
	AgentRef th = Agent::of_body(w, b, a);
	return Interactions::resolve(w, *th, v, ctx, &Interactions::PASS_RULE());
}

AgentRef cone(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 5.0);
	const double ang = Charge::paramf(inst, "angle", 30.0);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	AgentRef v = _volume(w, a, inst, a.chest(), dir);
	Verbs::fx(w, a, inst, "cone", D({{"length", rng_m}, {"angle", ang}, {"power", v->power}}));
	const double cos_lim = std::cos(deg_to_rad(ang));
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || (b.static_body && b.form != Form::Wall) || b.controller == a.id || &b == w.pool) continue;
		const Vec3 to = b.pos - a.chest();
		const Vec3 fl(to.x, 0.0f, to.z);
		if (fl.length() > rng_m + b.radius || (fl.length() > 0.5f && fl.normalized().dot(dir) < cos_lim)) continue;
		meet_body(w, &a, *v, b);
	}
	for (ActorState* t : w.actors_in_cone(a, dir, rng_m, ang)) {
		const std::string res = w.hit_actor(*t, _hit_info(a, &inst, *v, a.chest(), dir), v);
		_after_hit(w, a, inst, *t, res);
	}
	if (v->heat > 0.0) {
		w.ledger.spent += v->heat;
		v->heat = 0.0;
	}
	return v;
}

AgentRef beam(CombatWorld& w, ActorState& a, ActionInst& inst) {
	inst.data.set("pulse_t", 0.0);
	return _beam_once(w, a, inst);
}

void beam_tick(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double pulse = Charge::paramf(inst, "pulse", 0.0);
	if (pulse <= 0.0) return;
	inst.data.set("pulse_t", dnum(inst.data, "pulse_t", 0.0) + Sim::DT);
	if (dnum(inst.data, "pulse_t") >= pulse) {
		inst.data.set("pulse_t", 0.0);
		inst.attack_id = w.new_attack_id();
		if (Charge::paramf(inst, "heat", 0.0) > 0.0)
			Verbs::pay_dict(w, a, inst,
			                D({{"heat", Charge::paramf(inst, "heat", 0.0) * pulse / maxf(Charge::paramf(inst, "active", dnum(inst.def, "active")), 0.05)}}));
		_beam_once(w, a, inst);
	}
}

AgentRef _beam_once(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const double rng_m = Charge::paramf(inst, "range", 10.0);
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	const Vec3 start = a.hand_point() + Vec3(0, 0.2f, 0);
	AgentRef v = _volume(w, a, inst, start, dir);
	if (v->cls == "lightning" || Charge::paramb(inst, "conduct", false)) {
		const Vec3 aimp = dvec(inst.data, "aim_point", a.chest() + dir * rng_m);
		const Dict def = D({{"range", rng_m},
		                    {"damage", Charge::paramf(inst, "damage", 10.0)},
		                    {"balance", Charge::paramf(inst, "balance", 20.0)},
		                    {"conduct_budget", Charge::paramf(inst, "conduct_budget", Charge::paramf(inst, "damage", 10.0))},
		                    {"max_hops", Charge::parami(inst, "max_hops", 3)},
		                    {"E", v->power},
		                    {"tier", inst.tier()}});
		const Dict out = Conduction::discharge(w, a, aimp, def, inst.attack_id, Charge::paramb(inst, "allow_redirect", true));
		Verbs::fx(w, a, inst, "beam", D({{"path", out.get("path")}, {"length", rng_m}, {"power", v->power}}));
		return v;
	}
	const double width = Charge::paramf(inst, "width", 0.6);
	Vec3 end = start + dir * rng_m;
	double stop_t = 1.0;
	for (const Conduction::BarrierHit& hb : Conduction::barriers_on(w, start, end)) {
		if (hb.body == nullptr) {
			stop_t = hb.t;
			break;
		}
		const IxResult r = meet_body(w, &a, *v, *hb.body);
		if (r.stopped || r.pass_scale <= 0.0) {
			stop_t = hb.t;
			break;
		}
		v->power *= r.pass_scale;
	}
	end = start.lerp(end, f32(stop_t));
	const Vec3 seg = end - start;
	const double seg_len = seg.length();
	const bool pierce = Charge::paramb(inst, "pierce", false);
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || b.static_body || b.controller == a.id || b.form == Form::Wall) continue;
		const double tb = clampf((b.pos - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tb).distance_to(b.pos) <= width + b.radius) meet_body(w, &a, *v, b);
	}
	struct BeamHit {
		double t;
		ActorState* a;
	};
	std::vector<BeamHit> hits;
	for (auto& tp : w.actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0) continue;
		const double tt = clampf((t.chest() - start).dot(dir), 0.0, seg_len);
		if ((start + dir * tt).distance_to(t.chest()) <= width + Sim::ACTOR_RADIUS + 0.4) hits.push_back({tt, &t});
	}
	std::stable_sort(hits.begin(), hits.end(), [](const BeamHit& x, const BeamHit& y) { return x.t < y.t || (x.t == y.t && x.a->id < y.a->id); });
	for (const BeamHit& h : hits) {
		const std::string res = w.hit_actor(*h.a, _hit_info(a, &inst, *v, start, dir), v);
		_after_hit(w, a, inst, *h.a, res);
		if (!pierce) {
			end = start + dir * h.t;
			break;
		}
	}
	Array path;
	path.append(start);
	path.append(end);
	Verbs::fx(w, a, inst, "beam", D({{"length", start.distance_to(end)}, {"path", path}, {"power", v->power}}));
	if (v->heat > 0.0) {
		w.ledger.spent += v->heat;
		v->heat = 0.0;
	}
	return v;
}

void burst(CombatWorld& w, ActorState& a, ActionInst& inst) {
	const std::string at = Charge::params(inst, "at", "ahead");
	const Vec3 dir = dvec(inst.data, "face", a.forward());
	Vec3 p = a.pos + Vec3(0, 1.0f, 0);
	if (at == "ahead") {
		p = a.pos + dir * Charge::paramf(inst, "distance", 3.0) + Vec3(0, 1.0f, 0);
	} else if (at == "aim") {
		Vec3 ap = dvec(inst.data, "aim_point", a.chest() + dir * 8.0f);
		const double rmax = Charge::paramf(inst, "range", 10.0);
		if (a.chest().distance_to(ap) > rmax) ap = a.chest() + (ap - a.chest()).normalized() * rmax;
		p = ap;
	}
	p.y = f32(maxf(p.y, w.arena.ground_height(p.x, p.z, p.y + 0.5) + 0.5));
	Dict prm = D({{"radius", Charge::paramf(inst, "radius", 2.0)},
	              {"power", Charge::paramf(inst, "power", 8.0)},
	              {"damage", Charge::paramf(inst, "damage", 8.0)},
	              {"balance", Charge::paramf(inst, "balance", 20.0)},
	              {"knock", Charge::paramf(inst, "knock", 6.0)},
	              {"lift", Charge::paramf(inst, "lift", 2.0)},
	              {"cls", Charge::params(inst, "cls", "blast")},
	              {"heat_hu", Verbs::take_heat(inst)},
	              {"mat", Verbs::fx_mat(inst)},
	              {"status", Charge::params(inst, "status", "")},
	              {"status_t", Charge::paramf(inst, "status_t", 1.0)}});
	const double fuse = Charge::paramf(inst, "fuse", 0.0);
	if (fuse > 0.0) {
		MatBody* z = w.spawn_zone("fuse", p, 0.3, a.id, dnum(prm, "power"), Mat::Air, 0.0, -1.0);
		z->props.set("fuse", fuse);
		z->props.set("burst", prm);
		z->tier = inst.tier();
		z->heat_payload = dnum(prm, "heat_hu");
		prm.set("heat_hu", 0.0);
		Verbs::fx(w, a, inst, "cast", D({{"pos", p}, {"dur", fuse}}));
		return;
	}
	burst_at(w, &a, &inst, p, prm);
}

void burst_at(CombatWorld& w, ActorState* a, ActionInst* inst, Vec3 p, const Dict& prm) {
	const std::string cls = dstr(prm, "cls", "blast");
	const std::string chn = Interactions::class_channel(cls, "P");
	Dict ch;
	ch.set(chn, dnum(prm, "power", 8.0));
	const double heat = dnum(prm, "heat_hu", 0.0);
	if (heat > 0.0) ch.set("heat_hu", heat);
	AgentRef v = Agent::of_volume(&w, a, inst, cls, p, Vec3(), ch);
	v->data.set("knock", dnum(prm, "knock", 6.0));
	const double r = dnum(prm, "radius", 2.0);
	FxEvents::fx(w, "burst", dstr(prm, "mat", "blast"),
	             D({{"actor", a != nullptr ? a->id : -1},
	                {"pos", p},
	                {"radius", r},
	                {"power", v->power},
	                {"tier", v->tier},
	                {"move", inst != nullptr ? inst->id : std::string()},
	                {"element", inst != nullptr ? inst->element : -1}}));
	for (size_t i = 0; i < w.bodies.size(); ++i) {
		MatBody& b = *w.bodies[i];
		if (!b.alive || (b.static_body && b.form != Form::Wall) || &b == w.pool) continue;
		if (b.pos.distance_to(p) > r + b.radius) continue;
		if (a != nullptr && b.controller == a->id) continue;
		const Vec3 rel = b.pos - p;
		v->dir = rel.length() > 0.05f ? rel.normalized() : Vec3::Up();
		meet_body(w, a, *v, b);
	}
	const int aid = inst != nullptr ? inst->attack_id : w.new_attack_id();
	for (auto& tp : w.actors) {
		ActorState& t = *tp;
		if (t.health <= 0.0 || (a != nullptr && (&t == a || t.team == a->team))) continue;
		const Vec3 rel2 = t.chest() - p;
		if (rel2.length() > r + Sim::ACTOR_RADIUS) continue;
		if (!w.los(p, t.chest())) continue;
		const Vec3 fl(rel2.x, 0.0f, rel2.z);
		const Vec3 kd = fl.length() > 0.05f ? fl.normalized() : t.forward() * -1.0f;
		const Dict info = D({{"attacker", a != nullptr ? a->id : -1},
		                     {"attack_id", aid},
		                     {"damage", dnum(prm, "damage", 8.0)},
		                     {"balance", dnum(prm, "balance", 20.0)},
		                     {"knock", kd * dnum(prm, "knock", 6.0) + V3(0, dnum(prm, "lift", 2.0), 0)},
		                     {"kind", cls},
		                     {"from", p},
		                     {"power", v->power}});
		const std::string res = w.hit_actor(t, info, v);
		if ((res == "hit" || res == "knockdown") && !dstr(prm, "status", "").empty())
			Status::apply(w, t, dstr(prm, "status"), dnum(prm, "status_t", 1.0), 1.0, a != nullptr ? a->id : -1);
	}
	if (v->heat > 0.0) {
		w.ledger.spent += v->heat;
		v->heat = 0.0;
	}
}

bool fuse_tick(CombatWorld& w, MatBody& z, double) {
	if (z.age >= dnum(z.props, "fuse", 0.5) || dtruthy(z.props, "trigger")) {
		Dict prm = ddict(z.props, "burst").duplicate();
		prm.set("heat_hu", z.heat_payload);
		z.heat_payload = 0.0;
		ActorState* owner = w.get_actor(z.owner);
		burst_at(w, owner, nullptr, z.pos, prm);
		w.close_zone(z, "detonated");
	}
	return true;
}

}  // namespace VerbVolume
}  // namespace ff
