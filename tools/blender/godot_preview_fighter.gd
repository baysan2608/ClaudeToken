extends SceneTree
## Renders a line-up of the imported fighter in a few clip poses (and recoloured, as the runtime would do for
## the opponent) to a PNG, to eyeball skinning/materials in the real engine.
##   tools/scripts/godot.sh --render -s tools/blender/godot_preview_fighter.gd -- /abs/out.png

const GLB := "res://assets/characters/fighter.glb"
# [clip, time in seconds (-1 = contact from json), x position, cloth_main colour, accent colour]
const LINEUP := [
	["idle", 0.0, -3.0, Color(0.30, 0.45, 0.85), Color(0.95, 0.80, 0.25)],
	["stance_earth", 0.0, -1.8, Color(0.55, 0.40, 0.25), Color(0.30, 0.55, 0.25)],
	["guard", 0.0, -0.6, Color(0.30, 0.45, 0.85), Color(0.95, 0.80, 0.25)],
	["fire_jab", -1.0, 0.6, Color(0.80, 0.25, 0.20), Color(0.15, 0.15, 0.15)],
	["water_whip", -1.0, 1.8, Color(0.20, 0.60, 0.75), Color(0.90, 0.95, 1.00)],
	["air_gust", -1.0, 3.0, Color(0.85, 0.85, 0.90), Color(0.55, 0.40, 0.80)],
]


func _find(node: Node, cls: String) -> Node:
	if node.is_class(cls):
		return node
	for c in node.get_children():
		var r := _find(c, cls)
		if r:
			return r
	return null


func _initialize() -> void:
	_run()


func _run() -> void:
	await process_frame
	var out := "/tmp/fighter_godot.png"
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out = args[0]
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	var scene: PackedScene = load(GLB)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.62, 0.68, 0.76)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.75, 0.78, 0.85)
	e.ambient_light_energy = 0.6
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-45, 25, 0)
	sun.light_energy = 1.1
	root.add_child(sun)
	var floor_mi := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(12, 4)
	floor_mi.mesh = plane
	var fm := StandardMaterial3D.new()
	fm.albedo_color = Color(0.32, 0.34, 0.37)
	floor_mi.material_override = fm
	root.add_child(floor_mi)
	var cam := Camera3D.new()
	cam.fov = 38.0
	root.add_child(cam)
	cam.look_at_from_position(Vector3(0, 1.0, 6.6), Vector3(0, 0.88, 0))

	for item in LINEUP:
		var inst: Node3D = scene.instantiate()
		root.add_child(inst)
		inst.position.x = item[2]
		var player: AnimationPlayer = _find(inst, "AnimationPlayer")
		var clip: String = item[0]
		var t: float = item[1]
		if t < 0.0:
			t = float(clips[clip]["contact"])
		player.play(clip)
		player.seek(t, true)
		player.advance(0.0)
		player.pause()
		# runtime recolouring: duplicate surface materials by slot name
		var mi: MeshInstance3D = _find(inst, "MeshInstance3D")
		for s in mi.mesh.get_surface_count():
			var m := mi.mesh.surface_get_material(s)
			if m is BaseMaterial3D:
				var d: BaseMaterial3D = m.duplicate()
				if m.resource_name == "cloth_main":
					d.albedo_color = item[3]
				elif m.resource_name == "cloth_accent":
					d.albedo_color = item[4]
				mi.set_surface_override_material(s, d)
	for i in 4:
		await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	var err := img.save_png(out)
	print("saved ", out, " err=", err, " size=", img.get_size())
	quit()
