// Fourfold game logic island - the animation clip library and move -> clip resolution (ARCHITECTURE §8.3).
// Sources, later ones override earlier ones key by key:
//   1. built-in defaults generated from docs/MARTIAL_ARTS.md (FFGAnimDefaults.gen.cpp: 130 clips, 160 move rows);
//   2. Content/Fourfold/Data/clips.json  ("fourfold.clips/1": frames, loop, contact(s), hands, foot_plants, speed ...);
//   3. Content/Fourfold/Data/anim_map.json ("fourfold.anim_map/1": locomotion, reactions, air, guard, modes, moves, fallbacks).
// The Unreal side resolves each clip name to an asset ('<asset_root>/<asset>.<asset>'); a clip without an asset is
// "missing" and every lookup falls back (clip fallbacks, slot fallbacks, element stance, reference pose).
#pragma once

#include "FFGMath.h"
#include "ff/Types.h"

#include <array>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ffg {

struct ClipDef {
	std::string name;
	std::string asset;                 // asset name (A_<name>)
	int frames = 0;                    // at fps
	float duration = 0.0f;             // seconds (from JSON / frames / the asset itself)
	bool loop = false;
	std::vector<int> contacts;         // frames of the power points (first = "contact")
	float speed = 0.0f;                // design ground speed of a gait clip (m/s)
	std::string hand_l, hand_r;        // hand shape keys ("fist" ...)
	std::array<std::vector<std::pair<int, int>>, 2> plants;   // planted [start, end) frame ranges per foot (0 = l, 1 = r)
	bool has_plants = false;
	bool available = false;            // an asset was found (set by the Unreal side)
	int handle = -1;                   // index of the loaded asset on the Unreal side

	float Fps() const { return frames > 0 && duration > 0.0f ? static_cast<float>(frames) / duration : 60.0f; }
	// Power-point time in seconds (40 % of the duration when the clip has no contact).
	float ContactTime() const;
	float ContactTimeN(size_t i) const;
	// Is foot `side` planted at clip time t (-1 when the clip carries no plant data)?
	int PlantedAt(int side, float t) const;
};

// The clips one action uses, after tier / mode resolution.
struct MoveClips {
	std::string startup, hold, release, perfect;
	std::string hand_l, hand_r;
	void OverrideWith(const MoveClips& o);
	bool Empty() const { return startup.empty() && hold.empty() && release.empty(); }
};

struct MoveAnimEntry {
	MoveClips base;
	std::map<int, MoveClips> tiers;              // a tier entry applies from that tier up (cumulative)
	std::map<std::string, MoveClips> modes;      // lower-case action.data.mode
};

class AnimLibrary {
public:
	// ---- data
	float fps = 60.0f;
	std::string asset_root = "/Game/Fourfold/Characters/Fighter/Anims";
	std::map<std::string, ClipDef> clips;
	std::map<std::string, std::string> clip_fallbacks;     // missing clip -> stand-in
	std::map<std::string, MoveAnimEntry> moves;            // sim move id (or "guard@<el>", "evade@<el>")
	std::map<std::string, std::string> hand_poses;         // shape -> clip
	// locomotion
	std::string idle = "idle";
	std::array<std::string, 4> stance{{"e_stance", "w_stance", "f_stance", "a_stance"}};
	std::string walk = "walk", run = "run", strafe_l = "strafe_l", strafe_r = "strafe_r", back = "walk_back";
	// reactions / air / guard / modes / fallbacks
	std::map<std::string, std::string> reactions;
	std::map<std::string, std::string> air;
	std::string guard_default = "e_guard";
	std::array<std::string, 4> guard_by_element{{"e_guard", "w_shield", "guard", "a_guard"}};
	std::map<std::string, std::string> modes;              // actor stance / mode -> loop clip
	std::map<std::string, std::string> slot_fallbacks;     // slot name -> clip
	std::array<std::string, 4> hold_by_element{{"e_seize_loop", "w_hold", "f_charge", "a_guard"}};

	std::vector<std::string> warnings;                     // one line per problem found while loading

	AnimLibrary();   // built-in defaults
	void Reset();
	// Merges a clips.json / anim_map.json text (returns false and records a warning when it does not parse).
	bool LoadClipsJson(const std::string& text);
	bool LoadAnimMapJson(const std::string& text);

	// ---- used by the generated defaults
	void AddDefaultClip(const char* name, int frames, bool loop, std::initializer_list<int> contacts, float speed);
	void AddClipFallback(const char* clip, const char* alt) { clip_fallbacks[clip] = alt; }
	MoveAnimEntry& DefaultMove(const char* id) { return moves[id]; }

	// ---- queries
	const ClipDef* Find(const std::string& name) const;
	// The clip that plays for `name`: itself when available, else its fallback chain; nullptr when none is available.
	const ClipDef* Resolve(const std::string& name) const;
	const ClipDef* HandPose(const std::string& shape) const;
	// Clips of a running action. id = ActionView.id; guard_spec = action.data.spec for a guard; mode = data.mode.
	MoveClips ResolveMove(const std::string& id, const std::string& guard_spec, int element, ff::Slot slot, int tier,
	                      const std::string& mode, bool earth_wall_up) const;
	std::string StanceFor(int element, bool dummy) const;
	std::string Reaction(const std::string& key) const;
};

// Defined in FFGAnimDefaults.gen.cpp.
void AddBuiltinAnimDefaults(AnimLibrary& lib);

}  // namespace ffg
