class_name ComboTracker
extends RefCounted
## Live success detection for one LabCombos entry from sim events (docs/MOVESET.md section 9.3, section 14).
## Feed it every tick's events; it advances through the steps as the player's moves START (`action` events of
## the player, phase "startup", the move the step's (el, sub, slot) resolves to; tiered steps complete at the
## `charge` event that reaches the tier; shape steps at the `shape` / `split` event). Each step must start
## within its `within` seconds of the previous one, else the attempt fails and the tracker restarts. After
## the last step, a matching `result` event within its window makes it a success.

enum State { IDLE, RUNNING, SEQUENCE_DONE, SUCCESS, FAILED }

var combo: Dictionary = {}
var player_id := 1
var state := State.IDLE
## Index of the step being waited for (== steps.size() once the sequence is complete).
var step := 0
var t := 0.0                      # trainer clock (s)
var step_times: Array[float] = [] # clock time each completed step started
var attempts := 0
var successes := 0
var fail_reason := ""
var _wait_from := 0.0             # clock time the current step's window opened
var _done_at := 0.0
var _ids: Array[String] = []      # resolved move id per step


func start(c: Dictionary, pid: int) -> void:
	Moves.ensure()
	combo = c
	player_id = pid
	_ids.clear()
	for s in c.steps:
		_ids.append(Moves.resolve(int(s.el), int(s.sub), String(s.slot)))
	attempts = 0
	successes = 0
	_restart()
	state = State.RUNNING


func _restart() -> void:
	step = 0
	step_times.clear()
	t = 0.0
	_wait_from = 0.0
	fail_reason = ""
	state = State.RUNNING


func stop() -> void:
	state = State.IDLE
	combo = {}


func is_running() -> bool:
	return state == State.RUNNING or state == State.SEQUENCE_DONE


func steps() -> Array:
	return combo.get("steps", [])


## Seconds left in the current window (steps) or the result window.
func time_left() -> float:
	if state == State.RUNNING and step < steps().size():
		var within := float((steps()[step] as Dictionary).get("within", 4.0))
		if step == 0:
			return within
		return maxf(0.0, within - (t - _wait_from))
	if state == State.SEQUENCE_DONE:
		return maxf(0.0, float((combo.get("result", {}) as Dictionary).get("within", 6.0)) - (t - _done_at))
	return 0.0


## Window fraction used 0..1 (timing bar).
func window_used() -> float:
	if state == State.RUNNING and step > 0 and step < steps().size():
		var within := maxf(float((steps()[step] as Dictionary).get("within", 4.0)), 0.01)
		return clampf((t - _wait_from) / within, 0.0, 1.0)
	return 0.0


## Advance the clock (seconds of sim time) and consume this tick's events.
func update(dt: float, events: Array) -> void:
	if state == State.IDLE or combo.is_empty():
		return
	t += dt
	for e in events:
		_feed(e)
	_check_timeouts()


func _feed(e: Dictionary) -> void:
	if state == State.SUCCESS or state == State.FAILED:
		return
	if state == State.SEQUENCE_DONE:
		if _result_matches(e):
			state = State.SUCCESS
			successes += 1
		return
	if step >= steps().size():
		return
	var s: Dictionary = steps()[step]
	var type := String(e.get("type", ""))
	if String(s.get("kind", "move")) == "shape":
		if (type == "shape" or type == "split") and int(e.get("actor", player_id)) == player_id:
			_advance()
		return
	if type == "action" and int(e.get("actor", -1)) == player_id and String(e.get("phase", "")) == "startup":
		var mv := String(e.get("move", ""))
		if mv == _ids[step] and int(s.get("tier", 0)) <= 0:
			_advance()
		elif step > 0 and mv != _ids[step] and mv == _ids[0] and _ids[0] != _ids[step]:
			# Started over with the first move.
			_restart_with_first()
		return
	if type == "charge" and int(s.get("tier", 0)) > 0 and int(e.get("actor", -1)) == player_id and String(e.get("move", "")) == _ids[step]:
		if int(e.get("tier", 0)) >= int(s.tier):
			_advance()


func _restart_with_first() -> void:
	attempts += 1
	var keep := t
	_restart()
	t = keep
	step_times.append(t)
	step = 1
	_wait_from = t
	if step >= steps().size():
		_sequence_complete()


func _advance() -> void:
	step_times.append(t)
	step += 1
	_wait_from = t
	if step >= steps().size():
		_sequence_complete()


func _sequence_complete() -> void:
	_done_at = t
	var res: Dictionary = combo.get("result", {})
	if (res.get("any", []) as Array).is_empty():
		state = State.SUCCESS
		successes += 1
		attempts += 1
	else:
		state = State.SEQUENCE_DONE
		attempts += 1


func _check_timeouts() -> void:
	if state == State.RUNNING and step > 0 and step < steps().size():
		var within := float((steps()[step] as Dictionary).get("within", 4.0))
		if t - _wait_from > within:
			fail_reason = "too slow: %s" % LabCombos.step_text(steps()[step])
			attempts += 1
			var keep_fail := fail_reason
			_restart()
			fail_reason = keep_fail
	elif state == State.SEQUENCE_DONE:
		var rw := float((combo.get("result", {}) as Dictionary).get("within", 6.0))
		if t - _done_at > rw:
			# The moves were all played but the effect was not seen: report it and go again.
			fail_reason = "sequence played, no result: %s" % String((combo.get("result", {}) as Dictionary).get("text", ""))
			var keep := fail_reason
			_restart()
			fail_reason = keep


func _result_matches(e: Dictionary) -> bool:
	for pred in (combo.get("result", {}) as Dictionary).get("any", []):
		var ok := true
		for k in pred:
			if not e.has(k) or e[k] != pred[k]:
				ok = false
				break
		if ok:
			return true
	return false


## Human-readable status line.
func status_text() -> String:
	match state:
		State.IDLE:
			return "pick a combo"
		State.RUNNING:
			if step < steps().size():
				return "step %d/%d: %s" % [step + 1, steps().size(), LabCombos.step_text(steps()[step])]
			return ""
		State.SEQUENCE_DONE:
			return "sequence done, waiting for the result: %s" % String((combo.get("result", {}) as Dictionary).get("text", ""))
		State.SUCCESS:
			return "SUCCESS: %s" % String((combo.get("result", {}) as Dictionary).get("text", "combo complete"))
		State.FAILED:
			return fail_reason
	return ""
