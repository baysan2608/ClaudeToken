// Fourfold core - context counters for the HUD (see CounterHints.h).
#include "App/CounterHints.h"

#include "AI/AiPlanner.h"
#include "App/HudBuilder.h"
#include "Combat/Moves.h"
#include "Sim/Agent.h"
#include "Sim/Charge.h"
#include "Sim/CombatWorld.h"
#include "Sim/Interactions.h"
#include "Util/GdUtil.h"

#include <cctype>

namespace ff {
namespace CounterHints {

namespace {
const char* const kSlots[4] = {"guard", "push", "sink", "tech"};

int band_rank(const IxResult& r) {
	if (r.outcome == "pass") return 0;
	if (r.band == "fail" || r.outcome == "overwhelm") return 1;
	if (r.band == "partial" || r.outcome == "weaken") return 2;
	return 3;   // full, or a form / condition rule with a real outcome
}

const char* band_name(int rank) {
	switch (rank) {
		case 3: return "full";
		case 2: return "partial";
		case 1: return "fail";
		default: return "none";
	}
}
}  // namespace

std::string label_for(const std::string& outcome, const std::string& to) {
	static const Dict kNames = D({{"pass", "No effect"}, {"block", "Block"}, {"weaken", "Weaken"}, {"overwhelm", "Overwhelmed"},
	                              {"deflect", "Deflect"}, {"bend", "Curve away"}, {"absorb", "Absorb"}, {"extinguish", "Douse"},
	                              {"redirect", "Redirect"}, {"slow", "Slow"}, {"disperse", "Scatter"}, {"shatter", "Shatter"},
	                              {"capture", "Seize"}, {"heat", "Heat"}, {"reclaim", "Reclaim"}, {"reflect", "Send back"},
	                              {"sink", "Sink"}, {"conduct", "Conduct"}, {"ground", "Ground"}, {"amplify", "Amplify"},
	                              {"clash", "Clash"}, {"disrupt", "Disrupt"}, {"neutralize", "Neutralize"}, {"push", "Push back"}});
	if (outcome == "transform" && !to.empty()) {
		std::string t = to;
		for (char& ch : t)
			if (ch == '_') ch = ' ';
		return "To " + t;
	}
	if (kNames.has(outcome)) return dstr(kNames, outcome);
	// kit outcomes: "<element>_<verb...>" -> "Verb ..."
	std::string s = outcome;
	for (const char* pre : {"earth_", "water_", "fire_", "air_"}) {
		const std::string p(pre);
		if (s.rfind(p, 0) == 0) {
			s = s.substr(p.size());
			break;
		}
	}
	for (char& ch : s)
		if (ch == '_') ch = ' ';
	if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
	return s;
}

Result query(CombatWorld& w, ActorState& me) {
	Result res;
	if (me.stun > 0.0 || me.health <= 0.0) return res;
	// the most urgent threat: hostile bodies on their way, or a foe's volume attack being wound up
	AiThreat best;
	for (const BodyRef& b : w.bodies) {
		if (!b || !b->alive) continue;
		AiThreat t = AiPlanner::body_threat(w, me, *b);
		if (t.valid && t.tti <= kHorizon && (!best.valid || t.tti < best.tti)) best = t;
	}
	for (const auto& a : w.actors) {
		if (!a || a.get() == &me || a->team == me.team) continue;
		AiThreat t = AiPlanner::action_threat(w, me, a.get());
		if (t.valid && t.tti <= kHorizon && (!best.valid || t.tti < best.tti)) best = t;
	}
	if (!best.valid || !best.agent) return res;
	res.valid = true;
	res.threat_cls = best.cls;
	res.tti = best.tti;
	res.pos = best.body != nullptr ? best.body->pos : best.agent->pos;
	for (const char* slot : kSlots) {
		const std::string id = Moves::resolve(me.element, me.sub(), slot);
		if (id.empty()) continue;
		const Dict def = Moves::defs().get(id).as_dict();
		if (std::string(slot) != "guard" && !def.has("counter")) continue;
		if (std::string(slot) == "guard" && id != "guard" && !def.has("counter") && dstr(def, "verb", "") != "barrier") continue;
		if (std::string(slot) == "tech" && best.body == nullptr) continue;
		AiPlanner::KitMove c;
		c.id = id;
		c.element = me.element;
		c.sub = me.sub();
		c.slot = slot;
		const int maxt = id != "guard" ? Charge::max_tier(def) : 0;
		int top = -1, top_tier = 0;
		bool top_perfect = false;
		IxResult top_res;
		for (int tier = 0; tier <= maxt; ++tier) {
			for (int pf = 0; pf < (std::string(slot) == "guard" ? 2 : 1); ++pf) {
				AgentRef counter = AiPlanner::counter_agent(w, me, c, tier, pf == 1, best);
				if (std::string(slot) == "tech" && !AiPlanner::tech_legal(w, me, c, best, *counter)) continue;
				const IxResult r = Interactions::predict(&w, *best.agent, *counter);
				const int rank = band_rank(r);
				if (rank > top) {   // strictly better: the lowest tier / plain guard that reaches each band wins
					top = rank;
					top_tier = tier;
					top_perfect = pf == 1;
					top_res = r;
				}
			}
		}
		if (top < 0) continue;
		CounterHintView v;
		v.slot = slot;
		v.move = HudBuilder::_short_name(id);
		v.outcome = top_res.outcome;
		v.label = label_for(top_res.outcome, top_res.to);
		v.band = band_name(top);
		v.tier = top_tier;
		v.perfect = top_perfect;
		res.hints.push_back(v);
	}
	return res;
}

}  // namespace CounterHints
}  // namespace ff
