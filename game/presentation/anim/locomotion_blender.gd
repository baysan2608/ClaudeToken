class_name LocomotionBlender
extends RefCounted
## Synced locomotion blend space for the in-place gait clips (docs/ANIMATION.md):
##   stance (idle/element) <- speed -> walk <- speed -> run, and by direction relative to the
##   facing: forward / strafe_l / strafe_r / walk_back.
## All gaits share one normalised phase (left-foot touchdown near 0 in every clip, offsets below
## line them up exactly), so changing gait or direction never restarts a cycle. The phase advances
## at actual_speed / blended_stride, so the planted foot travels at ground speed (no skating).
## Weights are smoothed, so a sudden change of direction or lock-on cross-fades instead of popping.

## Design ground speed (m/s), stride per cycle (m) and phase offset of the left-foot touchdown.
const GAITS := {
	"walk": {"speed": 1.4, "stride": 1.26, "offset": 0.0},
	"run": {"speed": 5.5, "stride": 3.30, "offset": 0.0},
	"strafe_l": {"speed": 1.1, "stride": 0.99, "offset": 0.0},
	"strafe_r": {"speed": 1.1, "stride": 0.99, "offset": 0.0},
	"walk_back": {"speed": 1.0, "stride": 0.90, "offset": 0.0},
}
const WEIGHT_RATE := 9.0        # 1/s, how fast clip weights follow their targets
const MOVE_START := 0.12        # m/s: below this the stance is shown alone
const MOVE_FULL := 1.05         # m/s: above this the gait is fully in (stride shrinks below it)
const RUN_FROM := 2.2           # m/s walk -> run blend range (sim: walk <= 1.8, run >= 4.0)
const RUN_TO := 3.8
const DIRECTIONAL_MAX := 3.2    # m/s: faster than this only the forward run is used

var phase := 0.0
var stance_t := 0.0
var weights := {}               # clip -> smoothed weight (stance clips included)
var speed := 0.0
var cycle_rate := 0.0


## Target weights for a ground velocity expressed in the fighter's frame
## (x = toward the character's right, y = forward).
static func target_weights(local_vel: Vector2, stance: String) -> Dictionary:
	var spd := local_vel.length()
	var tw := {}
	var moving := smoothstep(MOVE_START, MOVE_FULL, spd)
	tw[stance] = 1.0 - moving
	if moving <= 0.0:
		return tw
	var fwd := 1.0
	var side := 0.0
	var back := 0.0
	var a := absf(atan2(local_vel.x, local_vel.y))   # 0 forward, PI backward
	if a <= PI * 0.5:
		var t := smoothstep(0.2, 0.8, a / (PI * 0.5))
		fwd = 1.0 - t
		side = t
	else:
		var t := smoothstep(0.2, 0.8, (a - PI * 0.5) / (PI * 0.5))
		fwd = 0.0
		side = 1.0 - t
		back = t
	# Running is always forward (the sim faces the run direction); fade the directional set out.
	var dir_w := 1.0 - smoothstep(DIRECTIONAL_MAX - 0.6, DIRECTIONAL_MAX + 0.2, spd)
	fwd = lerpf(1.0, fwd, dir_w)
	side *= dir_w
	back *= dir_w
	var run_w := smoothstep(RUN_FROM, RUN_TO, spd)
	tw["walk"] = moving * fwd * (1.0 - run_w)
	tw["run"] = moving * fwd * run_w
	tw["strafe_l" if local_vel.x < 0.0 else "strafe_r"] = moving * side
	tw["walk_back"] = moving * back
	return tw


func update(dt: float, local_vel: Vector2, stance: String) -> void:
	speed = local_vel.length()
	var tw := target_weights(local_vel, stance)
	var k := 1.0 - exp(-WEIGHT_RATE * dt)
	for c in tw:
		if not weights.has(c):
			weights[c] = 0.0
	var drop: Array = []
	for c in weights:
		var target := float(tw.get(c, 0.0))
		var w := float(weights[c])
		w += (target - w) * k
		if target <= 0.0 and w < 0.003:
			drop.append(c)
		weights[c] = w
	for c in drop:
		weights.erase(c)
	# Phase rate from the weighted stride of the gaits in the mix.
	var gw := 0.0
	var stride := 0.0
	for c in weights:
		if GAITS.has(c):
			gw += float(weights[c])
			stride += float(weights[c]) * float(GAITS[c].stride)
	if gw > 1e-3:
		stride /= gw
		# Blending with the (planted) stance shrinks the footprints by the gait's share of the mix:
		# the cadence follows the effective stride, so slow walks take short quick steps, not long
		# slow-motion ones (and the feet still travel at ground speed).
		var tot := 0.0
		for c in weights:
			tot += float(weights[c])
		var eff := stride * clampf(gw / maxf(tot, 1e-3), 0.25, 1.0)
		cycle_rate = clampf(speed / maxf(eff, 0.1), 0.5, 2.2)
	else:
		cycle_rate = 0.0
	phase = fposmod(phase + cycle_rate * dt, 1.0)
	stance_t += dt


func clip_time(c: String, length: float) -> float:
	if GAITS.has(c):
		return fposmod(phase + float(GAITS[c].offset), 1.0) * length
	return fposmod(stance_t, maxf(length, 0.01))


## [Animation, time, weight] entries for AnimClipSampler.blend, the heaviest max_clips only.
func entries(lib: Dictionary, max_clips: int = 6) -> Array:
	var out: Array = []
	for c in weights:
		var w := float(weights[c])
		if w < 0.003 or not lib.has(c):
			continue
		var anim: Animation = lib[c]
		out.append([anim, clip_time(c, anim.length), w])
	if out.size() > max_clips:
		out.sort_custom(func(x: Array, y: Array) -> bool: return float(x[2]) > float(y[2]))
		out.resize(max_clips)
	return out


func dominant() -> String:
	var best := ""
	var bw := -1.0
	for c in weights:
		if float(weights[c]) > bw:
			bw = float(weights[c])
			best = c
	return best


func gait_amount() -> float:
	var s := 0.0
	for c in weights:
		if GAITS.has(c):
			s += float(weights[c])
	return s
