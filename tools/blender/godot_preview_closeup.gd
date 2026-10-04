extends SceneTree
## Engine close-up renders of the imported fighter with the game's player palette.
##   tools/scripts/godot.sh --render --resolution 1000x700 -s tools/blender/godot_preview_closeup.gd -- OUT_PREFIX [clip@time,...]
## Writes OUT_PREFIX_<view>.png for views: full (3/4), face, hands, back.  `clip@time` (seconds, or `c` = contact), default idle@0.
## Palette = game.gd PLAYER_COLORS (albedo_color is *set* by the game, so textures must be tint-relative).

const GLB := "res://assets/characters/fighter.glb"
const PAL := {"cloth_main": Color(0.19, 0.23, 0.31), "cloth_accent": Color(0.62, 0.5, 0.32),
	"wraps": Color(0.86, 0.83, 0.77), "skin": Color(0.78, 0.6, 0.48), "hair": Color(0.12, 0.1, 0.09)}
# view: [camera position, look-at, fov, yaw of fighter degrees]
const VIEWS := {
	"full": [Vector3(1.1, 1.25, 2.9), Vector3(0, 0.9, 0), 32.0],
	"front": [Vector3(0.0, 1.2, 3.2), Vector3(0, 0.9, 0), 32.0],
	"back": [Vector3(-0.9, 1.3, -2.9), Vector3(0, 0.95, 0), 32.0],
	"face": [Vector3(0.45, 1.66, 0.85), Vector3(0, 1.62, 0), 28.0],
	"hands": [Vector3(0.0, 1.15, 1.25), Vector3(0, 1.1, 0), 40.0],
	"legs": [Vector3(0.9, 0.55, 1.5), Vector3(0, 0.45, 0), 34.0],
}


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
	var args := OS.get_cmdline_user_args()
	var prefix: String = args[0] if args.size() > 0 else "/tmp/fighter_cu"
	var spec: String = args[1] if args.size() > 1 else "idle@0"
	var views: Array = (args[2] if args.size() > 2 else "full,face,hands,back").split(",")
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	var scene: PackedScene = load(GLB)

	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.55, 0.62, 0.72)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	e.sky = Sky.new()
	var psm := ProceduralSkyMaterial.new()
	psm.sky_top_color = Color(0.45, 0.6, 0.85)
	psm.sky_horizon_color = Color(0.85, 0.8, 0.75)
	psm.ground_bottom_color = Color(0.3, 0.28, 0.26)
	psm.ground_horizon_color = Color(0.6, 0.55, 0.5)
	e.sky.sky_material = psm
	e.ambient_light_energy = 0.8
	e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.environment = e
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-42, 35, 0)
	sun.light_energy = 1.3
	sun.shadow_enabled = true
	root.add_child(sun)
	var floor_mi := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(12, 12)
	floor_mi.mesh = plane
	var fm := StandardMaterial3D.new()
	fm.albedo_color = Color(0.42, 0.38, 0.34)
	floor_mi.material_override = fm
	root.add_child(floor_mi)
	var inst: Node3D = scene.instantiate()
	root.add_child(inst)
	var player: AnimationPlayer = _find(inst, "AnimationPlayer")
	var parts := spec.split("@")
	var clip: String = parts[0]
	var t := 0.0
	if parts.size() > 1:
		t = float(clips[clip]["contact"]) if parts[1] == "c" else float(parts[1])
	player.play(clip)
	player.seek(t, true)
	player.advance(0.0)
	player.pause()
	var mi: MeshInstance3D = _find(inst, "MeshInstance3D")
	for s in mi.mesh.get_surface_count():
		var m := mi.mesh.surface_get_material(s)
		if m is BaseMaterial3D:
			var d: BaseMaterial3D = m.duplicate()
			var key: String = m.resource_name
			if PAL.has(key):
				d.albedo_color = PAL[key]
				d.roughness = 0.82 if key != "skin" else 0.6
			mi.set_surface_override_material(s, d)
	print("tris ~", mi.mesh.get_faces().size() / 3, " surfaces ", mi.mesh.get_surface_count())
	var cam := Camera3D.new()
	root.add_child(cam)
	for v in views:
		var vd: Array = VIEWS[v]
		cam.fov = vd[2]
		cam.look_at_from_position(vd[0], vd[1])
		for i in 3:
			await process_frame
		await RenderingServer.frame_post_draw
		var img := root.get_texture().get_image()
		img.save_png("%s_%s.png" % [prefix, v])
	print("saved ", prefix)
	quit()
