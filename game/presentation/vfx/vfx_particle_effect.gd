class_name VfxParticleEffect
extends VfxEffect
## Shared base for the GPU-particle one-shots (dust, steam, splash, embers).
## Subclasses build `_particles` (and optional extra systems) in _build() and implement _start().
## Time: effects age with advance(dt); in manual_time mode the GPU simulation is stepped with
## GPUParticles3D.request_particles_process so tests can scrub deterministically.

var _systems: Array[GPUParticles3D] = []
var _age: float = 0.0
var _duration: float = 1.0
var _playing: bool = false


func _init() -> void:
	_build()
	reset()


func _build() -> void:
	pass


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	_age = 0.0
	for ps in _systems:
		ps.emitting = false
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if manual_time:
		for ps in _systems:
			ps.request_particles_process(dt)
	if _age >= _duration:
		_playing = false
		for ps in _systems:
			ps.emitting = false
		_finish()


## Restart every system at the current transform and start the clock.
func _begin(duration: float) -> void:
	_duration = duration
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	for ps in _systems:
		ps.restart()
		ps.emitting = true
		if manual_time:
			ps.speed_scale = 0.0001


func _add_system(ps: GPUParticles3D) -> void:
	ps.one_shot = true
	ps.emitting = false
	ps.local_coords = false
	ps.fixed_fps = 0
	ps.interpolate = true
	ps.draw_order = GPUParticles3D.DRAW_ORDER_VIEW_DEPTH
	ps.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ps)
	_systems.append(ps)


static func make_quad() -> QuadMesh:
	var q := QuadMesh.new()
	q.size = Vector2(1.0, 1.0)
	return q


## 1D curve texture helper: points are Vector2(x, y) pairs.
static func curve_tex(points: Array[Vector2]) -> CurveTexture:
	var c := Curve.new()
	for p in points:
		c.add_point(p)
	var t := CurveTexture.new()
	t.curve = c
	t.width = 64
	return t


## Gradient colour ramp texture helper: offsets and colours.
static func ramp_tex(offsets: PackedFloat32Array, colors: PackedColorArray) -> GradientTexture1D:
	var g := Gradient.new()
	g.offsets = offsets
	g.colors = colors
	var t := GradientTexture1D.new()
	t.gradient = g
	t.width = 64
	return t
