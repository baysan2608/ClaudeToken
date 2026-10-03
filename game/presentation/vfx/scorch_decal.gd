class_name ScorchDecal
extends VfxEffect
## A fading scorch (soot + brief ember glow) or wet (dark, glossy) ground mark.
## Implemented as ONE lifted, alpha-blended quad (not a projected decal node and
## unreliable on the Mobile renderer, and a quad is cheaper). Best on flat ground; pass `normal`
## for walls. Pool cap 8.
##
##   place(pos, radius, kind, lifetime, normal)    kind = "scorch" | "wet"; normal defaults to UP
##
## Fades in over 0.08 s, out over the last 35 % of the lifetime; scorch marks glow for ~1 s.

const LIFT: float = 0.015

var _mi: MeshInstance3D
var _mat: ShaderMaterial
var _age: float = 0.0
var _life: float = 5.0
var _wet: bool = false
var _playing: bool = false


func _init() -> void:
	_mat = VfxMaterials.make("ground_mark")
	_mat.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	_mi = MeshInstance3D.new()
	_mi.name = "Mark"
	var q := QuadMesh.new()
	q.size = Vector2(1.0, 1.0)
	q.orientation = PlaneMesh.FACE_Y
	_mi.mesh = q
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	reset()


func place(pos: Vector3, radius: float, kind: String = "scorch", lifetime: float = 5.0, normal: Vector3 = Vector3.UP) -> void:
	_wet = kind == "wet"
	_life = maxf(lifetime, 0.2)
	var n: Vector3 = normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	var q: Quaternion = FireBurstFX._quat_up_to(n)
	var yaw: Quaternion = Quaternion(Vector3.UP, float(absi(hash(Vector3i(pos * 10.0))) % 628) / 100.0)
	_place(Transform3D(Basis(q * yaw), pos + n * LIFT))
	var r: float = maxf(radius, 0.05)
	_mi.scale = Vector3(r * 2.0, 1.0, r * 2.0)
	_mat.set_shader_parameter("kind", 1.0 if _wet else 0.0)
	_mat.set_shader_parameter("seed", float(absi(hash(Vector3i(pos * 7.0))) % 100) * 0.1)
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	_apply()


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
	if _age >= _life:
		_playing = false
		visible = false
		_finish()
		return
	_apply()


func _apply() -> void:
	var fade_in: float = clampf(_age / 0.08, 0.0, 1.0)
	var fade_out: float = 1.0 - smoothstep(_life * 0.65, _life, _age)
	_mat.set_shader_parameter("fade", fade_in * fade_out)
	_mat.set_shader_parameter("ember", 0.0 if _wet else exp(-_age * 2.4))
