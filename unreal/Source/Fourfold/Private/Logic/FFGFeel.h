// Fourfold game logic island - game feel policy (docs/MOVESET.md §10.2, ARCHITECTURE §7.5; port of the feel parts of
// game/presentation/fx_director.gd + vfx/fx_cues.gd): hit-stop budget, haptic rate limit, and the event -> feel mapping
// (hit-stop frames, camera shake / kick / FOV punch / zoom, haptics, white flashes, toasts, slow-motion assist).
// The Unreal side applies the result: global time dilation, the camera rig, FPlatformMisc mobile haptics, the HUD.
#pragma once

#include "FFGMath.h"
#include "ff/Events.h"
#include "ff/Snapshot.h"

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ffg {

struct FeelSpec {
	int hitstop = 0;           // 60 Hz frames
	float shake = 0.0f;        // camera shake amount (x 0.06 m offset amplitude, real time)
	float kick = 0.0f;         // metres along the hit
	float fov = 0.0f;          // degrees (negative = narrower)
	const char* haptic = "light";
};
FeelSpec FeelFor(std::string_view kind);   // t0 t1 t2 t3 block block_heavy perfect clash shatter transform boom

// Hit-stop: global time scale 0.05 for N rendered frames; <= 12 frozen frames in any rolling second; reduced motion caps
// one request at 3 frames. Slow-motion assist (perfect deflect) = 0.55 for 0.22 s of real time.
class HitStop {
public:
	static constexpr float kScale = 0.05f;
	static constexpr int kCapPerSecond = 12;
	static constexpr float kSlowmoScale = 0.55f;

	bool enabled = true;
	void Request(int frames, double now_s, bool reduced_motion);
	void RequestSlowmo(float real_seconds) { slowmo_ = std::max(slowmo_, real_seconds); }
	// Once per rendered frame AFTER the frame's events were handled: the time dilation the NEXT frame should run at.
	float FrameTick(double now_s, float real_dt);
	int Pending() const { return pending_; }
	void Reset() {
		pending_ = 0;
		slowmo_ = 0.0f;
		hist_.clear();
	}

private:
	void Trim(double now_s);
	int pending_ = 0;
	float slowmo_ = 0.0f;
	std::deque<double> hist_;
};

// One pulse at most every 60 ms; nothing when disabled.
class HapticGate {
public:
	static constexpr double kMinGapS = 0.060;
	bool Allow(double now_s, bool enabled) {
		if (!enabled || now_s - last_ < kMinGapS) return false;
		last_ = now_s;
		return true;
	}

private:
	double last_ = -1000.0;
};

struct FeelShake {
	float amount = 0.0f;
	Vec3 pos;                 // sim space (metres, +Y up)
	bool has_pos = false;     // false: no distance falloff
	float decay_s = 0.2f;
};
struct FeelKick {
	Vec3 dir;                 // sim space
	float amount = 0.0f;
};

struct FeelOutput {
	int hitstop = 0;                      // largest request this frame (frames)
	std::vector<FeelShake> shakes;
	std::vector<FeelKick> kicks;
	float fov_punch = 0.0f;               // degrees, 0 = none
	bool zoom = false;
	Vec3 zoom_at;
	std::vector<std::string> haptics;     // semantic kinds for the local player
	std::string flash;                    // "" | perfect | lightning | evade
	float slowmo = 0.0f;                  // real seconds of slow-motion assist requested
	std::vector<std::string> toasts;
	void Clear() { *this = FeelOutput(); }
};

struct FeelOptions {
	int player_id = -1;
	float flashes = 1.0f;     // setting 0..1
	bool slowmo_assist = false;
};

// Maps one frame of sim / session events to feel commands.
void HandleFeelEvents(const std::vector<ff::Event>& events, const ff::Snapshot& snap, const FeelOptions& opt, FeelOutput& out);

// Semantic haptic kind -> UE EMobileHapticsType name (ImpactLight / ImpactMedium / ImpactHeavy / FeedbackSuccess /
// FeedbackWarning / FeedbackError / SelectionChanged).
const char* HapticTypeFor(std::string_view kind);

}  // namespace ffg
