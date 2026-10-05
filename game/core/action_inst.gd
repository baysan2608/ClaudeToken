class_name ActionInst
extends RefCounted
## A running action (move instance). Phases: STARTUP -> ACTIVE -> RECOVERY -> done.
## CHARGE and CHANNEL are open-ended holds driven by input.

enum P { STARTUP, CHARGE, CHANNEL, ACTIVE, RECOVERY, DONE }
const P_NAMES := ["startup", "charge", "channel", "active", "recovery", "done"]

var id := ""                 # move id (Moves.DEFS key)
var def: Dictionary = {}
var element := 0             # element the action was started with (switching never changes it)
var phase := P.STARTUP
var t := 0.0                 # time in current phase
var total := 0.0             # time since start
var attack_id := 0           # attack instance for hit deduplication
var heavy := false           # charged variant
var data := {}               # per-action scratch (target ids, mode, aim, tier, slot, spec)
var interrupted := false
var sub := 0                 # sub-element the action was started with
var slot := ""               # input slot (strike thrust ground sweep guard push sink tech evade evade_hold)


## Charge tier reached (0..3), see Charge.
func tier() -> int:
	return int(data.get("tier", 0))


func phase_name() -> String:
	return P_NAMES[phase]
