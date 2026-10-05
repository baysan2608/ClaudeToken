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
var buffered_slot := ""       # slot of a buffered attack press (gesture)
var attack_hold := 0.0

# Telemetry for presentation
var last_hit_dir := Vector3.ZERO
var last_result := ""

# --- Moveset engine (docs/COMBAT_SPEC.md "Engine") -----------------------------
## Selected sub-element per element (0 = legacy kit). Switching affects the next action only.
var subs := [0, 0, 0, 0]
## Unlocked sub-elements per element.
var subs_unlocked := [[true, true, true, true], [true, true, true, true], [true, true, true, true], [true, true, true, true]]
## Active statuses: name -> {t: seconds left (< 0 = until removed), mag: float, src: actor id}. See Status.
var status := {}
var metal_carried := 12.0    # kg in the metal satchel (Earth/Metal)
var static_charge := 0.0     # 0..60 stored by the Static Ward (Fire/Lightning)
## Movement stance: "", stone_skin, iron, anchor, roots, lava_wade, overcharge, flight, hover, ...
var stance := ""
var armor := 0.0             # fraction of K damage removed (0..1)
var anchored := false        # immune to knockback / pull / lift
var flying := false          # airborne by a mode (flight / hover): immune to ground lines
## Chain/weave bookkeeping for MOVESET §9.1 (slots used in the current string, weave used).
var chain := {"n": 0, "slots": [], "weaved": false}


func sub() -> int:
	return int(subs[element])


func sub_of(e: int) -> int:
	return int(subs[clampi(e, 0, 3)])


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
