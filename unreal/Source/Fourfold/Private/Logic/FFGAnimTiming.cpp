// Fourfold game logic island - clip timing (see FFGAnimTiming.h).
#include "FFGAnimTiming.h"

namespace ffg {

float AnimTiming::StartupRate(float startup, float contact) {
	if (startup <= 1e-4f) return kRateMax;
	return Clampf(contact / startup, kRateMin, kRateMax);
}

float AnimTiming::StartupClipTime(float t, float startup, float contact) {
	contact = std::max(contact, 0.0f);
	if (startup <= 1e-4f) return contact;
	t = Clampf(t, 0.0f, startup);
	const float r = contact / startup;
	if (r > kRateMax) {
		// Too fast to play whole: start partway in, run at the maximum rate, land contact on time.
		return std::max(0.0f, contact - (startup - t) * kRateMax);
	}
	if (r < kRateMin) {
		// Too slow: anticipation to the chamber at the minimum rate, hold, then on to contact at the minimum rate.
		const float chamber = contact * kChamberFrac;
		return std::max(std::min(t * kRateMin, chamber), contact - (startup - t) * kRateMin);
	}
	return t * r;
}

float AnimTiming::RecoveryClipTime(float t, float recovery, float t0, float duration) {
	if (duration <= t0) return duration;
	if (recovery <= 1e-4f) return duration;
	const float rate = Clampf((duration - t0) / recovery, kRateMin, kRateMax);
	return std::min(t0 + std::max(t, 0.0f) * rate, duration);
}

float AnimTiming::RecoveryWeight(float t, float recovery) {
	if (recovery <= 1e-4f) return 0.0f;
	const float blend = std::min(0.18f, recovery * 0.6f);
	return 1.0f - SmoothStep(recovery - blend, recovery, t);
}

}  // namespace ffg
