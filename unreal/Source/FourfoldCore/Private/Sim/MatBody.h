// Fourfold core - port of game/core/mat_body.gd: one logical piece of material. Identity (id) survives every change
// of form and phase; splits create children with explicit mass division and provenance; merges record absorbed ids.
#pragma once

#include "ff/Math.h"
#include "ff/Types.h"
#include "ff/Value.h"
#include "Sim/Sim.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ff {

// Small int set (GDScript `{id: true}` dictionaries used as sets: hit_set).
struct IntSet {
	std::vector<int> v;
	bool has(int x) const { return std::find(v.begin(), v.end(), x) != v.end(); }
	void add(int x) {
		if (!has(x)) v.push_back(x);
	}
	void erase(int x) {
		auto it = std::find(v.begin(), v.end(), x);
		if (it != v.end()) v.erase(it);
	}
	void clear() { v.clear(); }
	size_t size() const { return v.size(); }
	bool empty() const { return v.empty(); }
};

class MatBody : public std::enable_shared_from_this<MatBody> {
public:
	int id = 0;
	bool alive = true;
	std::string origin;               // "ground@x,z", "pool", "wall", "split:<id>", "scenario"
	int parent_id = -1;
	std::vector<int> lineage;         // ancestor ids, oldest first
	std::vector<int> absorbed;        // ids merged into this body
	int64_t born_tick = 0;

	Mat mat = Mat::Stone;
	Form form = Form::Chunk;
	Phase phase = Phase::Solid;

	double mass = 0.0;                // kg
	double temp = Sim::AMBIENT_C;     // degrees C
	double liquid = 0.0;              // 0..1 (stone: melt fraction; water: 1 - ice fraction)

	Vec3 pos;
	Vec3 vel;
	double radius = 0.3;
	double max_speed = 30.0;
	bool on_ground = false;
	bool static_body = false;

	// Control
	int controller = -1;
	double authority = 0.0;
	Vec3 hold_point;
	int residual_owner = -1;
	double residual_authority = 0.0;

	// Attack instance (hit deduplication)
	int attack_id = 0;                // 0 = inert
	int attack_owner = -1;
	IntSet hit_set;
	double damage = 0.0;
	double balance_damage = 0.0;

	// Interaction record
	int last_actor = -1;
	std::string last_verb;
	int64_t last_tick = 0;

	double charge = 0.0;              // electrical

	// Lifetime
	double age = 0.0;
	double max_life = -1.0;           // < 0 = unlimited
	double rest_time = 0.0;

	// WAVE
	Vec3 wave_dir;
	double wave_budget = 0.0;
	double wave_width = 1.4;
	std::vector<Vec3> wave_path;
	bool wave_stalled = false;

	// WALL
	Vec3 wall_half{1.0f, 0.6f, 0.25f};
	double wall_yaw = 0.0;
	double wall_rise = 0.0;
	double wall_damage = 0.0;

	// Moveset engine
	int sub = 0;
	std::string tag;                  // "" = legacy
	int tier = 0;
	int owner = -1;
	double power = 0.0;
	double heat_payload = 0.0;
	double spin = 0.0;
	double zone_radius = 0.0;
	std::vector<int> captured;
	int captured_by = -1;
	double gravity_scale = 1.0;
	double hardness = -1.0;
	Dict props;

	bool is_stone() const { return mat == Mat::Stone; }
	bool is_water() const { return mat == Mat::Water; }
	bool is_metal() const { return mat == Mat::Metal; }
	bool is_zone() const { return form == Form::Zone; }
	bool is_molten() const { return mat == Mat::Stone && phase == Phase::Molten; }
	bool is_hot() const { return mat == Mat::Stone && (temp >= Sim::HOT_ROCK_C || liquid > 0.0); }
	bool is_conductive() const { return mat == Mat::Water && phase == Phase::Liquid; }
	bool is_projectile() const { return attack_id != 0 && controller < 0 && form != Form::Wave; }
	bool is_molten_any() const;
	double thermal_energy() const;
	void update_radius();
	void update_radius_puddle();
	void wall_damage_add(double x) { wall_damage += x; }
	void touch(int actor_id, const std::string& verb, int64_t tick) {
		last_actor = actor_id;
		last_verb = verb;
		last_tick = tick;
	}
	std::string describe() const;
};

using BodyRef = std::shared_ptr<MatBody>;

}  // namespace ff
