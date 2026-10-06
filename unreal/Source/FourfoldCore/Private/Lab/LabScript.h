// Fourfold core - port of game/ui/lab/lab_script.gd: a scripted input sequence, one Dict of InputFrame / ActorIntent
// fields per 60 Hz tick (Lab Try button, combo demo, "the rival throws it").
#pragma once

#include "ff/Input.h"
#include "ff/Value.h"
#include "Sim/ActorState.h"

#include <string>
#include <vector>

namespace ff {

class LabScript {
public:
	static constexpr int LEAD = 2;
	std::vector<Dict> frames;
	size_t tick = 0;
	std::string label;

	bool is_done() const { return tick >= frames.size(); }
	size_t length() const { return frames.size(); }
	Dict next();                         // the fields to apply this tick ({} once finished); advances
	Dict& _at(size_t t);
	void _hold(size_t from_t, size_t to_t, const std::string& key);

	static void apply_dict(InputFrame& o, const Dict& d);
	static void apply_dict(ActorIntent& o, const Dict& d);
	static double hold_seconds(const Dict& def, int tier);
	static LabScript for_move(int element, int sub, const std::string& slot, int tier = 0);
	static size_t ticks_for(int element, int sub, const std::string& slot, int tier = 0);
};

}  // namespace ff
