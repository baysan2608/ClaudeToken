class_name TouchLayout
extends RefCounted
## Pure geometry for the touch HUD: where the buttons, element chips, cancel
## zone, pause button and stick ghost sit, and which region a touch-down
## belongs to. No nodes, no drawing: unit-testable.
##
## Everything is authored in millimetres (physical thumb ergonomics) from the
## bottom-right usable corner for a right-handed player, then converted with
## `ppm` (viewport units per mm). Left-handed play mirrors x.

enum Id { ATTACK, GUARD, EVADE, TECH, TARGET, PAUSE, ELEM_0, ELEM_1, ELEM_2, ELEM_3, CANCEL }
const COUNT := 11
const NONE := -1

## [dx, dy, diameter] in mm, measured inward / upward from the usable corner.
const CLUSTER_MM: Array = [
	[17.0, 18.0, 17.0],  # ATTACK (primary, biggest, under the resting thumb)
	[40.0, 10.5, 13.5],  # GUARD  (hold; lower-left of attack)
	[12.0, 40.0, 12.5],  # EVADE  (above attack)
	[36.0, 34.0, 14.0],  # TECH   (upper-left arc)
	[23.0, 50.0, 9.0],   # TARGET cycle (small)
]
const PAUSE_DIAMETER_MM := 9.5
const PAUSE_INSET_MM := 7.0
const CHIP_DIAMETER_MM := 8.5
const CHIP_ARC_RADIUS_MM := 20.0
const CHIP_ARC_START_DEG := 185.0
const CHIP_ARC_STEP_DEG := 29.0
const CANCEL_DISTANCE_MM := 28.0
const CANCEL_RADIUS_MM := 7.0
const STICK_GHOST_MM := Vector2(25.0, 24.0)
const HIT_SLOP := 1.12
const MIN_HIT_MM := 5.0

# --- inputs ---------------------------------------------------------------------
var viewport_size := Vector2(1280, 720)
## Safe-area insets in viewport units: left, top, right, bottom.
var insets := Vector4.ZERO
var ppm: float = 8.0
var control_scale: float = 1.0
var preset: String = "default"
var left_handed: bool = false

# --- outputs --------------------------------------------------------------------
var centers := PackedVector2Array()
var radii := PackedFloat32Array()
var hit_radii := PackedFloat32Array()
var stick_ghost := Vector2.ZERO
var stick_radius: float = 80.0
## Technique aim drag that maps to a unit-length aim vector.
var aim_radius: float = 100.0
var split_x: float = 640.0
var usable := Rect2()
var margin: float = 24.0


func _init() -> void:
	centers.resize(COUNT)
	radii.resize(COUNT)
	hit_radii.resize(COUNT)


func configure(vp_size: Vector2, new_insets: Vector4, new_ppm: float, scale: float, new_preset: String, lefty: bool) -> void:
	viewport_size = vp_size
	insets = new_insets
	ppm = maxf(new_ppm, 0.5)
	control_scale = scale
	preset = new_preset
	left_handed = lefty
	recompute()


