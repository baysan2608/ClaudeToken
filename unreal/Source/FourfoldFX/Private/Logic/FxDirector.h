// FourfoldFX logic island - the FX director: one per world. Every rendered frame it takes the sim output
// (prev / curr snapshots, alpha, events) and produces the DrawList the Unreal renderer applies:
//   * body views keyed by body id (created on first sight, updated from body state, faded out when gone),
//   * one-shot cues from events (ports of FxDirector._event and FxCues: fx / interaction / charge / status / zone /
//     clash / morph / slump / convert / capture / ricochet / stance / mode / inrush / extinguish / stick ... and the
//     legacy events hit / block / launch / impact / wall / transform / shatter / flare / lightning / gust / lash ...),
//   * per-fighter persistent effects (charge tiers, status visuals, stance auras, water lash / draw, dash trails),
//   * world reactions (FxWorld): footfall dust, ground scars, pool splashes / ripples / steam, gusts + arena wind.
// Hit-stop, camera shake, haptics and screen flashes belong to the game's feel director; sounds to FourfoldAudio.
// Owner: stream `fx`.
#pragma once

#include "FxConfig.h"
#include "FxContext.h"
#include "FxDrawList.h"
#include "FxMapping.h"
#include "FxOneShots.h"
#include "FxViews.h"
#include "FxWorld.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ffx {

struct FxStats {
	int views = 0;
	int dyingViews = 0;
	int oneShots = 0;
	int actorFx = 0;
	int items = 0;
	int lights = 0;
	int triangles = 0;
	int eventsHandled = 0;
	int fractures = 0;        // physics debris requests this frame
	int worldFx = 0;          // world reactions running (scars, ripples, dust puffs; also counted in oneShots)
	int unmappedBodies = 0;   // live bodies without a view (should stay 0; listed in `unmapped`)
	std::vector<std::string> unmapped;
};

class FxDirector {
public:
	FxDirector();
	~FxDirector();
	FxDirector(const FxDirector&) = delete;
	FxDirector& operator=(const FxDirector&) = delete;

	void SetConfig(const FxConfig& cfg) { cfg_ = cfg; }
	const FxConfig& Config() const { return cfg_; }
	// Drops every view and effect (scenario change). Keys keep increasing (never reused).
	void Reset();
	// One rendered frame. The returned list is valid until the next Update / Reset.
	const DrawList& Update(const FxFrameIn& in);
	const FxStats& Stats() const { return stats_; }

private:
	struct ViewSlot {
		std::unique_ptr<BodyView> view;
		std::unique_ptr<Crackle> aux;   // charged-body crackle overlay
		float fade = 1.0f;              // dying views: 1 -> 0
	};
	struct ChargeFx;
	struct FireChargeFx;
	struct StatusFx;
	struct AuraFx;
	struct Transient;

	Ctx MakeCtx(const FxFrameIn& in, float dt);
	void SyncViews(Ctx& c);
	void UpdateActors(Ctx& c);
	void HandleEvent(Ctx& c, const ff::Event& e);
	// cue families (FxCues.cpp)
	void CueFx(Ctx& c, const ff::Value& d);
	void CueInteraction(Ctx& c, const ff::Value& d);
	void CueCharge(Ctx& c, const ff::Value& d);
	void CueStatus(Ctx& c, const ff::Value& d, bool fromEvent);
	void CueZone(Ctx& c, const ff::Value& d);
	void CueClash(Ctx& c, const ff::Value& d);
	void CueMisc(Ctx& c, const std::string& type, const ff::Value& d);
	void CueLegacy(Ctx& c, const std::string& type, const ff::Value& d);
	void Aura(Ctx& c, int actor, Fam fam, bool on);
	void LashTransient(Ctx& c, int actor, const Vec3& dir, float range);
	void DrawStream(Ctx& c, int actor, const Vec3& at, int body);
	void DashTrail(Ctx& c, int actor, const Vec3& dir);
	// helpers
	void RingM(Ctx& c, const Vec3& pos, const Vec3& normal, float r0, float r1, float dur, Fam fam, const RingOpts& o = RingOpts());
	void BurstM(Ctx& c, const Vec3& pos, const Vec3& normal, float strength, ffx::Burst style);
	Fam BodyFam(Ctx& c, int bodyId, Fam fallback) const;
	Fam ActorFam(Ctx& c, int actorId) const;
	Vec3 BodyPos(Ctx& c, int bodyId) const;
	// The body's view breaks into physics debris (BodyView::Break); false when it has no view or did not break.
	bool BreakView(Ctx& c, int bodyId);

	FxConfig cfg_;
	DrawList out_;
	KeyAlloc keys_;
	Rng rng_;
	OneShots fx_;
	WorldFx world_;
	double time_ = 0.0;
	int64_t lastTick_ = -1;
	std::map<int, ViewSlot> views_;                 // live views by body id
	std::vector<ViewSlot> dying_;                   // views fading out
	std::unordered_map<int, int> tierHint_;         // body id -> launch tier
	std::map<int, std::unique_ptr<ChargeFx>> charges_;
	std::map<int, std::unique_ptr<FireChargeFx>> fireCharges_;
	std::map<std::string, std::unique_ptr<StatusFx>> status_;   // "actor:status"
	std::map<int, std::unique_ptr<AuraFx>> auras_;
	std::vector<std::unique_ptr<Transient>> transients_;
	std::map<int, std::unique_ptr<Trail>> glides_;   // gliding / flying fighters
	float stingT_ = 0.0f;
	FxStats stats_;
};

}  // namespace ffx
