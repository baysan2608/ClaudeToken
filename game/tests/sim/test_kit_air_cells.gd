extends AirKitTest
## The Air column of the counter matrix (docs/MOVESET.md §8.4): every cell of the Air counter classes against its reference
## threat at its reference counter power, the partial / fail bands, outcome names that exist, the matrix coverage (every row
## has an answer in every Air column) and the guarantees: legacy cells are never replaced, T0 / T1 palm gust cells stay the core's.

const ROWS := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand", "sand_surge", "water", "water_wave",
	"ice", "mist", "steam", "vine", "flame", "blue_fire", "lightning", "blast", "gust", "tornado", "vacuum", "sound"]
const KNOWN_OUTCOMES := ["pass", "block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter", "sink",
	"conduct", "ground", "amplify", "extinguish", "weaken", "bend", "slow", "overwhelm", "clash", "disrupt", "neutralize", "heat", "push",
	"disperse", "air_cool", "air_cut", "air_split", "air_shrink", "air_shatter", "air_infuse", "air_catch", "air_slow_bend", "air_spatter",
	"air_contest", "air_compress", "air_snuff", "air_spit", "air_pop", "air_still"]


func _counter_for(ref: Dictionary, ccls: String, actor: ActorState) -> Agent:
	var move := String(ref.get("move", ""))
	var tier := int(ref.get("tier", 0))
	var perfect := bool(ref.get("perfect", false))
	if move == "wind_guard":
		var g := Agent.new()
		g.kind = "guard"
		g.ccls = &"guard_wind"
		g.power = Interactions.WIND_GUARD_CP
		g.perfect = perfect
		g.actor = actor
		return g
	var a := Agent.of_move(h.w, actor, move, tier, perfect)
	a.ccls = StringName(ccls)
	return a


