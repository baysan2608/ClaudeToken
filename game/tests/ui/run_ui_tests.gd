extends SceneTree
## Headless UI test runner:
##   tools/scripts/godot.sh --headless -s res://tests/ui/run_ui_tests.gd
## Exit code 0 = all passed, 1 = failures. Suites live next to this file and
## extend ui_test_case.gd; every method named test_* is run on a fresh state.

const SUITES: Array[String] = [
	"res://tests/ui/test_touch_controls.gd",
	"res://tests/ui/test_ui_misc.gd",
	"res://tests/ui/test_input_regressions.gd",
]


func _initialize() -> void:
	_run()


func _run() -> void:
	# The root window only joins the tree after _initialize returns.
	await process_frame
	# Optional filter:  -s res://tests/ui/run_ui_tests.gd -- --only=haptics
	var only := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--only="):
			only = a.substr(7)
	var total_tests := 0
	var total_checks := 0
	var failed_tests: Array[String] = []
	var all_failures: Array[String] = []
	for path in SUITES:
		var script: GDScript = load(path)
		if script == null:
			all_failures.append("could not load " + path)
			continue
		var methods: Array[String] = []
		for m in script.get_script_method_list():
			if String(m["name"]).begins_with("test_"):
				methods.append(m["name"])
		methods.sort()
		for name in methods:
			if only != "" and not name.contains(only):
				continue
			var suite: RefCounted = script.new()
			suite.host = self
			suite.current_test = name
			suite.before_each()
			var before: int = suite.failures.size()
			suite.call(name)
			suite.after_each()
			total_tests += 1
			total_checks += int(suite.checks)
			if suite.failures.size() > before:
				failed_tests.append(name)
				all_failures.append_array(suite.failures)
				print("  FAIL  ", name)
			else:
				print("  ok    ", name, "  (", suite.checks, " checks)")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://ui_test_settings.cfg"))
	print("")
	for f in all_failures:
		print("    ", f)
	print("UI tests: %d tests, %d checks, %d failed tests, %d failures" % [total_tests, total_checks, failed_tests.size(), all_failures.size()])
	quit(0 if all_failures.is_empty() else 1)
