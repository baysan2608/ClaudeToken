// Fourfold game logic island - clip timing against the sim's action phases (ARCHITECTURE §8.4, MARTIAL_ARTS §1.1).
// A clip is never played at its authored speed blindly: the STARTUP clip is time-mapped so its contact frame lands
// exactly at the end of the sim's effective startup, with the playback rate kept inside [kRateMin, kRateMax]:
//   * contact/startup inside the range: one constant rate;
//   * faster than kRateMax: the clip starts partway in (the anticipation is shortened), then runs at kRateMax;
//   * slower than kRateMin: the anticipation plays at kRateMin to the chamber point (50 % of the way to contact),
//     holds the chamber, then continues at kRateMin so contact still lands on the last startup frame.
// ACTIVE continues the clip from its contact at rate 1; RECOVERY re-aims the rate so the clip ends with the recovery
// and the action fades back to locomotion over its last frames.
#pragma once

#include "FFGMath.h"

namespace ffg {

struct AnimTiming {
	static constexpr float kRateMin = 0.6f;
	static constexpr float kRateMax = 1.6f;
	static constexpr float kChamberFrac = 0.5f;

	// Clip time at startup time t (0..startup) for a clip whose power point is at `contact` seconds.
	static float StartupClipTime(float t, float startup, float contact);
	// Effective playback rate of the startup mapping at t (debug / tests).
	static float StartupRate(float startup, float contact);
	// ACTIVE: the clip runs on from `from` (its contact) at rate 1.
	static float ActiveClipTime(float t, float from) { return from + std::max(t, 0.0f); }
	// RECOVERY: from clip time t0 to the clip end `duration` over `recovery` seconds (rate clamped), held at the end.
	static float RecoveryClipTime(float t, float recovery, float t0, float duration);
	// Weight of the action pose over locomotion during recovery (1 -> 0 over the last frames).
	static float RecoveryWeight(float t, float recovery);
	// Looping clip time.
	static float LoopTime(float t, float duration) { return duration > 1e-4f ? Fposmod(std::max(t, 0.0f), duration) : 0.0f; }
	// Non-looping clip time clamped to its length.
	static float OnceTime(float t, float duration) { return Clampf(t, 0.0f, std::max(duration, 0.0f)); }
	// Rate that fits a whole reaction clip into a stun of `stun` seconds (getup / knockdown), clamped.
	static float FitRate(float duration, float stun, float lo = 0.7f, float hi = 1.6f) {
		return stun > 1e-3f ? Clampf(duration / stun, lo, hi) : 1.0f;
	}
};

}  // namespace ffg
