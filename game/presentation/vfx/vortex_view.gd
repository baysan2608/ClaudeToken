class_name VortexView
extends VfxEffect
## Rotating air columns (Air / Vortex): twister projectile, tornado, eddy, funnel front, vortex wall.
## Two funnel layers (outer: wide and slow; inner: narrow and fast) with panning streak noise, and
## debris (rock chips, one opaque MultiMesh draw) orbiting faster near the core. Infusions tint it
## (sand, fire, water, steam). Vertex-animated: one uniform per layer per frame + up to 10 debris
## transforms.
##
##   configure(kind, infusion, seed)   kind "tornado" | "twister" | "eddy" | "funnel" | "vortex_wall"
##   set_shape(radius, height)         top radius and height in metres
##   set_spin(rad_s)                   from MatBody.spin (sign = direction)

const DEBRIS := 10
const INFUSION := {
	"": {"a": Color(0.74, 0.70, 0.62), "b": Color(0.96, 0.97, 1.0), "op": 0.42, "cover": 0.5, "glow": 1.0},
	"sand": {"a": Color(0.66, 0.52, 0.33), "b": Color(0.90, 0.77, 0.54), "op": 0.7, "cover": 0.78, "glow": 1.0},
	"fire": {"a": Color(0.9, 0.22, 0.02), "b": Color(1.0, 0.62, 0.18), "op": 0.85, "cover": 0.62, "glow": 2.0},
	"water": {"a": Color(0.22, 0.52, 0.66), "b": Color(0.76, 0.92, 0.98), "op": 0.62, "cover": 0.6, "glow": 1.0},
	"steam": {"a": Color(0.84, 0.86, 0.89), "b": Color(1.0, 1.0, 1.0), "op": 0.5, "cover": 0.55, "glow": 1.1},
}

var kind := "tornado"
var infusion := ""
var _outer: MeshInstance3D
var _inner: MeshInstance3D
var _mo: ShaderMaterial
var _mi_mat: ShaderMaterial
var _debris: MultiMeshInstance3D
var _deb_seed: PackedFloat32Array = PackedFloat32Array()
var _radius := 1.6
var _height := 3.0
var _spin := 4.0
var _spin_phase := 0.0
var _rise_phase := 0.0


func _init() -> void:
	_mo = VfxMaterials.make_fx("vortex")
	_mi_mat = VfxMaterials.make_fx("vortex")
	_outer = _layer("Outer", _mo)
	_inner = _layer("Inner", _mi_mat)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = VfxMesh.rock_mesh(5)
	mm.instance_count = DEBRIS
	_debris = MultiMeshInstance3D.new()
	_debris.name = "Debris"
	_debris.multimesh = mm
	var sm := VfxMaterials.make_stone()
	sm.set_shader_parameter("u_seed", 17.5)
	_debris.material_override = sm
	_debris.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_debris.custom_aabb = AABB(Vector3(-5, -0.5, -5), Vector3(10, 10, 10))
	add_child(_debris)
	var rng := RandomNumberGenerator.new()
	rng.seed = 4242
	for i in DEBRIS:
		_deb_seed.append_array(PackedFloat32Array([rng.randf(), rng.randf(), rng.randf(), rng.randf()]))
	configure("tornado", "", 0)
	reset()


func _layer(nm: String, mat: ShaderMaterial) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = nm
	mi.mesh = FxMesh.funnel_mesh()
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)
	return mi


func configure(kind_name: String, infusion_name: String = "", seed_value: int = 0) -> void:
	kind = kind_name
	infusion = infusion_name if INFUSION.has(infusion_name) else ""
	var inf: Dictionary = INFUSION[infusion]
	for k in 2:
		var m: ShaderMaterial = _mo if k == 0 else _mi_mat
		m.set_shader_parameter("tint_a", VfxPalette.v3(inf.a))
		m.set_shader_parameter("tint_b", VfxPalette.v3(inf.b))
		m.set_shader_parameter("opacity", float(inf.op) * (0.8 if k == 0 else 1.0))
		m.set_shader_parameter("cover", float(inf.cover))
		m.set_shader_parameter("glow", float(inf.glow))
		m.set_shader_parameter("seed", float(absi(seed_value) % 19) + float(k) * 3.7)
	_debris.visible = infusion != "fire" and kind != "eddy"
	_apply_shape()


