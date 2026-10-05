extends SceneTree
## Godot-side validation of the fighter import.
##   tools/scripts/godot.sh --headless --import
##   tools/scripts/godot.sh --headless -s tools/blender/validate_fighter.gd -- <abs path to fighter_expected_bones.json>
## Prints skeleton / animation / mesh facts and cross-checks bone positions against the Blender evaluation
## (written by validate_fighter.py). Exit code 1 on any failure.

const GLB := "res://assets/characters/fighter.glb"
const CLIPS_JSON := "res://assets/characters/fighter_clips.json"
const BONES := ["root", "hips", "spine", "chest", "neck", "head", "shoulder.L", "upper_arm.L", "forearm.L", "hand.L",
	"shoulder.R", "upper_arm.R", "forearm.R", "hand.R", "thigh.L", "shin.L", "foot.L", "toe.L", "thigh.R", "shin.R",
	"foot.R", "toe.R"]
## bones added in v2 (articulated hands, sash tails, hair); must exist too, 42 in total
const EXTRA_BONES := ["thumb_1", "thumb_2", "finger_im_1", "finger_im_2", "finger_rp_1", "finger_rp_2"]
const SECONDARY := ["sash_tail.L.001", "sash_tail.L.002", "sash_tail.L.003", "sash_tail.R.001", "sash_tail.R.002", "sash_tail.R.003",
	"hair_top.001", "hair_top.002"]

var failures := 0


