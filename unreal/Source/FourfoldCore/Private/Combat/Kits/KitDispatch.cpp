// Fourfold core - generic kit dispatch (KitWater / KitFire / KitAir lifecycle with per-move overrides).
#include "Combat/Kits/KitDispatch.h"

#include "Combat/Kits/Kits.h"
#include "Combat/Moves.h"
#include "Combat/Verbs.h"
#include "Util/GdUtil.h"

namespace ff {

std::string KitKey(const ActionInst& inst) {
	if (inst.id == "guard" && inst.data.has("spec")) return dstr(inst.data, "spec");
	return inst.id;
}

std::string KitPart(const ActionInst& inst) {
	if (inst.id == "guard") return dstr(ddict(inst.data, "spec_def"), "part", "");
	return dstr(inst.def, "part", "");
}

const KitHandlerMap& KitHandlers(int element) {
	static const KitHandlerMap water = [] {
		KitHandlerMap h;
		RegisterWaterHandlers(h);
		return h;
	}();
	static const KitHandlerMap fire = [] {
		KitHandlerMap h;
		RegisterFireHandlers(h);
		return h;
	}();
	static const KitHandlerMap air = [] {
		KitHandlerMap h;
		RegisterAirHandlers(h);
		return h;
	}();
	static const KitHandlerMap none;
	switch (element) {
		case 1: return water;
		case 2: return fire;
		case 3: return air;
		default: return none;
	}
}

namespace KitGeneric {
namespace {
const KitStages* kg_find(const KitHandlerMap& h, const ActionInst& inst) {
	auto it = h.find(KitKey(inst));
	return it == h.end() ? nullptr : &it->second;
}
bool kg_known(const ActionInst& inst) { return Moves::defs().has(KitKey(inst)); }
}  // namespace

void on_start(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = kg_find(h, inst);
	if (s != nullptr && s->start != nullptr) s->start(w, a, inst, it);
	else if (kg_known(inst)) Verbs::on_start(w, a, inst, it);
}

ActionPhase after_startup(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = kg_find(h, inst);
	if (s != nullptr && s->after != nullptr) return s->after(w, a, inst, it);
	if (kg_known(inst)) return Verbs::after_startup(w, a, inst, it);
	return ActionPhase::Active;
}

void on_phase(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {
	const KitStages* s = kg_find(h, inst);
	if (s != nullptr && s->phase != nullptr) s->phase(w, a, inst, p);
	else if (kg_known(inst)) Verbs::on_phase(w, a, inst, p);
}

void on_tick(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {
	const KitStages* s = kg_find(h, inst);
	if (s != nullptr && s->tick != nullptr) s->tick(w, a, inst, it);
	else if (kg_known(inst)) Verbs::on_tick(w, a, inst, it);
}

void on_interrupt(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {
	const KitStages* s = kg_find(h, inst);
	if (s != nullptr && s->interrupt != nullptr) s->interrupt(w, a, inst, reason);
	else if (kg_known(inst)) Verbs::on_interrupt(w, a, inst, reason);
}

}  // namespace KitGeneric

// ------------------------------------------------------------------ KitWater / KitFire / KitAir modules
#define FF_KIT_GENERIC_MODULE(NS, E)                                                                                      \
	namespace NS {                                                                                                        \
	void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {                             \
		KitGeneric::on_start(KitHandlers(E), w, a, inst, it);                                                             \
	}                                                                                                                     \
	ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {                 \
		return KitGeneric::after_startup(KitHandlers(E), w, a, inst, it);                                                 \
	}                                                                                                                     \
	void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p) {                                     \
		KitGeneric::on_phase(KitHandlers(E), w, a, inst, p);                                                              \
	}                                                                                                                     \
	void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it) {                              \
		KitGeneric::on_tick(KitHandlers(E), w, a, inst, it);                                                              \
	}                                                                                                                     \
	void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason) {                     \
		KitGeneric::on_interrupt(KitHandlers(E), w, a, inst, reason);                                                     \
	}                                                                                                                     \
	}

FF_KIT_GENERIC_MODULE(KitWater, 1)
FF_KIT_GENERIC_MODULE(KitFire, 2)
FF_KIT_GENERIC_MODULE(KitAir, 3)

#undef FF_KIT_GENERIC_MODULE

}  // namespace ff
