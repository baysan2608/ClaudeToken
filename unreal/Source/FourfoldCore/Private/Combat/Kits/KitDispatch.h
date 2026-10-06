// Fourfold core - per-move lifecycle overrides of the element kits (GDScript KitWater / KitFire / KitAir._handlers:
// handle(id, {start, after, phase, tick, interrupt})). Each sub-element file registers its stages by move id (a
// guard spec registers under the spec id); a missing stage runs the generic Verbs lifecycle.
#pragma once

#include "Sim/ActorState.h"

#include <string>
#include <unordered_map>

namespace ff {

class CombatWorld;

struct KitStages {
	void (*start)(CombatWorld&, ActorState&, ActionInst&, const ActorIntent&) = nullptr;
	ActionPhase (*after)(CombatWorld&, ActorState&, ActionInst&, const ActorIntent&) = nullptr;
	void (*phase)(CombatWorld&, ActorState&, ActionInst&, ActionPhase) = nullptr;
	void (*tick)(CombatWorld&, ActorState&, ActionInst&, const ActorIntent&) = nullptr;
	void (*interrupt)(CombatWorld&, ActorState&, ActionInst&, const std::string&) = nullptr;
};
using KitHandlerMap = std::unordered_map<std::string, KitStages>;

// The handler map of element e (1 water, 2 fire, 3 air), built on first use by the element's kit file.
const KitHandlerMap& KitHandlers(int element);
// The key a handler is registered under: the guard spec id for guards, else the move id (GDScript _key).
std::string KitKey(const ActionInst& inst);

// Generic dispatch shared by KitWater / KitFire / KitAir (GDScript on_start ... on_interrupt of those kits).
namespace KitGeneric {
void on_start(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(const KitHandlerMap& h, CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
}  // namespace KitGeneric

// Registration entry points per element (each sub-element file adds its moves' stages).
void RegisterWaterHandlers(KitHandlerMap& h);
void RegisterFireHandlers(KitHandlerMap& h);
void RegisterAirHandlers(KitHandlerMap& h);

}  // namespace ff
