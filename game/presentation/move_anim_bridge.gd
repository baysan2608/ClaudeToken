class_name MoveAnimBridge
extends RefCounted
## Animates moveset actions that FighterView does not animate itself, through its only public hook,
## FighterView.play_one_shot(clip, dur, contact_in). Created and stepped by FxDirector (every frame).
##
## Clips come from the move def (docs/MOVESET.md §15.2: anim, anim_hold, anim_active, anim_heavy -
## existing clip names; unknown names use FighterView's stand-ins) with slot / element fallbacks, so a
## kit move without clip data still moves. Timing:
##   STARTUP          `anim`, time-scaled so its authored contact (fighter_clips.json, read at runtime)
##                    lands at the end of startup, re-aimed every frame from the clip's real position
##   CHARGE / CHANNEL `anim_hold` looped by refreshing short one-shots
##   ACTIVE/RECOVERY  `anim_heavy` (heavy) / `anim_active`, else `anim` plays on through its contact
## It stops when the action ends (the last short one-shot runs out within REFRESH) or a stun starts
## (FighterView drops one-shots on stun; the bridge never re-issues during a stun).

const CLIPS_JSON := "res://assets/characters/fighter_clips.json"
## Actions FighterView already animates (legacy ids): left alone.
const ANIMATED := ["evade", "air_dash", "guard", "earth_attack", "earth_tech", "water_attack", "water_tech",
	"fire_attack", "lightning", "fire_tech", "pour", "vent", "air_attack", "air_tech"]
## Length of each issued one-shot; re-issued before it runs out while the phase holds.
const REFRESH := 0.12
const MIN_SPEED := 0.5
const MAX_SPEED := 2.5
## Fallback clips by slot / by element (all existing names or FighterView stand-ins).
const SLOT_CLIP := {"strike": "mv_palm_thrust", "thrust": "mv_palm_thrust", "ground": "mv_ground_slap",
	"sweep": "mv_roundhouse", "push": "mv_push_two_hand", "sink": "mv_stomp", "tech": "mv_wide_draw",
	"evade": "evade_fwd", "evade_hold": "evade_fwd", "guard": "mv_rising_guard"}
const HOLD_BY_ELEMENT := ["earth_hold", "water_hold", "fire_charge", "air_gust"]

static var _clips: Dictionary = {}

## actor id -> {key, clip, speed, left, phase}
var _st := {}
## Diagnostics (tests): one-shots issued.
var issued := 0


static func clip_data() -> Dictionary:
	if _clips.is_empty() and FileAccess.file_exists(CLIPS_JSON):
		var d: Variant = JSON.parse_string(FileAccess.get_file_as_string(CLIPS_JSON))
		if d is Dictionary:
			_clips = d
	return _clips


## Authored contact time (s) of a clip, else 40 % of its duration (FighterView's rule).
static func contact_of(clip: String) -> float:
	var c: Dictionary = clip_data().get(clip, {})
	if c.get("contact") != null:
		return float(c.contact)
	return float(c.get("duration", 0.5)) * 0.4


static func handles(inst: ActionInst) -> bool:
	return inst != null and not ANIMATED.has(inst.id)


## The clip names this bridge would use for an action in each phase family: [startup, hold, active].
static func clips_for(inst: ActionInst) -> Array:
	var d: Dictionary = inst.def
	var slot := inst.slot if inst.slot != "" else String(d.get("slot", "strike"))
	var base := String(d.get("anim", ""))
	if base == "":
		base = String(SLOT_CLIP.get(slot, "mv_palm_thrust"))
	var hold := String(d.get("anim_hold", d.get("anim_charge", "")))
	if hold == "":
		hold = HOLD_BY_ELEMENT[clampi(int(inst.element), 0, 3)]
	var act := ""
	if inst.heavy:
		act = String(d.get("anim_heavy", ""))
	if act == "":
		act = String(d.get("anim_active", ""))
	if act == "":
		act = base
	return [base, hold, act]


func clear() -> void:
	_st.clear()


func forget(actor_id: int) -> void:
	_st.erase(actor_id)


## Per rendered frame.
func update(world: CombatWorld, fighters: Dictionary, dt: float) -> void:
	if world == null:
		return
	for a in world.actors:
		var fv: FighterView = fighters.get(a.id, null)
		if fv == null:
			continue
		var inst := a.action
		if a.stun > 0.0 or not handles(inst):
			_st.erase(a.id)
			continue
		_drive(a, inst, fv, dt)


func _drive(a: ActorState, inst: ActionInst, fv: FighterView, dt: float) -> void:
	var P := ActionInst.P
	var ph := inst.phase
	var names := clips_for(inst)
	var fam := 0
	if ph == P.CHARGE or ph == P.CHANNEL:
		fam = 1
	elif ph == P.ACTIVE or ph == P.RECOVERY:
		fam = 2
	var clip := fv.resolve_clip(String(names[fam]))
	if clip == "":
		return
	var key := "%s:%d:%d:%s" % [inst.id, inst.attack_id, fam, clip]
	var s: Dictionary = _st.get(a.id, {})
	var same: bool = s.get("key", "") == key
	if not same and fam == 2 and s.get("clip", "") == clip:
		# the startup clip simply plays on through contact into the follow-through
		same = true
		s.key = key
	var speed := float(s.get("speed", 1.0)) if same else 1.0
	match fam:
		0:
			var su := float(inst.data.get("startup", inst.def.get("startup", 0.0)))
			var remaining := su - inst.total
			var contact := contact_of(clip)
			if not same:
				speed = clampf(contact / maxf(remaining, 0.03), MIN_SPEED, MAX_SPEED)
			elif fv.ap != null and String(fv.ap.current_animation) == clip:
				var left := contact - fv.ap.current_animation_position
				if left > 0.0 and remaining >= Sim.DT:
					speed = clampf(left / remaining, MIN_SPEED, MAX_SPEED)
		1:
			speed = 1.0
		2:
			if not same:
				# a released charge / a different active clip: race to its contact
				speed = MAX_SPEED
	var left_t := float(s.get("left", 0.0)) - dt if same else 0.0
	var resend: bool = not same or left_t < REFRESH * 0.5 or not is_equal_approx(speed, float(s.get("speed", speed)))
	if resend:
		# contact_in = contact / speed reproduces `speed` inside play_one_shot
		fv.play_one_shot(clip, REFRESH, contact_of(clip) / speed if fam != 1 else -1.0)
		issued += 1
		left_t = REFRESH
	_st[a.id] = {"key": key, "clip": clip, "speed": speed, "left": left_t, "fam": fam}


## Bridge state for an actor (tests / debug overlay).
func state_of(actor_id: int) -> Dictionary:
	return _st.get(actor_id, {})
