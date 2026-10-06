// FourfoldAudio logic island - loop keeper (one voice per body / actor / event key, fades in and out, never restarted per
// frame) and the random ambience accents. Engine-free. Port of AudioDirector.loop() / _process() from the Godot build:
//  * a loop starts when its state begins (fade in), is driven every frame, and fades out when the state ends (or the body is
//    gone: its last position is kept so it fades out where it died);
//  * zone loops (tornado, sandstorm, ...) fade slowly (30 dB/s in, 24 dB/s out), body loops quickly (90 / 60 dB/s).
#pragma once

#include "FFAManifest.h"
#include "FFARules.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ffa {

struct LoopState {
	std::string key;
	std::string sound;
	ff::Vec3 pos;
	bool two_d = false;
	bool zone = false;
	float vol_db = -40.0f;       // fade offset relative to the sound's manifest gain (floor .. target)
	float target_db = 0.0f;
	bool on = true;
	bool started = false;        // true in the frame the loop was created: the glue spawns its component
	bool finished = false;       // true in the frame the loop ended: the glue destroys its component
};

class LoopTracker {
public:
	void Configure(const FadeDef& fade, int max_loops) { fade_ = fade; max_loops_ = max_loops; }
	void BeginFrame() { wants_.clear(); }
	void Want(const LoopWant& w) { wants_[w.key] = w; }
	// An event keeps the loop alive until now + hold_s (refreshed by the next event).
	void Hold(const HoldRequest& h, double now);
	// Advances fades by `dt` (real seconds); afterwards States() describes what the glue must do this frame.
	void EndFrame(double now, float dt);
	// Every loop fades out and finishes (scenario change, shutdown).
	void StopAll();
	const std::vector<LoopState>& States() const { return states_; }
	int ActiveCount() const;

private:
	struct Held {
		HoldRequest req;
		double expires = 0.0;
	};
	FadeDef fade_;
	int max_loops_ = 14;
	std::unordered_map<std::string, LoopWant> wants_;
	std::unordered_map<std::string, Held> holds_;
	std::vector<LoopState> states_;
};

struct AccentPlay {
	std::string sound;
	float gain_db = 0.0f;
	float pitch_var = 0.0f;
};

class AmbienceScheduler {
public:
	void Configure(const std::vector<AccentDef>& accents, uint32_t seed = 4242u);
	void Update(float dt, std::vector<AccentPlay>& out);

private:
	std::vector<AccentDef> accents_;
	std::vector<float> left_;
	std::vector<int> last_;
	Rng rng_;
};

}  // namespace ffa
