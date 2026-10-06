// Fourfold core - port of game/core/fx_events.gd: event catalogue (MOVESET §15.6, COMBAT_SPEC E8) and emit helpers.
// One-shot cues only: persistent visuals come from body state, never from events.
#pragma once

#include "ff/Value.h"

#include <string>
#include <string_view>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;
struct ActionInst;

namespace FxEvents {

// kind: "fx" | "mat" | "shape" | "tag" | "event" | "outcome"
bool is_known(std::string_view kind, std::string_view key);
// One-shot cue: missing keys get neutral defaults so consumers read them blindly.
void fx(CombatWorld& w, const std::string& fx_key, const std::string& mat, const Dict& d = Dict());
// fx cue for an action (actor, element, sub, move, tier, pos = hand point, dir = data.face).
void fx_for(CombatWorld& w, const ActorState& a, const ActionInst& inst, const std::string& fx_key, const std::string& mat,
            const Dict& d = Dict());
void charge(CombatWorld& w, const ActorState& a, const ActionInst& inst, int tier, bool ready);
void zone(CombatWorld& w, const MatBody& b, const std::string& phase);
void status(CombatWorld& w, const ActorState& a, const std::string& nm, bool on, double t, double mag);
// fx `mat` key for a body (material family + state).
std::string mat_of(const MatBody& b);

}  // namespace FxEvents
}  // namespace ff
