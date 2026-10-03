extends SceneTree
## Headless test runner: tools/scripts/godot.sh --headless -s res://tests/run_tests.gd [-- filter]
## Runs every test_* method of every res://tests/sim/test_*.gd. Exit code 1 on failure.

func _init() -> void:
	var filter := ""
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		filter = args[0]
	var total := 0
	var failed := 0
	var dir := DirAccess.open("res://tests/sim")
	var files: Array = []
	for f in dir.get_files():
		if f.begins_with("test_") and f.ends_with(".gd"):
			files.append(f)
	files.sort()
	var t0 := Time.get_ticks_msec()
	for f in files:
		var script: GDScript = load("res://tests/sim/" + f)
		for m in script.get_script_method_list():
			var mname: String = m.name
			if not mname.begins_with("test_"):
				continue
			if filter != "" and not (f + ":" + mname).contains(filter):
				continue
			var tc: TestCase = script.new()
			tc.current = f.get_basename() + "." + mname
			tc.call(mname)
			total += 1
			for n in tc.notes:
				print("  note  ", n)
			if tc.failures.is_empty():
				print("PASS  ", tc.current)
			else:
				failed += 1
				print("FAIL  ", tc.current)
				for e in tc.failures:
					print("      ", e)
	print("\n%d tests, %d failed (%d ms)" % [total, failed, Time.get_ticks_msec() - t0])
	quit(1 if failed > 0 else 0)
