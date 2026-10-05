class_name LabTuning
extends RefCounted
## Live tuning (docs/MOVESET.md section 14): every numeric field of every move def (flat keys and the
## numeric parameters of `tiers.t1..t3`) and the numeric fields of the counter-rule cells (thresholds:
## eff, full_at, partial_at, perfect_mult, absorb_on_fail, channel weights w.K..P, ...). Moves go through
## Moves.set_override (restored by Moves.clear_overrides), rules are edited in place and restored from a
## saved copy. save / load / export use user://tuning.cfg (ConfigFile):
##   [moves]  "<move id>/<path>" = value        path: "damage" or "tiers.t2.mass"
##   [rules]  "<rule key>#<index>/<field>" = value   field: "eff" or "w.K"

const PATH := "user://tuning.cfg"

## Moves: id -> {path: value} of what is currently changed.
static var move_changes := {}
## Rules: "key#idx" -> {field: value} changed, and the originals to restore.
static var rule_changes := {}
static var _rule_orig := {}


# ================================================================ moves

## Every numeric field of a def as [{path, value, tier}] in a stable order (flat keys, then tiers).
static func numeric_fields(id: String) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var def: Dictionary = Moves.DEFS.get(id, {})
	var keys := def.keys()
	keys.sort()
	for k in keys:
		if k == "tiers" or k == "id":
			continue
		var v: Variant = def[k]
		if (v is float or v is int) and not (v is bool):
			out.append({"path": String(k), "value": float(v), "tier": -1})
	var tiers: Dictionary = def.get("tiers", {})
	for t in ["t1", "t2", "t3"]:
		if not tiers.has(t):
			continue
		var tk: Array = (tiers[t] as Dictionary).keys()
		tk.sort()
		for k in tk:
			var v2: Variant = tiers[t][k]
			if (v2 is float or v2 is int) and not (v2 is bool):
				out.append({"path": "tiers.%s.%s" % [t, k], "value": float(v2), "tier": int(String(t).substr(1))})
	return out


static func get_value(id: String, path: String) -> Variant:
	var def: Dictionary = Moves.DEFS.get(id, {})
	if path.begins_with("tiers."):
		var parts := path.split(".")
		return ((def.get("tiers", {}) as Dictionary).get(parts[1], {}) as Dictionary).get(parts[2])
	return def.get(path)


## Sets one field live (Moves.set_override). Returns false for an unknown move / path.
static func set_value(id: String, path: String, value: float) -> bool:
	if not Moves.DEFS.has(id):
		return false
	if path.begins_with("tiers."):
		var parts := path.split(".")
		if parts.size() != 3:
			return false
		var tiers: Dictionary = (Moves.DEFS[id].get("tiers", {}) as Dictionary).duplicate(true)
		if not tiers.has(parts[1]) or not (tiers[parts[1]] as Dictionary).has(parts[2]):
			return false
		tiers[parts[1]][parts[2]] = value
		Moves.set_override(id, "tiers", tiers)
	else:
		if not Moves.DEFS[id].has(path):
			return false
		Moves.set_override(id, path, value)
	if not move_changes.has(id):
		move_changes[id] = {}
	move_changes[id][path] = value
	return true


## Original (registered) value of a path, for the slider range and the "modified" mark.
static func original_value(id: String, path: String) -> Variant:
	var ov := Moves.overrides()
	if not ov.has(id):
		return get_value(id, path)
	# Moves keeps the originals privately: read them through a snapshot of a fresh registration is overkill;
	# the saved defaults come from the change log instead.
	return _orig_of(id, path)


static func _orig_of(id: String, path: String) -> Variant:
	var o: Dictionary = Moves._orig.get(id, {})
	if path.begins_with("tiers."):
		var parts := path.split(".")
		if o.has("tiers") and o.tiers is Dictionary:
			return ((o.tiers as Dictionary).get(parts[1], {}) as Dictionary).get(parts[2])
		return get_value(id, path)
	if o.has(path):
		return o[path]
	return get_value(id, path)


static func is_changed(id: String, path: String) -> bool:
	return (move_changes.get(id, {}) as Dictionary).has(path)


static func change_count() -> int:
	var n := 0
	for id in move_changes:
		n += (move_changes[id] as Dictionary).size()
	for k in rule_changes:
		n += (rule_changes[k] as Dictionary).size()
	return n


# ================================================================ rules

## Every rule cell as [{key, idx, id, label}] (key = "threat|counter", idx = position in its list).
static func rule_cells() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var all := Interactions.all_rules()
	var keys := all.keys()
	keys.sort()
	for k in keys:
		var lst: Array = all[k]
		for i in lst.size():
			var r: Dictionary = lst[i]
			out.append({"key": String(k), "idx": i, "id": String(r.get("id", "")), "label": "%s%s" % [String(k), "" if lst.size() == 1 else " #%d" % i]})
	return out


static func _rule_ref(key: String, idx: int) -> Dictionary:
	var lst: Array = Interactions.all_rules().get(key, [])
	if idx < 0 or idx >= lst.size():
		return {}
	return lst[idx]


