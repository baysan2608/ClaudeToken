// Fourfold core - Godot RandomNumberGenerator (PCG32 XSH-RR, Godot's RandomPCG wrapper) so seeded runs match
// Godot: seed, randi, randf, randf_range, randi_range, randfn. Algorithm: M.E. O'Neill's minimal PCG32
// (pcg-random.org) as used by Godot core/math/random_pcg.* (MIT); reimplemented here.
#pragma once

#include <cstdint>

namespace ff {

class Rng {
public:
	static constexpr uint64_t kDefaultSeed = 12047754176567800795ULL;
	static constexpr uint64_t kDefaultInc = 1442695040888963407ULL;

	Rng() { set_seed(kDefaultSeed); }
	explicit Rng(uint64_t seed) { set_seed(seed); }

	// GDScript `rng.seed = s`.
	void set_seed(uint64_t s);
	uint64_t seed() const { return seed_; }
	uint64_t state() const { return state_; }
	void set_state(uint64_t s) { state_ = s; }

	uint32_t randi();                         // 0 .. 2^32-1
	uint32_t randi_bounded(uint32_t bound);   // 0 .. bound-1 (unbiased)
	float randf();                            // [0, 1] (Godot's float path with clz)
	double randd();                           // double [0, 1]
	float randf_range(float from, float to);  // RandomNumberGenerator.randf_range (real_t = float)
	int randi_range(int from, int to);        // inclusive
	float randfn(float mean = 0.0f, float deviation = 1.0f);

private:
	uint64_t state_ = 0;
	uint64_t inc_ = kDefaultInc;
	uint64_t seed_ = 0;
};

}  // namespace ff
