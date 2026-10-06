// Fourfold core tests - port of game/tests/sim/test_kit_earth_util.gd: shared helpers of the Earth kit suites.
#pragma once

#include "sim_harness.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fft {
namespace EU {

inline constexpr int TIER_HOLD[4] = {1, 40, 70, 118};   // attack / guard hold ticks that reach T0..T3
int slot_gesture(const std::string& slot);              // thrust UP, ground DOWN, sweep SIDE

// A duel: Earth fighter A (sub `sub`) at z = 4 facing a dummy target T at z = 4 - dist. Returns {a, t}.
struct Duel {
	ff::ActorState* a = nullptr;
	ff::ActorState* t = nullptr;
};
Duel duel(SimHarness& h, int sub, int t_elem = ff::Sim::FIRE, double dist = 7.0);

// Every fx event of the actor uses catalogued keys (fx, mat, shape).
std::vector<std::string> bad_fx(const SimHarness& h, int actor_id);
// Steps n ticks, tracking the lowest Focus (+ reserve as Focus) in *st_min when given.
void steps(SimHarness& h, ff::ActorState* a, int n, double* st_min = nullptr);
// Performs the input of a slot at a tier (hold time) and releases it.
void perform(SimHarness& h, ff::ActorState* a, const std::string& slot, int tier, double* st_min = nullptr);

using Prep = std::function<void(SimHarness&, ff::ActorState*, ff::ActorState*)>;
struct MoveRun {
	std::string move;
	bool started = false, paid = false, ended = false;
	int tier = 0;
	std::vector<std::string> bad_fx;
	ff::ActorState* a = nullptr;
	ff::ActorState* t = nullptr;
	std::unique_ptr<SimHarness> h;
	int fx = 0;
};
// Runs a slot at a tier on a fresh duel (seed 7).
MoveRun run_move(int sub, const std::string& slot, int tier, const Prep& prep = nullptr);

// A molten stone body at p (booked: ground_taken + generated heat).
ff::MatBody* lava(SimHarness& h, double mass, ff::Vec3 p, double liquid = 1.0);
// A lava wave of `owner` at p flowing along dir.
ff::MatBody* lava_wave(SimHarness& h, double mass, ff::Vec3 p, ff::Vec3 dir, ff::ActorState* owner = nullptr);
// A standing wall body raised by `owner` at p facing yaw.
ff::MatBody* wall(SimHarness& h, ff::Mat mat, const std::string& tag, double mass, ff::Vec3 p, ff::ActorState* owner = nullptr, double yaw = 0.0);
// Predicts a threat body against a counter body / a counter move at a tier.
ff::IxResult pr(SimHarness& h, ff::MatBody* threat, ff::MatBody* counter, int tier = 0, bool perfect = false, ff::ActorState* by = nullptr);
ff::IxResult pr(SimHarness& h, ff::MatBody* threat, const std::string& move, int tier = 0, bool perfect = false, ff::ActorState* by = nullptr);
// Predicts a volume (class, channels) against a counter body / move.
ff::IxResult prv(SimHarness& h, const std::string& cls, const ff::Dict& ch, ff::MatBody* counter);
ff::IxResult prv(SimHarness& h, const std::string& cls, const ff::Dict& ch, const std::string& move, int tier = 0, ff::ActorState* by = nullptr);
// A loose body of `mat` at p moving with vel as an attack of owner (stone / sand / glass book ground_taken, metal metal_taken).
ff::MatBody* shot(SimHarness& h, ff::Mat mat, double mass, ff::Vec3 p, ff::Vec3 vel, ff::ActorState* owner = nullptr, const std::string& tag = "",
                  double temp = ff::Sim::AMBIENT_C);
// GDScript keeps a removed body object alive while referenced: tests that read a body after it may be removed (and
// cleaned up at the end of a tick) hold a BodyRef.
inline ff::BodyRef keep(ff::MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }
double energy_drift(SimHarness& h, double e0);
double e0(SimHarness& h);

}  // namespace EU
}  // namespace fft
