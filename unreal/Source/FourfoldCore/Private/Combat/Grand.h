// Fourfold core - grand techniques (Unreal-only addition, 2026-10-13; no Godot reference): one per element, paid with
// a full Surge meter (ActorState::surge), started with the "grand" press (InputFrame::grand_pressed). Module "grand".
//   earth  grand_upheaval  - a ridge erupts segment by segment along the aim (open world: a real cut-and-fill terrain
//                            edit; arenas: standing stone walls drawn from the ground ledger) and launches everyone on it
//   fire   grand_firestorm - a fan of wide flame walls rolling 30 m out, burning trail fields (heat booked as generated)
//   water / air            - fall back to the fire / earth shapes until their own grand techniques exist
#pragma once

#include "Sim/ActorState.h"

#include <string>

namespace ff {

class CombatWorld;

namespace Grand {
inline constexpr double kSurgeMax = 100.0;
inline constexpr double kSurgeRate = 8.0;        // per second, always (12.5 s from empty)
inline constexpr double kSurgePerDamage = 1.2;   // per point of damage dealt

// Registers the grand move defs once (idempotent). Returns the id for an element.
std::string id_for(int element);
void ensure_defs();

void on_start(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
ActionPhase after_startup(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_phase(CombatWorld& w, ActorState& a, ActionInst& inst, ActionPhase p);
void on_tick(CombatWorld& w, ActorState& a, ActionInst& inst, const ActorIntent& it);
void on_interrupt(CombatWorld& w, ActorState& a, ActionInst& inst, const std::string& reason);
}  // namespace Grand

}  // namespace ff
