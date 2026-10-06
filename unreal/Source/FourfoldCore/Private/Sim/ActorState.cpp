// Fourfold core - ports of game/core/actor_state.gd / action_inst.gd (non-inline parts).
#include "Sim/ActorState.h"

#include "Util/GdUtil.h"

namespace ff {

int ActionInst::tier() const { return dint(data, "tier", 0); }

ActorState::ActorState() { chain = D({{"n", 0}, {"slots", Array()}, {"weaved", false}}); }

}  // namespace ff
