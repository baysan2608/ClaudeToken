// Fourfold core - ports of the engine-free HUD context builders of game/game.gd (Game._hud_context,
// attack_ring_context, gesture_petals, guard_petals, shape_label, charge_ring_context, _tech_context, _debug_lines,
// _short_name) producing ff::HudModel.
#pragma once

#include "ff/Snapshot.h"
#include "ff/ViewModels.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class MatBody;

namespace HudBuilder {

std::string _short_name(const std::string& id);
Dict gesture_petals(const ActorState& a);          // {up, down, side} -> short names
Dict guard_petals(const ActorState& a);            // {up, down}
std::string shape_label(const ActorState& a);
Dict charge_ring_context(const ActorState& a);     // {slot, tier, frac, max} or {}
Dict attack_ring_context(const ActorState& a);     // {attack_charge, attack_element, attack_decide?}
double attack_charge_sec(int element);             // TouchControls.attack_charge_sec
ChargeView charge_view(const ActorState& a);
bool water_in_reach(CombatWorld& w, ActorState& player, const ActorIntent& it);
// _tech_context: {label, ok, marker_body?, marker_label?}
Dict tech_context(CombatWorld& w, ActorState& player, const ActorIntent& it, MatBody* held);

struct Context {
	CombatWorld* world = nullptr;
	ActorState* player = nullptr;
	const ActorIntent* player_intent = nullptr;
	std::string scenario_title, objective, challenge_text;
};
HudModel build(const Context& c);
std::vector<StatusView> statuses_of(const ActorState& a);
std::vector<std::string> debug_lines(CombatWorld& w, const std::string& scenario_id, const std::string& ai_debug);

}  // namespace HudBuilder
}  // namespace ff
