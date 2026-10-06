// Fourfold core - port of game/ui/lab/move_list_data.gd: data behind the Lab move list (per element / sub-element:
// every bound move with input text, costs, S/A/R frames, tier table, description) and the MoveInfo view model.
#pragma once

#include "ff/Value.h"
#include "ff/ViewModels.h"

#include <string>
#include <vector>

namespace ff {
namespace MoveListData {

std::string input_text(const std::string& slot, const std::string& device);
std::string cost_text(const Dict& def);
std::string frames_text(const Dict& def);
std::vector<Dict> tier_rows(const Dict& def);
std::vector<Dict> rows(int element, int sub, const std::string& device);
int count_all(const std::string& device = "keyboard");
MoveInfo move_info(const std::string& id);

}  // namespace MoveListData
}  // namespace ff
