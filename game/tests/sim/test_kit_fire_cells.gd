extends FireKitTest
## The Fire column of the counter matrix (docs/MOVESET.md §8.3): every cell of the Fire counter classes against its
## reference threat at its reference counter power (full and partial bands), the coverage of every row in every
## sub-element column, the legacy cells kept untouched, and the counter rule scaling with the threat (holding longer /
## bringing more power changes the answer).

const ROWS := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "sand", "sand_surge", "water", "water_wave",
	"ice", "mist", "steam", "vine", "flame", "blue_fire", "lightning", "blast", "gust", "tornado", "vacuum", "sound"]
## Counter classes of each sub-element column (docs/kits/fire.md lists the move behind each).
const COLUMNS := {
	"Flame": ["aura_flame", "flame", "fire_field", "fireball", "heat_grip", "draw_heat", "heat_ranged"],
	"Blue": ["aura_blue", "blue_fire", "corona", "magma_rift", "heat_ranged"],
	"Lightning": ["ward_static", "lightning", "ground_current", "static_field"],
	"Combustion": ["guard_blast", "blast", "ember"],
}


func _counter_for(ref: Dictionary, ccls: String, actor: ActorState) -> Agent:
	var move := String(ref.get("move", ""))
	var a := Agent.of_move(h.w, actor, move, int(ref.get("tier", 0)), bool(ref.get("perfect", false)))
	a.ccls = StringName(ccls)
	if a.power < 0.0:
		a.power = 10.0
	return a


