// Fourfold core - read-only state for presentation (UE fighters, VFX, audio, HUD, camera).
// FROZEN CONTRACT (architect). Owner after creation: stream `core` (additive changes only).
// Persistent visuals are driven ONLY by this state (mat / form / tag / heat / liquid / zone_radius / tier / spin ...),
// one-shot cues only by events (ff/Events.h) - the rule of the Godot build (docs/VFX.md).
// All positions are sim space: metres, +Y up (convert with Fourfold/Public/FourfoldCoords.h).
#pragma once

#include "ff/Config.h"
#include "ff/Math.h"
#include "ff/OpenWorld.h"
#include "ff/Types.h"
#include "ff/Value.h"

#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace ff {

struct StatusView {
	std::string name;     // wet burning chilled frozen rooted slowed muddy slick blinded concealed deafened shocked anchored armored levitating charged + kit statuses
	float t = 0.0f;       // seconds left (< 0 = until removed)
	float mag = 0.0f;
};

// Charge.progress of the running action (HUD tier ring, anim intensity, charge VFX).
struct ChargeView {
	bool active = false;          // action in CHARGE or CHANNEL with max tier > 0
	std::string button;           // "attack" | "guard" | "tech" | "evade" (where the touch ring is drawn)
	int tier = 0;                 // tier reached 0..3
	float frac = 0.0f;            // progress toward the next tier 0..1
	int max_tier = 0;
};

struct ActionView {
	bool active = false;          // false when the actor runs no action
	std::string id;               // move id (Moves.DEFS key), e.g. "earth_attack", "razor_disc", "guard"
	std::string name;             // display name of the move (def.name before " / ")
	int element = 0;              // element / sub-element the action was started with
	int sub = 0;
	Slot slot = Slot::None;
	ActionPhase phase = ActionPhase::Startup;
	float t = 0.0f;               // seconds in the current phase
	float total = 0.0f;           // seconds since the action started
	int tier = 0;                 // charge tier reached (inst.tier())
	bool heavy = false;
	float startup = 0.0f;         // EFFECTIVE phase lengths the sim uses (CombatWorld._startup_of / _active_of / def)
	float active_time = 0.0f;
	float recovery = 0.0f;
	Vec3 aim;                     // inst.data.aim (unit, world XZ) or facing
	Vec3 aim_point;               // inst.data.aim_point
	bool aim_active = false;
	int held_body = -1;           // body held by this action (grip / magma hold / water draw ...), -1 none
	Value data;                   // copy of inst.data (Dict) for anything else (mode, spec, targets ...)
};

struct ActorView {
	int id = -1;
	std::string name;
	int team = 0;
	bool is_dummy = false;
	bool is_player = false;       // the locally controlled fighter
	bool is_rival = false;        // the AI opponent of the scenario
	bool ai_controlled = false;

	Vec3 pos;                     // feet
	Vec3 vel;
	float facing = 0.0f;          // yaw radians; forward = (sin f, 0, cos f)
	bool grounded = true;
	float ground_y = 0.0f;
	bool in_water = false;
	std::string surface;          // "stone" | "water" | "metal" | zone surfaces
	bool gliding = false;
	bool flying = false;

	float health = 100.0f, balance = 100.0f, focus = 100.0f;
	float heat_reserve = 0.0f, wetness = 0.0f;
	float water_carried = 0.0f, metal_carried = 0.0f, static_charge = 0.0f;

	int element = 0;
	int sub = 0;                                  // sub-element of the current element
	std::array<int, 4> subs{{0, 0, 0, 0}};
	std::array<bool, 4> elements{{true, true, true, true}};
	std::array<std::array<bool, 4>, 4> subs_unlocked{};

	ActionView action;
	float stun = 0.0f;
	std::string stun_kind;        // "" | light | heavy | knockdown | getup | guard_break | bound
	float iframes = 0.0f;
	bool guarding = false;
	int held_body = -1;
	int lock_target = -1;
	int wall_body = -1;
	Vec3 last_hit_dir;
	std::string last_result;      // last guard / hit result ("block", "perfect", "hit", ...)
	std::string stance;           // "" | stone_skin | iron | anchor | roots | lava_wade | overcharge | flight | hover ...
	float armor = 0.0f;
	bool anchored = false;
	std::vector<StatusView> statuses;
	ChargeView charge;
};

