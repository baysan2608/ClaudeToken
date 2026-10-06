extends SceneTree
## Fourfold UE port - Godot -> JSON data exporter (architect tool; owned by the `core` stream afterwards).
##
## Runs the Godot game's registries headless and writes the authoritative data the C++ port loads:
##   moves.json        every move def (BASE_DEFS + kit registrations), bindings, registry constants
##   rules.json        every Interactions rule (registration order), outcome handlers, tag classes, channel hooks
##   hooks.json        body ticks, zone effects, tech previews, status specs (code hooks are exported by NAME)
##   sim.json          Sim / Materials constants, arena (ArenaMap.make_lab), class catalogues
##   scenarios.json    Scenarios.LIST + spar choices
##   lab.json          spawner catalogue (+ param specs / defaults), combos, AI presets
##   move_index.json   compact per-move summary for presentation streams (anim / fx / sfx keys)
##   golden_matrix.json  MatrixQuery.predict for every spawner threat x counter x tier x perfect (conformance oracle)
##
## Usage (repo root):
##   /home/user/tools/godot --headless --path game -s "$PWD/unreal/Tools/godot_export/export_core_data.gd" -- \
##       --out="$PWD/unreal/Source/FourfoldCore/Data" [--no-golden]
##
## Encoding (see unreal/docs/ARCHITECTURE.md "Data formats"):
##   int -> JSON integer; float -> JSON number that always contains '.' or 'e' (round-trip repr);
##   Vector2/3 -> {"$v2":[x,y]} / {"$v3":[x,y,z]}; Color -> {"$color":[r,g,b,a]}; PackedVector3Array -> {"$v3a":[[..],..]};
##   Callable -> {"$fn":"Class.method"} (lambdas: {"$fn":"Class.<lambda>","script":"res://..."});
##   Dictionary with any non-String key -> {"$map":[[key,value],...]} (insertion order kept);
##   StringName -> plain string; inf/nan -> {"$f":"inf"|"-inf"|"nan"}. Object keys keep Godot insertion order.

var _lambdas := 0
var _fns := {}


func _init() -> void:
	var out_dir := "res://../unreal/Source/FourfoldCore/Data"
	var golden := true
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out_dir = a.substr(6)
		elif a == "--no-golden":
			golden = false
	Moves.ensure()
	DirAccess.make_dir_recursive_absolute(out_dir)
	_write(out_dir, "moves.json", _moves())
	_write(out_dir, "rules.json", _rules())
	_write(out_dir, "hooks.json", _hooks())
	_write(out_dir, "sim.json", _sim())
	_write(out_dir, "scenarios.json", _scenarios())
	_write(out_dir, "lab.json", _lab())
	_write(out_dir, "move_index.json", _move_index(), 3)
	if golden:
		_write(out_dir, "golden_matrix.json", _golden(), 2)
	print("export: lambdas=%d distinct_fns=%d" % [_lambdas, _fns.size()])
	quit(0)


# ------------------------------------------------------------------ encoding

func _fn_name(c: Callable) -> Dictionary:
	var o = c.get_object()
	var cls := "?"
	var path := ""
	if o is Script:
		cls = String((o as Script).get_global_name())
		path = (o as Script).resource_path
	elif o != null and o.get_script() != null:
		cls = String((o.get_script() as Script).get_global_name())
		path = (o.get_script() as Script).resource_path
	var m := String(c.get_method())
	if m == "" or m.begins_with("<"):
		_lambdas += 1
		var d := {"$fn": "%s.<lambda>" % cls, "script": path}
		_fns[d["$fn"] + "#" + str(_lambdas)] = true
		return d
	_fns["%s.%s" % [cls, m]] = true
	return {"$fn": "%s.%s" % [cls, m]}


