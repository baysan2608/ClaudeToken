// Fourfold core tests - the kit counter cells (GDScript <Kit>Rules.CELLS) rebuilt from the exported rules (every rule
// with owner "<kit>", registration order, CELLS key format of each kit), plus the per-cell reference metadata (move /
// tier / expect / tp) from CoreTests/data/kit_cell_refs.json (Tools/godot_export/extract_cell_refs.py).
#pragma once

#include "ff/Value.h"

#include <string>
#include <vector>

namespace fft {

struct KitCell {
	std::string key, id, threat, counter;
	std::vector<int> tiers;
};

// Cells of a kit ("earth" "water" "fire" "air") in CELLS order (a re-registered key keeps its first position).
std::vector<KitCell> kit_cells(const std::string& owner);
// {cells key: {id, threat, counter, ref, tiers}} for a kit, empty when the fixture is missing.
ff::Dict kit_cell_refs(const std::string& owner);
// GDScript str(Array) of ints: "[1, 2]".
std::string gd_int_array_str(const std::vector<int>& v);

}  // namespace fft