struct BodyView {
	int id = -1;
	Mat mat = Mat::Stone;
	Form form = Form::Chunk;
	Phase phase = Phase::Solid;
	std::string tag;              // MatBody.tag (FxEvents.BODY_TAGS), "" = legacy
	std::string fx_mat;           // FxEvents.mat_of(b): stone metal sand glass magma water ice mist steam plant flame blue lightning blast wind vortex vacuum sound
	Vec3 pos, vel;
	float radius = 0.3f;
	float mass = 0.0f;
	float temp = 20.0f;           // degrees C
	float liquid = 0.0f;          // melt fraction (stone/metal/sand/glass) or 1 - ice fraction (water)
	float charge = 0.0f;          // electrical (crackle when > 4)
	float power = 0.0f;
	float heat_payload = 0.0f;    // FIRE bodies
	float spin = 0.0f;
	float zone_radius = 0.0f;
	float age = 0.0f;
	float max_life = -1.0f;
	int tier = 0;
	int sub = 0;
	int owner = -1;               // zones: owning actor (-1 neutral)
	int controller = -1;          // actor holding it (-1 none)
	int attack_id = 0;            // 0 = inert
	int attack_owner = -1;
	bool on_ground = false;
	bool static_body = false;
	// form WALL
	Vec3 wall_half{1.0f, 0.6f, 0.25f};
	float wall_yaw = 0.0f;
	float wall_rise = 0.0f;       // 0..1
	float wall_damage = 0.0f;     // >= 1 crumbles
	// form WAVE
	Vec3 wave_dir;
	float wave_width = 1.4f;
	float wave_budget = 0.0f;
	bool wave_stalled = false;
	std::vector<Vec3> wave_path;  // recent trail (bounded)
	std::vector<int> captured;    // bodies held inside this one
	int captured_by = -1;
	Value props;                  // copy of MatBody.props (Dict): blue, mine, infused, face_hu, recall, ...
};

struct ArenaBox {
	Vec3 min, max;
	std::string kind;             // wall | ledge | pillar
	std::string surface;          // stone
	std::string name;
};

struct ArenaView {
	float half_size = 16.0f;
	std::vector<ArenaBox> solids;
	Vec2 pool_min, pool_max;      // x, z
	float pool_floor = -0.3f, pool_level = -0.05f;
	Vec2 metal_min, metal_max;    // x, z
	float metal_top = 0.02f;
	Vec3 player_spawn, opponent_spawn;
	// Open world (additive, 2026-10-09): the terrain under this window (null in the Lab arenas) and the world
	// position of local (0,0,0). TerrainAt = local standing floor from the terrain alone (no solids / metal).
	std::shared_ptr<const WorldDef> world;
	double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;
	int64_t revision = 0;          // bumps whenever the session rebuilds this view (scenario load, open-world re-centre)
	float TerrainAt(float x, float z) const {
		return world ? static_cast<float>(world->FloorAt(x + origin_x, z + origin_z) - origin_y) : 0.0f;
	}
};

struct Snapshot {
	int64_t tick = 0;
	double sim_time = 0.0;        // tick * kSimDt
	int player_id = -1;
	int rival_id = -1;
	std::vector<ActorView> actors;
	std::vector<BodyView> bodies; // alive bodies only, sim order
	double origin_x = 0.0, origin_y = 0.0, origin_z = 0.0;   // open world: world position of sim (0,0,0) (additive)

	const ActorView* FindActor(int id) const {
		for (const ActorView& a : actors)
			if (a.id == id) return &a;
		return nullptr;
	}
	const BodyView* FindBody(int id) const {
		for (const BodyView& b : bodies)
			if (b.id == id) return &b;
		return nullptr;
	}
};

}  // namespace ff
