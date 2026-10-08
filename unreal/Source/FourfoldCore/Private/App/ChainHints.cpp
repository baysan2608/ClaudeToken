// Fourfold core - chain prompts for the HUD (see ChainHints.h).
#include "App/ChainHints.h"

#include "App/HudBuilder.h"
#include "Combat/Moves.h"
#include "Sim/ActorState.h"
#include "Sim/CombatWorld.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

namespace ff {
namespace ChainHints {

Result query(const CombatWorld& w, const ActorState& me) {
	(void)w;
	Result r;
	if (me.stun > 0.0 || me.health <= 0.0 || me.action == nullptr) return r;
	const ActionInst& inst = *me.action;
	const Dict& d = inst.def;
	if (inst.phase != ActionPhase::Recovery || !d.has("chain")) return r;
	// CombatWorld::_chain_ok, read-only
	const double rec = dnum(d, "recovery") * Status::recovery_mult(me);
	const double frac = rec > 0.0 ? inst.t / rec : 1.0;
	const bool contact = dtruthy(inst.data, "contact");
	const double need = contact ? dnum(d, "chain") : dnum(d, "cancel", 1.0);
	if (frac < need - 1e-6) return r;
	const int n = dint(me.chain, "n", 0);
	if (n >= CombatWorld::CHAIN_MAX) return r;
	const bool weave = me.element != inst.element || me.sub() != inst.sub;
	if (weave && (dtruthy(me.chain, "weaved") || !contact || me.focus < CombatWorld::WEAVE_COST)) return r;
	const Array used = darr(me.chain, "slots");
	for (const char* slot : {"strike", "thrust", "ground", "sweep"}) {
		if (used.has(Value(std::string(slot)))) continue;
		const std::string id = Moves::resolve(me.element, me.sub(), slot);
		if (id.empty() || !Moves::defs().has(id)) continue;
		r.slots.push_back(slot);
		r.moves.push_back(HudBuilder::_short_name(id));
	}
	if (r.slots.empty()) return r;
	r.open = true;
	r.n = n;
	r.weave = weave;
	r.left = rec > 0.0 ? maxf(0.0, rec - inst.t) : 0.0;
	r.window = rec > 0.0 ? rec * maxf(0.0, 1.0 - need) : 0.0;
	return r;
}

}  // namespace ChainHints
}  // namespace ff