func enc(v: Variant) -> Variant:
	match typeof(v):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_STRING:
			return v
		TYPE_FLOAT:
			var f := float(v)
			if is_inf(f):
				return {"$f": "inf" if f > 0.0 else "-inf"}
			if is_nan(f):
				return {"$f": "nan"}
			return f
		TYPE_STRING_NAME:
			return String(v)
		TYPE_VECTOR2:
			return {"$v2": [v.x, v.y]}
		TYPE_VECTOR3:
			return {"$v3": [v.x, v.y, v.z]}
		TYPE_COLOR:
			return {"$color": [v.r, v.g, v.b, v.a]}
		TYPE_PACKED_VECTOR3_ARRAY:
			var arr := []
			for p in v:
				arr.append([p.x, p.y, p.z])
			return {"$v3a": arr}
		TYPE_CALLABLE:
			return _fn_name(v)
		TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY, TYPE_PACKED_FLOAT32_ARRAY, TYPE_PACKED_FLOAT64_ARRAY, TYPE_PACKED_INT32_ARRAY, TYPE_PACKED_INT64_ARRAY:
			var a := []
			for x in v:
				a.append(enc(x))
			return a
		TYPE_DICTIONARY:
			var all_str := true
			for k in v:
				if typeof(k) != TYPE_STRING and typeof(k) != TYPE_STRING_NAME:
					all_str = false
					break
			if all_str:
				var o := {}
				for k in v:
					o[String(k)] = enc(v[k])
				return o
			var pairs := []
			for k in v:
				pairs.append([enc(k), enc(v[k])])
			return {"$map": pairs}
		TYPE_OBJECT:
			if v == null:
				return null
			if v is Script:
				return {"$script": String((v as Script).get_global_name())}
			return {"$object": str(v)}
	return {"$unsupported": type_string(typeof(v)), "str": str(v)}


## JSON writer: floats always carry '.' or 'e' (round-trip var_to_str); stable key order (insertion).
func js(v: Variant, ind: String = "", depth: int = 0, inline_from: int = 7) -> String:
	match typeof(v):
		TYPE_NIL:
			return "null"
		TYPE_BOOL:
			return "true" if v else "false"
		TYPE_INT:
			return str(v)
		TYPE_FLOAT:
			var s := var_to_str(float(v))
			if not (s.contains(".") or s.contains("e") or s.contains("E")):
				s += ".0"
			return s
		TYPE_STRING:
			return JSON.stringify(v)
		TYPE_ARRAY:
			if v.is_empty():
				return "[]"
			var simple := true
			for x in v:
				if typeof(x) == TYPE_ARRAY or typeof(x) == TYPE_DICTIONARY:
					simple = false
					break
			var parts: PackedStringArray = []
			for x in v:
				parts.append(js(x, ind + "  ", depth + 1, inline_from))
			if simple or depth >= inline_from:
				return "[" + ", ".join(parts) + "]"
			return "[\n" + ind + "  " + (",\n" + ind + "  ").join(parts) + "\n" + ind + "]"
		TYPE_DICTIONARY:
			if v.is_empty():
				return "{}"
			var parts2: PackedStringArray = []
			for k in v:
				parts2.append(JSON.stringify(String(k)) + ": " + js(v[k], ind + "  ", depth + 1, inline_from))
			if depth >= inline_from:
				return "{" + ", ".join(parts2) + "}"
			return "{\n" + ind + "  " + (",\n" + ind + "  ").join(parts2) + "\n" + ind + "}"
	return JSON.stringify(str(v))


func _write(dir: String, name: String, data: Variant, inline_from: int = 7) -> void:
	var path := dir.path_join(name)
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		push_error("cannot write " + path)
		return
	f.store_string(js(data, "", 0, inline_from) + "\n")
	f.close()
	print("wrote ", path)


# ------------------------------------------------------------------ sections

func _moves() -> Dictionary:
	var defs := {}
	for id in Moves.DEFS:
		defs[id] = enc(Moves.DEFS[id])
	return {
		"schema": "fourfold.moves/1",
		"godot_version": Engine.get_version_info().string,
		"constants": {"HOLD_THRESHOLD": Moves.HOLD_THRESHOLD, "BUFFER_TIME": Moves.BUFFER_TIME,
			"PERFECT_WINDOW": Moves.PERFECT_WINDOW, "GUARD_MASH_LOCK": Moves.GUARD_MASH_LOCK},
		"base_ids": Moves.BASE_DEFS.keys(),
		"registered": Array(Moves.REGISTERED),
		"legacy_bindings": enc(Moves.LEGACY_BINDINGS),
		"def_defaults": enc(Moves.DEF_DEFAULTS),
		"bindings": enc(Moves.BINDINGS),
		"techniques": enc(Moves.TECHNIQUES),
		"defs": defs,
	}