func test_every_reference_cell_gives_its_documented_outcome() -> void:
	h = SimHarness.new(3)
	var w := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.AIR)
	var n := 0
	var bad := []
	for key in AirRules.CELLS:
		var cell: Dictionary = AirRules.CELLS[key]
		var ref: Dictionary = cell.ref
		if not ref.has("expect") or String(ref.get("expect", "")) == "":
			continue
		var t := String(ref.get("threat", cell.threat))
		var c := String(cell.counter)
		var spec: Dictionary = AirRules.REF.get(t, {"tp": 10.0, "mass": 10.0})
		var tp := float(ref.get("tp", spec.tp))
		var th := threat(h.w, t, tp, float(ref.get("mass", spec.mass)), "H" if ["flame", "blue_fire", "ember"].has(t) else ("E" if t == "lightning" else ("P" if ["blast", "gust", "tornado", "vacuum", "sound"].has(t) else "K")))
		var ctr := _counter_for(ref, c, w)
		var pr := Interactions.predict(h.w, th, ctr)
		n += 1
		if String(pr.outcome) != String(ref.expect):
			bad.append("%s x %s [%s T%d%s] TP %.1f CP %.1f eff %.2f -> %s (%s), expected %s" % [t, c, ref.get("move", ""), int(ref.get("tier", 0)),
				"*" if ref.get("perfect", false) else "", tp, float(pr.cp_eff), float(pr.eff), pr.outcome, pr.band, ref.expect])
	check(bad.is_empty(), "%d reference cells off: %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])
	check(n >= 80, "at least 80 reference cells are checked (%d)" % n)
	note("%d reference cells checked, %d cells registered" % [n, AirRules.CELLS.size()])


func test_rules_use_known_outcomes_and_simple_cells_follow_their_bands() -> void:
	h = SimHarness.new(3)
	var w := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.AIR)
	var bad := []
	for key in AirRules.CELLS:
		var cell: Dictionary = AirRules.CELLS[key]
		var tiers: Array = cell.tiers
		var probe := [-1] if tiers.is_empty() else tiers
		var r := Interactions.rule(StringName(cell.threat), StringName(cell.counter), int(probe[0]), {})
		for k in ["outcome", "partial", "fail", "perfect", "else", "inert", "inert_else", "fallback"]:
			if r.has(k) and not KNOWN_OUTCOMES.has(String(r[k])):
				bad.append("%s: %s '%s'" % [key, k, r[k]])
		for bd in r.get("bands", []):
			if not KNOWN_OUTCOMES.has(String(bd[1])):
				bad.append("%s: band '%s'" % [key, bd[1]])
		if r.has("bands") or r.has("when") or r.has("by_form") or r.has("inert"):
			continue
		var spec: Dictionary = AirRules.REF.get(String(cell.threat), {"tp": 10.0, "mass": 10.0})
		var eff := float(r.get("eff", 1.0))
		var full_at := float(r.get("full_at", 1.0))
		var partial_at := float(r.get("partial_at", 0.5))
		if partial_at <= 0.0 or full_at <= partial_at:
			continue
		var got := []
		for ratio in [full_at * 1.2 + 0.01, (full_at + partial_at) * 0.5, partial_at * 0.6]:
			var th := threat(h.w, String(cell.threat), float(spec.tp), float(spec.mass))
			var ctr := Agent.new()
			ctr.kind = "guard"
			ctr.ccls = StringName(cell.counter)
			ctr.power = ratio * float(spec.tp) / maxf(eff, 1e-6)
			ctr.tier = int(probe[0]) if int(probe[0]) >= 0 else 0
			ctr.actor = w
			got.append(String(Interactions.predict(h.w, th, ctr).outcome))
		var want := [String(r.get("outcome", "block")), String(r.get("partial", "weaken")), String(r.get("fail", "overwhelm"))]
		if got != want:
			bad.append("%s: bands %s, rule says %s" % [key, got, want])
	check(bad.is_empty(), "%d problems: %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])


func test_every_row_has_an_answer_in_every_air_column() -> void:
	h = SimHarness.new(3)
	var missing := []
	for col in AirRules.COLUMNS:
		for row in ROWS:
			var found := false
			for c in AirRules.COLUMNS[col]:
				for tier in [-1, 0, 1, 2, 3]:
					if Interactions.has_rule(StringName(row), StringName(c), tier):
						found = true
			if not found:
				missing.append("%s x %s" % [row, col])
	check(missing.is_empty(), "rows without any cell in a column: %s" % [missing])
	# and the kit's own cells (not just legacy wildcards) cover the rows of its main classes
	for pair in [["gust", ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand_cloud", "water", "water_wave", "ice", "mist", "steam", "flame", "tornado", "vacuum", "sound"]],
			["tornado", ROWS], ["bubble_null", ROWS], ["vacuum_well", ROWS], ["sound", ROWS]]:
		for row in pair[1]:
			var own := false
			for tier in [-1, 0, 1, 2, 3]:
				var rr := Interactions.rule(StringName(row), StringName(pair[0]), tier, {})
				if String(rr.get("owner", "")) == "air":
					own = true
			if not own:
				missing.append("own cell %s x %s" % [row, pair[0]])
	check(missing.is_empty(), "cells missing: %s" % [missing])


func test_cells_never_replace_legacy_cells() -> void:
	h = SimHarness.new(3)
	for key in AirRules.CELLS:
		var cell: Dictionary = AirRules.CELLS[key]
		for r in Interactions.all_rules().get("%s|%s" % [cell.threat, cell.counter], []):
			if String(r.get("owner", "")) == "air":
				check(not r.get("legacy", false), "%s is not a legacy cell" % key)
	# T0 / T1 palm gust and the legacy Wind Guard cells are the core's
	check(Interactions.rule(&"stone", &"gust", 0).get("id", "") == "legacy_gust", "T0 palm gust x stone is the legacy cell")
	check(Interactions.rule(&"stone", &"gust", 1).get("id", "") == "legacy_gust", "T1 cyclone x stone is the legacy cell")
	check(Interactions.rule(&"lava_wave", &"gust", 1).get("id", "") == "legacy_gust", "T1 x lava wave is the legacy cell (waves untouched)")
	check(Interactions.rule(&"lava_wave", &"gust", 2).get("owner", "") == "air", "T2 x lava wave is the kit's")
	check(Interactions.rule(&"flame", &"guard_wind", 0).get("id", "") == "legacy_air_vs_flare", "Wind Guard x flame stays the legacy cell")
	check(Interactions.rule(&"stone", &"guard_wind", 0).get("id", "") == "wind_guard_light", "Wind Guard x stone stays the core's power rule")
	check(Interactions.rule(&"ember", &"guard_wind", 0).get("owner", "") == "air", "Wind Guard x ember uses the fire bands")


func test_the_kit_registers_a_def_for_every_air_slot_of_every_sub_element() -> void:
	Moves.ensure()
	for sub in 4:
		for slot in Sim.SLOTS:
			var id := Moves.resolve(3, sub, slot)
			check(id != "", "Air/%s %s bound" % [Sim.SUB_NAMES[3][sub], slot])
			if id != "":
				var d: Dictionary = Moves.DEFS[id]
				for k in ["name", "desc", "ai"]:
					if sub > 0 or not ["air_attack", "air_tech", "air_dash", "guard"].has(id):
						check(d.has(k), "%s has %s" % [id, k])
	var n := 0
	for id in Moves.REGISTERED:
		if int(Moves.DEFS[id].get("element", -1)) == 3:
			n += 1
	check(n >= 37, "at least 37 Air moves registered (%d)" % n)
