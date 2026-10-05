extends WaterKitTest
## The Water column of the counter matrix (docs/MOVESET.md §8.2): every cell of the Water counter classes against its
## reference threat at its reference counter power, the partial / fail bands, outcome names that exist, the matrix
## coverage (every row has an answer in every sub-element column) and the physical resolutions of the headline cells.

const ROWS := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand", "sand_surge", "water", "water_wave",
	"ice", "mist", "steam", "vine", "flame", "blue_fire", "lightning", "blast", "gust", "tornado", "vacuum", "sound"]
## Counter classes of each sub-element column (the move that provides each is in docs/kits/water.md).
const COLUMNS := {
	"Water": ["shield_water", "wave_water", "water_jet", "spray", "grip_water"],
	"Ice": ["wall_ice", "rime", "frost", "freeze", "grip_ice"],
	"Mist": ["screen_steam", "fog", "condense", "grip_vapor"],
	"Plant": ["wall_vine", "grip_vine", "anchor"],
}
## Rows a column deliberately answers by not answering (MOVESET §8.2 "PASS" / "FAIL": evade or switch). They still
## carry a rule cell that says so, so the AI and the Lab matrix can read it; they are listed here for the docs.
const KNOWN_OUTCOMES := ["pass", "block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter", "sink",
	"conduct", "ground", "amplify", "extinguish", "weaken", "bend", "slow", "overwhelm", "clash", "disrupt", "neutralize", "heat", "push",
	"disperse", "water_carry", "water_ridge", "water_freeze", "water_skin", "water_hot_block", "water_dampen", "water_condense_in",
	"water_brittle", "water_feed", "water_drown", "water_melt", "water_sling", "water_quench", "water_burn"]


func _counter_for(ref: Dictionary, ccls: String, actor: ActorState) -> Agent:
	var move := String(ref.get("move", ""))
	var tier := int(ref.get("tier", 0))
	var perfect := bool(ref.get("perfect", false))
	if move == "guard":
		var g := Agent.new()
		g.kind = "guard"
		g.ccls = StringName(ccls)
		g.power = 6.0                  # a 6 kg waterskin shield: 6 kg x 1.0
		g.perfect = perfect
		g.actor = actor
		return g
	var a := Agent.of_move(h.w, actor, move, tier, perfect)
	a.ccls = StringName(ccls)
	return a