func _rules() -> Dictionary:
	var rules := {}
	var src: Dictionary = Interactions._rules
	for k in src:
		var arr := []
		for r in src[k]:
			arr.append(enc(r))
		rules[k] = arr
	var handlers := {}
	for k in Interactions._handlers:
		handlers[String(k)] = enc(Interactions._handlers[k])
	var chan := {}
	for k in Interactions._channel_hooks:
		chan[String(k)] = enc(Interactions._channel_hooks[k])
	return {
		"schema": "fourfold.rules/1",
		"note": "rules[key] arrays are in registration order; key = 'threat|counter' (see Interactions._rk). Rules with `tiers` are tried first.",
		"constants": {"PLAIN_GUARD_CP": Interactions.PLAIN_GUARD_CP, "WIND_GUARD_CP": Interactions.WIND_GUARD_CP,
			"ENV_CP": Interactions.ENV_CP, "HU_PER_PU": Interactions.HU_PER_PU, "IX_EVENT_TICKS": Interactions.IX_EVENT_TICKS,
			"CONTACT_TICKS": Interactions.CONTACT_TICKS, "PARTIAL_ONCE": Interactions.PARTIAL_ONCE},
		"threat_classes": enc(Interactions.THREAT_CLASSES),
		"volume_classes": enc(Interactions.VOLUME_CLASSES),
		"counter_classes": enc(Interactions.COUNTER_CLASSES),
		"threat_family": enc(Interactions.THREAT_FAMILY),
		"counter_family": enc(Interactions.COUNTER_FAMILY),
		"kind_class": enc(Interactions.KIND_CLASS),
		"class_channel": enc(Interactions.CLASS_CHANNEL),
		"zone_counter": enc(Interactions.ZONE_COUNTER),
		"default_rule": enc(Interactions.DEFAULT_RULE),
		"clash_rule": enc(Interactions.CLASH_RULE),
		"pass_rule": enc(Interactions.PASS_RULE),
		"tag_threat": enc(Interactions._tag_threat),
		"tag_counter": enc(Interactions._tag_counter),
		"channel_hooks": chan,
		"outcome_handlers": handlers,
		"rules": rules,
	}


func _hooks() -> Dictionary:
	var bt := {}
	for k in CombatWorld._body_ticks:
		bt[String(k)] = enc(CombatWorld._body_ticks[k])
	var ze := {}
	for k in CombatWorld._zone_effects:
		ze[String(k)] = enc(CombatWorld._zone_effects[k])
	var tp := {}
	for k in CombatWorld._tech_previews:
		tp[String(k)] = enc(CombatWorld._tech_previews[k])
	return {
		"schema": "fourfold.hooks/1",
		"body_ticks": bt,
		"zone_effects": ze,
		"tech_previews": tp,
		"status_specs": enc(Status.SPECS),
	}


func _sim() -> Dictionary:
	var consts := {}
	for c in ClassDB.class_get_integer_constant_list("Object"):
		pass
	var sim_script: Script = load("res://core/sim.gd")
	var cmap: Dictionary = sim_script.get_script_constant_map()
	for k in cmap:
		consts[k] = enc(cmap[k])
	var mats: Script = load("res://core/materials.gd")
	var mmap: Dictionary = mats.get_script_constant_map()
	var mat_consts := {}
	for k in mmap:
		mat_consts[k] = enc(mmap[k])
	var cw: Script = load("res://core/combat_world.gd")
	var cw_consts := {}
	var cwm: Dictionary = cw.get_script_constant_map()
	for k in cwm:
		if typeof(cwm[k]) in [TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_ARRAY, TYPE_DICTIONARY, TYPE_BOOL]:
			cw_consts[k] = enc(cwm[k])
	var arena := ArenaMap.make_lab()
	var solids := []
	for s in arena.solids:
		solids.append(enc(s))
	return {
		"schema": "fourfold.sim/1",
		"sim": consts,
		"materials": mat_consts,
		"combat_world": cw_consts,
		"arena_lab": {"half_size": arena.half_size, "solids": solids, "pool_min": enc(arena.pool_min), "pool_max": enc(arena.pool_max),
			"pool_floor": arena.pool_floor, "pool_level": arena.pool_level, "metal_min": enc(arena.metal_min),
			"metal_max": enc(arena.metal_max), "metal_top": arena.metal_top, "player_spawn": enc(arena.player_spawn),
			"opponent_spawn": enc(arena.opponent_spawn)},
	}


func _scenarios() -> Dictionary:
	return {
		"schema": "fourfold.scenarios/1",
		"list": enc(Scenarios.LIST),
		"spar_difficulties": enc(Scenarios.SPAR_DIFFICULTIES),
		"spar_difficulty_labels": enc(Scenarios.SPAR_DIFFICULTY_LABELS),
		"spar_kits": enc(Scenarios.SPAR_KITS),
		"spar_kit_labels": enc(Scenarios.SPAR_KIT_LABELS),
	}


