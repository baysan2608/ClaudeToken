class_name ShardsFX
extends VfxEffect
## Shatter one-shot: up to 14 solid shards (one MultiMesh draw) thrown from a point, tumbling under
## gravity and bouncing once on the ground, shrinking away at the end. Glass / ice shards
## (crystal.gdshader), stone chips (stone.gdshader), metal splinters (metal.gdshader), bark bits.
## CPU-simulated (14 transforms per frame), deterministic per seed. 0.9 s.
##
##   play(pos, dir, strength, mat, seed = 0, ground_y = pos.y - 1)

const MAX_N := 14
const DURATION := 0.9

var mat_name := "stone"
var _mmi: MultiMeshInstance3D
var _mats := {}
var _p := PackedVector3Array()
var _v := PackedVector3Array()
var _axis := PackedVector3Array()
var _spin := PackedFloat32Array()
var _size := PackedFloat32Array()
var _n := 0
var _age := 0.0
var _ground := 0.0
var _playing := false
var _rng := RandomNumberGenerator.new()


func _init() -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.instance_count = MAX_N
	mm.visible_instance_count = 0
	_mmi = MultiMeshInstance3D.new()
	_mmi.name = "Shards"
	_mmi.multimesh = mm
	_mmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mmi)
	_p.resize(MAX_N)   # packed arrays are values: resize each one itself
	_v.resize(MAX_N)
	_axis.resize(MAX_N)
	_spin.resize(MAX_N)
	_size.resize(MAX_N)
	reset()


func _material(m: String) -> ShaderMaterial:
	if _mats.has(m):
		return _mats[m]
	var sm: ShaderMaterial
	match m:
		"glass", "ice":
			sm = VfxMaterials.make_fx("crystal")
			var cs: Dictionary = CrystalView.STYLE[m]
			sm.set_shader_parameter("tint", VfxPalette.v3(cs.tint))
			sm.set_shader_parameter("frost", float(cs.frost))
			sm.set_shader_parameter("opacity", 0.75)
			sm.set_shader_parameter("edge_glow", 1.0)
		"metal":
			sm = VfxMaterials.make_fx("metal")
		"plant":
			sm = VfxMaterials.make_fx("vine")
		_:
			sm = VfxMaterials.make_stone()
			sm.set_shader_parameter("u_seed", 31.5)
	_mats[m] = sm
	return sm


func play(pos: Vector3, dir: Vector3, strength: float, mat: String, seed_value: int = 0, ground_y: float = -INF) -> void:
	mat_name = mat
	var key := mat if mat in ["glass", "ice", "metal", "plant"] else "stone"
	_mmi.material_override = _material(key)
	var mm := _mmi.multimesh
	match key:
		"glass", "ice":
			mm.mesh = FxMesh.crystal_mesh(seed_value % 8, "shard")
		"metal", "plant":
			mm.mesh = FxMesh.spike_mesh()
		_:
			mm.mesh = VfxMesh.rock_mesh(9 + absi(seed_value) % 4)
	strength = clampf(strength, 0.2, 2.0)
	_rng.seed = seed_value * 1103 + 17
	_n = clampi(int(6.0 + 8.0 * strength * 0.6), 5, MAX_N)
	if VfxMaterials.lite():
		_n = mini(_n, 8)
	var d := dir.normalized() if dir.length_squared() > 1e-6 else Vector3.UP
	for i in _n:
		var r := Vector3(_rng.randfn(), absf(_rng.randfn()) * 0.8 + 0.3, _rng.randfn()).normalized()
		_p[i] = Vector3(_rng.randfn(), _rng.randfn(), _rng.randfn()) * 0.06
		_v[i] = (r * 0.65 + d * 0.5).normalized() * _rng.randf_range(2.0, 5.5) * (0.6 + 0.4 * strength)
		_axis[i] = Vector3(_rng.randfn(), _rng.randfn(), _rng.randfn()).normalized()
		_spin[i] = _rng.randf_range(6.0, 18.0)
		_size[i] = _rng.randf_range(0.04, 0.1) * (0.7 + 0.3 * strength) * (1.6 if key == "glass" or key == "ice" else 1.0)
	_ground = ground_y if ground_y > -1e9 else pos.y - 1.0
	_place_at(pos)
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	mm.visible_instance_count = _n
	_mmi.custom_aabb = AABB(Vector3(-4, -3, -4), Vector3(8, 6, 8))
	_apply(0.0)


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	visible = false
	set_process(false)
	_mmi.multimesh.visible_instance_count = 0


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if _age >= DURATION:
		_playing = false
		_finish()
		return
	var floor_local := _ground - position.y
	for i in _n:
		var v: Vector3 = _v[i]
		v.y -= 9.8 * dt
		var p: Vector3 = _p[i] + v * dt
		if p.y < floor_local + _size[i] * 0.5 and v.y < 0.0:
			p.y = floor_local + _size[i] * 0.5
			v = Vector3(v.x * 0.45, -v.y * 0.3, v.z * 0.45)
			_spin[i] *= 0.5
		_p[i] = p
		_v[i] = v
	_apply(dt)


func _apply(_dt: float) -> void:
	var t := _age / DURATION
	var shrink := 1.0 - smoothstep(0.65, 1.0, t)
	var mm := _mmi.multimesh
	for i in _n:
		var s := _size[i] * shrink
		var b := Basis(_axis[i], _spin[i] * _age).scaled(Vector3.ONE * maxf(s, 1e-4))
		mm.set_instance_transform(i, Transform3D(b, _p[i]))
