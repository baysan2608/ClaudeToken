// FourfoldFX logic island - per-frame input of the director and the context every view / effect draws with.
// Owner: stream `fx`.
#pragma once

#include "FxArena.h"
#include "FxBase.h"
#include "FxConfig.h"
#include "FxDrawList.h"
#include "FxTypes.h"

#include "ff/Events.h"
#include "ff/Snapshot.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ffx {

class OneShots;

struct FxCamera {
	Vec3 pos{0.0f, 3.0f, 8.0f};
	Vec3 fwd{0.0f, -0.3f, -1.0f};
	Vec3 up{0.0f, 1.0f, 0.0f};
};

// Bone positions of one fighter this frame (sim space), from AFourfoldFighter::GetBoneLocation in the glue.
struct FxAnchors {
	int actor = -1;
	std::array<Vec3, static_cast<size_t>(Bone::Count)> bones{};
};

struct FxFrameIn {
	const ff::Snapshot* prev = nullptr;
	const ff::Snapshot* curr = nullptr;
	float alpha = 1.0f;
	const std::vector<ff::Event>* events = nullptr;
	float dt = 1.0f / 60.0f;          // game (dilated) seconds since the last rendered frame
	bool paused = false;
	FxCamera cam;
	const ff::ArenaView* arena = nullptr;
	const std::vector<FxAnchors>* anchors = nullptr;   // may be null (sim fallbacks are used)
	int quality = 2;                  // 0..2 (UFourfoldSettingsSubsystem::GetEffectiveQuality)
	float flashes = 1.0f;             // Settings.Flashes 0..1
	bool reducedMotion = false;
};

class KeyAlloc {
public:
	uint32_t New() { return next_++; }
	uint32_t Peek() const { return next_; }

private:
	uint32_t next_ = 1;
};

struct Ctx {
	const FxConfig& cfg;
	const QualityLevel& q;
	const FxFrameIn& in;
	DrawList& out;
	KeyAlloc& keys;
	Rng& rng;
	OneShots& fx;       // pooled one-shot effects (views and cues spawn through it)
	float dt;          // clamped frame dt (0 while paused)
	double time;       // accumulated FX time (stops while paused / during hit-stop slow-down proportionally)

	float Ground(const Vec3& p) const { return GroundUnder(in.arena, p); }
	float GroundAt(float x, float z, float fromY) const { return GroundHeight(in.arena, x, z, fromY); }

	const ff::ActorView* Actor(int id) const { return in.curr ? in.curr->FindActor(id) : nullptr; }
	const ff::BodyView* Body(int id) const { return in.curr ? in.curr->FindBody(id) : nullptr; }

	// Interpolated actor feet position (prev -> curr).
	Vec3 ActorPos(int id) const {
		const ff::ActorView* a = Actor(id);
		if (!a) return Vec3();
		const ff::ActorView* p = in.prev ? in.prev->FindActor(id) : nullptr;
		return p ? LerpV(p->pos, a->pos, in.alpha) : a->pos;
	}
	// Bone position from the glue's anchors, else the sim's fallback points (ActorState.chest / hand_point).
	Vec3 Anchor(int actorId, Bone b) const {
		if (in.anchors)
			for (const FxAnchors& an : *in.anchors)
				if (an.actor == actorId) return an.bones[static_cast<size_t>(b)];
		const ff::ActorView* a = Actor(actorId);
		if (!a) return Vec3();
		const Vec3 p = ActorPos(actorId);
		const Vec3 f(std::sin(a->facing), 0.0f, std::cos(a->facing));
		const Vec3 r(f.z, 0.0f, -f.x);   // right of facing (right-handed, +Y up)
		switch (b) {
			case Bone::Pelvis: return p + Vec3(0.0f, 0.95f, 0.0f);
			case Bone::Spine: return p + Vec3(0.0f, 1.1f, 0.0f);
			case Bone::Chest: return p + Vec3(0.0f, 1.25f, 0.0f);
			case Bone::Head: return p + Vec3(0.0f, 1.62f, 0.0f);
			case Bone::HandL: return p + f * 0.55f - r * 0.12f + Vec3(0.0f, 1.2f, 0.0f);
			case Bone::HandR: return p + f * 0.55f + r * 0.12f + Vec3(0.0f, 1.2f, 0.0f);
			case Bone::FootL: return p - r * 0.14f + Vec3(0.0f, 0.06f, 0.0f);
			case Bone::FootR: return p + r * 0.14f + Vec3(0.0f, 0.06f, 0.0f);
			default: return p;
		}
	}
	// The "hand point" of a cast: midway between both hands (FighterView.hand_position).
	Vec3 Hands(int actorId) const { return LerpV(Anchor(actorId, Bone::HandL), Anchor(actorId, Bone::HandR), 0.5f); }
	Vec3 Chest(int actorId) const { return Anchor(actorId, Bone::Chest); }

	// Basis whose +Z faces the camera from p (+Y as close to the camera's up as possible).
	Basis Facing(const Vec3& p) const { return Basis::FromFwdUp(in.cam.pos - p, in.cam.up); }
	Vec3 ToCam(const Vec3& p) const { return Norm(in.cam.pos - p, Vec3(0.0f, 0.0f, 1.0f)); }
};

}  // namespace ffx