func _lab() -> Dictionary:
	var entries := []
	for e in SpawnCatalog.entries():
		var x: Dictionary = enc(e)
		x["param_specs"] = enc(SpawnCatalog.param_specs(e))
		x["defaults"] = enc(SpawnCatalog.defaults(e))
		x["describe_default"] = SpawnCatalog.describe(e, SpawnCatalog.defaults(e))
		entries.append(x)
	var presets := {}
	var ps: Script = load("res://actors/ai_presets.gd")
	var pmap: Dictionary = ps.get_script_constant_map()
	for k in pmap:
		presets[k] = enc(pmap[k])
	var lab_consts := {}
	var ls: Script = load("res://ui/lab/lab_session.gd")
	var lmap: Dictionary = ls.get_script_constant_map()
	for k in lmap:
		lab_consts[k] = enc(lmap[k])
	return {
		"schema": "fourfold.lab/1",
		"spawn_groups": enc(SpawnCatalog.GROUPS),
		"spawn_entries": entries,
		"combos": enc(LabCombos.all()),
		"matrix_counters": enc(MatrixQuery.counters()),
		"ai_presets": presets,
		"lab_session": lab_consts,
	}


func _move_index() -> Dictionary:
	var out := []
	for e in 4:
		for s in 4:
			for slot in Sim.SLOTS:
				var id := Moves.resolve(e, s, slot)
				if id == "" or not Moves.DEFS.has(id):
					continue
				var d: Dictionary = Moves.DEFS[id]
				var tiers := []
				for t in ["t1", "t2", "t3"]:
					if (d.get("tiers", {}) as Dictionary).has(t):
						tiers.append(t)
				var anims := {}
				for k in d:
					if String(k).begins_with("anim"):
						anims[k] = d[k]
				out.append(enc({"element": e, "sub": s, "slot": slot, "id": id, "name": d.get("name", id),
					"desc": d.get("desc", ""), "module": d.get("module", ""), "verb": d.get("verb", ""),
					"startup": d.get("startup", 0.0), "active": d.get("active", 0.0), "recovery": d.get("recovery", 0.0),
					"heavy_min": d.get("heavy_min", null), "tier_times": d.get("tier_times", null), "tiers": tiers,
					"max_tier": Charge.max_tier(d), "anims": anims, "fx": d.get("fx", {}), "sfx": d.get("sfx", {}),
					"counter": d.get("counter", {}), "threat": d.get("threat", {}), "ai": d.get("ai", {}),
					"mat": d.get("mat", ""), "tag": d.get("tag", ""), "legacy": Moves.BASE_DEFS.has(id),
					"has_hooks": d.has("hook_execute") or d.has("hook_tick") or d.has("hook_impact")}))
	return {"schema": "fourfold.move_index/1", "element_names": enc(Sim.ELEMENT_NAMES), "sub_names": enc(Sim.SUB_NAMES),
		"slots": enc(Sim.SLOTS), "moves": out}


func _golden() -> Dictionary:
	var rows := []
	var counters := MatrixQuery.counters()
	var n := 0
	for e in SpawnCatalog.entries():
		if bool(e.get("inert_only", false)):
			continue
		var params := SpawnCatalog.defaults(e)
		var t := MatrixQuery.make_threat(String(e.id), params)
		if t.is_empty():
			rows.append({"threat": e.id, "ok": false})
			continue
		for c in counters:
			var max_t := int(c.max_tier)
			var perfects := [false]
			if String(c.slot) == "guard" or String(c.kind) == "legacy":
				perfects = [false, true]
			for tier in range(0, max_t + 1):
				for pf in perfects:
					var r := MatrixQuery.predict(String(e.id), params, String(c.id), tier, pf)
					n += 1
					if not r.ok:
						continue
					rows.append({"threat": e.id, "params": enc(params), "counter": c.id, "tier": tier, "perfect": pf,
						"tp": r.tp, "cp": r.cp, "cp_eff": r.cp_eff, "ratio": r.ratio, "band": r.band, "outcome": r.outcome,
						"rule_id": r.rule_id, "to": r.to, "threat_cls": r.threat_cls, "counter_cls": r.counter_cls})
	print("golden: %d predictions, %d rows" % [n, rows.size()])
	return {"schema": "fourfold.golden_matrix/1",
		"note": "MatrixQuery.predict(entry, defaults, counter, tier, perfect) from the Godot build; floats are exact Godot values.",
		"rows": rows}
