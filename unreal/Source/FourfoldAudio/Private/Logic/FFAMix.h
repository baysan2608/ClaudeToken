// FourfoldAudio logic island - small mixing helpers (dB, bus volumes, deterministic RNG). Engine-free.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace ffa {

inline float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

inline float LinearToDb(float lin) { return lin <= 1e-6f ? -120.0f : 20.0f * std::log10(lin); }

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float MoveToward(float cur, float target, float step) {
	if (std::fabs(target - cur) <= step) return target;
	return cur + (target > cur ? step : -step);
}

// Linear map of v from [in_lo, in_hi] to [out_lo, out_hi], clamped.
inline float MapRange(float v, float in_lo, float in_hi, float out_lo, float out_hi) {
	if (in_hi == in_lo) return out_lo;
	const float t = Clamp01((v - in_lo) / (in_hi - in_lo));
	return out_lo + (out_hi - out_lo) * t;
}

// Volumes of FFourfoldSettings (0..1): master x bus.
struct BusVolumes {
	float master = 1.0f, sfx = 1.0f, ui = 0.8f, ambience = 0.8f;
	float For(int bus) const {   // 0 sfx, 1 ui, 2 ambience
		const float b = bus == 1 ? ui : (bus == 2 ? ambience : sfx);
		return Clamp01(master) * Clamp01(b);
	}
};

// xorshift32: deterministic, cheap, good enough for pitch / chance / accents.
class Rng {
public:
	explicit Rng(uint32_t seed = 0x9E3779B9u) : s_(seed ? seed : 1u) {}
	uint32_t NextU32() {
		s_ ^= s_ << 13;
		s_ ^= s_ >> 17;
		s_ ^= s_ << 5;
		return s_;
	}
	float Next01() { return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f); }
	float Range(float lo, float hi) { return lo + (hi - lo) * Next01(); }
	int Index(int n) { return n <= 1 ? 0 : static_cast<int>(NextU32() % static_cast<uint32_t>(n)); }

private:
	uint32_t s_;
};

}  // namespace ffa
