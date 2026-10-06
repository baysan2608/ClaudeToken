// FourfoldAudio logic island - loop keeper and ambience accents (engine-free).
#include "FFALoops.h"

#include <algorithm>

namespace ffa {

void LoopTracker::Hold(const HoldRequest& h, double now) {
	Held& e = holds_[h.key];
	e.req = h;
	e.expires = now + static_cast<double>(h.hold_s);
}

int LoopTracker::ActiveCount() const {
	int n = 0;
	for (const LoopState& s : states_) n += (s.on && !s.finished) ? 1 : 0;
	return n;
}

void LoopTracker::EndFrame(double now, float dt) {
	// 1. last frame's finished loops are gone, started flags are consumed
	states_.erase(std::remove_if(states_.begin(), states_.end(), [](const LoopState& s) { return s.finished; }), states_.end());
	for (LoopState& s : states_) s.started = false;

	// 2. event-held loops count as wanted until they expire
	for (auto it = holds_.begin(); it != holds_.end();) {
		if (now >= it->second.expires) {
			it = holds_.erase(it);
			continue;
		}
		if (wants_.find(it->first) == wants_.end()) {
			LoopWant w;
			w.key = it->first;
			w.sound = it->second.req.sound;
			w.pos = it->second.req.pos;
			w.gain_db = it->second.req.gain_db;
			w.zone = it->second.req.zone;
			wants_[w.key] = std::move(w);
		}
		++it;
	}

	// 3. existing loops: still wanted -> on (position / target follow); otherwise fade out where they were
	int running = 0;
	for (LoopState& s : states_) {
		const auto w = wants_.find(s.key);
		if (w != wants_.end()) {
			s.on = true;
			s.pos = w->second.pos;
			s.target_db = w->second.gain_db;
			s.zone = w->second.zone;
			wants_.erase(w);
			++running;
		} else {
			s.on = false;
		}
	}

	// 4. new loops (never beyond the loop budget: the running ones keep their voices)
	std::vector<const LoopWant*> fresh;
	fresh.reserve(wants_.size());
	for (const auto& kv : wants_) fresh.push_back(&kv.second);
	std::sort(fresh.begin(), fresh.end(), [](const LoopWant* a, const LoopWant* b) { return a->key < b->key; });   // deterministic
	for (const LoopWant* wp : fresh) {
		if (running >= max_loops_) break;
		const LoopWant& w = *wp;
		LoopState s;
		s.key = w.key;
		s.sound = w.sound;
		s.pos = w.pos;
		s.two_d = w.two_d;
		s.zone = w.zone;
		s.target_db = w.gain_db;
		s.vol_db = std::max(fade_.floor_db, w.gain_db - (w.zone ? 40.0f : 24.0f));
		s.on = true;
		s.started = true;
		states_.push_back(std::move(s));
		++running;
	}

	// 5. fades
	for (LoopState& s : states_) {
		if (s.on) {
			s.vol_db = MoveToward(s.vol_db, s.target_db, (s.zone ? fade_.zone_in : fade_.body_in) * dt);
		} else {
			s.vol_db = MoveToward(s.vol_db, fade_.floor_db, (s.zone ? fade_.zone_out : fade_.body_out) * dt);
			if (s.vol_db <= fade_.floor_db + 1.0f) s.finished = true;
		}
	}
}

void LoopTracker::StopAll() {
	wants_.clear();
	holds_.clear();
	for (LoopState& s : states_) {
		s.on = false;
		s.started = false;
		s.vol_db = fade_.floor_db;
		s.finished = true;
	}
}

void AmbienceScheduler::Configure(const std::vector<AccentDef>& accents, uint32_t seed) {
	accents_ = accents;
	rng_ = Rng(seed);
	left_.assign(accents_.size(), 0.0f);
	last_.assign(accents_.size(), -1);
	for (size_t i = 0; i < accents_.size(); ++i) left_[i] = rng_.Range(accents_[i].min_gap_s * 0.3f, accents_[i].max_gap_s);
}

void AmbienceScheduler::Update(float dt, std::vector<AccentPlay>& out) {
	for (size_t i = 0; i < accents_.size(); ++i) {
		left_[i] -= dt;
		if (left_[i] > 0.0f) continue;
		const AccentDef& a = accents_[i];
		left_[i] = rng_.Range(a.min_gap_s, std::max(a.max_gap_s, a.min_gap_s));
		if (a.sounds.empty()) continue;
		int pick = rng_.Index(static_cast<int>(a.sounds.size()));
		if (a.sounds.size() > 1 && pick == last_[i]) pick = (pick + 1) % static_cast<int>(a.sounds.size());
		last_[i] = pick;
		AccentPlay p;
		p.sound = a.sounds[static_cast<size_t>(pick)];
		p.gain_db = a.gain_db;
		p.pitch_var = a.pitch_var;
		out.push_back(std::move(p));
	}
}

}  // namespace ffa