## Numeric fields of a rule: [{path, value}] (top-level numbers and the channel weights w.*).
static func rule_fields(key: String, idx: int) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var r := _rule_ref(key, idx)
	var ks := r.keys()
	ks.sort()
	for k in ks:
		var v: Variant = r[k]
		if (v is float or v is int) and not (v is bool):
			out.append({"path": String(k), "value": float(v)})
		elif k == "w" and v is Dictionary:
			for c in ["K", "H", "C", "E", "P"]:
				if (v as Dictionary).has(c):
					out.append({"path": "w.%s" % c, "value": float(v[c])})
	# The common thresholds are always offered, even when the cell relies on the defaults.
	for dflt in [["eff", 1.0], ["full_at", 1.0], ["partial_at", 0.5], ["perfect_mult", 1.5], ["absorb_on_fail", 0.5]]:
		var have := false
		for f in out:
			if f.path == dflt[0]:
				have = true
		if not have:
			out.append({"path": dflt[0], "value": float(dflt[1])})
	return out


static func set_rule_value(key: String, idx: int, path: String, value: float) -> bool:
	var r := _rule_ref(key, idx)
	if r.is_empty():
		return false
	var ck := "%s#%d" % [key, idx]
	if not _rule_orig.has(ck):
		_rule_orig[ck] = r.duplicate(true)
	if path.begins_with("w."):
		if not (r.get("w") is Dictionary):
			r["w"] = {}
		(r["w"] as Dictionary)[path.substr(2)] = value
	else:
		r[path] = value
	if not rule_changes.has(ck):
		rule_changes[ck] = {}
	rule_changes[ck][path] = value
	return true


# ================================================================ reset / save / load / export

## Restores every move and rule to the registered values.
static func reset_all() -> void:
	Moves.clear_overrides()
	move_changes.clear()
	for ck in _rule_orig:
		var parts := String(ck).split("#")
		var r := _rule_ref(parts[0], int(parts[1]))
		if r.is_empty():
			continue
		var orig: Dictionary = _rule_orig[ck]
		for k in r.keys():
			if not orig.has(k):
				r.erase(k)
		for k in orig:
			r[k] = orig[k]
	_rule_orig.clear()
	rule_changes.clear()


static func save(path: String = PATH) -> int:
	var cf := ConfigFile.new()
	for id in move_changes:
		for p in move_changes[id]:
			cf.set_value("moves", "%s/%s" % [id, p], float(move_changes[id][p]))
	for ck in rule_changes:
		for p in rule_changes[ck]:
			cf.set_value("rules", "%s/%s" % [ck, p], float(rule_changes[ck][p]))
	return cf.save(path)


## Applies a saved file on top of the current values. Returns the number of fields applied (-1: no file).
static func load_file(path: String = PATH) -> int:
	var cf := ConfigFile.new()
	if cf.load(path) != OK:
		return -1
	var n := 0
	if cf.has_section("moves"):
		for k in cf.get_section_keys("moves"):
			var i := String(k).find("/")
			if i > 0 and set_value(String(k).substr(0, i), String(k).substr(i + 1), float(cf.get_value("moves", k))):
				n += 1
	if cf.has_section("rules"):
		for k in cf.get_section_keys("rules"):
			var i2 := String(k).rfind("/")
			if i2 <= 0:
				continue
			var cell := String(k).substr(0, i2)
			var hp := cell.rfind("#")
			if hp > 0 and set_rule_value(cell.substr(0, hp), int(cell.substr(hp + 1)), String(k).substr(i2 + 1), float(cf.get_value("rules", k))):
				n += 1
	return n


## A text diff of everything changed ("move/path: from -> to"), for the clipboard and docs/TUNING_LOG.md.
static func export_text() -> String:
	var lines: Array[String] = ["# Fourfold Lab tuning export"]
	var ids := move_changes.keys()
	ids.sort()
	for id in ids:
		var ps: Array = (move_changes[id] as Dictionary).keys()
		ps.sort()
		for p in ps:
			var orig: Variant = _orig_of(String(id), String(p))
			lines.append("%s.%s: %s -> %s" % [id, p, _fmt(orig), _fmt(move_changes[id][p])])
	var cks := rule_changes.keys()
	cks.sort()
	for ck in cks:
		var ps2: Array = (rule_changes[ck] as Dictionary).keys()
		ps2.sort()
		for p in ps2:
			var o: Variant = (_rule_orig.get(ck, {}) as Dictionary).get(p)
			lines.append("rule %s.%s: %s -> %s" % [ck, p, _fmt(o), _fmt(rule_changes[ck][p])])
	return "\n".join(lines) + "\n"


static func _fmt(v: Variant) -> String:
	if v == null:
		return "-"
	return str(snappedf(float(v), 0.0001))


## Writes the export next to the cfg (user://tuning_export.txt) and returns its text.
static func export_file(path: String = "user://tuning_export.txt") -> String:
	var txt := export_text()
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f != null:
		f.store_string(txt)
	return txt
