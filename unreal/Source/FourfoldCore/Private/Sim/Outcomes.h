// Fourfold core - port of game/core/outcomes.gd: outcome handlers of the counter rule (MOVESET §15.3, COMBAT_SPEC
// E4.3). Every handler moves heat and mass only through the CombatWorld ledger helpers. Signature:
//   handler(w, threat, counter, res, rule, ctx) -> bool (false = declined: the rule's fallback runs).
#pragma once

#include "ff/Value.h"
#include "Sim/Interactions.h"

#include <string>

namespace ff {

class CombatWorld;
class Agent;
class MatBody;
class ActorState;

namespace Outcomes {

bool apply(CombatWorld& w, const std::string& nm, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);

int _id(const Agent* x);
double _left(const IxResult& res, double absorb);
double draw_heat(Agent* src, double hu);
double move_heat(CombatWorld& w, Agent* src, MatBody& dst, double hu);
MatBody* _subject(Agent& t, Agent& c, const Dict& r);
Agent* _heat_src(Agent& t, Agent& c, const MatBody* subject);
void scale_damage(MatBody* b, double f);
double heat_capacity(const MatBody* b);
bool disruptable(const ActorState& who);
void _break_counter(CombatWorld& w, Agent& c, IxResult& res);

bool pass_(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool block(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool deflect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool redirect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool reflect(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool reclaim(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool capture(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool absorb(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool transform(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool heat(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool shatter(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool sink(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool conduct(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool ground(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool amplify(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool extinguish(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool weaken(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool bend(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool slow(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool overwhelm(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool clash(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool disrupt(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool neutralize(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool push(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);
bool disperse(CombatWorld& w, Agent& t, Agent& c, IxResult& res, const Dict& r, IxCtx& ctx);

}  // namespace Outcomes
}  // namespace ff
