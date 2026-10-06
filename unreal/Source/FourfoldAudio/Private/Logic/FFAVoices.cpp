// FourfoldAudio logic island - voice caps, limiters, ducker (engine-free).
#include "FFAVoices.h"

#include "FFAMix.h"

#include <algorithm>
#include <cmath>

namespace ffa {

std::string Limiters::Slot(const std::string& name, int key, const LimiterDef& d) {
	return d.per.empty() ? name : name + "#" + std::to_string(key);
}

bool Limiters::Peek(const std::string& name, int key, double now) const {
	if (name.empty()) return true;
	const auto it = defs_.find(name);
	if (it == defs_.end()) return true;
	const auto l = last_.find(Slot(name, key, it->second));
	return l == last_.end() || now - l->second >= static_cast<double>(it->second.gap_s) - 1e-4;
}

void Limiters::Commit(const std::string& name, int key, double now) {
	if (name.empty()) return;
	const auto it = defs_.find(name);
	if (it == defs_.end()) return;
	last_[Slot(name, key, it->second)] = now;
}

Decision VoiceBook::Admit(const SoundDef& s, double now, double duration_s) {
	Decision d;
	Prune(now);
	const bool counted = s.bus == "sfx";
	// 1. per-sound cap: steal the oldest voice of the same sound
	int same = 0;
	size_t oldest = voices_.size();
	for (size_t i = 0; i < voices_.size(); ++i) {
		if (voices_[i].sound != s.name) continue;
		++same;
		if (oldest == voices_.size() || voices_[i].start < voices_[oldest].start) oldest = i;
	}
	if (same >= std::max(s.max_voices, 1) && oldest < voices_.size()) {
		d.steal.push_back(voices_[oldest].id);
		voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(oldest));
	}
	// 2. global cap (sfx bus): drop when everything running outranks us, else steal the lowest priority / oldest
	if (counted) {
		int total = 0;
		for (const Voice& v : voices_) total += v.counted ? 1 : 0;
		if (total >= global_) {
			size_t victim = voices_.size();
			for (size_t i = 0; i < voices_.size(); ++i) {
				if (!voices_[i].counted) continue;
				if (victim == voices_.size() || voices_[i].priority < voices_[victim].priority ||
				    (voices_[i].priority == voices_[victim].priority && voices_[i].start < voices_[victim].start))
					victim = i;
			}
			if (victim == voices_.size() || voices_[victim].priority > s.priority) return d;   // dropped
			d.steal.push_back(voices_[victim].id);
			voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(victim));
		}
	}
	Voice v;
	v.id = next_id_++;
	v.sound = s.name;
	v.start = now;
	v.end = now + std::max(duration_s, 0.05);
	v.priority = s.priority;
	v.counted = counted;
	d.id = v.id;
	d.play = true;
	voices_.push_back(std::move(v));
	return d;
}

void VoiceBook::Release(uint64_t id) {
	voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; }), voices_.end());
}

void VoiceBook::Prune(double now, std::vector<uint64_t>* expired) {
	for (size_t i = 0; i < voices_.size();) {
		if (now >= voices_[i].end) {
			if (expired) expired->push_back(voices_[i].id);
			voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
		} else {
			++i;
		}
	}
}

int VoiceBook::ActiveOf(const std::string& sound) const {
	int n = 0;
	for (const Voice& v : voices_) n += v.sound == sound ? 1 : 0;
	return n;
}

bool Ducker::IsTrigger(const std::string& sound) const {
	return std::find(def_.triggers.begin(), def_.triggers.end(), sound) != def_.triggers.end();
}

float Ducker::Step(float dt) {
	const float target = (active_ && hold_left_ > 0.0f) ? def_.db : 0.0f;
	if (active_) {
		hold_left_ -= dt;
		if (hold_left_ <= 0.0f) active_ = false;
	}
	const float mag = std::fabs(def_.db);
	const float speed = target < db_ ? mag / std::max(def_.attack_s, 0.001f) : mag / std::max(def_.release_s, 0.001f);
	db_ = MoveToward(db_, target, speed * dt);
	return db_;
}

}  // namespace ffa
