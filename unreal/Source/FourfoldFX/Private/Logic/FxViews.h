// FourfoldFX logic island - persistent body views (ports + upgrades of StoneView, LavaWaveView, EarthWallView,
// WaterBlob/RibbonView, puddle, MetalView, CloudView, GroundStripView, CrystalView, SpikesView, VineView,
// FlameFieldView, FireballView, ShellView, VortexView, WindBladeView, CrackleView, GroundDecalView, tremor rings,
// lava pool). A view is created when a body is first seen, updated every frame from the body state ONLY
// (interpolated position), and drawn; when the body disappears it is drawn a little longer with a fade (1 -> 0).
// Owner: stream `fx`.
#pragma once

#include "FxContext.h"
#include "FxMapping.h"
#include "FxMesh.h"
#include "FxOneShots.h"

#include <memory>
#include <vector>

namespace ffx {

struct BodyFrame {
	const ff::BodyView& b;
	const ff::BodyView* prev;   // same body at the previous sim tick (nullptr on its first tick)
	Vec3 p;                     // interpolated position
	bool newTick;               // a sim tick was stepped since the last frame
};

class BodyView {
public:
	explicit BodyView(ViewSel s) : sel(std::move(s)) {}
	virtual ~BodyView() = default;
	virtual void Init(const BodyFrame& f, Ctx& c) = 0;
	// Reads the body state (alive frames only).
	virtual void Update(const BodyFrame& f, Ctx& c) = 0;
	// Emits draw items: fade 1 while the body lives, 1 -> 0 while the view fades out after the body is gone.
	virtual void Draw(Ctx& c, float fade) = 0;
	virtual float FadeTime() const { return 0.25f; }
	// The body broke (shatter / wall_crumble): emits physics debris (FractureReq) when the glue simulates it and
	// returns true if it did (the caller then skips the procedural chips). A view whose body ends with the break stops
	// drawing: its pieces take its place.
	virtual bool Break(Ctx& c) {
		(void)c;
		return false;
	}

	ViewSel sel;
	int body = -1;
	int tierHint = 0;           // charge tier of the launch that threw this body (from `launch` events)
	std::vector<Vec3> trail;    // body positions, one per sim tick (<= 6), for ribbons / lashes / slugs
	Vec3 lastPos;
};

std::unique_ptr<BodyView> MakeView(const ViewSel& sel);

// Overlay crackle (any body with charge > 4) and the standalone ground-current / status crackle share this.
class Crackle {
public:
	enum class Mode : uint8_t { Body, Ground, Actor };
	void Setup(Ctx& c, Mode m, float radius, uint32_t seed);
	void SetTarget(const Vec3& center, const std::vector<Vec3>* path = nullptr);
	void SetIntensity(float i) { intensity_ = Clamp(i, 0.0f, 1.5f); }
	// Re-strikes on its own clock and draws (no light: overlays never use the scene's light budget).
	void Draw(Ctx& c, float fade, int attachActor = -1);

private:
	Mode mode_ = Mode::Body;
	float radius_ = 0.4f, intensity_ = 1.0f, t_ = 0.0f, age_ = 1.0f;
	Vec3 center_;
	std::vector<Vec3> path_;
	Rng rng_;
	std::vector<BoltLine> lines_;
	MeshData mesh_;
	uint32_t key_ = 0;
};

// Flame tongues as one mesh (fire fields, fire lines, burning status, flame columns).
class FlameTongues {
public:
	enum class Mode : uint8_t { Field, Line, Burning, Column };
	void Setup(Ctx& c, Mode m, uint32_t seed, float radius, float height, bool blue);
	void SetPath(Ctx& c, const std::vector<Vec3>& pts);
	void SetIntensity(float i) { intensity_ = Sat(i); }
	void Draw(Ctx& c, const Xform& x, float fade, bool light, int attachActor = -1, Bone bone = Bone::Pelvis);
	Mode mode() const { return mode_; }

private:
	void Build(Ctx& c, const std::vector<Vec3>& bases, const std::vector<Color>& data);
	Mode mode_ = Mode::Field;
	float radius_ = 1.0f, height_ = 0.8f, intensity_ = 1.0f, scroll_ = 0.0f;
	bool blue_ = false;
	uint32_t seed_ = 0, key_ = 0, keyLight_ = 0;
	Vec3 lightPos_;
	Vec3 sig_{1e9f, 1e9f, 1e9f};
	MeshData mesh_;
};

// Vine / root tubes as one mesh (lattice wall, briar, roots, lash, rooted status).
class VineTubes {
public:
	void Setup(Ctx& c, const std::string& mode, uint32_t seed, const Vec3& size);
	void SetPath(const std::vector<Vec3>& pts, float radius);
	void SetState(float grow, float burn, float frozen);
	void Draw(Ctx& c, const Xform& x, float fade, int attachActor = -1, Bone bone = Bone::Pelvis);

private:
	void Rebuild();
	std::string mode_;
	std::vector<std::vector<Vec3>> paths_;
	std::vector<float> radii_;
	float grow_ = 1.0f, builtGrow_ = -1.0f, burn_ = 0.0f, frozen_ = 0.0f, phase_ = 0.0f;
	uint32_t seed_ = 0, key_ = 0;
	bool dirty_ = true;
	Vec3 sig_{1e9f, 1e9f, 1e9f};
	MeshData mesh_;
};

// Camera-facing puff volume (clouds, fog, steam, sandstorm, slug, veil, smoke): the CloudView shader on the CPU.
class PuffCloud {
public:
	void Configure(Ctx& c, CloudStyle style, uint32_t seed);
	void SetShape(float radius, float height, float squashX = 1.0f) {
		radius_ = MaxF(radius, 0.05f);
		height_ = MaxF(height, 0.05f);
		squash_ = squashX;
	}
	void SetAmount(float a) { amount_ = Sat(a); }
	// x = volume frame (pos = ground centre, basis = orientation); `opacityScale` multiplies the style opacity.
	void Draw(Ctx& c, const Xform& x, float fade, float opacityScale = 1.0f, int attachActor = -1,
	          Bone bone = Bone::Pelvis);

private:
	struct Puff {
		float ang, rf, h, rnd;
	};
	CloudStyle style_ = CloudStyle::Mist;
	std::vector<Puff> outer_, core_;
	float radius_ = 1.5f, height_ = 1.0f, squash_ = 1.0f, amount_ = 1.0f, phase_ = 0.0f;
	uint32_t keyOuter_ = 0, keyCore_ = 0;
	MeshData meshOuter_, meshCore_;
};

}  // namespace ffx