func recompute() -> void:
	var vp := viewport_size
	var spread := 1.0
	var size_mul := 1.0
	match preset:
		"compact":
			spread = 0.86
			size_mul = 0.92
		"wide":
			spread = 1.16
	var kp := ppm * control_scale * spread
	var ks := ppm * control_scale * size_mul
	margin = 3.0 * ppm
	# Work in the right-handed frame; mirror at the end.
	var il := insets.z if left_handed else insets.x
	var ir := insets.x if left_handed else insets.z
	var it := insets.y
	var ib := insets.w
	usable = Rect2(il, it, vp.x - il - ir, vp.y - it - ib)
	var anchor := Vector2(usable.end.x - margin, usable.end.y - margin)

	aim_radius = minf(0.15 * vp.y, 16.0 * ppm)
	_place(anchor, kp, ks)
	# Shrink the whole cluster uniformly when it would cross the screen
	# midline or the top edge (small phones, big control_scale / wide preset).
	var avail_x := anchor.x - (vp.x * 0.5 + margin * 0.4)
	var avail_y := anchor.y - (usable.position.y + margin * 0.5)
	var fit := 1.0
	for _pass in 3:
		var min_x := INF
		var min_y := INF
		for i in COUNT:
			if i == Id.PAUSE:
				continue
			min_x = minf(min_x, centers[i].x - radii[i])
			min_y = minf(min_y, centers[i].y - radii[i])
		var need := minf(avail_x / maxf(anchor.x - min_x, 1.0), avail_y / maxf(anchor.y - min_y, 1.0))
		if need >= 0.999:
			break
		fit = maxf(fit * need * 0.995, 0.4)
		_place(anchor, kp * fit, ks * fit)

	# Pause: top corner.
	centers[Id.PAUSE] = Vector2(usable.end.x - PAUSE_INSET_MM * ppm, usable.position.y + PAUSE_INSET_MM * ppm)
	radii[Id.PAUSE] = PAUSE_DIAMETER_MM * 0.5 * ppm * minf(control_scale, 1.15)

	# Stick ghost + sizes.
	stick_ghost = Vector2(usable.position.x + margin + STICK_GHOST_MM.x * ppm * control_scale,
			anchor.y - STICK_GHOST_MM.y * ppm * control_scale)
	stick_radius = clampf(0.11 * vp.y, 9.0 * ppm, 15.0 * ppm) * control_scale
	split_x = vp.x * 0.5

	for i in COUNT:
		hit_radii[i] = maxf(radii[i] * HIT_SLOP, MIN_HIT_MM * ppm)
	hit_radii[Id.CANCEL] = radii[Id.CANCEL]

	if left_handed:
		for i in COUNT:
			centers[i].x = vp.x - centers[i].x
		stick_ghost.x = vp.x - stick_ghost.x
		var ux := vp.x - usable.end.x
		usable = Rect2(ux, usable.position.y, usable.size.x, usable.size.y)


## Place the five cluster buttons, element chips and cancel zone.
func _place(anchor: Vector2, kp: float, ks: float) -> void:
	for i in 5:
		var spec: Array = CLUSTER_MM[i]
		centers[i] = anchor - Vector2(spec[0], spec[1]) * kp
		radii[i] = float(spec[2]) * 0.5 * ks
	# Element chips: arc around the technique button, Earth at the low end.
	var tech: Vector2 = centers[Id.TECH]
	for e in UiStyle.ELEMENT_COUNT:
		var a := deg_to_rad(CHIP_ARC_START_DEG - CHIP_ARC_STEP_DEG * e)
		centers[Id.ELEM_0 + e] = tech + Vector2(cos(a), -sin(a)) * CHIP_ARC_RADIUS_MM * kp
		radii[Id.ELEM_0 + e] = CHIP_DIAMETER_MM * 0.5 * ks
	# Cancel zone: further out along the corner -> technique direction.
	var dir := (tech - anchor).normalized()
	radii[Id.CANCEL] = CANCEL_RADIUS_MM * ks
	# Always clear of the aim ring, even when the cluster is small.
	var min_dist := aim_radius + radii[Id.CANCEL] + 0.8 * radii[Id.TECH] + 3.0 * ppm
	centers[Id.CANCEL] = tech + dir * maxf(CANCEL_DISTANCE_MM * kp, min_dist)


## True when `pos` is on the movement-stick side of the screen.
func is_stick_side(pos: Vector2) -> bool:
	return pos.x > split_x if left_handed else pos.x < split_x


## Which control a touch-down at `pos` lands on (TouchLayout.Id) or NONE.
## The nearest control (distance / hit radius) wins. CANCEL is never returned.
func hit_test(pos: Vector2) -> int:
	var best := NONE
	var best_score := 1.0
	for i in Id.CANCEL:
		var d := pos.distance_to(centers[i]) / hit_radii[i]
		if d < best_score:
			best_score = d
			best = i
	return best


func in_cancel_zone(pos: Vector2) -> bool:
	return pos.distance_to(centers[Id.CANCEL]) <= radii[Id.CANCEL]
