class_name AnimDebugDraw
extends MeshInstance3D
## Debug markers for one fighter's rig: foot IK goals (green crosses) and the look target
## (yellow). Enabled with AnimRigSettings.debug_draw (anim lab, or F10 with --animdebug).

var _im := ImmediateMesh.new()


func _init() -> void:
	mesh = _im
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.vertex_color_use_as_albedo = true
	m.no_depth_test = true
	material_override = m
	top_level = true


func draw(rig: FighterAnimRig) -> void:
	_im.clear_surfaces()
	_im.surface_begin(Mesh.PRIMITIVE_LINES)
	for p in rig.dbg_points:
		_cross(p, 0.07, Color(0.3, 1.0, 0.4))
	if rig.look_w > 0.05:
		_cross(rig.look_world, 0.15, Color(1.0, 0.9, 0.2))
	# keep the surface valid even when empty
	_im.surface_set_color(Color(0, 0, 0, 0))
	_im.surface_add_vertex(Vector3.ZERO)
	_im.surface_add_vertex(Vector3.ZERO)
	_im.surface_end()


func _cross(p: Vector3, s: float, c: Color) -> void:
	for ax in [Vector3.RIGHT, Vector3.UP, Vector3.BACK]:
		_im.surface_set_color(c)
		_im.surface_add_vertex(p - ax * s)
		_im.surface_set_color(c)
		_im.surface_add_vertex(p + ax * s)
