class_name TestCase
extends RefCounted
## Minimal assertion base for headless tests (no external framework).

var failures: Array[String] = []
var current := ""
var notes: Array[String] = []


func check(cond: bool, msg: String) -> bool:
	if not cond:
		failures.append("%s: %s" % [current, msg])
	return cond


func near(a: float, b: float, eps: float, msg: String) -> bool:
	return check(absf(a - b) <= eps, "%s (got %.4f, want %.4f ±%.4f)" % [msg, a, b, eps])


func note(s: String) -> void:
	notes.append("%s: %s" % [current, s])
