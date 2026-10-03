class_name Progression
extends RefCounted
## Saved mastery progress: unlocked techniques and completed challenges.
## user://progress.cfg. "Reset progress" in the pause menu clears it.

const PATH := "user://progress.cfg"
const ALL := ["magma", "heat_draw", "lightning", "redirect_current", "glide"]

var unlocked := {}
var done := {}
var lab_mode := false      # testing: everything unlocked
var last_scenario := "molten_exchange"
var path := PATH


static func load_from(p: String = PATH) -> Progression:
	var pr := Progression.new()
	pr.path = p
	var cf := ConfigFile.new()
	if cf.load(p) == OK:
		for t in cf.get_value("progress", "unlocked", []):
			pr.unlocked[t] = true
		for c in cf.get_value("progress", "done", []):
			pr.done[c] = true
		pr.lab_mode = cf.get_value("progress", "lab_mode", false)
		pr.last_scenario = cf.get_value("progress", "last_scenario", "molten_exchange")
	return pr


func save() -> void:
	var cf := ConfigFile.new()
	cf.set_value("progress", "unlocked", unlocked.keys())
	cf.set_value("progress", "done", done.keys())
	cf.set_value("progress", "lab_mode", lab_mode)
	cf.set_value("progress", "last_scenario", last_scenario)
	cf.save(path)


func reset() -> void:
	unlocked.clear()
	done.clear()
	lab_mode = false
	save()


func kit() -> Dictionary:
	var k := {}
	for t in ALL:
		if lab_mode or unlocked.has(t):
			k[t] = true
	return k


func is_done(challenge_id: String) -> bool:
	return done.has(challenge_id)


## Marks a challenge complete. Returns true when it unlocked something new.
func complete(challenge_id: String, unlock: String) -> bool:
	done[challenge_id] = true
	var fresh := not unlocked.has(unlock)
	unlocked[unlock] = true
	save()
	return fresh
