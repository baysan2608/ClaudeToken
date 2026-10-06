// Fourfold core - Godot-compatible PCG32 random numbers (see Rng.h).
#include "Util/Rng.h"

#include <cmath>

namespace ff {
namespace {

uint32_t RngStep(uint64_t& state, uint64_t inc) {
	const uint64_t old = state;
	state = old * 6364136223846793005ULL + (inc | 1u);
	const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
	const uint32_t rot = static_cast<uint32_t>(old >> 59u);
	return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

int RngClz32(uint32_t x) {
	int n = 0;
	if (x == 0) return 32;
	while ((x & 0x80000000u) == 0) {
		x <<= 1;
		++n;
	}
	return n;
}

}  // namespace

void Rng::set_seed(uint64_t s) {
	seed_ = s;
	// pcg32_srandom_r(&pcg, seed, DEFAULT_INC)
	state_ = 0u;
	inc_ = (kDefaultInc << 1u) | 1u;
	RngStep(state_, inc_);
	state_ += s;
	RngStep(state_, inc_);
}

uint32_t Rng::randi() { return RngStep(state_, inc_); }

uint32_t Rng::randi_bounded(uint32_t bound) {
	if (bound == 0) return 0;
	const uint32_t threshold = (0u - bound) % bound;
	for (;;) {
		const uint32_t r = randi();
		if (r >= threshold) return r % bound;
	}
}

float Rng::randf() {
	const uint32_t proto_exp_offset = randi();
	if (proto_exp_offset == 0) return 0.0f;
	return std::ldexp(static_cast<float>(randi() | 0x80000001u), -32 - RngClz32(proto_exp_offset));
}

double Rng::randd() {
	const uint32_t proto_exp_offset = randi();
	if (proto_exp_offset == 0) return 0.0;
	const uint64_t hi = static_cast<uint64_t>(randi()) << 32;
	const uint64_t significand = hi | randi() | 0x8000000000000001ULL;
	return std::ldexp(static_cast<double>(significand), -64 - RngClz32(proto_exp_offset));
}

float Rng::randf_range(float from, float to) { return randf() * (to - from) + from; }

int Rng::randi_range(int from, int to) {
	if (from == to) return from;
	const int lo = from < to ? from : to;
	const uint32_t span = static_cast<uint32_t>(from > to ? from - to : to - from);
	return static_cast<int>(randi_bounded(span + 1u)) + lo;
}

float Rng::randfn(float mean, float deviation) {
	float temp = randf();
	if (temp < 0.00001f) temp += 0.00001f;
	const float c = std::cos(6.2831853071795864769f * randf());
	const double r = static_cast<double>(c) * std::sqrt(-2.0 * static_cast<double>(std::log(temp)));
	return static_cast<float>(static_cast<double>(mean) + static_cast<double>(deviation) * r);
}

}  // namespace ff
