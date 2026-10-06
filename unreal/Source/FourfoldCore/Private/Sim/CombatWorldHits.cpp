// Fourfold core - port of game/core/combat_world.gd: hits, guards, line queries, grips and control.
#include "Sim/CombatWorld.h"

#include "Combat/Moves.h"
#include "Sim/Agent.h"
#include "Sim/FxEvents.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

#include <algorithm>
#include <cmath>

namespace ff {
namespace {

const char* cw_mat_of_kind(const std::string& kind) {
	static const char* const kMap[][2] = {{"stone", "stone"}, {"water", "water"},   {"lava", "magma"},   {"fire", "flame"},
	                                      {"air", "wind"},     {"lightning", "lightning"}, {"blast", "blast"}, {"sound", "sound"},
	                                      {"ice", "ice"},      {"sand", "sand"},     {"steam", "steam"},  {"vacuum", "vacuum"},
	                                      {"metal", "metal"},  {"plant", "plant"},   {"blue", "blue"},    {"frost", "ice"}};
	for (const auto& p : kMap)
		if (kind == p[0]) return p[1];
	return "";
}

}  // namespace

std::string CombatWorld::hit_actor(ActorState& t, const Dict& info, AgentRef agent_in) {
	const int aid = dint(info, "attack_id", 0);
	if (t.hits_taken.count(aid)) return "dup";
	t.hits_taken[aid] = tick;
	_mark_contact(info);
	const Vec3 from = dvec(info, "from", t.pos + t.forward());
	Vec3 to_src = from - t.pos;
	to_src.y = 0.0f;
	const bool facing_ok = to_src.length() < 0.01f || t.forward().dot(to_src.normalized()) > -0.15f;
	double dmg = dnum(info, "damage");
	double bal = dnum(info, "balance");
	const std::string kind = dstr(info, "kind", "");
	if (t.iframes > 0.0) {
		emit("evaded", D({{"actor", t.id}, {"attack", aid}, {"kind", kind}}));
		t.last_result = "evaded";
		return "evaded";
	}
	AgentRef agent;
	double knock_scale = 1.0;
	if (t.guarding && !dbool(info, "unblockable", false)) {
		// The guard is a counter: the rule cell (threat class x guard class) decides.
		agent = Agent::of_hit(*this, info, agent_in);
		if (info.has("facing_vel") && !dbool(info, "facing_vel") && perfect_guard(t)) {
			// Legacy: a perfect guard turns a projectile only when it faces the incoming velocity.
			Dict vi = info.duplicate();
			vi.erase("agent");
			vi.erase("body");
			agent = Agent::of_hit(*this, vi, nullptr);
		}
		AgentRef counter = Agent::of_guard(*this, t);
		const Dict rl = Interactions::rule(agent->cls, counter->ccls, counter->tier);
		const bool gface = facing_ok || (counter->perfect && dbool(info, "facing_vel", false));
		if (gface || dtruthy(rl, "aura")) {
			IxCtx ctx;
			ctx.info = info;
			ctx.info_agent = agent_in;
			ctx.target = &t;
			const IxResult res = Interactions::resolve(*this, *agent, *counter, ctx);
			if (!res.result.empty()) return res.result;
			if (res.stopped && res.pass_scale <= 0.0) {
				// The guard stopped the threat outright: a clean block.
				Dict bev = D({{"actor", t.id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", res.outcome}, {"clean", true}});
				bev.merge(_hit_meta(info, agent.get(), t), false);
				emit("block", bev);
				t.last_result = "block";
				return "block";
			}
			dmg *= res.pass_scale;
			bal *= res.pass_scale;
			knock_scale = res.pass_scale;
		}
	}
	if (kind == "lightning" && t.wetness > 0.3) dmg *= 1.5;
	Vec3 knock = dvec(info, "knock") * knock_scale;
	if (t.anchored || !t.status.empty() || t.armor > 0.0) {
		if (agent == nullptr) agent = Agent::of_hit(*this, info, agent_in);
		if (Interactions::family(agent->cls) == "pressure" && (t.anchored || t.status.has("anchored"))) {
			AgentRef st = Agent::of_stance(this, t);
			IxCtx ctx;
			ctx.info = info;
			ctx.info_agent = agent_in;
			const IxResult sres = Interactions::resolve(*this, *agent, *st, ctx);
			knock *= sres.knock_scale;
		}
		if (agent->ch.K > 0.0 || kind == "stone" || kind == "metal") dmg *= 1.0 - Status::armor(t);
		if (Status::immune(t, "knockback")) knock = Vec3(0.0f, knock.y, 0.0f);
		if (Status::immune(t, "lift")) knock.y = std::min(knock.y, 0.0f);
	}
	t.health = maxf(0.0, t.health - dmg);
	t.balance -= bal;
	t.balance_idle = 0.0;
	t.vel += knock;
	if (knock.y > 0.0f) t.grounded = false;
	t.last_hit_dir = to_src.length() > 0.01f ? -to_src.normalized() : -t.forward();
	std::string res2 = "hit";
	const bool armored = !t.stance.empty() && (t.armor > 0.0 || t.anchored) && t.balance > 0.0;
	if (t.balance <= 0.0) {
		_stagger(t, "knockdown", 1.1, info);
		t.balance = 45.0;
		res2 = "knockdown";
	} else if (armored) {
		// the hit event carries armored = true
	} else if (bal >= 25.0) {
		_stagger(t, "heavy", 0.5, info);
	} else {
		_stagger(t, "light", 0.26, info);
	}
	Dict ev = D({{"actor", t.id},
	             {"attacker", info.get("attacker", Value(-1))},
	             {"damage", dmg},
	             {"kind", kind},
	             {"result", res2},
	             {"body", info.get("body", Value(-1))},
	             {"armored", armored && res2 == "hit"}});
	ev.merge(_hit_meta(info, agent ? agent.get() : agent_in.get(), t), false);
	emit("hit", ev);
	t.last_result = res2;
	return res2;
}

Dict CombatWorld::_hit_meta(const Dict& info, const Agent* agent, const ActorState& t) const {
	const Agent* g = agent;
	Vec3 d = dvec(info, "knock");
	if (d.length() < 1e-4f) d = t.pos - dvec(info, "from", t.pos);
	d.y = 0.0f;
	std::string mat = dstr(info, "mat", "");
	double power = dnum(info, "power", dnum(info, "damage", 0.0));
	int tier = dint(info, "tier", 0);
	if (g != nullptr) {
		power = g->total();
		tier = maxi(tier, g->tier);
		if (mat.empty() && g->body != nullptr) mat = FxEvents::mat_of(*g->body);
	}
	if (mat.empty()) mat = cw_mat_of_kind(dstr(info, "kind", ""));
	return D({{"power", power}, {"mat", mat}, {"tier", tier}, {"dir", d.length() > 1e-4f ? d.normalized() : Vec3()}});
}

void CombatWorld::_mark_contact(const Dict& info) {
	ActorState* att = get_actor(dint(info, "attacker", -1));
	if (att == nullptr || att->action == nullptr) return;
	const int aid = dint(info, "attack_id", 0);
	if (att->action->attack_id == aid || dint(info, "src_attack", -1) == att->action->attack_id) att->action->data.set("contact", true);
}

std::string CombatWorld::guard_chip(ActorState& t, const Dict& info, const Dict& rule, const Agent* threat, double ratio) {
	const std::string kind = dstr(rule, "kind", dstr(info, "kind", ""));
	double chip = dnum(rule, "chip", 0.12);
	double balm = dnum(rule, "bal", 0.55);
	double kn = dnum(rule, "knock", 0.35);
	if (dbool(rule, "chip_scale", false) && ratio > 0.0) {
		const double mult = maxf(1.0, 0.5 / ratio);
		chip = minf(chip * mult, 0.9);
		balm *= mult;
		kn = minf(kn * mult, 1.0);
	}
	Dict ev = D({{"actor", t.id}, {"attacker", info.get("attacker", Value(-1))}, {"kind", kind}});
	ev.merge(_hit_meta(info, threat, t), false);
	if (chip == 0.0 && balm == 0.0 && kn == 0.0) {
		emit("block", ev);
		return "block";
	}
	const double dmg = dnum(info, "damage", 0.0);
	const double bal = dnum(info, "balance", 0.0);
	t.health -= dmg * chip;
	t.balance -= bal * balm;
	t.balance_idle = 0.0;
	const Vec3 kb = dvec(info, "knock");
	t.vel += kb * kn;
	emit("block", ev);
	t.last_result = "block";
	if (t.balance <= 0.0) {
		_stagger(t, "guard_break", 0.7, info);
		t.balance = 35.0;
		return "guard_break";
	}
	return "block";
}

void CombatWorld::_stagger(ActorState& t, const std::string& kind, double dur, const Dict&) {
	if (t.action != nullptr) interrupt_action(t, "hit");
	t.guarding = false;
	t.gliding = false;
	t.stun = maxf(t.stun, dur);
	t.stun_kind = kind;
	t.buffered = "";
	emit("stagger", D({{"actor", t.id}, {"kind", kind}}));
}

bool CombatWorld::perfect_guard(const ActorState& t) const {
	if (!t.guarding || t.guard_tick < 0) return false;
	if (t.action != nullptr && dtruthy(t.action->data, "mashed")) return false;
	return static_cast<double>(tick - t.guard_tick) * Sim::DT <= Moves::PERFECT_WINDOW;
}

int CombatWorld::guard_element(const ActorState& a) const {
	if (a.action != nullptr && a.action->id == "guard") return a.action->element;
	return a.element;
}

std::vector<ActorState*> CombatWorld::actors_in_cone(const ActorState& a, Vec3 dir, double rng_m, double cone_deg) const {
	std::vector<ActorState*> out;
	const double cos_lim = std::cos(deg_to_rad(cone_deg));
	for (const auto& tp : actors) {
		ActorState& t = *tp;
		if (&t == &a || t.team == a.team || t.health <= 0.0) continue;
		Vec3 to = t.pos - a.pos;
		to.y = 0.0f;
		const double d = to.length();
		if (d > rng_m + Sim::ACTOR_RADIUS) continue;
		if (d > 0.5 && to.normalized().dot(dir) < cos_lim) continue;
		if (!arena.has_los(a.chest(), t.chest())) continue;
		if (_wall_between(a.chest(), t.chest())) continue;
		out.push_back(&t);
	}
	return out;
}

bool CombatWorld::_wall_between(Vec3 p0, Vec3 p1) const { return wall_hit(p0, p1) >= 0.0; }

double CombatWorld::wall_hit(Vec3 p0, Vec3 p1) const {
	double best = -1.0;
	for (const BodyRef& bp : bodies) {
		const MatBody& b = *bp;
		if (!b.alive || b.form != Form::Wall || b.wall_rise <= 0.5) continue;
		const double t = wall_segment_t(p0, p1, b);
		if (t >= 0.0 && (best < 0.0 || t < best)) best = t;
	}
	return best;
}

double CombatWorld::wall_segment_t(Vec3 p0, Vec3 p1, const MatBody& b) const {
	const double c = std::cos(b.wall_yaw);
	const double s = std::sin(b.wall_yaw);
	const Vec3 r0 = p0 - b.pos;
	const Vec3 r1 = p1 - b.pos;
	const Vec3 l0 = V3(r0.x * c - r0.z * s, r0.y, r0.x * s + r0.z * c);
	const Vec3 l1 = V3(r1.x * c - r1.z * s, r1.y, r1.x * s + r1.z * c);
	const Vec3 mn(-b.wall_half.x, -0.2f, -b.wall_half.z);
	const Vec3 mx = V3(b.wall_half.x, b.wall_half.y * 2.0 * b.wall_rise, b.wall_half.z);
	return ArenaMap::slab(l0, l1 - l0, mn, mx);
}

bool CombatWorld::los(Vec3 p0, Vec3 p1) const { return arena.has_los(p0, p1) && !_wall_between(p0, p1); }

// ============================================================== grips / control

void CombatWorld::request_grip(ActorState& a, MatBody& b, double strength, const std::string& verb) {
	if (b.mass > a.max_control_mass) {
		emit("control_fail", D({{"actor", a.id}, {"body", b.id}, {"reason", "mass"}, {"mass", b.mass}}));
		return;
	}
	_grips.push_back(GripRequest{a.id, b.id, strength, verb, a.action});
}

double CombatWorld::grip_strength(const ActorState& a, const MatBody& b, double base, double reach) const {
	const double d = a.chest().distance_to(b.pos);
	const double falloff = 1.0 - 0.45 * clampf((d - 2.0) / maxf(reach - 2.0, 0.1), 0.0, 1.0);
	const double mass_pen = 0.35 * clampf(b.mass / a.max_control_mass, 0.0, 1.0);
	const double focus_pen = a.focus > 5.0 ? 0.0 : 0.3;
	return maxf(0.0, base * falloff - mass_pen - focus_pen);
}

void CombatWorld::_resolve_grips() {
	if (_grips.empty()) return;
	std::stable_sort(_grips.begin(), _grips.end(), [](const GripRequest& x, const GripRequest& y) {
		if (x.body != y.body) return x.body < y.body;
		if (!is_equal_approx(x.strength, y.strength)) return x.strength > y.strength;
		return x.actor < y.actor;
	});
	std::vector<int> done;
	const std::vector<GripRequest> grips = std::move(_grips);
	_grips.clear();
	for (const GripRequest& g : grips) {
		MatBody* b = get_body(g.body);
		ActorState* a = get_actor(g.actor);
		if (b == nullptr || !b->alive || a == nullptr) continue;
		// Withdrawn unless the action that reached is still channelling.
		if (a->stun > 0.0 || a->action == nullptr || a->action != g.inst || a->action->phase != ActionPhase::Channel) continue;
		if (std::find(done.begin(), done.end(), b->id) != done.end()) {
			if (b->controller != a->id) emit("control_fail", D({{"actor", a->id}, {"body", b->id}, {"reason", "contest"}}));
			continue;
		}
		done.push_back(b->id);
		if (b->controller == a->id) {
			b->authority = g.strength;
			continue;
		}
		double holder = 0.0;
		if (b->controller >= 0) holder = b->authority;
		else if (b->residual_owner >= 0 && b->residual_owner != a->id) holder = b->residual_authority;
		if (g.strength > holder + (holder > 0.0 ? GRIP_MARGIN : 0.0) && g.strength > 0.05) {
			const int prev = b->controller;
			if (prev >= 0) {
				ActorState* pa = get_actor(prev);
				if (pa != nullptr) {
					pa->held_body = -1;
					emit("control_lost", D({{"actor", prev}, {"body", b->id}, {"reason", "contest"}, {"by", a->id}}));
				}
			}
			take_control(*a, *b, g.strength, g.verb);
		} else {
			emit("control_fail", D({{"actor", a->id}, {"body", b->id}, {"reason", "contest"}}));
		}
	}
}

void CombatWorld::take_control(ActorState& a, MatBody& b, double strength, const std::string& verb) {
	// Catching a moving body: its momentum goes into the catcher (pushback + Focus).
	const Vec3 impulse = b.vel * b.mass;
	if (impulse.length() > 20.0f) {
		Vec3 push = impulse / Sim::ACTOR_MASS * 0.45;
		push.y = 0.0f;
		a.vel += push;
		spend_focus(a, minf(a.focus, impulse.length() * 0.02));
		emit("intercept", D({{"actor", a.id}, {"body", b.id}, {"impulse", impulse.length()}, {"verb", verb}}));
	}
	if (a.held_body >= 0 && a.held_body != b.id) release_body(a, Vec3(), false);
	if (b.form != Form::Wave && !b.wave_path.empty()) {
		b.wave_path.clear();
		emit("reform", D({{"body", b.id}}));
	}
	b.controller = a.id;
	b.authority = strength;
	b.residual_owner = -1;
	b.residual_authority = 0.0;
	b.attack_id = 0;
	b.attack_owner = -1;
	b.hit_set.clear();
	b.on_ground = false;
	b.rest_time = 0.0;
	b.age = 0.0;
	b.hold_point = a.hand_point();
	b.touch(a.id, verb, tick);
	a.held_body = b.id;
	emit("control_won", D({{"actor", a.id}, {"body", b.id}, {"verb", verb}}));
}

MatBody* CombatWorld::release_body(ActorState& a, Vec3 v, bool as_attack, double damage, double balance) {
	MatBody* b = get_body(a.held_body);
	a.held_body = -1;
	if (b == nullptr || !b->alive) return nullptr;
	b->controller = -1;
	b->authority = 0.0;
	b->vel = v;
	b->residual_owner = a.id;
	b->residual_authority = Interactions::cohesion(b->tier);
	b->touch(a.id, "release", tick);
	if (as_attack) {
		b->attack_id = new_attack_id();
		b->attack_owner = a.id;
		b->hit_set.clear();
		b->hit_set.add(a.id);
		b->damage = damage;
		b->balance_damage = balance;
	}
	emit("release", D({{"actor", a.id}, {"body", b->id}, {"attack", as_attack}}));
	return b;
}

MatBody* CombatWorld::held(ActorState& a) {
	MatBody* b = get_body(a.held_body);
	if (b == nullptr || !b->alive || b->controller != a.id) {
		a.held_body = -1;
		return nullptr;
	}
	return b;
}

}  // namespace ff
