// Fourfold game logic island - per-fighter animation director (ARCHITECTURE §8.4). Runs on the game thread once per
// rendered frame from the sim snapshots and produces an AnimRecipe: which clips to sample at which times and weights,
// the hand-shape overrides, additive reactions, a cross-fade request and every procedural-layer parameter. The native
// anim instance proxy (UFourfoldAnimInstance) only evaluates the recipe on the worker thread.
//
// Priorities: stun reaction > perfect / one-shot overlay > action (startup -> hold -> release -> recovery, contact
// aligned to the sim) > airborne (jump / fall / glide) > movement mode loop > locomotion blend. Every change of the
// dominant source triggers an inertial cross-fade of 3-6 frames.
#pragma once

#include "FFGAnimLibrary.h"
#include "FFGAnimTiming.h"
#include "FFGLocomotion.h"
#include "FFGSprings.h"
#include "ff/Snapshot.h"

#include <array>
#include <string>
#include <vector>

namespace ffg {

struct ClipSample {
	const ClipDef* clip = nullptr;
	float time = 0.0f;
	float weight = 0.0f;
};

struct AnimRecipe {
	std::vector<ClipSample> base;            // full-body blend (normalised by the evaluator); empty = reference pose
	std::vector<ClipSample> legs;            // legs-only gait layer over the base
	float legs_weight = 0.0f;
	ClipSample additive;                     // additive delta clip(time) - clip(0) (block impact)
	float additive_weight = 0.0f;
	std::array<const ClipDef*, 2> hand{{nullptr, nullptr}};   // 0 = left, 1 = right finger overrides
	std::array<float, 2> hand_weight{{0.0f, 0.0f}};
	uint32_t transition_serial = 0;          // bumps when a cross-fade must start
	float transition_time = 0.0f;            // its duration (s)
	// procedural layers (model axes; radians / metres)
	float lean_pitch = 0.0f;                 // + tips the top forward
	float lean_roll = 0.0f;                  // + tips the top toward the character's right
	float land_y = 0.0f;                     // pelvis compression along up (negative = down)
	Vec3 spring_torso, spring_head, spring_arm_l, spring_arm_r;   // rotation vectors, model space
	float aim_yaw = 0.0f;                    // + turns the chest toward the character's left
	float aim_weight = 0.0f;
	bool has_look = false;
	Vec3 look_target;                        // model space (metres)
	float look_weight = 0.0f;
	float ik_weight = 0.0f;
	float lock_weight = 0.0f;
	std::array<int, 2> plant_hint{{-1, -1}}; // from the dominant clip's foot_plants: -1 unknown, 0 lifted, 1 planted
	float breathe = 0.0f;                    // breathing additive amplitude 0..1
	float breathe_phase = 0.0f;              // radians
	int lod = 0;                             // 0 full, 1 reduced (no look / chains), 2 clips only
	bool chains = true;                      // secondary spring chains on
	float dt = 0.0f;                         // animation seconds this frame
	std::string debug;                       // one line: source / clip / time
};

struct ReactionEvent {
	enum Kind { Hit, Block, Perfect, Land, Burn } kind = Hit;
	Vec3 dir;            // model space, the way the blow travels
	float strength = 1.0f;
	float down_speed = 0.0f;   // Land: vertical speed (m/s)
	bool knockdown = false;
	bool heavy = false;
};

struct DirectorInput {
	const ff::ActorView* cur = nullptr;
	const ff::ActorView* prev = nullptr;   // may be null (first frame)
	float alpha = 1.0f;                    // interpolation prev -> cur
	float dt = 0.0f;                       // animation seconds (0 while paused / frozen; scaled by the Lab time scale)
	Vec2 local_vel;                        // measured, x = character's right, y = forward (m/s)
	Vec2 local_acc;                        // measured acceleration in the same frame (m/s^2)
	float yaw_rate = 0.0f;                 // rad/s, + = turning left
	bool dummy = false;
	int lod = 0;
	bool has_look_target = false;
	Vec3 look_target;                      // model space (metres)
	bool look_is_threat = false;
	std::vector<ReactionEvent> events;     // this actor's reactions this frame
	// Physical knockdown (presentation): the ragdoll has landed, so the get-up starts while the sim still counts the
	// knockdown down; it then spans the rest of the knockdown + the sim's get-up. side 1 = lying on the back.
	bool getup_early = false;
	int getup_side = 0;
};

class AnimDirector {
public:
	static constexpr float kSimDt = 1.0f / 60.0f;
	static constexpr float kSimGetupS = 0.75f;   // the sim's get-up stun after a knockdown (CombatWorld::_timers)

	const AnimLibrary* lib = nullptr;
	ModelAxes axes;
	HitReactor hits;
	LandingSpring landing;
	LocomotionBlender loco;

	void Reset();
	const AnimRecipe& Update(const DirectorInput& in);
	const AnimRecipe& Recipe() const { return r_; }
	const std::string& SourceKey() const { return key_; }

private:
	void AddLocomotion(const ff::ActorView& a, float weight, std::vector<ClipSample>& out, int max_clips, bool gaits_only = false);
	bool ActionPose(const DirectorInput& in, const ff::ActorView& a, float& w_act);
	void ReactionPose(const DirectorInput& in, const ff::ActorView& a);
	void Procedural(const DirectorInput& in, const ff::ActorView& a, bool free_loco, float legs);
	void SetKey(const std::string& key, float fade_frames);
	const ClipDef* Clip(const std::string& name) const { return lib ? lib->Resolve(name) : nullptr; }
	std::string EvadeClip(const ff::ActorView& a) const;

	AnimRecipe r_;
	std::string key_;
	uint32_t serial_ = 0;
	// action instance tracking
	std::string act_id_;
	ff::ActionPhase act_phase_ = ff::ActionPhase::Done;
	float act_total_ = 0.0f;
	uint32_t act_serial_ = 0;
	float hand_w_ = 0.0f;
	// reactions
	std::string stun_kind_;
	float stun_t_ = 0.0f;
	float stun_rate_ = 1.0f;
	uint32_t stun_serial_ = 0;
	// overlays
	const ClipDef* overlay_ = nullptr;
	float overlay_t_ = 0.0f;
	uint32_t overlay_serial_ = 0;
	const ClipDef* additive_ = nullptr;
	float additive_t_ = 0.0f;
	// air
	bool was_grounded_ = true;
	float prev_vy_ = 0.0f;
	float air_t_ = 0.0f;
	bool jumped_ = false;
	// smoothed procedural weights
	float ik_w_ = 1.0f, look_w_ = 0.0f, aim_w_ = 0.0f, legs_w_ = 0.0f, breathe_w_ = 0.0f, breathe_phase_ = 0.0f;
	Vec2 lean_;
	bool first_ = true;
};

}  // namespace ffg
