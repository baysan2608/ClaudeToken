// Fourfold core - chain prompts for the HUD (docs/game/CONTROLS_HUD_PLAN.md part C). Not a port: a read-only query over
// the player's current action. While its recovery is inside the chain window (MOVESET §9.1: same sub-element, after
// contact, at most CHAIN_MAX moves, each slot once per string; a weave also needs Focus) it lists the attack slots that
// would chain right now - the same test as CombatWorld::_chain_ok, without marking a pending chain.
#pragma once

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;

namespace ChainHints {

struct Result {
	bool open = false;                 // a chain press now would cancel the recovery
	int n = 0;                         // moves in the string so far
	bool weave = false;                // the element / sub-element changed since the move started (costs Focus)
	double left = 0.0;                 // s until the recovery ends (the window closes)
	double window = 0.0;               // s the window lasts in all
	std::vector<std::string> slots;    // strike | thrust | ground | sweep, in that order
	std::vector<std::string> moves;    // their short names
};

Result query(const CombatWorld& w, const ActorState& me);

}  // namespace ChainHints
}  // namespace ff
