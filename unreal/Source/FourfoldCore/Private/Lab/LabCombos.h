// Fourfold core - ports of game/ui/lab/combos.gd (the combo trainer's table, Data/lab.json "combos") and
// combo_tracker.gd (live success detection from sim events).
#pragma once

#include "ff/Value.h"
#include "ff/ViewModels.h"

#include <string>
#include <vector>

namespace ff {

namespace LabCombos {
const std::vector<Dict>& all();
Dict find(const std::string& id);
std::string step_text(const Dict& step);
std::string step_input(const Dict& step, const std::string& device);
LabCombo to_view(const Dict& c);
}  // namespace LabCombos

class ComboTracker {
public:
	enum class State { Idle, Running, SequenceDone, Success, Failed };
	Dict combo;
	int player_id = 1;
	State state = State::Idle;
	size_t step = 0;
	double t = 0.0;
	std::vector<double> step_times;
	int attempts = 0;
	int successes = 0;
	std::string fail_reason;

	void start(const Dict& c, int pid);
	void stop();
	bool is_running() const { return state == State::Running || state == State::SequenceDone; }
	Array steps() const;
	double time_left() const;
	double window_used() const;
	void update(double dt, const std::vector<Dict>& events);
	std::string status_text() const;

private:
	double wait_from_ = 0.0;
	double done_at_ = 0.0;
	std::vector<std::string> ids_;
	void _restart();
	void _feed(const Dict& e);
	void _restart_with_first();
	void _advance();
	void _sequence_complete();
	void _check_timeouts();
	bool _result_matches(const Dict& e) const;
};

}  // namespace ff
