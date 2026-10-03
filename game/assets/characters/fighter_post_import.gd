@tool
extends EditorScenePostImport
## Import-time fix-up for fighter.glb: glTF cannot carry loop flags, so loop modes come from fighter_clips.json
## (written by tools/blender/build_fighter.py). Referenced by `import_script/path` in fighter.glb.import.


func _post_import(scene: Node) -> Object:
	var clips = JSON.parse_string(FileAccess.get_file_as_string("res://assets/characters/fighter_clips.json"))
	if typeof(clips) != TYPE_DICTIONARY:
		push_warning("fighter_post_import: fighter_clips.json missing or invalid; loop modes not set")
		return scene
	var player := scene.find_child("AnimationPlayer", true, false) as AnimationPlayer
	if player == null:
		return scene
	for clip_name in player.get_animation_list():
		var info = clips.get(String(clip_name))
		if info != null and info.get("loop", false):
			player.get_animation(clip_name).loop_mode = Animation.LOOP_LINEAR
	return scene
