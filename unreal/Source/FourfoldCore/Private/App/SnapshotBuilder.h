// Fourfold core - builds the read-only ff::Snapshot / ff::ArenaView from a CombatWorld (presentation state).
#pragma once

#include "ff/Snapshot.h"

namespace ff {

class CombatWorld;
class ActorState;

namespace SnapshotBuilder {

struct Roles {
	int player_id = -1;
	int rival_id = -1;
	bool player_ai = false;   // autoplay duel: the AI drives the player
	bool rival_ai = false;
};

void build(const CombatWorld& w, const Roles& roles, Snapshot& out);
ArenaView arena(const CombatWorld& w);
ActionView action_view(const CombatWorld& w, const ActorState& a);

}  // namespace SnapshotBuilder
}  // namespace ff
