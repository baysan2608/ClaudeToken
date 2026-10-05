class_name AnimClipSampler
extends RefCounted
## Samples the baked fighter clips straight from their Animation resources and blends any
## number of them with per-clip times and weights (normalised quaternion average). This is
## the synced locomotion blend space: every gait is sampled at the same normalised phase, so
## walk/run/strafe/backpedal mix without foot pops and without an AnimationTree (the
## AnimationPlayer keeps driving the actions and their contact-frame alignment).
##
## Track maps are cached per Animation (all fighters share the imported glb resources), so a
## blend costs one native interpolate call per bone track and clip.

const _ROT := Animation.TYPE_ROTATION_3D
const _POS := Animation.TYPE_POSITION_3D

## Animation instance id -> PackedInt32Array of (track, bone, type) triples.
static var _maps := {}

var bone_count := 0
var rots: Array[Quaternion] = []      # blended local rotations (valid where has_rot)
var has_rot := PackedByteArray()
var pos_acc := Vector3.ZERO           # blended hips local position (if has_pos)
var pos_bone := -1
var has_pos := false


func _init(n_bones: int = 0) -> void:
	resize(n_bones)


func resize(n_bones: int) -> void:
	bone_count = n_bones
	rots.resize(n_bones)
	has_rot.resize(n_bones)


static func track_map(anim: Animation, skel: Skeleton3D) -> PackedInt32Array:
	var key := anim.get_instance_id()
	if _maps.has(key):
		return _maps[key]
	var out := PackedInt32Array()
	for t in anim.get_track_count():
		var ty := anim.track_get_type(t)
		if ty != _ROT and ty != _POS:
			continue
		var b := skel.find_bone(String(anim.track_get_path(t).get_concatenated_subnames()))
		if b < 0:
			continue
		out.append(t)
		out.append(b)
		out.append(ty)
	_maps[key] = out
	return out


## entries: Array of [Animation, time_sec, weight]; weights need not sum to 1.
## mask: optional per-bone 0/1 bytes (empty = all bones).
func blend(entries: Array, skel: Skeleton3D, mask: PackedByteArray = PackedByteArray()) -> void:
	for i in bone_count:
		rots[i] = Quaternion(0, 0, 0, 0)
		has_rot[i] = 0
	pos_acc = Vector3.ZERO
	has_pos = false
	var wsum := 0.0
	for e in entries:
		wsum += float(e[2])
	if wsum <= 1e-5:
		return
	var use_mask := mask.size() == bone_count
	for e in entries:
		var w := float(e[2]) / wsum
		if w <= 1e-4:
			continue
		var anim: Animation = e[0]
		var tm := float(e[1])
		var m := track_map(anim, skel)
		var i := 0
		while i < m.size():
			var tr := m[i]
			var b := m[i + 1]
			var ty := m[i + 2]
			i += 3
			if use_mask and mask[b] == 0:
				continue
			if ty == _ROT:
				var q := anim.rotation_track_interpolate(tr, tm)
				var acc := rots[b]
				if acc.dot(q) < 0.0:
					q = -q
				rots[b] = acc + q * w
				has_rot[b] = 1
			else:
				pos_acc += anim.position_track_interpolate(tr, tm) * w
				pos_bone = b
				has_pos = true


## Writes the blend over the skeleton's current (mixer) pose with weight w.
func apply(skel: Skeleton3D, w: float) -> void:
	if w <= 1e-4:
		return
	for b in bone_count:
		if has_rot[b] == 0:
			continue
		var q := rots[b].normalized()
		if w >= 0.999:
			skel.set_bone_pose_rotation(b, q)
		else:
			skel.set_bone_pose_rotation(b, skel.get_bone_pose_rotation(b).slerp(q, w))
	if has_pos and pos_bone >= 0:
		skel.set_bone_pose_position(pos_bone, skel.get_bone_pose_position(pos_bone).lerp(pos_acc, w))
