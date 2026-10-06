// Fourfold core tests - port of game/tests/sim/test_kit_air_util.gd (AirKitTest): shared fixture of the Air kit suites
// (ledger snapshots, duels, direct move runs, run-when-ready, body / zone queries, synthetic threats, lava waves, fx
// catalogue check, finite-world check).
#pragma once

#include "sim_harness.h"

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fft {

struct AirCase : HarnessCase {
	struct Snap {
		std::array<double, 5> v{};   // energy water earth metal plant
	};
	static Snap snap(ff::CombatWorld& w);
	void ledgers_ok(const Snap& base, const std::string& label, double eps = 1e-5);

	// Air fighter A (sub s, kit {glide}) facing a passive (dummy) rival R `dist` m away, after the turn-to-face ticks.
	std::pair<ff::ActorState*, ff::ActorState*> duel(int sub = 0, int rival_element = ff::Sim::EARTH, uint64_t seed_value = 3,
	                                                 double dist = 10.0);
	// Starts `id` directly at `tier` (charge frozen) with the buttons held `hold_ticks`, then released; steps `after` more.
	ff::ActionRef run_move(ff::ActorState* p, const std::string& id, int tier, int hold_ticks = 30, int after = 90);
	// Starts `id` at `tier` and calls `on_ready` at the last moment (T0 right after the press, higher tiers after the
	// frozen hold of `hold` ticks), then releases and steps `after` ticks.
	ff::ActionRef run_when(ff::ActorState* p, const std::string& id, int tier, const std::function<void()>& on_ready, int hold = 28,
	                       int after = 40);
	std::vector<ff::BodyRef> bodies_of(ff::Mat mat, int form = -1);
	std::vector<ff::BodyRef> bodies_tagged(const std::string& tag);
	std::vector<ff::BodyRef> zones_tagged(const std::string& tag);
	static ff::AgentRef threat(ff::CombatWorld& w, const std::string& cls, double tp, double mass = 10.0, const std::string& channel = "K");
	// A live lava wave (20 kg = 396 HU, the flagship's poured wave) heading +z.
	ff::BodyRef lava_wave(double mass, ff::Vec3 pos = ff::Vec3());
	void fx_catalogued(const std::string& label);
	void finite_world(const std::string& label);
	static bool mode_shape(const std::string& shape);
	static ff::BodyRef keep(ff::MatBody* b);
};

}  // namespace fft
