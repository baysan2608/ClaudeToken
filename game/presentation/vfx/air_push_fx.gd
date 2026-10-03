class_name AirPushFX
extends VfxEffect
## Air push pressure cone: a travelling pressure band on an open cone that refracts the screen with a
## small noise offset (the single optional cheap distortion; ~1.2 % screen UV max, low opacity so
## enemies stay visible) plus 14 dust streaks racing along the axis. 2 draw calls, ~400 + 56 tris.
##
##   play(origin, dir, radius, length)     radius = radius of the cone at its far end, metres
##
## `VfxMaterials.screen_refraction = false` (set before creating the effect) swaps in the "lite" shader:
## no screen read at all, just a faint tinted band + streaks.

const DURATION: float = 0.55

var _body: Node3D
var _cone: MeshInstance3D
var _streaks: MeshInstance3D
var _mat_cone: ShaderMaterial
var _mat_streak: ShaderMaterial
var _age: float = 0.0
var _playing: bool = false


func _init() -> void:
	_body = Node3D.new()
	_body.name = "Body"
	add_child(_body)
	_mat_cone = VfxMaterials.make_air_push()
	_mat_streak = VfxMaterials.make("air_streak")
	_cone = MeshInstance3D.new()
	_cone.name = "Cone"
	_cone.mesh = VfxMesh.cone_mesh()
	_cone.material_override = _mat_cone
	_cone.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_body.add_child(_cone)
	_streaks = MeshInstance3D.new()
	_streaks.name = "Streaks"
	_streaks.mesh = VfxMesh.streak_mesh()
	_streaks.material_override = _mat_streak
	_streaks.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_body.add_child(_streaks)
	reset()


func play(origin: Vector3, dir: Vector3, radius: float, length: float) -> void:
	var d: Vector3 = dir.normalized() if dir.length_squared() > 1e-8 else Vector3.FORWARD
	var q: Quaternion = FireBurstFX._quat_up_to(d)
	_place(Transform3D(Basis(q), origin))
	_body.scale = Vector3(maxf(radius, 0.1), maxf(length, 0.3), maxf(radius, 0.1))
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	_apply(0.0)


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	_age = 0.0
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if _age >= DURATION:
		_playing = false
		visible = false
		_finish()
		return
	_apply(_age)


func _apply(t: float) -> void:
	var n: float = clampf(t / DURATION, 0.0, 1.0)
	_mat_cone.set_shader_parameter("age", n)
	_mat_streak.set_shader_parameter("age", n)
