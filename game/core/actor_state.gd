class_name ActorState
extends RefCounted
## Authoritative state of one fighter. The simulation is the single movement
## authority; views only interpolate pos/facing.

var id := 0
var name := ""
var team := 0
var is_dummy := false        # practice target: never acts

var pos := Vector3.ZERO      # feet
var vel := Vector3.ZERO
var facing := 0.0            # yaw radians; forward = (sin, 0, cos) * -1 convention: see forward()
var grounded := true
var ground_y := 0.0
var in_water := false
var surface := "stone"
var gliding := false

var health := Sim.HEALTH_MAX
var balance := Sim.BALANCE_MAX
var focus := Sim.FOCUS_MAX
var heat_reserve := 0.0
var wetness := 0.0
var water_carried := 6.0     # kg in the waterskin (refills from sources)
var focus_idle := 0.0        # time since the last Focus spend
var balance_idle := 0.0

var element := Sim.Element.EARTH
var elements := [true, true, true, true]
## Unlocked techniques: magma, heat_draw, lightning, redirect_current, updraft, ice_shard
var kit := {}
var max_control_mass := 80.0

var action: ActionInst = null
var stun := 0.0              # hitstun / stagger remaining
var stun_kind := ""          # "light", "heavy", "knockdown", "getup", "guard_break", "bound"
var iframes := 0.0
var guarding := false
var guard_tick := -1000      # tick guard was pressed (perfect window)
var guard_press_tick := -1000
var held_body := -1
var lock_target := -1
var hits_taken := {}         # attack_id -> tick (dedup)
var burn_cd := 0.0           # contact burn cooldown
var wall_body := -1

var buffered := ""           # buffered press: "attack", "evade", "tech", "guard"
var buffered_tick := -1000
var attack_hold := 0.0

# Telemetry for presentation
var last_hit_dir := Vector3.ZERO
var last_result := ""


func forward() -> Vector3:
	return Vector3(sin(facing), 0.0, cos(facing))


func has(tech: String) -> bool:
	return kit.get(tech, false)


func busy() -> bool:
	return action != null or stun > 0.0


func chest() -> Vector3:
	return pos + Vector3(0, 1.25, 0)


func hand_point() -> Vector3:
	return pos + forward() * 0.55 + Vector3(0, 1.2, 0)
