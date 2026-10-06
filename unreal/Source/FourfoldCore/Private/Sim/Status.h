// Fourfold core - port of game/core/status.gd: actor statuses (MOVESET §15.8). ActorState.status: name -> {t, mag, src};
// t < 0 = until removed. A spec declares modifiers (speed, recovery, friction, armor, tech_cost, dps, rooted) and
// immunities (immune: [burn, conduct, lift, knockback, pull, ground]), no_lock, hidden, wet. Kit statuses come from
// hooks.json "status_specs" (Status.register in Godot). Events: status {actor, status, on, t, mag}.
#pragma once

#include "ff/Value.h"

#include <string>
#include <string_view>

namespace ff {

class CombatWorld;
class ActorState;

namespace Status {

inline constexpr double WET_AT = 0.3;
inline constexpr double HIDDEN_RANGE = 2.0;

void ensure();
Dict& specs();                                   // Status.SPECS (process-wide)
void register_spec(const std::string& nm, const Dict& spec);
Dict spec(std::string_view nm);
void apply(CombatWorld& w, ActorState& a, const std::string& nm, double dur, double mag = 1.0, int src = -1);
void remove(CombatWorld& w, ActorState& a, const std::string& nm);
bool has(const ActorState& a, std::string_view nm);
void tick(CombatWorld& w, ActorState& a, double dt);
double speed_mult(const ActorState& a);
double recovery_mult(const ActorState& a);
double friction_mult(const ActorState& a);
double tech_cost_mult(const ActorState& a);
bool rooted(const ActorState& a);
double armor(const ActorState& a);
bool immune(const ActorState& a, std::string_view what);
bool lock_blocked(const ActorState& a);
bool hidden(const ActorState& a);

}  // namespace Status
}  // namespace ff