func set_shape(radius: float, height: float) -> void:
	_radius = maxf(radius, 0.2)
	_height = maxf(height, 0.3)
	_apply_shape()


func _apply_shape() -> void:
	var r := _radius
	var h := _height
	var rb := r * 0.22
	var sk := r * 0.5
	var sw := 0.25 * r
	match kind:
		"twister":
			rb = r * 0.3
			sk = r * 0.25
		"eddy":
			rb = r * 0.55
			sk = r * 0.2
			sw = 0.05
		"funnel":
			rb = r * 0.4
			sk = r * 0.3
		"vortex_wall":
			rb = r * 0.92
			sk = r * 0.12
			sw = 0.05
	_mo.set_shader_parameter("r_base", rb)
	_mo.set_shader_parameter("r_top", r)
	_mo.set_shader_parameter("skirt", sk)
	_mo.set_shader_parameter("height", h)
	_mo.set_shader_parameter("sway", sw)
	_mi_mat.set_shader_parameter("r_base", rb * 0.55)
	_mi_mat.set_shader_parameter("r_top", r * 0.62)
	_mi_mat.set_shader_parameter("skirt", sk * 0.4)
	_mi_mat.set_shader_parameter("height", h * 0.92)
	_mi_mat.set_shader_parameter("sway", sw)
	var e := r + sk + 1.0
	var aabb := AABB(Vector3(-e, -0.2, -e), Vector3(2.0 * e, h + 0.6, 2.0 * e))
	_outer.custom_aabb = aabb
	_inner.custom_aabb = aabb
	_debris.custom_aabb = aabb


func set_spin(rad_s: float) -> void:
	_spin = rad_s if absf(rad_s) > 0.5 else 4.0 * signf(rad_s if rad_s != 0.0 else 1.0)


func on_acquire() -> void:
	visible = true
	set_process(true)


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)


func advance(dt: float) -> void:
	var rev := _spin / TAU
	_spin_phase += rev * dt * 0.6
	_rise_phase += dt
	_mo.set_shader_parameter("spin_phase", _spin_phase)
	_mo.set_shader_parameter("rise_phase", _rise_phase)
	_mi_mat.set_shader_parameter("spin_phase", _spin_phase * 1.8)
	_mi_mat.set_shader_parameter("rise_phase", _rise_phase * 1.3)
	if not _debris.visible:
		return
	# debris: inner chips orbit faster (angular speed ~ 1/r), climb and fall back on a slow cycle
	var mm := _debris.multimesh
	for i in DEBRIS:
		var s0 := _deb_seed[i * 4]
		var s1 := _deb_seed[i * 4 + 1]
		var s2 := _deb_seed[i * 4 + 2]
		var s3 := _deb_seed[i * 4 + 3]
		var hf := fposmod(s1 + _rise_phase * (0.12 + 0.1 * s2), 1.0)
		var rr := lerpf(_radius * 0.25, _radius * 0.85, s2) * (0.5 + 0.6 * hf)
		var ang := s0 * TAU + _spin_phase * TAU * (1.4 / (0.4 + s2))
		var p := Vector3(cos(ang) * rr, hf * _height * 0.85 + 0.1, sin(ang) * rr)
		var sz := lerpf(0.04, 0.11, s3) * (1.0 - smoothstep(0.75, 1.0, hf))
		var b := Basis(Vector3(s1, s2, s3).normalized(), ang * 2.0 + s0 * 6.0).scaled(Vector3.ONE * sz)
		mm.set_instance_transform(i, Transform3D(b, p))
