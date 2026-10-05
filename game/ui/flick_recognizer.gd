class_name FlickRecognizer
extends RefCounted
## Pure flick recognition for the ATTACK and GUARD buttons (docs/MOVESET.md section 3, docs/CONTROLS.md).
##
## A flick is a finger travel of at least FLICK_MM from where it landed on the button:
##   ATTACK: inside FLICK_WINDOW_S of the press, or at release after a hold (any later travel).
##   GUARD : any time while held; the origin re-bases after every flick so push and sink can follow
##           each other without lifting.
## The direction is the dominant axis of the travel (45 degree quadrants, screen space, y down):
## UP / DOWN / SIDE (left or right). The guard only knows UP (push) and DOWN (sink).

const FLICK_MM := 6.0
const FLICK_WINDOW_S := 0.25
## Travel past this fraction of the threshold already lights the petal ("this is where it will go").
const PREVIEW_FRACTION := 0.55

var origin := Vector2.ZERO
var fired := false
## Gesture currently pointed at (Sim.Gesture), for the petal highlight. Cleared on a new press.
var hot := 0
## Last gesture this recogniser reported (Sim.Gesture).
var last := 0


func begin(pos: Vector2) -> void:
	origin = pos
	fired = false
	hot = 0
	last = 0


## Sim.Gesture of a screen-space displacement (y down). NONE for a zero vector.
static func classify(d: Vector2) -> int:
	if d.length_squared() < 1e-6:
		return Sim.Gesture.NONE
	if absf(d.y) > absf(d.x):
		return Sim.Gesture.UP if d.y < 0.0 else Sim.Gesture.DOWN
	return Sim.Gesture.SIDE


## Travel threshold in viewport units.
static func threshold_px(ppm: float) -> float:
	return FLICK_MM * ppm


## Drag update. `held_s` is how long the button has been held. Returns the gesture recognised NOW
## (NONE if none). `guard` selects the guard rules (no window, only UP/DOWN, re-basing).
func update(pos: Vector2, held_s: float, ppm: float, guard: bool = false) -> int:
	var d := pos - origin
	var thr := threshold_px(ppm)
	var g := classify(d)
	if guard and g == Sim.Gesture.SIDE:
		g = Sim.Gesture.NONE
	hot = g if d.length() >= thr * PREVIEW_FRACTION else Sim.Gesture.NONE
	if d.length() < thr or g == Sim.Gesture.NONE:
		return Sim.Gesture.NONE
	if guard:
		last = g
		origin = pos
		hot = Sim.Gesture.NONE
		return g
	if fired or held_s > FLICK_WINDOW_S:
		return Sim.Gesture.NONE
	fired = true
	last = g
	return g


## Lift. Returns the gesture recognised at release (ATTACK after a hold), else NONE.
func release(pos: Vector2, ppm: float) -> int:
	if fired:
		return Sim.Gesture.NONE
	var d := pos - origin
	if d.length() < threshold_px(ppm):
		return Sim.Gesture.NONE
	last = classify(d)
	fired = true
	return last
