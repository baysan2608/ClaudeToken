// Fourfold core - element kit module dispatchers (modules "kit_earth" "kit_water" "kit_fire" "kit_air"): ports of
// game/combat/kits/<element>/kit_<element>.gd. Each dispatches by the def key `part` (guard specs: the spec's part) to
// its sub-element file; parts that are not ported yet fall back to the generic verbs (playable, approximate).
#pragma once

#include "Sim/ActorState.h"

#include <string>

namespace ff {

class CombatWorld;

#define FF_KIT_MODULE_DECL(NS)                                                                                  \
	namespace NS {                                                                                              \
	void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);                      \
	ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);          \
	void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);                              \
	void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);                       \
	void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);              \
	}

FF_KIT_MODULE_DECL(KitEarth)
FF_KIT_MODULE_DECL(KitWater)
FF_KIT_MODULE_DECL(KitFire)
FF_KIT_MODULE_DECL(KitAir)

#undef FF_KIT_MODULE_DECL

// The `part` a kit dispatches on (guards: the spec's part).
std::string KitPart(const ActionInst& inst);

}  // namespace ff
