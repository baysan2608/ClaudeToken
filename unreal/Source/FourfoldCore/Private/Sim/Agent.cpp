// Fourfold core - port of game/core/agent.gd.
#include "Sim/Agent.h"

#include "Combat/Moves.h"
#include "Sim/ActorState.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Materials.h"
#include "Sim/MatBody.h"
#include "Util/GdUtil.h"

namespace ff {

AgentRef Agent::of_body(CombatWorld& w, MatBody& b, const ActorState* against) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "body";
	g->body = &b;
	g->cls = Interactions::classify(b);
	g->ccls = Interactions::counter_class(b, &w);
	g->mat = static_cast<int>(b.mat);
	g->pos = b.pos;
	g->mass = b.mass;
	g->speed = b.vel.length();
	g->dir = g->speed > 1e-4 ? b.vel.normalized() : Vec3();
	g->tier = b.tier;
	int owner = b.attack_owner;
	if (owner < 0) owner = b.controller >= 0 ? b.controller : b.owner;
	if (owner < 0 && b.form == Form::Wall) owner = b.last_actor;
	g->actor = owner >= 0 ? w.get_actor(owner) : nullptr;
	g->hostile = b.attack_id != 0 && (against == nullptr || b.attack_owner != against->id);
	const double e = b.thermal_energy();
	g->heat = maxf(0.0, e);
	g->ch.K = b.mass * g->speed / 20.0;
	g->ch.H = maxf(0.0, e) / 20.0;
	g->ch.C = maxf(0.0, -e) / 20.0;
	g->ch.E = b.charge;
	if (b.power > 0.0) {
		const std::string chn = dstr(b.props, "channel", "P");
		if (double* s = g->ch.slot(chn)) *s += b.power;
	}
	if (b.form == Form::Zone || b.form == Form::Cloud || b.power > 0.0) g->power = b.power > 0.0 ? b.power : -1.0;
	if (const ChannelFn* hook = Interactions::channel_hook(b.tag)) (*hook)(w, b, *g);
	return g;
}

AgentRef Agent::of_volume(CombatWorld* w, ActorState* a, ActionInst* inst, const std::string& cls, Vec3 pos, Vec3 dir,
                          const Dict& channels) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "volume";
	g->cls = cls;
	g->ccls = cls;
	g->actor = a;
	g->inst = inst;
	g->def = inst != nullptr ? inst->def : Dict();
	g->pos = pos;
	g->dir = dir;
	g->tier = inst != nullptr ? inst->tier() : 0;
	double best = 0.0;
	for (const auto& it : channels) {
		if (it.first == "heat_hu") continue;
		g->ch.set(it.first, it.second.as_float());
		best = maxf(best, it.second.as_float());
	}
	g->power = best;
	if (channels.has("heat_hu")) g->heat = dnum(channels, "heat_hu");
	g->mat = -1;
	g->hostile = true;
	if (w != nullptr && inst != nullptr) {
		const double cp = Charge::counter_power(Charge::pdef(*inst), g->tier);
		if (cp >= 0.0) g->power = cp;
	}
	return g;
}

AgentRef Agent::of_hit(CombatWorld& w, const Dict& info, const AgentRef& agent) {
	if (agent) return agent;
	MatBody* b = w.get_body(dint(info, "body", -1));
	if (b != nullptr && b->alive) {
		AgentRef gb = of_body(w, *b);
		gb->hostile = true;
		return gb;
	}
	const std::string kind = dstr(info, "kind", "");
	const std::string cls = Interactions::kind_class(kind, "blast");
	const std::string chn = Interactions::class_channel(cls, "P");
	Dict ch;
	ch.set(chn, dnum(info, "power", dnum(info, "damage", 0.0)));
	return of_volume(&w, w.get_actor(dint(info, "attacker", -1)), nullptr, cls, dvec(info, "from"), Vec3(), ch);
}

