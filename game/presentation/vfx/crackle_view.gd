class_name CrackleView
extends VfxEffect
## Persistent electricity: a charged body's crackle (short arcs over its surface), a ground current
## crawling along its path, a static field / overcharged fighter. Re-strikes one pooled-free
## LightningArcFX (no light: overlays never use the scene's light budget) every 0.08-0.25 s.
##
##   setup(mode, radius, seed)   mode "body" | "ground" | "actor"
##   set_target(center, path)    world centre; ground mode crawls along `path` (tail -> front)
##   set_intensity(i01)          arc rate and brightness (MatBody.charge / power)

var mode := "body"
var _arc: LightningArcFX
var _rng := RandomNumberGenerator.new()
var _t := 0.0
var _center := Vector3.ZERO
var _path := PackedVector3Array()
var _radius := 0.4
var _intensity := 1.0


func _init() -> void:
	_arc = LightningArcFX.new()
	_arc.name = "Arc"
	_arc.manual_time = true
	_arc.use_light = false
	_arc.half_width = 0.045
	add_child(_arc)
	reset()


func setup(mode_name: String, radius: float, seed_value: int) -> void:
	mode = mode_name
	_radius = maxf(radius, 0.05)
	_rng.seed = seed_value * 7 + 1
	_arc.half_width = 0.08 if mode == "ground" else 0.045
	_t = 0.0


func set_target(center: Vector3, path: PackedVector3Array = PackedVector3Array()) -> void:
	_center = center
	_path = path


func set_intensity(i01: float) -> void:
	_intensity = clampf(i01, 0.0, 1.5)
	_arc.intensity = lerpf(0.4, 1.0, minf(_intensity, 1.0))


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_arc.reset()


func advance(dt: float) -> void:
	_arc.advance(dt)
	if _intensity <= 0.02:
		return
	_t -= dt
	if _t > 0.0:
		return
	_t = _rng.randf_range(0.06, 0.18) / maxf(_intensity, 0.3)
	var pts := PackedVector3Array()
	match mode:
		"ground":
			var n := _path.size()
			if n < 2:
				return
			# crawl: a jagged arc from the front back along the path, hugging the ground
			var i := n - 1 - _rng.randi_range(0, mini(2, n - 2))
			var j := maxi(i - _rng.randi_range(2, 4), 0)
			var a := _path[i] + Vector3(0, 0.05, 0)
			var b := _path[j] + Vector3(_rng.randf_range(-0.5, 0.5), 0.05, _rng.randf_range(-0.5, 0.5))
			var m := a.lerp(b, 0.5) + Vector3(_rng.randf_range(-0.3, 0.3), _rng.randf_range(0.1, 0.35), _rng.randf_range(-0.3, 0.3))
			pts = PackedVector3Array([a, m, b])
		_:
			var d0 := Vector3(_rng.randfn(), _rng.randfn() * 0.7, _rng.randfn()).normalized()
			var d1 := (d0 + Vector3(_rng.randfn(), _rng.randfn(), _rng.randfn()) * 0.8).normalized()
			var r := _radius * (1.05 if mode == "body" else 0.55)
			var c := _center + (Vector3(0, 0.9, 0) if mode == "actor" else Vector3.ZERO)
			var hgt := Vector3(0, _rng.randf_range(-0.6, 0.6), 0) if mode == "actor" else Vector3.ZERO
			pts = PackedVector3Array([c + d0 * r + hgt, c + (d0 + d1).normalized() * r * 1.25, c + d1 * r - hgt * 0.5])
	_arc.strike(pts, _rng.randi())
