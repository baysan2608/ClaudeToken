// Fourfold core - port of game/ui/lab/lab_session.gd: Lab / dev-panel state that outlives the panel (time scale,
// frame step, infinite resources, god mode, AI settings, overlay).
#pragma once

#include "ff/Value.h"
#include "ff/ViewModels.h"

#include <string>
#include <vector>

namespace ff {

class CombatWorld;
class ActorState;

class LabSession {
public:
	double time_scale = 1.0;
	bool frozen = false;
	bool infinite = false;
	bool god = false;
	bool overlay = false;
	bool ai_enabled = true;
	std::string ai_preset = "adept";
	std::string ai_kit = "mixed";
	int ai_sub = -1;
	std::string ai_drill;
	int _steps = 0;

	void request_step(int n = 1) { _steps += n; }
	bool consume_step();
	int pending_steps() const { return _steps; }
	void set_time_scale(double v);
	void pre_step(CombatWorld& w) const;
	void post_step(ActorState* player) const;
	// Element indices of the AI kit choice (empty = leave the scenario's own).
	std::vector<int> ai_elements() const;
	Dict ai_config() const;
	static Dict legacy_cfg(const std::string& preset);
	Dict to_dict() const;
	void from_dict(const Dict& d);
	LabState to_state() const;
	void from_state(const LabState& s);
};

}  // namespace ff