func test_every_reference_cell_gives_its_documented_outcome() -> void:
	h = SimHarness.new(3)
	var f := h.actor("F", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)
	var n := 0
	var bad := []
	for key in FireRules.CELLS:
		var cell: Dictionary = FireRules.CELLS[key]
		var ref: Dictionary = cell.ref
		if String(ref.get("expect", "")) == "":
			continue
		var t := String(cell.threat)
		var c := String(cell.counter)
		var spec: Dictionary = FireRules.REF.get(t, {"tp": 10.0, "mass": 10.0})
		var tp := float(ref.get("tp", spec.tp))
		var ch := "K"
		match t:
			"flame", "blue_fire", "fire_field", "ember":
				ch = "H"
			"lightning":
				ch = "E"
			"blast", "gust", "tornado", "vacuum", "sound":
				ch = "P"
		var th := threat(h.w, t, tp, float(spec.mass), ch)
		var ctr := _counter_for(ref, c, f)
		var pr := Interactions.predict(h.w, th, ctr)
		n += 1
		if String(pr.outcome) != String(ref.expect):
			bad.append("%s x %s [%s T%d%s] TP %.1f CP %.1f eff %.2f -> %s (%s), expected %s" % [t, c, ref.get("move", ""), int(ref.get("tier", 0)),
				"*" if ref.get("perfect", false) else "", tp, float(pr.cp_eff), float(pr.eff), pr.outcome, pr.band, ref.expect])
	check(bad.is_empty(), "%d reference cells off:\n         %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])
	check(n >= 90, "at least 90 reference cells are checked (%d)" % n)
	note("%d reference cells checked, %d cells registered" % [n, FireRules.CELLS.size()])


func test_partial_and_fail_bands_follow_each_rule() -> void:
	h = SimHarness.new(3)
	var f := h.actor("F", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)
	var bad := []
	var checked := 0
	for key in FireRules.CELLS:
		var cell: Dictionary = FireRules.CELLS[key]
		var tiers: Array = cell.tiers
		var r := Interactions.rule(StringName(cell.threat), StringName(cell.counter), int(tiers[0]) if not tiers.is_empty() else 0, {})
		if r.is_empty() or r.has("bands") or r.has("when") or r.has("by_form"):
			continue
		var full_at := float(r.get("full_at", 1.0))
		var partial_at := float(r.get("partial_at", 0.5))
		if partial_at <= 0.0 or full_at <= partial_at:
			continue
		var eff := float(r.get("eff", 1.0))
		var spec: Dictionary = FireRules.REF.get(String(cell.threat), {"tp": 10.0, "mass": 10.0})
		var got := []
		for ratio in [full_at * 1.2 + 0.01, (full_at + partial_at) * 0.5, partial_at * 0.6]:
			var th := threat(h.w, String(cell.threat), float(spec.tp), float(spec.mass))
			var ctr := Agent.new()
			ctr.kind = "move"
			ctr.ccls = StringName(cell.counter)
			ctr.power = ratio * float(spec.tp) / maxf(eff, 1e-6)
			ctr.tier = int(tiers[0]) if not tiers.is_empty() else 0
			ctr.actor = f
			got.append(String(Interactions.predict(h.w, th, ctr).outcome))
		var want := [String(r.get("outcome", "block")), String(r.get("partial", "weaken")), String(r.get("fail", "overwhelm"))]
		checked += 1
		if got != want:
			bad.append("%s: bands %s, rule says %s" % [key, got, want])
	check(bad.is_empty(), "%d problems: %s" % [bad.size(), "\n         ".join(PackedStringArray(bad))])
	check(checked >= 25, "partial bands checked on %d cells" % checked)


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


func test_cells_never_replace_legacy_cells() -> void:
	h = SimHarness.new(3)
	for key in FireRules.CELLS:
		var cell: Dictionary = FireRules.CELLS[key]
		for r in Interactions.all_rules().get("%s|%s" % [cell.threat, cell.counter], []):
			if String(r.get("owner", "")) == "fire":
				check(not r.get("legacy", false), "%s is not a legacy cell" % key)
	check(Interactions.rule(&"flame", &"aura_flame", 0).get("id", "") == "legacy_heat_sink", "Heat Sink stays the legacy cell")
	check(Interactions.rule(&"lightning", &"aura_flame", 0).get("id", "") == "legacy_redirect_current", "Return Current stays legacy")
	check(Interactions.rule(&"magma", &"aura_flame", 0).get("legacy", false), "magma x Flame Guard stays legacy (the extension runs before contact)")
	check(Interactions.rule(&"stone", &"heat_grip", 0).get("id", "") == "legacy_heat", "magma grip legality stays legacy")
	check(Interactions.rule(&"water", &"flame", 0).get("id", "") == "legacy_flare_water", "flare x water stays the legacy steam cell")


## Counter strength scales with the threat: the same answer works on a small threat and fails on a big one; more
## tier (holding longer) turns the band.
func test_counter_power_scales_with_threat_and_tier() -> void:
	h = SimHarness.new(3)
	var f := h.actor("F", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)
	# Reactive Blast (18): a 20 kg stone (17) is deflected, a 45 kg one (31.5) is only blocked.
	var light := Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), Agent.of_move(h.w, f, "reactive_blast", 0, false))
	var heavy := Interactions.predict(h.w, threat(h.w, "stone_heavy", 31.5, 45.0), Agent.of_move(h.w, f, "reactive_blast", 0, false))
	check(String(light.outcome) == "fire_reactive" and String(heavy.outcome) == "block", "Reactive Blast: light %s, heavy %s" % [light.outcome, heavy.outcome])
	# Detonation by tier vs a tornado (30): Pop T1 passes, Detonation T3 disrupts it.
	var t1 := Interactions.predict(h.w, threat(h.w, "tornado", 30.0, 0.0, "P"), Agent.of_move(h.w, f, "pop", 1, false))
	var t3 := Interactions.predict(h.w, threat(h.w, "tornado", 30.0, 0.0, "P"), Agent.of_move(h.w, f, "pop", 3, false))
	check(String(t1.outcome) == "pass" and String(t3.outcome) == "fire_disrupt_zone", "tornado: T1 %s, T3 %s" % [t1.outcome, t3.outcome])
	# Bolts vs a stone wall (CP 30): Bolt T1 (24) grounds, Storm Bolt T2 (36) shatters and continues with 36 - 15 = 21.
	var wall := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, 120.0, Vector3(0, 0, 0), "t")
	var b1 := threat(h.w, "lightning", 24.0, 0.0, "E")
	var b2 := threat(h.w, "lightning", 36.0, 0.0, "E")
	var p1 := Interactions.predict(h.w, b1, Agent.of_body(h.w, wall))
	var p2 := Interactions.predict(h.w, b2, Agent.of_body(h.w, wall))
	check(String(p1.outcome) == "ground" and String(p2.outcome) == "shatter", "wall vs bolts: T1 %s, T2 %s" % [p1.outcome, p2.outcome])
	h.w.decay_body(wall, "t")
	# Searing Beam T2 melts a flying stone, the Blue Lance (T1) only warms it.
	var s1 := Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), Agent.of_move(h.w, f, "blue_needle", 1, false))
	var s2 := Interactions.predict(h.w, threat(h.w, "stone", 17.0, 20.0), Agent.of_move(h.w, f, "blue_needle", 2, false))
	check(String(s1.outcome) == "fire_heat" and String(s2.outcome) == "fire_melt", "blue vs stone: T1 %s, T2 %s" % [s1.outcome, s2.outcome])
	# Wind vs a fire field: weak wind fans it, strong wind snuffs it (field 15 vs gust 7 / 40).
	var field := FireUtil.spawn_field(h.w, f.id, Vector3(0, 0, -2), 1.5, 2.0, 300.0)
	var weak := Interactions.predict(h.w, threat(h.w, "gust", 7.0, 0.0, "P"), Agent.of_body(h.w, field))
	var strong := Interactions.predict(h.w, threat(h.w, "gust", 40.0, 0.0, "P"), Agent.of_body(h.w, field))
	check(String(weak.outcome) == "fire_fanned" and String(strong.outcome) == "fire_snuffed", "wind vs fire: weak %s, strong %s" % [weak.outcome, strong.outcome])
