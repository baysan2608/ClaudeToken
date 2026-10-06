// Fourfold core - port of game/scenarios/scenarios.gd: resettable lab scenarios (Data/scenarios.json LIST), practice
// menu items, world builder and the Free Spar kit parser.
#pragma once

#include "ff/Value.h"
#include "ff/ViewModels.h"

#include <memory>
#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;
class Progression;

namespace Scenarios {

struct Built {
	std::unique_ptr<CombatWorld> world;
	ActorState* player = nullptr;
	ActorState* opponent = nullptr;
	Dict ai_cfg;
	Dict launcher;
	Dict def;
};

const Array& LIST();
Dict get_def(const std::string& id);        // the first scenario when unknown (GDScript LIST[0])
bool has(const std::string& id);
bool is_spar_difficulty(const std::string& s);
bool is_spar_kit(const std::string& s);
std::vector<std::string> spar_difficulties();
std::vector<std::string> spar_difficulty_labels();
std::vector<std::string> spar_kits();
std::vector<std::string> spar_kit_labels();
std::vector<PracticeItem> practice_items(const Progression& progress);
Built build(const std::string& id, const Progression& progress, uint64_t seed_value = 1);
// "mixed" / "earth".."air" / "all" / "fire/blue" -> {elements: Array, sub: int (-1 = every sub-element)}.
Dict spar_kit(const std::string& key);

}  // namespace Scenarios
}  // namespace ff