func _fail(msg: String) -> void:
	failures += 1
	print("FAIL: ", msg)


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
	var scene: PackedScene = load(GLB)
	if scene == null:
		_fail("cannot load " + GLB)
		quit(1)
		return
	var inst: Node3D = scene.instantiate()
	root.add_child(inst)
	await process_frame
	var skel: Skeleton3D = _find(inst, "Skeleton3D")
	var player: AnimationPlayer = _find(inst, "AnimationPlayer")
	var mi: MeshInstance3D = _find(inst, "MeshInstance3D")
	print("== scene tree ==")
	_dump(inst, 0)

	print("== skeleton ==")
	print("bone count: ", skel.get_bone_count())
	var names: Array = []
	for i in skel.get_bone_count():
		names.append(skel.get_bone_name(i))
	print("bones: ", names)
	for b in BONES:
		if skel.find_bone(b) < 0:
			_fail("missing bone " + b)
	var want_bones := BONES.size() + 2 * EXTRA_BONES.size() + SECONDARY.size()
	for e in EXTRA_BONES:
		for sd in ["L", "R"]:
			if skel.find_bone("%s.%s" % [e, sd]) < 0:
				_fail("missing finger bone %s.%s" % [e, sd])
	for b in SECONDARY:
		if skel.find_bone(b) < 0:
			_fail("missing secondary bone " + b)
	if skel.get_bone_count() != want_bones:
		_fail("bone count %d != %d" % [skel.get_bone_count(), want_bones])
	# the 22 original bones keep their rest transform convention: root, hips ... (names are what the game uses)

	print("== mesh ==")
	var mesh: Mesh = mi.mesh
	print("surfaces: ", mesh.get_surface_count())
	for s in mesh.get_surface_count():
		var mat := mesh.surface_get_material(s)
		print("  surface ", s, " material=", mat.resource_name if mat else "<none>", " color=", (mat as BaseMaterial3D).albedo_color if mat is BaseMaterial3D else "-")
	print("mesh skin bound: ", mi.skin != null, " skeleton path: ", mi.skeleton)
	# global AABB of the (rest-pose) mesh
	var aabb: AABB = mi.global_transform * mi.get_aabb()
	print("global AABB (rest): pos=", aabb.position, " size=", aabb.size)
	print("height (y extent): %.3f m  (min y %.3f, max y %.3f)" % [aabb.size.y, aabb.position.y, aabb.end.y])
	if absf(aabb.size.y - 1.75) > 0.05:
		_fail("height not ~1.75: %.3f" % aabb.size.y)
	# orientation: front of the character must be +Z, left +X
	skel.force_update_all_bone_transforms()
	var gt := skel.global_transform
	var foot_p := (gt * skel.get_bone_global_pose(skel.find_bone("foot.L"))).origin
	var toe_p := (gt * skel.get_bone_global_pose(skel.find_bone("toe.L"))).origin
	var hand_l := (gt * skel.get_bone_global_pose(skel.find_bone("hand.L"))).origin
	var hand_r := (gt * skel.get_bone_global_pose(skel.find_bone("hand.R"))).origin
	print("foot.L ", foot_p, " toe.L ", toe_p, " -> toes point ", "+Z (front is +Z)" if toe_p.z > foot_p.z else "-Z")
	if toe_p.z <= foot_p.z:
		_fail("character does not face +Z")
	print("hand.L x=%.3f hand.R x=%.3f -> left is %s" % [hand_l.x, hand_r.x, "+X" if hand_l.x > hand_r.x else "-X"])
	if hand_l.x <= hand_r.x:
		_fail("left is not +X")
	var sole_min := minf(foot_p.y, toe_p.y)
	print("ankle y (rest) %.3f, ball y %.3f (soles sit at y=0 -> mesh min y %.3f)" % [foot_p.y, toe_p.y, aabb.position.y])

	print("== animations ==")
	var clips: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(CLIPS_JSON))
	var list := player.get_animation_list()
	print("animation count: ", list.size())
	var bad_len := 0
	for n in list:
		var a := player.get_animation(n)
		var info: Dictionary = clips.get(String(n), {})
		var dur: float = info.get("duration", -1.0)
		var tag := ""
		if info.is_empty():
			tag = " <-- NOT IN JSON"
			_fail("animation %s not in clips json" % n)
		elif absf(a.length - dur) > 0.002:
			tag = " <-- LENGTH MISMATCH (json %.4f)" % dur
			bad_len += 1
			_fail("length mismatch %s" % n)
		var want_loop: bool = info.get("loop", false)
		var loop_ok: bool = (a.loop_mode != Animation.LOOP_NONE) == want_loop
		if not loop_ok:
			tag += " <-- loop mode %d but json loop=%s" % [a.loop_mode, str(want_loop)]
		print("  %-18s len=%.4f  tracks=%2d  loop_mode=%d%s" % [n, a.length, a.get_track_count(), a.loop_mode, tag])
	for k in clips.keys():
		if not list.has(StringName(k)):
			_fail("json clip %s missing from glb" % k)

	print("== pose cross-check vs Blender ==")
	var args := OS.get_cmdline_user_args()
	if args.size() > 0 and FileAccess.file_exists(args[0]):
		var expected: Array = JSON.parse_string(FileAccess.get_file_as_string(args[0]))
		var worst := 0.0
		for e in expected:
			var clip: String = e["clip"]
			player.play(clip)
			player.seek(float(e["time"]), true)
			player.advance(0.0)
			skel.force_update_all_bone_transforms()
			var gt2 := skel.global_transform
			var err_clip := 0.0
			for bname in e["bones_blender"].keys():
				var bl: Array = e["bones_blender"][bname]
				var want := Vector3(bl[0], bl[2], -bl[1])      # Blender (x, y, z) -> glTF/Godot (x, z, -y)
				var got := (gt2 * skel.get_bone_global_pose(skel.find_bone(bname))).origin
				err_clip = maxf(err_clip, got.distance_to(want))
			worst = maxf(worst, err_clip)
			print("  %-14s t=%.3f  max bone-position error %.2f mm" % [clip, e["time"], err_clip * 1000.0])
			if err_clip > 0.003:
				_fail("pose mismatch in %s (%.1f mm)" % [clip, err_clip * 1000.0])
		print("  worst error: %.2f mm" % (worst * 1000.0))
	else:
		print("  (no expected-bones json given; skipped)")

	# bind/skin sanity: every surface should have bone arrays
	var fmt: int = mesh.surface_get_format(0)
	print("surface 0 has bones: ", (fmt & Mesh.ARRAY_FORMAT_BONES) != 0, " weights: ", (fmt & Mesh.ARRAY_FORMAT_WEIGHTS) != 0)

	print("RESULT: ", "PASS" if failures == 0 else "FAIL (%d)" % failures)
	quit(1 if failures > 0 else 0)


func _dump(n: Node, depth: int) -> void:
	if depth > 3:
		return
	print("  ".repeat(depth), n.name, " [", n.get_class(), "]")
	for c in n.get_children():
		_dump(c, depth + 1)
