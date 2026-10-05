extends SceneTree
## Fixed-camera stills of the arena environment for before/after comparisons.
##   tools/scripts/godot.sh --render --resolution 1280x592 -s ../tools/textures/env_preview.gd -- /abs/outdir prefix [quality]
## Writes <outdir>/<prefix>_<shot>.png for every camera in SHOTS.

# [name, eye, target, fov]
const SHOTS := [
	["overview", Vector3(0, 6.0, 14.5), Vector3(0, 0.4, -2.0), 58.0],
	["gameplay", Vector3(2.5, 3.6, 12.0), Vector3(0.5, 0.8, 4.0), 55.0],
	["pool", Vector3(4.0, 3.2, 8.5), Vector3(10.0, 0.0, -1.0), 55.0],
	["metal", Vector3(-3.0, 2.6, 5.0), Vector3(-9.0, 0.0, -1.0), 55.0],
	["ledge", Vector3(-8.5, 1.8, -6.0), Vector3(-12.5, 0.9, -12.5), 55.0],
	["terrace", Vector3(5.0, 2.0, 5.5), Vector3(-1.0, 0.5, 11.5), 60.0],
	["ground_close", Vector3(1.5, 1.2, 3.0), Vector3(0.0, 0.0, -1.5), 50.0],
]


func _initialize() -> void:
	_run()


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var out_dir: String = args[0] if args.size() > 0 else "/tmp"
	var prefix: String = args[1] if args.size() > 1 else "env"
	var quality := int(args[2]) if args.size() > 2 else 2
	DirAccess.make_dir_recursive_absolute(out_dir)
	var av := ArenaView.new()
	root.add_child(av)
	av.build(ArenaMap.make_lab(), quality)
	var cam := Camera3D.new()
	root.add_child(cam)
	cam.current = true
	root.msaa_3d = Viewport.MSAA_2X if quality >= 2 else Viewport.MSAA_DISABLED
	var only := ""
	for a in args:
		if a.begins_with("shot="):
			only = a.substr(5)
	if "nowater" in args:
		av.get_node("pool_water").visible = false
	if "noprops" in args:
		av.props.visible = false
	for s in SHOTS:
		if only != "" and s[0] != only:
			continue
		cam.position = s[1]
		cam.look_at_from_position(s[1], s[2], Vector3.UP)
		cam.fov = s[3]
		for i in 6:
			await process_frame
		await RenderingServer.frame_post_draw
		var img := root.get_texture().get_image()
		img.save_png("%s/%s_%s.png" % [out_dir, prefix, s[0]])
	quit()
