class_name RingFX
extends VfxEffect
## Expanding (or contracting) ring one-shot: shock rings, sound rings / echoes, deflect rings, charge
## tier rings, ground ripples, absorb swirls, implosions. Up to two staggered rings (2 draws, one
## premultiplied layer each), ring.gdshader on a flat quad, optionally camera-facing.
##
##   play(pos, normal, r0, r1, dur, color, opts = {})
##     normal: ring plane normal (UP = on the ground); opts: count (1..2), width (fraction of the
##     radius), cover (0 glow .. 1 tint), glow (HDR), billboard (bool), style (0 band, 1 spiral,
##     2 soft disc), ease ("out" | "in"), hold (stay at full size, e.g. charge rings)

var _q: Array[MeshInstance3D] = []
var _m: Array[ShaderMaterial] = []
var _age := 0.0
var _dur := 0.4
var _r0 := 0.2
var _r1 := 1.0
var _count := 1
var _playing := false
var _ease_in := false
var _hold := false
var _width := 0.08
var _alpha := 1.0


func _init() -> void:
	for i in 2:
		var m := VfxMaterials.make_fx("ring")
		var q := MeshInstance3D.new()
		q.name = "Ring%d" % i
		q.mesh = FxMesh.ground_quad()
		q.material_override = m
		q.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		q.extra_cull_margin = 2.0
		add_child(q)
		_q.append(q)
		_m.append(m)
	reset()


func play(pos: Vector3, normal: Vector3, r0: float, r1: float, dur: float, col: Color, opts: Dictionary = {}) -> void:
	_r0 = maxf(r0, 0.01)
	_r1 = maxf(r1, 0.01)
	_dur = maxf(dur, 0.05)
	_count = clampi(int(opts.get("count", 1)), 1, 2)
	_ease_in = String(opts.get("ease", "out")) == "in"
	_hold = bool(opts.get("hold", false))
	_width = float(opts.get("width", 0.08))
	_alpha = float(opts.get("alpha", 1.0))
	var bb := bool(opts.get("billboard", false))
	var n := normal.normalized() if normal.length_squared() > 1e-6 else Vector3.UP
	var b := Basis()   # billboard: ring.gdshader faces the camera and only reads the scale
	if not bb:
		var x := n.cross(Vector3.FORWARD if absf(n.dot(Vector3.FORWARD)) < 0.9 else Vector3.RIGHT).normalized()
		b = Basis(x, n, x.cross(n)).orthonormalized()
	_place(Transform3D(b, pos))
	for i in 2:
		var m := _m[i]
		m.set_shader_parameter("color", VfxPalette.v3(col))
		m.set_shader_parameter("cover", float(opts.get("cover", 0.3)))
		m.set_shader_parameter("glow", float(opts.get("glow", 1.5)))
		m.set_shader_parameter("style", float(opts.get("style", 0.0)))
		m.set_shader_parameter("billboard", 1.0 if bb else 0.0)
		m.set_shader_parameter("seed", float(i) * 1.7 + pos.x * 0.13)
		_q[i].mesh = FxMesh.face_quad() if bb else FxMesh.ground_quad()
		_q[i].visible = i < _count
	_age = 0.0
	_playing = true
	visible = true
	set_process(true)
	_apply()


func is_playing() -> bool:
	return _playing


func reset() -> void:
	_playing = false
	visible = false
	set_process(false)
	_age = 0.0


func advance(dt: float) -> void:
	if not _playing:
		return
	_age += dt
	if _age >= _dur:
		_playing = false
		_finish()
		return
	_apply()


func _apply() -> void:
	for i in _count:
		var t := clampf((_age - float(i) * _dur * 0.22) / (_dur * (1.0 - 0.22 * float(_count - 1))), 0.0, 1.0)
		var e := t * t if _ease_in else 1.0 - pow(1.0 - t, 2.5)
		if _hold:
			e = 1.0 - pow(1.0 - minf(t * 3.0, 1.0), 2.0)
		var r := lerpf(_r0, _r1, e)
		var scale_q := r / 0.8   # the band sits at 0.8 of the quad half size
		_q[i].scale = Vector3(scale_q, 1.0, scale_q) if _q[i].mesh == FxMesh.ground_quad() else Vector3(scale_q, scale_q, 1.0)
		var fade := (1.0 - smoothstep(0.55, 1.0, t)) * smoothstep(0.0, 0.06, t)
		var m := _m[i]
		m.set_shader_parameter("alpha", fade * _alpha * (1.0 if i == 0 else 0.7))
		m.set_shader_parameter("width", _width * 0.8 * (1.0 + 0.5 * t))
		m.set_shader_parameter("radius", 0.8)
		m.set_shader_parameter("phase", _age)
