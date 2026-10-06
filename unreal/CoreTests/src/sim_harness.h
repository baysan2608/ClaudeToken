// Fourfold core tests - port of game/tests/sim/sim_harness.gd: drives a CombatWorld with scripted intents.
#pragma once

#include "ff/Value.h"
#include "ff_test.h"
#include "Combat/Moves.h"
#include "Sim/ActorState.h"
#include "Sim/Agent.h"
#include "Sim/CombatWorld.h"
#include "Sim/Hooks.h"
#include "Sim/Interactions.h"
#include "Sim/Status.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fft {

class SimHarness {
public:
	std::unique_ptr<ff::CombatWorld> owned;
	ff::CombatWorld* w = nullptr;
	std::map<int, ff::ActorIntent> intents;
	std::vector<ff::Dict> log;

	explicit SimHarness(uint64_t seed_value = 1);
	~SimHarness();

	ff::ActorState* actor(const std::string& nm, ff::Vec3 p, int team, const ff::Dict& kit = ff::Dict(), int element = 0);
	ff::ActorIntent& it(const ff::ActorState* a);
	void press(const ff::ActorState* a, const std::string& what);
	void release(const ff::ActorState* a, const std::string& what);
	void cancel_tech(const ff::ActorState* a);
	void aim(const ff::ActorState* a, ff::Vec3 dir);
	void element(const ff::ActorState* a, int e);
	void step(int n = 1);
	// Steps until cond() is true (checked after each tick). Returns ticks used or -1.
	int until(const std::function<bool()>& cond, int max_ticks = 600);
	std::vector<ff::Dict> events(const std::string& type) const;
	ff::Dict last_event(const std::string& type) const;          // empty Dict when none
	bool has_event(const std::string& type, const std::string& key = "", const ff::Value& value = ff::Value()) const;
	bool any_event(const std::string& type, const std::function<bool(const ff::Dict&)>& pred) const;
	int count_events(const std::string& type, const std::function<bool(const ff::Dict&)>& pred = nullptr) const;
	std::vector<ff::Dict> filter(const std::string& type, const std::function<bool(const ff::Dict&)>& pred) const;

	void sub(const ff::ActorState* a, int s);
	void flick(const ff::ActorState* a, const std::string& button, int gesture, bool press_too = true);
	void hold(const ff::ActorState* a, const std::string& button, int ticks);
	void evade_hold(const ff::ActorState* a, int ticks);
	// mat: Mat index (int) or name.
	ff::MatBody* launch_at(ff::ActorState* target, int mat, double mass, double speed, double temp = ff::Sim::AMBIENT_C,
	                       const std::string& tag = "", ff::ActorState* owner = nullptr, double dist = 6.0);
	ff::MatBody* launch_at(ff::ActorState* target, const std::string& mat, double mass, double speed, double temp = ff::Sim::AMBIENT_C,
	                       const std::string& tag = "", ff::ActorState* owner = nullptr, double dist = 6.0);
	ff::MatBody* spawn_zone(const std::string& tag, ff::Vec3 pos, double radius, ff::ActorState* owner = nullptr, double power = 0.0);
	ff::IxResult predict(const ff::Agent& threat, const std::string& move_id, int tier = 0, bool perfect = false,
	                     ff::ActorState* by = nullptr);

	void begin_scope();
	void end_scope();
	void legacy_bindings_only();

private:
	struct Saved {
		ff::Moves::State moves;
		ff::Interactions::State rules;
		ff::Dict status;
		std::map<std::string, ff::BodyTickFn> ticks;
		std::map<std::string, ff::ZoneEffectFn> zones;
		std::map<std::string, ff::TechPreviewFn> previews;
	};
	std::optional<Saved> saved_;
};

// Event field helpers (GDScript e.get(k)).
inline const ff::Value& ev(const ff::Dict& e, std::string_view k) { return e.get(k); }
inline int ev_i(const ff::Dict& e, std::string_view k, int def = 0) { return static_cast<int>(e.get(k).as_int(def)); }
inline double ev_f(const ff::Dict& e, std::string_view k, double def = 0.0) { return e.get(k).is_nil() ? def : e.get(k).as_float(def); }
inline std::string ev_s(const ff::Dict& e, std::string_view k) { return e.get(k).is_string() ? e.get(k).as_string() : e.get(k).to_string(); }
inline bool ev_b(const ff::Dict& e, std::string_view k) { return e.get(k).truthy(); }

}  // namespace fft

namespace fft {
// Base fixture for ported suites: owns the SimHarness (GDScript `var h: SimHarness`).
struct HarnessCase : TestCase {
	std::unique_ptr<SimHarness> hp;
	SimHarness& H(uint64_t seed = 1) {
		hp = std::make_unique<SimHarness>(seed);
		return *hp;
	}
	SimHarness& h() { return *hp; }
};
}  // namespace fft