AgentRef Agent::of_guard(CombatWorld& w, ActorState& a) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "guard";
	g->actor = &a;
	g->pos = a.chest();
	g->dir = a.forward();
	g->perfect = w.perfect_guard(a);
	ActionInst* inst = a.action.get();
	Dict spec;
	if (inst != nullptr && inst->id == "guard") {
		g->inst = inst;
		spec = ddict(inst->data, "spec_def");
		g->tier = inst->tier();
	}
	g->def = spec;
	const Value& c = spec.get("counter");
	if (c.has("cls")) {
		g->ccls = vstr(c.get("cls"));
		g->power = Charge::counter_power(spec, g->tier);
		if (g->power < 0.0) {
			MatBody* hb = w.held(a);
			if (hb != nullptr) {
				g->body = hb;
				g->power = hb->mass * Materials::hardness(*hb);
			} else {
				g->power = Interactions::PLAIN_GUARD_CP;
			}
		}
	} else {
		const int ge = w.guard_element(a);
		MatBody* hb2 = w.held(a);
		switch (ge) {
			case Sim::EARTH:
				g->ccls = "guard_earth";
				g->power = Interactions::PLAIN_GUARD_CP;
				break;
			case Sim::WATER:
				if (hb2 != nullptr && hb2->is_water() && inst != nullptr && dtruthy(inst->data, "shield")) {
					g->ccls = "shield_water";
					g->body = hb2;
					g->power = hb2->mass * Materials::hardness(*hb2);
				} else {
					g->ccls = "guard";
					g->power = Interactions::PLAIN_GUARD_CP;
				}
				break;
			case Sim::FIRE:
				g->ccls = "aura_flame";
				g->power = Interactions::PLAIN_GUARD_CP;
				break;
			case Sim::AIR:
				g->ccls = "guard_wind";
				g->power = Interactions::WIND_GUARD_CP;
				break;
			default: break;
		}
	}
	g->cls = g->ccls;
	return g;
}

AgentRef Agent::of_move(CombatWorld* w, ActorState* a, const std::string& move_id, int tier, bool perfect) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "move";
	g->actor = a;
	g->tier = tier;
	g->perfect = perfect;
	g->def = Moves::defs().get(move_id).as_dict();
	if (a != nullptr) {
		g->pos = a->chest();
		g->dir = a->forward();
	}
	const Value& c = g->def.get("counter");
	if (c.has("cls")) g->ccls = vstr(c.get("cls"));
	else if (move_id == "guard" || g->def.empty()) g->ccls = "guard";
	const double cp = Charge::counter_power(g->def, tier);
	if (cp >= 0.0) {
		g->power = cp;
		// Moves whose power depends on the fighter's situation (Tidal Rush: the water in reach) scale it here.
		const CounterScaleHook sc = Hooks::counter_scale_of(g->def.get("counter_scale"));
		if (sc != nullptr && w != nullptr && a != nullptr) g->power = cp * sc(*w, *a, tier);
	} else if (dstr(g->def, "verb", "") == "barrier") {
		const double m = Charge::pgetf(g->def, tier, "mass", 0.0);
		const double hard = vnum(Charge::pget(g->def, tier, "hardness", Materials::prop(def_mat(g->def), "hardness", Value(0.25))));
		g->power = m * hard;
	} else if (g->ccls == "guard") {
		g->power = Interactions::PLAIN_GUARD_CP;
	}
	g->cls = g->ccls;
	return g;
}

int Agent::def_mat(const Dict& def) {
	const Value& m = def.get("mat", Value("stone"));
	if (m.is_int()) return static_cast<int>(m.as_int());
	return maxi(0, Sim::mat_index(vstr(m)));
}

AgentRef Agent::of_env(CombatWorld* w, const std::string& kind, Vec3 pos, ActorState* a) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "env";
	g->ccls = kind;
	g->cls = g->ccls;
	g->pos = pos;
	g->actor = a;
	g->power = Interactions::ENV_CP;
	if (kind == "pool" && w != nullptr) g->body = w->pool;
	else if (kind == "puddle" && w != nullptr) g->body = w->puddle_at(pos);
	return g;
}

AgentRef Agent::of_stance(CombatWorld* /*w*/, ActorState& a) {
	AgentRef g = std::make_shared<Agent>();
	g->kind = "stance";
	g->actor = &a;
	g->ccls = "anchor";
	g->cls = g->ccls;
	g->pos = a.pos;
	const Dict st = ddict(a.status, "anchored");
	g->power = !st.empty() ? dnum(st, "mag", 0.0) : dnum(ddict(a.status, "stance_cp"), "mag", 0.0);
	if (g->power <= 0.0) g->power = a.anchored ? 30.0 : 0.0;
	g->perfect = false;
	return g;
}

}  // namespace ff
