class_name ChargeFX
extends VfxEffect
## Charge telegraph per fighter (docs/MOVESET.md §11.2), driven every frame while a move charges or
## channels: T1 a small element ring + motes at the hands; T2 the ring grows and pulses, a ground
## ripple ring under the feet and a rim of light; T3 a full ring, a fresnel aura shell, one light
## pulse and a 2-frame white "ready" glint. Colour from VfxPalette (element / sub).
##
##   set_charge(tier, frac, mat)   tier 0..3, frac 0..1 toward the next tier
##   set_anchor(hands, feet)       world positions (every frame)
## 3 premultiplied quads/shell, never all overlapping (<= 2 layers on screen), 1 light at T3 only.

const GLINT_FRAMES := 2

var _ring: MeshInstance3D
var _ring_mat: ShaderMaterial
var _ripple: MeshInstance3D
var _ripple_mat: ShaderMaterial
var _aura: MeshInstance3D
var _aura_mat: ShaderMaterial
var _light: OmniLight3D
var _tier := -1
var _frac := 0.0
var _mat_name := "flame"
var _t := 0.0
var _glint := 0
var _pulse := 0.0
var _hands := Vector3.ZERO
var _feet := Vector3.ZERO


func _init() -> void:
	top_level = true
	_ring_mat = VfxMaterials.make_fx("ring")
	_ring_mat.set_shader_parameter("billboard", 1.0)
	_ring_mat.set_shader_parameter("cover", 0.15)
	_ring = _quad("HandRing", FxMesh.face_quad(), _ring_mat)
	_ripple_mat = VfxMaterials.make_fx("ring")
	_ripple_mat.set_shader_parameter("cover", 0.6)
	_ripple = _quad("GroundRipple", FxMesh.ground_quad(), _ripple_mat)
	_aura_mat = VfxMaterials.make_fx("shell")
	_aura_mat.set_shader_parameter("rim_power", 2.2)
	_aura_mat.set_shader_parameter("streak", -0.4)
	_aura_mat.set_shader_parameter("pulse", 0.25)
	_aura = _quad("Aura", FxMesh.sphere_mesh(2), _aura_mat)
	_light = OmniLight3D.new()
	_light.name = "Pulse"
	_light.shadow_enabled = false
	_light.omni_range = 4.0
	_light.visible = false
	add_child(_light)
	reset()


func _quad(nm: String, mesh: Mesh, mat: ShaderMaterial) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.name = nm
	mi.mesh = mesh
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mi.extra_cull_margin = 1.0
	add_child(mi)
	return mi


func set_anchor(hands: Vector3, feet: Vector3) -> void:
	_hands = hands
	_feet = feet


func set_charge(tier: int, frac: float, mat: String) -> void:
	_frac = clampf(frac, 0.0, 1.0)
	if mat != _mat_name:
		_mat_name = mat
		_tier = -1
	if tier != _tier:
		if tier > _tier and tier >= 3:
			_glint = GLINT_FRAMES
			_pulse = 1.0
		_tier = tier
		var c := VfxPalette.v3(VfxPalette.color(mat))
		_ring_mat.set_shader_parameter("color", c)
		_ripple_mat.set_shader_parameter("color", c)
		_aura_mat.set_shader_parameter("color", c)
		_light.light_color = VfxPalette.color(mat)
	visible = tier >= 1
	set_process(visible)


func on_acquire() -> void:
	visible = false


func is_playing() -> bool:
	return visible


func reset() -> void:
	visible = false
	set_process(false)
	_tier = -1
	_glint = 0
	_pulse = 0.0
	_light.visible = false


func advance(dt: float) -> void:
	_t += dt
	var tier := maxi(_tier, 0)
	var grow := float(tier) + _frac * 0.6
	# hand ring: T1 small, T2 larger + pulse, T3 full
	var rr := 0.12 + 0.07 * grow
	var pulse := 1.0 + (0.12 * sin(_t * 14.0) if tier >= 2 else 0.0)
	_ring.position = _hands
	_ring.scale = Vector3.ONE * rr * pulse / 0.8
	_ring_mat.set_shader_parameter("alpha", 0.45 + 0.1 * float(tier))
	_ring_mat.set_shader_parameter("width", 0.08)
	_ring_mat.set_shader_parameter("glow", 1.0 + 0.3 * float(tier) + (5.0 if _glint > 0 else 0.0))
	_ring_mat.set_shader_parameter("phase", _t)
	# ground ripple (T2+): an expanding ring repeating under the feet
	_ripple.visible = tier >= 2
	if _ripple.visible:
		var rp := fposmod(_t * 0.9, 1.0)
		var r2 := 0.3 + 0.9 * rp
		_ripple.position = _feet + Vector3(0, 0.03, 0)
		_ripple.scale = Vector3(r2 / 0.8, 1.0, r2 / 0.8)
		_ripple_mat.set_shader_parameter("alpha", (1.0 - rp) * 0.45)
		_ripple_mat.set_shader_parameter("width", 0.035)
		_ripple_mat.set_shader_parameter("phase", _t)
	# T3: aura shell + one light pulse + the 2-frame ready glint
	_aura.visible = tier >= 3
	if _aura.visible:
		_aura.position = _feet + Vector3(0, 0.95, 0)
		_aura.scale = Vector3(0.62, 1.05, 0.62)
		_aura_mat.set_shader_parameter("phase", _t)
		_aura_mat.set_shader_parameter("opacity", 0.28 + 0.5 * _pulse)
	_pulse = maxf(0.0, _pulse - dt * 2.5)
	_light.visible = _pulse > 0.02
	_light.position = _hands
	_light.light_energy = 1.6 * _pulse
	if _glint > 0:
		_glint -= 1
