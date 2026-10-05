class_name FighterSecondaryMotion
extends RefCounted
## Secondary motion for cloth/hair chains with Godot's SpringBoneSimulator3D.
## The chains are found by bone name at runtime, so the rig can grow them later without code
## changes: sash tails `sash_tail.L.001..00N` / `sash_tail.R.001..00N`, and any chain whose bone
## names start with "hair" (root = the hair bone whose parent is not a hair bone). With no such
## bones nothing is created (clean no-op). Thigh capsules keep the sash out of the legs.

const SASH_PREFIXES := ["sash_tail.L", "sash_tail.R"]


## Returns [[root_name, end_name, kind], ...] for every chain present in the skeleton.
static func find_chains(sk: Skeleton3D) -> Array:
	var out: Array = []
	for pre in SASH_PREFIXES:
		var first := "%s.001" % pre
		if sk.find_bone(first) < 0:
			continue
		var last := first
		for i in range(2, 10):
			var nm := "%s.%03d" % [pre, i]
			if sk.find_bone(nm) < 0:
				break
			last = nm
		out.append([first, last, "sash"])
	for b in sk.get_bone_count():
		var nm := sk.get_bone_name(b)
		if not nm.to_lower().begins_with("hair"):
			continue
		var p := sk.get_bone_parent(b)
		if p >= 0 and sk.get_bone_name(p).to_lower().begins_with("hair"):
			continue
		# Follow the first hair child down to the chain's end.
		var end := b
		var guard := 0
		while guard < 16:
			guard += 1
			var nxt := -1
			for c in sk.get_bone_children(end):
				if sk.get_bone_name(c).to_lower().begins_with("hair"):
					nxt = c
					break
			if nxt < 0:
				break
			end = nxt
		if end != b:
			out.append([nm, sk.get_bone_name(end), "hair"])
	return out


## Adds a configured SpringBoneSimulator3D under the skeleton, or returns null when the skeleton
## has no secondary chains.
static func attach(sk: Skeleton3D) -> SpringBoneSimulator3D:
	var chains := find_chains(sk)
	if chains.is_empty():
		return null
	var sim := SpringBoneSimulator3D.new()
	sim.name = "SecondaryMotion"
	sim.setting_count = chains.size()
	var has_sash := false
	for i in chains.size():
		var ch: Array = chains[i]
		sim.set_root_bone_name(i, ch[0])
		sim.set_end_bone_name(i, ch[1])
		sim.set_extend_end_bone(i, true)
		sim.set_center_from(i, SpringBoneSimulator3D.CENTER_FROM_WORLD_ORIGIN)
		if ch[2] == "sash":
			has_sash = true
			sim.set_end_bone_length(i, 0.06)
			sim.set_stiffness(i, 0.9)
			sim.set_drag(i, 0.35)
			sim.set_gravity(i, 0.9)
			sim.set_radius(i, 0.025)
		else:
			sim.set_end_bone_length(i, 0.04)
			sim.set_stiffness(i, 2.4)
			sim.set_drag(i, 0.55)
			sim.set_gravity(i, 0.25)
			sim.set_radius(i, 0.015)
		sim.set_gravity_direction(i, Vector3.DOWN)
	if has_sash:
		for side in ["L", "R"]:
			var thigh: String = "thigh." + side
			if sk.find_bone(thigh) < 0:
				continue
			var cap := SpringBoneCollisionCapsule3D.new()
			cap.name = "ThighCapsule" + side
			cap.bone_name = thigh
			cap.radius = 0.085
			cap.height = 0.5
			# The capsule's axis is its local Y; bone Y runs along the thigh, centre it mid-bone.
			cap.position_offset = Vector3(0, 0.2, 0)
			sim.add_child(cap)
	sk.add_child(sim)
	return sim
