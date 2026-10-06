// Fourfold core tests - data completeness: every exported def / rule / hook / status loads, counts match the JSON.
#include "ff_test.h"

#include "Combat/Moves.h"
#include "Data/GameData.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"

using namespace ff;
using fft::S;

FF_TEST(test_data, test_embedded_data_parses) {
	check(GameData::ok(), "embedded data parses: " + GameData::error());
	check(GameData::moves().get("defs").as_dict().size() == 162, S("162 defs (", GameData::moves().get("defs").as_dict().size(), ")"));
	check(GameData::moves().get("bindings").as_dict().size() == 160, "160 bindings");
	check(GameData::rules().get("rules").as_dict().size() == 1237, "1237 rule keys");
	check(GameData::hooks().get("status_specs").as_dict().size() == 37, "37 kit status specs");
	check(GameData::lab().get("spawn_entries").as_array().size() == 39, S("39 spawn entries (", GameData::lab().get("spawn_entries").as_array().size(), ")"));
	check(GameData::lab().get("combos").as_array().size() == 28, S("28 combos (", GameData::lab().get("combos").as_array().size(), ")"));
}

FF_TEST(test_data, test_registries_load) {
	Moves::ensure_ready();
	check(Moves::defs().size() == 162, S("Moves.DEFS has 162 (", Moves::defs().size(), ")"));
	check(Moves::bindings().size() == 160, S("Moves.BINDINGS has 160 (", Moves::bindings().size(), ")"));
	check(Moves::resolve(0, 0, "strike") == "earth_attack", "earth strike resolves to earth_attack");
	Interactions::ensure_ready();
	size_t total = 0;
	for (const auto& kv : Interactions::all_rules()) total += kv.second.size();
	check(Interactions::all_rules().size() == 1237, S("1237 rule keys (", Interactions::all_rules().size(), ")"));
	check(total == 1264, S("1264 rules (", total, ")"));
	Status::ensure_ready();
	check(Status::specs().size() >= 37, S("status specs >= 37 (", Status::specs().size(), ")"));
}

FF_TEST(test_data, test_hook_names_listed) {
	const auto ref = Hooks::referenced();
	check(ref.size() >= 212, S("212 referenced hook names (", ref.size(), ")"));
	const auto un = Hooks::unresolved();
	std::string names;
	for (const auto& n : un) names += (names.empty() ? "" : ", ") + n.first + " " + n.second;
	check(un.empty(), S(un.size(), " of ", ref.size(), " hook names have no C++ function: ", names));
}
