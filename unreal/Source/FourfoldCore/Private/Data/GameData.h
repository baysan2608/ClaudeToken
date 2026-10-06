// Fourfold core - the Godot-exported data (Data/*.json, embedded), parsed once on first use.
// Formats: Tools/godot_export/export_core_data.gd header. Values are read-only here; registries (Moves,
// Interactions, Status, Hooks) copy what they mutate.
#pragma once

#include "ff/Value.h"

#include <string>

namespace ff {
namespace GameData {

bool ok();
const std::string& error();
const Dict& moves();        // moves.json
const Dict& rules();        // rules.json
const Dict& hooks();        // hooks.json
const Dict& sim();          // sim.json
const Dict& scenarios();    // scenarios.json
const Dict& lab();          // lab.json
const Dict& move_index();   // move_index.json

// "fn:Class.method" -> "Class.method" ("" when v is not a function reference).
std::string fn_name(const Value& v);

}  // namespace GameData
}  // namespace ff
