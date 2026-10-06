// Fourfold core tests - port of game/tests/sim/test_kit_fire_util.gd (FireKitTest): shared fixture of the Fire kit
// suites (ledger snapshots, duels, direct move runs, body / zone queries, synthetic threats, fx catalogue check).
#pragma once

#include "sim_harness.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace fft {

struct FireCase : HarnessCase {
	// Energy and every mass ledger of a world, as one snapshot (exact conservation checks).
	struct Snap {
		std::array<double, 5> v{};   // energy water earth metal plant
	};
	static Snap snap(ff::CombatWorld& w);
	bool ledgers_ok(const Snap& base, const std::string& label, double eps = 1e-4);

	// Fire fighter F (sub s) facing a rival R `dist` m away (both on open flat ground), after the turn-to-face ticks.
	std::pair<ff::ActorState*, ff::ActorState*> duel(int sub = 0, int rival_element = ff::Sim::EARTH, uint64_t seed_value = 5, double dist = 8.0,
	                                                 const ff::Dict& kit = ff::Dict());
	// Starts `id` directly at `tier` (charge frozen) with the buttons held `hold_ticks`, then released; steps `after` more.
	ff::ActionRef run_move(ff::ActorState* p, const std::string& id, int tier, int hold_ticks = 30, int after = 90);
	std::vector<ff::BodyRef> bodies_of(ff::Mat mat, int form = -1);
	std::vector<ff::BodyRef> zones_tagged(const std::string& tag);
	std::vector<ff::Dict> events_of(const std::string& type, const std::string& key, const ff::Value& value);
	// A synthetic threat agent: class, threat power (PU on the main channel), mass.
	static ff::AgentRef threat(ff::CombatWorld& w, const std::string& cls, double tp, double mass = 10.0, const std::string& channel = "K");
	// A stone wall (Bulwark, 120 kg, risen) owned by `owner` at `p` facing yaw.
	ff::MatBody* bulwark(ff::ActorState* owner, ff::Vec3 p, double yaw, double mass = 120.0);
	// Every fx / interaction key logged by the harness is catalogued (docs/MOVESET.md §15.6).
	void fx_catalogued(const std::string& label);
};

}  // namespace fft