func test_every_reference_cell_gives_its_documented_outcome() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	var n := 0
	var bad := []
	for key in WaterRules.CELLS:
		var cell: Dictionary = WaterRules.CELLS[key]
		var ref: Dictionary = cell.ref
		if not ref.has("expect") or String(ref.get("expect", "")) == "":
			continue
		var t := String(cell.threat)
		var c := String(cell.counter)
		var spec: Dictionary = WaterRules.REF.get(t, {"tp": 10.0, "mass": 10.0})
		var tp := float(ref.get("tp", spec.tp))
		var th := threat(h.w, t, tp, float(spec.mass))
		var ctr := _counter_for(ref, c, w)
		var pr := Interactions.predict(h.w, th, ctr)
		n += 1
		if String(pr.outcome) != String(ref.expect):
			bad.append("%s x %s [%s T%d%s] TP %.1f CP %.1f eff %.2f -> %s (%s), expected %s" % [t, c, ref.get("move", ""), int(ref.get("tier", 0)),
				"*" if ref.get("perfect", false) else "", tp, float(pr.cp_eff), float(pr.eff), pr.outcome, pr.band, ref.expect])
	check(bad.is_empty(), "%d reference cells off: %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])
	check(n >= 60, "at least 60 reference cells are checked (%d)" % n)
	note("%d reference cells checked, %d cells registered" % [n, WaterRules.CELLS.size()])


func test_rules_use_known_outcomes_and_all_bands_resolve() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	var bad := []
	for key in WaterRules.CELLS:
		var cell: Dictionary = WaterRules.CELLS[key]
		var r := Interactions.rule(StringName(cell.threat), StringName(cell.counter), -1, {})
		if r.is_empty() or String(r.get("owner", "")) != "water":
			# a tier-restricted cell: look the tiers up
			for tier in cell.tiers:
				r = Interactions.rule(StringName(cell.threat), StringName(cell.counter), int(tier), {})
		for k in ["outcome", "partial", "fail", "perfect", "else"]:
			if r.has(k):
				var o := String(r[k])
				if not KNOWN_OUTCOMES.has(o):
					bad.append("%s: %s '%s'" % [key, k, o])
		for bd in r.get("bands", []):
			if not KNOWN_OUTCOMES.has(String(bd[1])):
				bad.append("%s: band '%s'" % [key, bd[1]])
		if r.has("fallback") and not KNOWN_OUTCOMES.has(String(r.fallback)):
			bad.append("%s: fallback '%s'" % [key, r.fallback])
		# the three bands at ratios 1.2 / 0.7 / 0.3 follow the rule
		var spec: Dictionary = WaterRules.REF.get(String(cell.threat), {"tp": 10.0, "mass": 10.0})
		if r.has("bands") or r.has("when") or r.has("by_form") or r.has("inert"):
			continue
		var eff := float(r.get("eff", 1.0))
		var full_at := float(r.get("full_at", 1.0))
		var partial_at := float(r.get("partial_at", 0.5))
		var got := []
		var ratios := [full_at * 1.2 + 0.01, (full_at + partial_at) * 0.5, partial_at * 0.6]
		if partial_at <= 0.0 or full_at <= partial_at:
			continue
		for ratio in ratios:
			var th := threat(h.w, String(cell.threat), float(spec.tp), float(spec.mass))
			var ctr := Agent.new()
			ctr.kind = "guard"
			ctr.ccls = StringName(cell.counter)
			ctr.power = ratio * float(spec.tp) / maxf(eff, 1e-6)
			ctr.tier = int(cell.tiers[0]) if not (cell.tiers as Array).is_empty() else 0
			ctr.actor = w
			got.append(String(Interactions.predict(h.w, th, ctr).outcome))
		var want := [String(r.get("outcome", "block")), String(r.get("partial", "weaken")), String(r.get("fail", "overwhelm"))]
		if got != want:
			bad.append("%s: bands %s, rule says %s" % [key, got, want])
	check(bad.is_empty(), "%d problems: %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])


func test_every_row_has_an_answer_in_every_sub_element_column() -> void:
	h = SimHarness.new(3)
	var missing := []
	for col in COLUMNS:
		for row in ROWS:
			var found := false
			for c in COLUMNS[col]:
				for tier in [-1, 0, 1, 2, 3]:
					if Interactions.has_rule(StringName(row), StringName(c), tier):
						found = true
			if not found:
				missing.append("%s x %s" % [row, col])
	check(missing.is_empty(), "rows without any cell in a column: %s" % [missing])


func test_cells_never_replace_legacy_or_environment_cells() -> void:
	h = SimHarness.new(3)
	for key in WaterRules.CELLS:
		var cell: Dictionary = WaterRules.CELLS[key]
		for r in Interactions.all_rules().get("%s|%s" % [cell.threat, cell.counter], []):
			if String(r.get("owner", "")) == "water":
				check(not r.get("legacy", false), "%s is not a legacy cell" % key)
	# The legacy lash and the legacy shield cells are untouched.
	check(Interactions.rule(&"stone", &"water_jet", 0).get("id", "") == "legacy_lash", "T0 lash cell is the legacy one")
	check(Interactions.rule(&"stone", &"water_jet", 1).get("id", "") == "legacy_lash", "T1 lash cell is the legacy one")
	check(Interactions.rule(&"stone", &"water_jet", 2).get("owner", "") == "water", "T2 jet cells are ours")
	check(Interactions.rule(&"flame", &"shield_water", 0).get("id", "") == "legacy_flare_water", "flame x water shield is the legacy steam cell")
	check(Interactions.rule(&"lava_wave", &"puddle", 0).get("id", "") == "legacy_quench", "puddle quench stays legacy")


# ---------------------------------------------------------------- the headline physical resolutions

func test_a_thrown_stone_is_blocked_by_an_ice_wall_at_cp_22() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	var r := h.actor("R", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	h.step(20)
	var wall := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.WALL, 50.0, Vector3(0, 0, 2.5), "test")
	h.w.mass_ledger.moisture_taken += 50.0
	WaterUtil.freeze_body(h.w, wall)
	wall.tag = &"ice"
	wall.hardness = 0.44
	wall.wall_half = Vector3(1.2, 0.85, 0.3)
	wall.wall_rise = 1.0
	wall.static_body = true
	wall.props["standing"] = 99.0
	wall.touch(w.id, "wall", 0)
	var stone := h.launch_at(w, "stone", 20.0, 17.0, Sim.AMBIENT_C, "", r, 7.0)
	var t := h.until(func(): return h.events("interaction").any(func(e): return e.counter == "wall_ice"), 80)
	check(t > 0, "the stone meets the wall")
	var ev := h.events("interaction").filter(func(e): return e.counter == "wall_ice")
	check(not ev.is_empty() and ev[0].outcome == "block" and ev[0].threat == "stone", "block: %s" % [ev[0] if ev.size() else "none"])
	check(wall.alive and w.health == 100.0, "wall holds, fighter untouched")
	check(float(ev[0].cp) >= 22.0 - 0.01, "CP 22 (%s)" % ev[0].cp)


func test_a_steam_screen_never_stops_a_boulder_and_a_big_wave_beats_a_small_screen() -> void:
	h = SimHarness.new(3)
	var w := h.actor("W", Vector3(0, 0, 4), 0, {}, Sim.Element.WATER)
	# Counter strength scales with the threat: a boulder (TP 110) against a Steam Screen (CP 10-18) fails or passes.
	var boulder := threat(h.w, "boulder", 110.0, 200.0)
	var sc := Interactions.predict(h.w, boulder, Agent.of_move(h.w, w, "steam_screen", 3, false))
	check(sc.band == "fail" or sc.outcome == "pass", "steam screen vs boulder: %s / %s" % [sc.band, sc.outcome])
	var wall := Interactions.predict(h.w, boulder, _counter_for({"move": "ice_wall", "tier": 3}, "wall_ice", w))
	check(wall.outcome == "overwhelm", "an 80 kg ice wall vs a boulder: crushed (%s, ratio %.2f)" % [wall.outcome, wall.ratio])
	# The wave: T0 18 vs a 27 PU lava wave is partial, T1 24 is full (eff 1.5).
	var lava := threat(h.w, "lava_wave", 27.3, 20.0)
	var t0 := Interactions.predict(h.w, lava, Agent.of_move(h.w, w, "tidal_rush", 0, false))
	var t1 := Interactions.predict(h.w, lava, Agent.of_move(h.w, w, "tidal_rush", 1, false))
	check(t0.band == "partial" and t1.band == "full", "Tidal Rush T0 partial (%.2f), T1 full (%.2f) vs a 20 kg lava wave" % [t0.ratio, t1.ratio])
