// FourfoldAudio logic island - the rule engine: ff::Event -> sound play requests, state-driven loop wants, footsteps.
// Engine-free (no Unreal headers); unit-tested with CMake (Private/Logic/tests). Port of the Godot AudioDirector / FxDirector /
// FxCues cue logic, driven by the manifest data (see FFAManifest.h).
#pragma once

#include "FFAManifest.h"
#include "FFAMix.h"

#include "ff/Events.h"
#include "ff/Snapshot.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ffa {

// Read-only world access for the rules (the current snapshot).
class IWorld {
public:
	virtual ~IWorld() = default;
	virtual int PlayerId() const = 0;
	virtual const ff::ActorView* Actor(int id) const = 0;
	virtual const ff::BodyView* Body(int id) const = 0;
};

class SnapshotWorld final : public IWorld {
public:
	explicit SnapshotWorld(const ff::Snapshot& s) : snap_(s) {}
	int PlayerId() const override { return snap_.player_id; }
	const ff::ActorView* Actor(int id) const override { return snap_.FindActor(id); }
	const ff::BodyView* Body(int id) const override { return snap_.FindBody(id); }

private:
	const ff::Snapshot& snap_;
};

// A one-shot to play (sim-space position).
struct PlayRequest {
	std::string sound;
	bool has_pos = false;
	ff::Vec3 pos;
	bool two_d = false;
	float gain_db = 0.0f;              // rule gain + gain map (the sound's own manifest gain is added by the mixer)
	float pitch = 1.0f;
	float delay_s = 0.0f;
	std::string limit;                 // named limiter ("" none)
	int limit_key = -1;                // actor / body id the limiter is scoped to (per = actor)
};

// A loop kept alive for `hold_s` by an event (refreshed by the next event).
struct HoldRequest {
	std::string key;
	std::string sound;
	ff::Vec3 pos;
	float gain_db = 0.0f;
	float hold_s = 0.3f;
	bool zone = false;
};

// A loop that should be playing this frame (state-driven).
struct LoopWant {
	std::string key;
	std::string sound;
	ff::Vec3 pos;
	float gain_db = 0.0f;
	bool zone = false;
	bool two_d = false;
};

struct StepOut {
	int actor = -1;
	std::string sound;
	ff::Vec3 pos;
	float gain_db = 0.0f;
	float pitch_var = 0.0f;
	bool cloth = false;                // add a quiet cloth rustle (cloth_sound)
	std::string cloth_sound;
};

inline const char* ElementName(int element) {
	static const char* const kNames[4] = {"earth", "water", "fire", "air"};
	return (element >= 0 && element < 4) ? kNames[element] : "";
}

class RuleEngine {
public:
	explicit RuleEngine(const Manifest* manifest = nullptr, uint32_t seed = 12345u);
	void SetManifest(const Manifest* manifest) { m_ = manifest; }

	// Appends the plays and event-held loops one event triggers.
	void ProcessEvent(const ff::Event& e, const IWorld& world, std::vector<PlayRequest>& plays, std::vector<HoldRequest>& holds);

	// Appends the loops the state of this frame wants (bodies and actors; positions interpolate prev -> curr by alpha).
	void EvaluateLoops(const ff::Snapshot& curr, const ff::Snapshot* prev, float alpha, std::vector<LoopWant>& out);

	// Surface class of an actor surface string (stone water puddle metal ice mud sand wood).
	std::string ClassifySurface(const std::string& surface) const;

	// Number of rule plays dropped because the resolved sound is not in the manifest (diagnostic).
	int MissingSounds() const { return missing_; }
	const std::string& LastMissing() const { return last_missing_; }

	static bool EvalCond(const Cond& c, const ff::Value& field);
	static ff::Value DictPath(const ff::Value& root, const std::string& path);

private:
	struct Ctx;
	bool AllConds(const std::vector<Cond>& conds, const Ctx& ctx) const;
	ff::Value Derived(const std::string& name, const Ctx& ctx) const;
	ff::Value Field(const std::string& name, const Ctx& ctx) const;
	std::string Resolve(const SoundRef& ref, const Ctx& ctx);
	bool ResolvePos(const std::string& source, const Ctx& ctx, ff::Vec3& out) const;
	std::string ClassMat(const std::string& cls) const;
	std::string BodyMat(const IWorld& w, int id) const;
	std::string RoundRobin(const std::string& key, const std::vector<std::string>& list);

	const Manifest* m_ = nullptr;
	Rng rng_;
	std::unordered_map<std::string, uint32_t> rr_;
	int missing_ = 0;
	std::string last_missing_;
};

// Footsteps from ground speed (cadence follows speed), per surface, never the same variant twice in a row.
class StepTracker {
public:
	explicit StepTracker(uint32_t seed = 777u) : rng_(seed) {}
	void Reset() { acc_.clear(); count_.clear(); last_.clear(); }
	void Update(const Manifest& m, const RuleEngine& rules, const ff::Snapshot& snap, float dt, std::vector<StepOut>& out);

private:
	Rng rng_;
	std::unordered_map<int, float> acc_;
	std::unordered_map<int, int> count_;
	std::unordered_map<std::string, int> last_;
};

}  // namespace ffa
