class_name ChargeAimFX
extends VfxEffect
## Charge / aim phase indicator: a thin crackling line from the caster toward the aim point with a
## small glow dot at the source. One static 62-vertex mesh; endpoints, charge and crackle are driven
## by shader uniforms every frame (no mesh rebuild, no allocation).
##
##   set_aim(from, to, t01)    world-space endpoints; t01 = charge progress (line calms and brightens)
##   hide_aim()                stop showing the line
##
## The node is top_level (world space geometry).

const SEGMENTS: int = 28

static var _mesh_cache: ArrayMesh = null

var _mi: MeshInstance3D
var _mat: ShaderMaterial


func _init() -> void:
	top_level = true
	_mat = VfxMaterials.make("charge_aim")
	_mi = MeshInstance3D.new()
	_mi.name = "Line"
	_mi.mesh = _get_mesh()
	_mi.material_override = _mat
	_mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_mi)
	reset()


func set_aim(from: Vector3, to: Vector3, t01: float) -> void:
	top_level = true
	_place(Transform3D.IDENTITY)
	visible = true
	_mat.set_shader_parameter("p_from", from)
	_mat.set_shader_parameter("p_to", to)
	_mat.set_shader_parameter("charge", clampf(t01, 0.0, 1.0))
	var lo := Vector3(minf(from.x, to.x), minf(from.y, to.y), minf(from.z, to.z)) - Vector3(0.4, 0.4, 0.4)
	var hi := Vector3(maxf(from.x, to.x), maxf(from.y, to.y), maxf(from.z, to.z)) + Vector3(0.4, 0.4, 0.4)
	_mi.custom_aabb = AABB(lo, hi - lo)


func hide_aim() -> void:
	visible = false


func reset() -> void:
	visible = false
	set_process(false)


static func _get_mesh() -> ArrayMesh:
	if _mesh_cache != null:
		return _mesh_cache
	var verts := PackedVector3Array()
	var uvs := PackedVector2Array()
	var cols := PackedColorArray()
	var idx := PackedInt32Array()
	for i in SEGMENTS + 1:
		var s: float = float(i) / float(SEGMENTS)
		for side in 2:
			verts.append(Vector3.ZERO)
			uvs.append(Vector2(float(side), s))
			cols.append(Color(0, 0, 0, 0))
	for i in SEGMENTS:
		var q: int = i * 2
		idx.append_array(PackedInt32Array([q, q + 1, q + 2, q + 1, q + 3, q + 2]))
	# glow quad at the source
	var g: int = verts.size()
	for c in [Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1)]:
		verts.append(Vector3.ZERO)
		uvs.append(c)
		cols.append(Color(0, 0, 0, 1))
	idx.append_array(PackedInt32Array([g, g + 1, g + 2, g + 1, g + 3, g + 2]))
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = cols
	arrays[Mesh.ARRAY_INDEX] = idx
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_mesh_cache = mesh
	return mesh
