// Fourfold core tests - port of game/tests/sim/test_kit_water_base.gd (WaterKitTest): shared fixture of the Water kit
// suites (ledger snapshots, duels, direct move runs, body / zone queries, synthetic threats, fx catalogue check).
#pragma once

#include "sim_harness.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fft {

struct WaterCase : HarnessCase {
	// Energy and every mass ledger of a world, as one snapshot (exact conservation checks).
	struct Snap {
		std::array<double, 5> v{};   // energy water earth metal plant
	};
	static Snap snap(ff::CombatWorld& w);
	void ledgers_ok(const Snap& base, const std::string& label, double eps = 1e-5);

	// Water fighter W (sub s) facing a passive rival R `dist` m away, after the turn-to-face ticks.
	std::pair<ff::ActorState*, ff::ActorState*> duel(int sub = 0, int rival_element = ff::Sim::EARTH, uint64_t seed_value = 3,
	                                                 double dist = 10.0);
	// Starts `id` directly at `tier` (charge frozen) with the buttons held `hold_ticks`, then released; steps `after` more.
	ff::ActionRef run_move(ff::ActorState* p, const std::string& id, int tier, int hold_ticks = 30, int after = 90);
	std::vector<ff::BodyRef> bodies_of(ff::Mat mat, int form = -1);
	std::vector<ff::BodyRef> zones_tagged(const std::string& tag);
	// A synthetic threat agent: class, threat power (PU on the main channel K unless given), mass.
	static ff::AgentRef threat(ff::CombatWorld& w, const std::string& cls, double tp, double mass = 10.0, const std::string& channel = "K");
	// Every fx / interaction key logged by the harness is catalogued (docs/MOVESET.md §15.6).
	void fx_catalogued(const std::string& label);
	// VerbMotion / VerbGrip shape kinds accepted as fx shapes (MODE_SHAPES).
	static bool mode_shape(const std::string& shape);
	// Keeps a body alive past end-of-tick cleanup so a test can read it after stepping.
	static ff::BodyRef keep(ff::MatBody* b);
};

}  // namespace fft
