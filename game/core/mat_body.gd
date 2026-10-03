class_name MatBody
extends RefCounted
## One logical piece of material. Identity (id) survives every change of form
## and phase; views are replaced, the body is not. Splits create children with
## explicit mass division and provenance; merges record absorbed ids.

var id := 0
var alive := true
## Provenance: how it entered the world ("ground@x,z", "pool", "wall", "split:<id>", "scenario")
var origin := ""
var parent_id := -1
var lineage: Array[int] = []       # ancestor ids, oldest first
var absorbed: Array[int] = []      # ids merged into this body
var born_tick := 0

var mat := Sim.Mat.STONE
var form := Sim.Form.CHUNK
var phase := Sim.Phase.SOLID

var mass := 0.0                    # kg
var temp := Sim.AMBIENT_C          # °C
## Liquid fraction 0..1 (stone: melt fraction; water: 1 - ice fraction).
var liquid := 0.0

var pos := Vector3.ZERO
var vel := Vector3.ZERO
var radius := 0.3
var max_speed := 30.0
var on_ground := false
var static_body := false           # pools: never move

# --- Control -----------------------------------------------------------------
var controller := -1               # actor id holding this body, or -1
var authority := 0.0               # 0..1 strength of the current hold
var hold_point := Vector3.ZERO     # target position while held (world)
## After release the thrower keeps decaying authority, so instant re-catch is hard.
var residual_owner := -1
var residual_authority := 0.0

# --- Attack instance (hit deduplication) -------------------------------------
var attack_id := 0                 # 0 = inert (cannot damage)
var attack_owner := -1
var hit_set := {}                  # actor_id -> true for the current attack_id
var damage := 0.0
var balance_damage := 0.0

# --- Interaction record ------------------------------------------------------
var last_actor := -1
var last_verb := ""
var last_tick := 0

# --- Electrical / surface -----------------------------------------------------
var charge := 0.0

# --- Lifetime ----------------------------------------------------------------
var age := 0.0
var max_life := -1.0               # < 0 = unlimited
var rest_time := 0.0               # seconds spent resting on ground

# --- Wave state (form WAVE) ---------------------------------------------------
var wave_dir := Vector3.ZERO
var wave_budget := 0.0             # metres of travel left
var wave_width := 1.4
var wave_path := PackedVector3Array()  # recent trail for the view (bounded)
var wave_stalled := false

# --- Wall state (form WALL) ---------------------------------------------------
var wall_half := Vector3(1.0, 0.6, 0.25)
var wall_yaw := 0.0
var wall_rise := 0.0               # 0..1
var wall_damage := 0.0             # >= 1 crumbles


func is_stone() -> bool:
	return mat == Sim.Mat.STONE


func is_water() -> bool:
	return mat == Sim.Mat.WATER


func is_molten() -> bool:
	return mat == Sim.Mat.STONE and phase == Sim.Phase.MOLTEN


func is_hot() -> bool:
	return mat == Sim.Mat.STONE and (temp >= Sim.HOT_ROCK_C or liquid > 0.0)


func is_conductive() -> bool:
	# Game rule: liquid water conducts. Stone, lava and ice do not.
	return mat == Sim.Mat.WATER and phase == Sim.Phase.LIQUID


func is_projectile() -> bool:
	return attack_id != 0 and controller < 0 and form != Sim.Form.WAVE


func thermal_energy() -> float:
	## Heat above ambient in HU, including latent heat. Used by the energy ledger.
	if mat == Sim.Mat.STONE:
		return mass * Sim.STONE_C * (temp - Sim.AMBIENT_C) + mass * Sim.STONE_LATENT * liquid
	if mat == Sim.Mat.WATER:
		return mass * Sim.WATER_C * (temp - Sim.AMBIENT_C) - mass * Sim.WATER_LATENT_FUSION * (1.0 - liquid)
	return 0.0


func update_radius() -> void:
	match mat:
		Sim.Mat.STONE:
			radius = Sim.stone_radius(mass)
		Sim.Mat.WATER:
			radius = Sim.water_radius(mass)
		_:
			radius = 0.5


func update_radius_puddle() -> void:
	radius = clampf(sqrt(mass / (PI * 10.0)), 0.3, 2.2)


func wall_damage_add(x: float) -> void:
	wall_damage += x


func touch(actor_id: int, verb: String, tick: int) -> void:
	last_actor = actor_id
	last_verb = verb
	last_tick = tick


func describe() -> String:
	return "#%d %s %s/%s %.1fkg %.0f°C liq=%.2f ctl=%d" % [
		id, ["stone", "water", "steam"][mat], Sim.FORM_NAMES[form], Sim.PHASE_NAMES[phase],
		mass, temp, liquid, controller]
