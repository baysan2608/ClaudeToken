// Fourfold core - port of game/combat/moves.gd: data-driven move definitions shared by player and AI.
// DEFS = BASE_DEFS + kit registrations (exported from Godot: Data/moves.json) + Lab live-tuning overrides.
// Process-wide registry like Godot's static vars; inst.def handles alias DEFS entries (tuning edits apply live).
#pragma once

#include "ff/Value.h"

#include <string>
#include <vector>

namespace ff {
namespace Moves {

inline constexpr double HOLD_THRESHOLD = 0.18;   // attack held this long becomes a charge
inline constexpr double BUFFER_TIME = 0.15;      // press buffer
inline constexpr double PERFECT_WINDOW = 0.18;   // guard press this close before contact = perfect
inline constexpr double GUARD_MASH_LOCK = 0.35;  // a new perfect window needs this gap since the last press

void ensure_ready();
Dict& defs();                                    // Moves.DEFS
Dict& bindings();                                // Moves.BINDINGS ("e/s/slot" -> id)
const std::vector<std::string>& registered();    // Moves.REGISTERED
const std::vector<std::string>& base_ids();      // BASE_DEFS keys
bool is_base(const std::string& id);
const Dict& techniques();                        // Moves.TECHNIQUES
std::string legacy_binding(int element, const std::string& slot);   // LEGACY_BINDINGS[e][slot] or ""

Dict get_def(const std::string& id);
void register_def(const std::string& id, const Dict& def);
void unregister(const std::string& id);
void bind(int element, int sub, const std::string& slot, const std::string& id);
void unbind(int element, int sub, const std::string& slot);
std::string resolve(int element, int sub, const std::string& slot);
std::vector<std::string> list(int element, int sub);
std::string slot_of(int element, int sub, const std::string& id);

void set_override(const std::string& id, const std::string& key, const Value& value);
void clear_overrides();
Dict overrides();
Dict originals();                                // Moves._orig: id -> {key: registered value}

struct State {
	Dict defs, bindings, orig, over;
	std::vector<std::string> registered;
};
State save_state();
void load_state(const State& st);
// Test helper (SimHarness.legacy_bindings_only): only the legacy sub-0 bindings remain.
void legacy_bindings_only();

}  // namespace Moves
}  // namespace ff
