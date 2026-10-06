// Fourfold core - port of game/ui/lab/spawn_catalog.gd: the Lab spawner's catalogue (Data/lab.json spawn_entries):
// every material and state of the moveset, spawned inert at the aim point or launched at the player by the rival,
// with mass / speed / temperature / tier parameters. Everything goes through the world's ledgers.
#pragma once

#include "ff/Math.h"
#include "ff/Value.h"
#include "Lab/LabScript.h"

#include <memory>
#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class MatBody;

namespace SpawnCatalog {

struct SpawnResult {
	bool ok = false;
	std::vector<MatBody*> bodies;
	std::unique_ptr<LabScript> script;
	std::string msg;
};

const std::vector<Dict>& entries();
Dict find(const std::string& id);
std::vector<std::string> ids();
Dict param_specs(const Dict& e);
Dict defaults(const Dict& e);
std::string describe(const Dict& e, const Dict& params);
ActorState* owner_of(CombatWorld& w, const ActorState& player);
void heat_to(CombatWorld& w, MatBody& b, double temp);
Vec3 launch_origin(const ActorState& target, const ActorState* owner);
SpawnResult spawn(CombatWorld& w, const std::string& id, const Dict& params, ActorState& target, ActorState* owner, Vec3 at, bool launch);
void top_up(CombatWorld& w, ActorState& a);

}  // namespace SpawnCatalog
}  // namespace ff
