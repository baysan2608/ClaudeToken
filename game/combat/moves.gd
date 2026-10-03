class_name Moves
extends RefCounted
## Data-driven move definitions shared by player and AI. Times in seconds.
## startup = anticipation (input is acknowledged on the first frame; the strike
## lands at the end of startup), active = hit window, recovery = committed.
## cancel: phase fraction of RECOVERY after which guard/evade may cancel.
## anim / anim_active: clip names (game/assets/characters/fighter_clips.json);
## the view time-scales the clip so its "contact" lands at the end of startup.

const HOLD_THRESHOLD := 0.18     # attack held this long becomes a charge
const BUFFER_TIME := 0.15        # press buffer
const PERFECT_WINDOW := 0.18     # guard press this close before contact = perfect
const GUARD_MASH_LOCK := 0.35    # a new perfect window needs this gap since the last press

const DEFS := {
	# ---------------------------------------------------------------- common
	"evade": {"module": "common", "startup": 0.0, "active": 0.30, "recovery": 0.12,
		"distance": 2.8, "iframes": 0.14, "cost": 4.0, "anim": "evade"},
	"air_dash": {"module": "common", "startup": 0.0, "active": 0.26, "recovery": 0.10,
		"distance": 4.6, "iframes": 0.18, "cost": 9.0, "anim": "air_dash"},
	"guard": {"module": "common", "startup": 0.0, "active": 0.0, "recovery": 0.10, "anim": "guard"},
	"vent": {"module": "fire", "startup": 0.12, "active": 0.25, "recovery": 0.2, "anim": "fire_release"},
	# ---------------------------------------------------------------- earth
	"earth_attack": {"module": "earth", "element": 0, "startup": 0.24, "active": 0.06, "recovery": 0.30,
		"heavy_min": 0.55, "cancel": 0.6, "cost": 7.0, "heavy_cost": 14.0,
		"mass": 20.0, "heavy_mass": 45.0, "speed": 17.0, "heavy_speed": 14.0,
		"damage": 12.0, "balance": 24.0, "heavy_damage": 20.0, "heavy_balance": 50.0,
		"anim": "earth_lift", "anim_active": "earth_throw", "anim_heavy": "earth_heavy"},
	"earth_tech": {"module": "earth", "element": 0, "startup": 0.12, "active": 0.06, "recovery": 0.28,
		"cancel": 0.5, "reach": 7.5, "rip_time": 0.28, "cost": 6.0, "speed": 18.0,
		"damage": 13.0, "balance": 26.0, "anim": "earth_hold", "anim_active": "earth_throw"},
	# ---------------------------------------------------------------- water
	"water_attack": {"module": "water", "element": 1, "startup": 0.16, "active": 0.12, "recovery": 0.28,
		"heavy_min": 0.45, "cancel": 0.6, "cost": 5.0, "heavy_cost": 10.0,
		"range": 4.6, "arc": 70.0, "damage": 8.0, "balance": 16.0, "knock": 2.5,
		"shard_mass": 4.0, "shard_speed": 24.0, "heavy_damage": 15.0, "heavy_balance": 26.0,
		"anim": "water_whip", "anim_heavy": "water_freeze"},
	"water_tech": {"module": "water", "element": 1, "startup": 0.15, "active": 0.08, "recovery": 0.3,
		"cancel": 0.5, "reach": 7.5, "draw_rate": 14.0, "max_draw": 12.0, "speed": 16.0,
		"damage": 10.0, "balance": 32.0, "cost": 6.0, "anim": "water_draw", "anim_hold": "water_hold", "anim_active": "water_whip"},
	# ---------------------------------------------------------------- fire
	"fire_attack": {"module": "fire", "element": 2, "startup": 0.10, "active": 0.08, "recovery": 0.22,
		"heavy_min": 0.40, "cancel": 0.55, "cost_hu": 60.0, "heavy_cost_hu": 160.0,
		"range": 4.6, "cone": 18.0, "heavy_range": 6.5, "heavy_cone": 26.0,
		"damage": 7.0, "balance": 10.0, "heavy_damage": 15.0, "heavy_balance": 30.0,
		"heat": 60.0, "heavy_heat": 160.0, "lightning_min": 0.65,
		"anim": "fire_jab", "anim_charge": "fire_charge", "anim_active": "fire_release"},
	"lightning": {"module": "fire", "element": 2, "startup": 0.0, "active": 0.12, "recovery": 0.40,
		"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4,
		"cost": 22.0, "anim": "lightning_release"},
	"fire_tech": {"module": "fire", "element": 2, "startup": 0.12, "draw_startup": 0.35, "active": 0.08, "recovery": 0.26,
		"cancel": 0.5, "reach": 5.0, "draw_range": 9.0, "heat_rate": 650.0, "draw_rate": 260.0,
		"grip": 0.9, "speed": 15.0, "damage": 13.0, "balance": 26.0,
		"anim": "magma_hold", "anim_draw": "heat_draw", "anim_active": "pour"},
	"pour": {"module": "fire", "element": 2, "startup": 0.25, "active": 0.05, "recovery": 0.35,
		"wave_speed": 7.5, "base_budget": 6.0, "budget_per_kg": 0.3, "damage": 18.0, "balance": 55.0,
		"anim": "pour"},
	# ---------------------------------------------------------------- air
	"air_attack": {"module": "air", "element": 3, "startup": 0.12, "active": 0.10, "recovery": 0.24,
		"heavy_min": 0.40, "cancel": 0.6, "cost": 5.0, "heavy_cost": 12.0,
		"range": 5.5, "cone": 35.0, "heavy_range": 7.0, "heavy_cone": 45.0,
		"knock": 7.0, "heavy_knock": 11.0, "damage": 3.0, "balance": 20.0,
		"heavy_damage": 6.0, "heavy_balance": 36.0, "anim": "air_push", "anim_heavy": "air_gust"},
	"air_tech": {"module": "air", "element": 3, "startup": 0.10, "active": 0.0, "recovery": 0.15,
		"lift_speed": 8.6, "glide_fall": 1.6, "glide_speed": 6.0, "cost": 15.0, "glide_cost": 6.0,
		"anim": "jump", "anim_hold": "glide"},
}

## Techniques that can be unlocked (mastery) and what they do.
const TECHNIQUES := {
	"magma": "Fire technique on a stone: seize it and melt it; release to pour a lava wave.",
	"heat_draw": "Fire technique on lava or hot rock: extract heat into your reserve.",
	"lightning": "Fire: hold attack to charge a lightning strike that follows conductors.",
	"redirect_current": "Fire guard timed against lightning sends it back.",
	"glide": "Air technique: keep holding after an updraft to glide.",
}


static func get_def(id: String) -> Dictionary:
	return DEFS[id]
