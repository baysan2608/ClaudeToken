extends SceneTree
## Loads every script under the given dirs so parse/type errors surface. Exit 1 on failure.
## tools/scripts/godot.sh --headless -s res://tests/check_scripts.gd
func _init() -> void:
	var bad := 0
	var n := 0
	for dir in ["res://core", "res://combat", "res://actors", "res://presentation", "res://ui", "res://scenarios", "res://audio", "res://"]:
		for f in _list(dir, dir == "res://"):
			n += 1
			var s: Script = load(f)
			if s == null or not s.can_instantiate():
				print("FAIL ", f)
				bad += 1
	print("checked %d scripts, %d failed" % [n, bad])
	quit(1 if bad > 0 else 0)

func _list(dir: String, shallow: bool) -> Array[String]:
	var out: Array[String] = []
	var d := DirAccess.open(dir)
	if d == null:
		return out
	for f in d.get_files():
		if f.ends_with(".gd"):
			out.append(dir.path_join(f))
	if not shallow:
		for sub in d.get_directories():
			out.append_array(_list(dir.path_join(sub), false))
	return out
