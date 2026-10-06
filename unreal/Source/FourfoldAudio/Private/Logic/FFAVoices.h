// FourfoldAudio logic island - voice caps, named rate limiters and the ambience ducker. Engine-free.
//  * Limiters: "at most one start per gap (seconds), optionally scoped per actor" (stinger gap 0.1 s, swings, breaths ...).
//  * VoiceBook: per-sound voice caps (manifest max_voices) and a global cap. Decides play / drop and which running voices to
//    steal (oldest of the same sound; under the global cap the lowest-priority, then oldest). The Unreal glue stops stolen
//    voices with a short fade (mix.steal_fade_s) instead of cutting them dead.
//  * Ducker: smooth attack / hold / release dip of the ambience bus under the loudest events.
#pragma once

#include "FFAManifest.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffa {

class Limiters {
public:
	void Configure(const std::unordered_map<std::string, LimiterDef>& defs) { defs_ = defs; last_.clear(); }
	// True when a start is allowed now; does NOT record it (call Commit once the sound really starts).
	bool Peek(const std::string& name, int key, double now) const;
	void Commit(const std::string& name, int key, double now);
	void Clear() { last_.clear(); }

private:
	std::unordered_map<std::string, LimiterDef> defs_;
	std::unordered_map<std::string, double> last_;
	static std::string Slot(const std::string& name, int key, const LimiterDef& d);
};

struct Decision {
	bool play = false;
	uint64_t id = 0;                     // the new voice (when play)
	std::vector<uint64_t> steal;         // running voices to stop with a short fade
};

class VoiceBook {
public:
	void Configure(int global_voices) { global_ = global_voices; }
	// Admits (or drops) a one-shot; registers the voice when admitted. `duration_s` is the already pitch-scaled length.
	Decision Admit(const SoundDef& s, double now, double duration_s);
	// The glue found the voice finished (or stopped) earlier than estimated.
	void Release(uint64_t id);
	// Removes voices whose estimated end has passed; appends their ids to `expired` when given.
	void Prune(double now, std::vector<uint64_t>* expired = nullptr);
	int Active() const { return static_cast<int>(voices_.size()); }
	int ActiveOf(const std::string& sound) const;
	void Clear() { voices_.clear(); }

private:
	struct Voice {
		uint64_t id = 0;
		std::string sound;
		double start = 0.0, end = 0.0;
		int priority = 2;
		bool counted = true;             // counts against the global cap (sfx bus only)
	};
	std::vector<Voice> voices_;
	uint64_t next_id_ = 1;
	int global_ = 28;
};

class Ducker {
public:
	void Configure(const DuckDef& d) { def_ = d; }
	bool IsTrigger(const std::string& sound) const;
	void Trigger() { hold_left_ = def_.hold_s; active_ = true; }
	// Advances by real dt; returns the current gain offset in dB (0 .. def.db).
	float Step(float dt);
	float CurrentDb() const { return db_; }

private:
	DuckDef def_;
	float db_ = 0.0f;
	float hold_left_ = 0.0f;
	bool active_ = false;
};

}  // namespace ffa
