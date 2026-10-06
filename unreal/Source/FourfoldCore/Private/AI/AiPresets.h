// Fourfold core - port of game/actors/ai_presets.gd: difficulty presets of the sparring AI (Data/lab.json
// ai_presets.TABLE): reaction counter misjudge timing_err elements subs aggression chain weave punish env tiers kit.
#pragma once

#include "ff/Value.h"

#include <string>
#include <utility>
#include <vector>

namespace ff {
namespace AiPresets {

const Dict& TABLE();
std::vector<std::string> names();
Dict get_preset(const std::string& name);           // a deep copy (adept when unknown)
// "element:<e>/<sub>" drill spec -> {element, sub} (numbers or names); {-1, -1} if invalid.
std::pair<int, int> parse_element_drill(const std::string& drill);

}  // namespace AiPresets
}  // namespace ff
