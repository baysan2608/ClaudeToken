// FourfoldFX logic island - per-fighter persistent effects owned by the director (private header of FxDirector.cpp /
// FxCues.cpp): charge tiers (ChargeFX: T1 hand ring + motes, T2 + ground ripple + body rim shell, T3 + aura shell,
// light pulse, 2-frame glint), the legacy fire charge flame / bolt aim line, status visuals, stance auras, water lash
// and draw streams, dash and glide trails. Owner: stream `fx`.
#pragma once

#include "FxDirector.h"
#include "FxParticles.h"

namespace ffx {

struct FxDirector::ChargeFx {
	int actor = -1;
	int tier = -1;
	float frac = 0.0f;
	Fam fam = Fam::Flame;
	std::string move;
	float t = 0.0f, pulse = 0.0f, moteT = 0.0f;
	int glint = 0;
	uint32_t keyRing = 0, keyRipple = 0, keyAura = 0, keyLight = 0, keyMotes = 0;
	ParticleSet motes;
	MeshData motesMesh;
	void Init(Ctx& c, int a) {
		actor = a;
		keyRing = c.keys.New();
		keyRipple = c.keys.New();
		keyAura = c.keys.New();
		keyLight = c.keys.New();
		keyMotes = c.keys.New();
		motes.Reset(12, static_cast<uint32_t>(a) * 97u + 5u);
	}
	void Set(int newTier, float newFrac, Fam f) {
		frac = Sat(newFrac);
		if (f != fam) {
			fam = f;
			tier = -1;
		}
		if (newTier != tier) {
			if (newTier > tier && newTier >= 3) {
				glint = 2;
				pulse = 1.0f;
			}
			tier = newTier;
		}
	}
	void Draw(Ctx& c, const Vec3& hands, const Vec3& feet);
};

// Legacy fire charge (FireChargeFX: steady teardrop flame at the hands) and, once the bolt is ready, the crackling
// aim line (ChargeAimFX) toward the lock target.
struct FxDirector::FireChargeFx {
	int actor = -1;
	bool aim = false;
	float t = 0.0f, scroll = 0.0f, restrike = 0.0f;
	uint32_t keyOuter = 0, keyInner = 0, keyLight = 0, keyLine = 0, keyDot = 0;
	std::vector<BoltLine> lines;
	MeshData line;
	Rng rng;
	void Init(Ctx& c, int a) {
		actor = a;
		keyOuter = c.keys.New();
		keyInner = c.keys.New();
		keyLight = c.keys.New();
		keyLine = c.keys.New();
		keyDot = c.keys.New();
		rng.Seed(static_cast<uint64_t>(a) * 131u + 7u);
	}
	void Draw(Ctx& c, const Vec3& hands, const Vec3& target, float charge01);
};

struct FxDirector::StatusFx {
	int actor = -1;
	std::string name, style;
	float t = 0.0f, missing = 0.0f;
	std::unique_ptr<FlameTongues> flames;
	std::unique_ptr<Crackle> crackle;
	std::unique_ptr<VineTubes> vines;
	std::unique_ptr<PuffCloud> veil;
	uint32_t keyShell = 0;
	float phase = 0.0f;
};

struct FxDirector::AuraFx {
	int actor = -1;
	Fam fam = Fam::Wind;
	uint32_t key = 0;
	float phase = 0.0f, fade = 0.0f;
	bool on = true;
};

struct FxDirector::Transient {
	enum class Kind : uint8_t { Lash, Draw, Dash } kind = Kind::Lash;
	int actor = -1, body = -1;
	float t = 0.0f, dur = 0.28f, range = 3.0f, splash = 0.0f;
	Vec3 dir, at;
	uint32_t key = 0;
	MeshData mesh;
	Trail trail;
};

// Status name -> visual style (FxCues.STATUS_FX).
std::string_view StatusStyle(std::string_view status);

}  // namespace ffx
