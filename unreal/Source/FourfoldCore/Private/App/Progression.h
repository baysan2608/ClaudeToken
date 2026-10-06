// Fourfold core - port of game/scenarios/progression.gd: mastery progress (unlocked techniques, completed challenges,
// Lab mode, last scenario, Free Spar rival choice). Persisted by the host as JSON text (Session::SaveProgress):
//   {"schema": 1, "unlocked": ["magma", ...], "done": ["m_stone_reader", ...], "lab_mode": false,
//    "last_scenario": "lab", "spar_difficulty": "adept", "spar_kit": "mixed"}
#pragma once

#include "ff/Value.h"

#include <string>
#include <vector>

namespace ff {

class Progression {
public:
	static const std::vector<std::string>& ALL();       // magma heat_draw lightning redirect_current glide
	static const std::vector<std::string>& LAB_OMIT();  // lightning

	std::vector<std::string> unlocked;    // insertion order (GDScript dictionary keys)
	std::vector<std::string> done;
	bool lab_mode = false;
	std::string last_scenario = "molten_exchange";
	std::string spar_difficulty = "adept";
	std::string spar_kit = "mixed";

	void reset();
	Dict kit() const;
	static Dict lab_kit();
	bool is_done(const std::string& challenge_id) const;
	bool has_unlocked(const std::string& t) const;
	// Marks a challenge complete. Returns true when it unlocked something new.
	bool complete(const std::string& challenge_id, const std::string& unlock);

	std::string to_json() const;
	bool from_json(const std::string& text);
};

}  // namespace ff
