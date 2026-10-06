// Fourfold core tests - shared rig of the AI suites (test_ai_*.gd, test_regressions_ai.gd): a harness, a player, an
// opponent driven by an AiBrain, and AiTestAccess (the brain's private state the GDScript tests read directly).
#pragma once

#include "sim_harness.h"

#include "AI/AiBrain.h"
#include "AI/AiPlanner.h"
#include "AI/AiPresets.h"

#include <memory>
#include <string>

namespace ff {
struct AiTestAccess {
	static double& t(AiBrain& b) { return b.t_; }
	static double& next_attack(AiBrain& b) { return b.next_attack_; }
	static std::string& hold(AiBrain& b) { return b.hold_; }
	static double& hold_until(AiBrain& b) { return b.hold_until_; }
	static std::map<std::string, double>& seen(AiBrain& b) { return b.seen_; }
	static std::map<std::string, std::string>& decided(AiBrain& b) { return b.decided_; }
	static void start_draw(AiBrain& b, MatBody& body) { b._start_draw(body); }
	static double pool_distance(AiBrain& b) { return b._pool_distance(); }
};
}  // namespace ff

namespace fft {

struct AiRig {
	std::unique_ptr<SimHarness> h;
	ff::ActorState* p = nullptr;
	ff::ActorState* o = nullptr;
	std::unique_ptr<ff::AiBrain> ai;
	// One AI tick then one world tick (GDScript h.intents[o.id] = ai.think(Sim.DT); h.step()).
	void tick(int n = 1) {
		for (int k = 0; k < n; ++k) {
			h->intents[o->id] = ai->think(ff::Sim::DT);
			h->step();
		}
	}
	void think() { h->intents[o->id] = ai->think(ff::Sim::DT); }
};

// A kit literal {element: [subs]} -> the configure() dict pieces ("elements" array, "subs" {"e": [subs]}).
inline ff::Array kit_elements(const ff::AiKit& kit) {
	ff::Array a;
	for (const auto& kv : kit) a.append(kv.first);
	return a;
}
inline ff::Dict kit_subs(const ff::AiKit& kit) {
	ff::Dict d;
	for (const auto& kv : kit) {
		ff::Array s;
		for (int x : kv.second) s.append(x);
		d.set(std::to_string(kv.first), s);
	}
	return d;
}
// GDScript c.merge(opts, true).
inline void merge_into(ff::Dict& c, const ff::Dict& opts) {
	for (const auto& kv : opts) c.set(kv.first, kv.second);
}

}  // namespace fft
