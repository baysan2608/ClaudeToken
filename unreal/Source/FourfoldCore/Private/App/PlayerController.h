// Fourfold core - port of game/actors/player_controller.gd: maps one InputFrame (screen space) + camera yaw to a
// world-space ActorIntent. The camera looks along (sin yaw, 0, cos yaw) on the ground plane.
#pragma once

#include "ff/Input.h"
#include "Sim/ActorState.h"

namespace ff {

class PlayerController {
public:
	ActorIntent intent;
	const ActorIntent& build(const InputFrame& f, double cam_yaw);
	// The inverse mapping used by the attract-mode duel (AI intent -> screen-space input, game/actors/autoplay.gd _duel).
	static InputFrame frame_from_intent(const ActorIntent& it, double cam_yaw);
};

}  // namespace ff
