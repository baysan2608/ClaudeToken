// Fourfold game logic island - game feel policy (docs/MOVESET.md §10.2, ARCHITECTURE §7.5; port of the feel parts of
// game/presentation/fx_director.gd + vfx/fx_cues.gd): hit-stop budget, haptic rate limit, and the event -> feel mapping
// (hit-stop frames, camera shake / kick / FOV punch / zoom, haptics, white flashes, toasts, slow-motion assist).
// The Unreal side applies the result: global time dilation, the camera rig, FPlatformMisc mobile haptics, the HUD.
#pragma once

#include "FFGMath.h"
#include "ff/Events.h"
#include "ff/Snapshot.h"

#include <cmath>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace ffg {

struct FeelSpec {
	int hitstop = 0;           // 1/60 s units (run against real time, any frame rate)
	float shake = 0.0f;        // camera trauma 0..1 (rotational shake x trauma^2, real time)
	float kick = 0.0f;         // metres along the hit (spring peak)
	float fov = 0.0f;          // degrees (negative = narrower)
	const char* haptic = "light";
	bool roll = false;         // the shake also rolls the view (T3 / knockdown)
};
FeelSpec FeelFor(std::string_view kind);   // t0 t1 t2 t3 block block_heavy perfect clash shatter transform boom

// Linear trauma decay of table shakes (per second).
inline constexpr float kShakeTraumaDecay = 2.2f;

// Hit-stop: global time scale 0.05 for N/60 s of REAL time (frame-rate independent), then an ease back to full speed
// over 50 ms; at most 20/60 s frozen in any rolling second; reduced motion caps one request at 3/60 s.
// Slow-motion assist (perfect deflect, a setting) = 0.55 for 0.22 s of real time.
// Cinematic slow motion (big counters, KO): after the hit-stop, 0.25 for the hold, then an ease back over 0.25 s.
class HitStop {
public:
	static constexpr float kScale = 0.05f;
	static constexpr int kCapPerSecond = 20;
	static constexpr int kReducedCap = 3;
	static constexpr float kEaseOutS = 0.05f;
	static constexpr float kSlowmoScale = 0.55f;
	static constexpr float kCineScale = 0.25f;
	static constexpr float kCineEaseS = 0.25f;
	static constexpr float kCineHoldS = 0.4f;
	static constexpr float kCineHoldKoS = 0.9f;

	bool enabled = true;
	void Request(int frames, double now_s, bool reduced_motion);
	// real_seconds > 0: the slow-motion assist; < 0: a cinematic hold of -real_seconds (EncodeCinematic). The sign keeps
	// UFourfoldSimSubsystem::RequestSlowmo the single entry point.
	void RequestSlowmo(float real_seconds) {
		if (real_seconds < 0.0f)
			RequestCinematic(-real_seconds);
		else
			slowmo_ = std::max(slowmo_, real_seconds);
	}
	static float EncodeCinematic(float hold_s) { return -std::fabs(hold_s); }
	void RequestCinematic(float hold_s) {
		cine_hold_ = std::max(cine_hold_, hold_s);
		cine_out_t_ = -1.0f;
	}
	// Once per rendered frame AFTER the frame's events were handled: the time dilation the NEXT frame should run at.
	// real_dt = the frame that just ran (at the dilation this returned last time).
	float FrameTick(double now_s, float real_dt);
	int Pending() const { return static_cast<int>(std::lround(pending_s_ * 60.0f)); }
	float PendingSeconds() const { return pending_s_; }
	bool CinematicActive() const { return cine_hold_ > 0.0f || cine_out_t_ >= 0.0f; }
	void Reset() {
		pending_s_ = 0.0f;
		slowmo_ = 0.0f;
		cine_hold_ = 0.0f;
		cine_out_t_ = -1.0f;
		ease_t_ = -1.0f;
		last_freeze_ = false;
		hist_.clear();
	}

private:
	struct Used {
		double at;
		float s;
	};
	void Trim(double now_s);
	float UsedSeconds() const;
	float pending_s_ = 0.0f;
	float slowmo_ = 0.0f;
	float cine_hold_ = 0.0f;
	float cine_out_t_ = -1.0f;
	float ease_t_ = -1.0f;
	bool last_freeze_ = false;
	std::deque<Used> hist_;
};

// Cinematic slow motion at most once every 3 s (a KO always gets it).
class CinematicGate {
public:
	static constexpr double kCooldownS = 3.0;
	bool Allow(double now_s, bool force) {
		if (!force && now_s - last_ < kCooldownS) return false;
		last_ = now_s;
		return true;
	}

private:
	double last_ = -1000.0;
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
	float decay_s = 0.2f;     // trauma reaches 0 after this long
	bool player = false;      // the local player gave / took it (falloff floor)
	bool roll = false;
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
	float cinematic = 0.0f;               // real seconds of cinematic slow motion (big counter / KO), 0 = none
	Vec3 cinematic_at;
	bool cinematic_ko = false;            // ignores the cooldown
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
