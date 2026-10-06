// Fourfold core - ports of game/ui/lab/combos.gd and combo_tracker.gd.
#include "Lab/LabCombos.h"

#include "Combat/Moves.h"
#include "Data/GameData.h"
#include "Lab/MoveListData.h"
#include "Util/GdUtil.h"

namespace ff {

namespace LabCombos {

const std::vector<Dict>& all() {
	static const std::vector<Dict> table = [] {
		std::vector<Dict> t;
		for (const Value& v : GameData::lab().get("combos").as_array()) t.push_back(v.as_dict());
		return t;
	}();
	return table;
}

Dict find(const std::string& id) {
	for (const Dict& c : all())
		if (dstr(c, "id") == id) return c;
	return Dict();
}

std::string step_text(const Dict& step) {
	const int el = dint(step, "el");
	const int sb = dint(step, "sub");
	const std::string id = Moves::resolve(el, sb, dstr(step, "slot"));
	std::string nm = dstr(Moves::defs().get(id).as_dict(), "name", id);
	const size_t sep = nm.find(" / ");
	if (sep != std::string::npos) nm = nm.substr(0, sep);
	if (dstr(step, "kind", "move") == "shape") return std::string(SubName(el, sb)) + ": shape (tap ATTACK)";
	std::string t;
	if (dint(step, "tier", 0) > 0) t = " T" + itos(dint(step, "tier"));
	return std::string(ElementName(el)) + " / " + std::string(SubName(el, sb)) + ": " + nm + t;
}

std::string step_input(const Dict& step, const std::string& device) {
	if (dstr(step, "kind", "move") == "shape") return device == "touch" ? "tap ATTACK" : (device == "keyboard" ? "J" : "X");
	return MoveListData::input_text(dstr(step, "slot"), device);
}

LabCombo to_view(const Dict& c) {
	LabCombo v;
	v.id = dstr(c, "id");
	v.name = dstr(c, "name");
	v.description = dstr(c, "desc");
	v.result_text = dstr(ddict(c, "result"), "text");
	for (const Value& sv : darr(c, "steps")) {
		const Dict s = sv.as_dict();
		LabComboStep st;
		st.text = step_text(s);
		st.element = dint(s, "el");
		st.sub = dint(s, "sub");
		st.slot = SlotFromName(dstr(s, "slot"));
		st.tier = dint(s, "tier");
		st.kind = dstr(s, "kind", "move");
		st.hold = static_cast<float>(dnum(s, "hold"));
		st.within = static_cast<float>(dnum(s, "within", 4.0));
		st.hint = dstr(s, "hint");
		v.steps.push_back(st);
	}
	return v;
}

}  // namespace LabCombos

void ComboTracker::start(const Dict& c, int pid) {
	Moves::ensure_ready();
	combo = c;
	player_id = pid;
	ids_.clear();
	for (const Value& s : darr(c, "steps")) ids_.push_back(Moves::resolve(dint(s, "el"), dint(s, "sub"), dstr(s, "slot")));
	attempts = 0;
	successes = 0;
	_restart();
	state = State::Running;
}

void ComboTracker::_restart() {
	step = 0;
	step_times.clear();
	t = 0.0;
	wait_from_ = 0.0;
	fail_reason.clear();
	state = State::Running;
}

void ComboTracker::stop() {
	state = State::Idle;
	combo = Dict();
}

Array ComboTracker::steps() const { return darr(combo, "steps"); }

double ComboTracker::time_left() const {
	const Array st = steps();
	if (state == State::Running && step < st.size()) {
		const double within = dnum(st[step], "within", 4.0);
		if (step == 0) return within;
		return maxf(0.0, within - (t - wait_from_));
	}
	if (state == State::SequenceDone) return maxf(0.0, dnum(ddict(combo, "result"), "within", 6.0) - (t - done_at_));
	return 0.0;
}

double ComboTracker::window_used() const {
	const Array st = steps();
	if (state == State::Running && step > 0 && step < st.size()) {
		const double within = maxf(dnum(st[step], "within", 4.0), 0.01);
		return clampf((t - wait_from_) / within, 0.0, 1.0);
	}
	return 0.0;
}

void ComboTracker::update(double dt, const std::vector<Dict>& events) {
	if (state == State::Idle || combo.empty()) return;
	t += dt;
	for (const Dict& e : events) _feed(e);
	_check_timeouts();
}

void ComboTracker::_feed(const Dict& e) {
	if (state == State::Success || state == State::Failed) return;
	if (state == State::SequenceDone) {
		if (_result_matches(e)) {
			state = State::Success;
			successes += 1;
		}
		return;
	}
	const Array st = steps();
	if (step >= st.size()) return;
	const Dict s = st[step].as_dict();
	const std::string type = dstr(e, "type", "");
	if (dstr(s, "kind", "move") == "shape") {
		if ((type == "shape" || type == "split") && dint(e, "actor", player_id) == player_id) _advance();
		return;
	}
	if (type == "action" && dint(e, "actor", -1) == player_id && dstr(e, "phase", "") == "startup") {
		const std::string mv = dstr(e, "move", "");
		if (mv == ids_[step] && dint(s, "tier", 0) <= 0) _advance();
		else if (step > 0 && mv != ids_[step] && mv == ids_[0] && ids_[0] != ids_[step]) _restart_with_first();
		return;
	}
	if (type == "charge" && dint(s, "tier", 0) > 0 && dint(e, "actor", -1) == player_id && dstr(e, "move", "") == ids_[step]) {
		if (dint(e, "tier", 0) >= dint(s, "tier")) _advance();
	}
}

void ComboTracker::_restart_with_first() {
	attempts += 1;
	const double keep = t;
	_restart();
	t = keep;
	step_times.push_back(t);
	step = 1;
	wait_from_ = t;
	if (step >= steps().size()) _sequence_complete();
}

void ComboTracker::_advance() {
	step_times.push_back(t);
	step += 1;
	wait_from_ = t;
	if (step >= steps().size()) _sequence_complete();
}

void ComboTracker::_sequence_complete() {
	done_at_ = t;
	const Dict res = ddict(combo, "result");
	if (darr(res, "any").empty()) {
		state = State::Success;
		successes += 1;
		attempts += 1;
	} else {
		state = State::SequenceDone;
		attempts += 1;
	}
}

void ComboTracker::_check_timeouts() {
	const Array st = steps();
	if (state == State::Running && step > 0 && step < st.size()) {
		const double within = dnum(st[step], "within", 4.0);
		if (t - wait_from_ > within) {
			fail_reason = "too slow: " + LabCombos::step_text(st[step].as_dict());
			attempts += 1;
			const std::string keep_fail = fail_reason;
			_restart();
			fail_reason = keep_fail;
		}
	} else if (state == State::SequenceDone) {
		const double rw = dnum(ddict(combo, "result"), "within", 6.0);
		if (t - done_at_ > rw) {
			fail_reason = "sequence played, no result: " + dstr(ddict(combo, "result"), "text", "");
			const std::string keep = fail_reason;
			_restart();
			fail_reason = keep;
		}
	}
}

bool ComboTracker::_result_matches(const Dict& e) const {
	for (const Value& pred : darr(ddict(combo, "result"), "any")) {
		bool ok = true;
		for (const auto& kv : pred.as_dict()) {
			if (!e.has(kv.first) || e.get(kv.first) != kv.second) {
				ok = false;
				break;
			}
		}
		if (ok) return true;
	}
	return false;
}

std::string ComboTracker::status_text() const {
	switch (state) {
		case State::Idle: return "pick a combo";
		case State::Running: {
			const Array st = steps();
			if (step < st.size())
				return "step " + itos(static_cast<int64_t>(step) + 1) + "/" + itos(static_cast<int64_t>(st.size())) + ": " + LabCombos::step_text(st[step].as_dict());
			return "";
		}
		case State::SequenceDone: return "sequence done, waiting for the result: " + dstr(ddict(combo, "result"), "text", "");
		case State::Success: return "SUCCESS: " + dstr(ddict(combo, "result"), "text", "combo complete");
		case State::Failed: return fail_reason;
	}
	return "";
}

}  // namespace ff
