// Fourfold core - context counters for the HUD (docs/game/CONTROLS_HUD_PLAN.md part A). Not a port: a read-only query
// over the sim. Picks the most urgent threat coming at the player (a hostile body, or a foe's volume attack in its
// startup / charge) and evaluates the current element / sub-element's guard (+ perfect), push, sink and technique against
// it with the counter rule itself (Interactions::predict over the AI planner's counter agents), so the petals always say
// what the sim would do. Timing, cost and reach gates are left to the player.
#pragma once

#include "ff/ViewModels.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;

namespace CounterHints {

inline constexpr double kHorizon = 2.5;   // s: threats further out are not shown

struct Result {
	bool valid = false;
	std::string threat_cls;
	double tti = 0.0;
	Vec3 pos;
	std::vector<CounterHintView> hints;
};

Result query(CombatWorld& w, ActorState& me);
// "reflect" -> "Send back", "water_freeze" -> "Freeze", "transform" + to "lava" -> "To lava", ...
std::string label_for(const std::string& outcome, const std::string& to);

}  // namespace CounterHints
}  // namespace ff
