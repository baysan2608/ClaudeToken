extends "res://tests/ui/ui_test_case.gd"
## Lab tooling: dev panel actions, session cheats, spawner, move list, combo trainer, matrix viewer, live
## tuning (save / load), the HUD hints and the Try path through the real input pipeline.

class LabGame extends Game:
	func _ready() -> void:
		pass


func _lab_game(id: String = "lab") -> LabGame:
	var g := LabGame.new()
	g.progress = Progression.new()
	g.progress.path = "user://test_ui_lab_progress.cfg"
	g.hud = Hud.new()
	g.lab_panel = LabPanel.new()
	var r := Scenarios.build(id, g.progress, 1)
	g.scenario_id = r.def.id
	g.scen_def = r.def
	g.world = r.world
	g.player = r.player
	g.opponent = r.opponent
	return g


func _free(g: LabGame) -> void:
	g.hud.free()
	g.lab_panel.free()
	g.free()


func _step(g: LabGame, n: int = 1) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var f := InputFrame.new()
	for k in n:
		f.clear_edges()
		if g._player_script != null:
			LabScript.apply_dict(f, g._player_script.next())
			if g._player_script.is_done():
				g._player_script = null
		var intents := {g.player.id: g.pc.build(f, PI)}
		if g.opponent:
			intents[g.opponent.id] = g._rival_intent()
		g.lab.pre_step(g.world)
		g.world.step(intents)
		g.lab.post_step(g.player)
		out.append_array(g.world.take_events())
		if g.combo_tracker.is_running():
			g.combo_tracker.update(Sim.DT, out)
	return out


# ---------------------------------------------------------------- session

func test_session_freeze_step_and_cheats() -> void:
	var s := LabSession.new()
	check(not s.consume_step(), "no step requested")
	s.request_step(2)
	check(s.consume_step() and s.consume_step() and not s.consume_step(), "two requested ticks run, then none")
	s.set_time_scale(5.0)
	check_near(s.time_scale, 1.0, 1e-6, "time scale clamps to 1")
	s.set_time_scale(0.0)
	check_near(s.time_scale, 0.1, 1e-6, "and to 0.1")
	var w := CombatWorld.new(1)
	var p := w.add_actor("P", Vector3(0, 0, 5), 0, {}, 0)
	p.focus = 3.0
	p.water_carried = 0.5
	p.metal_carried = 1.0
	var e0 := w.system_energy()
	var m0 := w.water_mass()
	var mm0 := w.metal_mass()
	s.infinite = true
	s.pre_step(w)
	check_near(p.focus, Sim.FOCUS_MAX, 1e-6, "infinite Focus")
	check_near(p.heat_reserve, Sim.RESERVE_MAX, 1e-6, "infinite heat reserve")
	check_near(p.water_carried, 6.0, 1e-6, "full waterskin")
	check_near(p.metal_carried, 12.0, 1e-6, "full satchel")
	check_near(w.system_energy() - e0 - w.ledger_balance(), 0.0, 1e-3, "the heat top-up is booked")
	check_near(w.water_mass() - m0, 0.0, 1e-3, "the water top-up is booked")
	check_near(w.metal_mass() - mm0, 0.0, 1e-3, "the metal top-up is booked")
	p.health = 10.0
	p.balance = 5.0
	s.post_step(p)
	check(p.health < Sim.HEALTH_MAX, "god mode off: damage stays")
	s.god = true
	s.post_step(p)
	check(p.health == Sim.HEALTH_MAX and p.balance == Sim.BALANCE_MAX, "god mode refills health and balance")
	s.ai_kit = "fire"
	s.ai_sub = 1
	s.ai_preset = "master"
	var cfg := s.ai_config()
	check_eq(cfg.preset, "master", "AI preset")
	check_eq(cfg.elements, [2], "single-element kit")
	check_eq(cfg.subs, {2: [1]}, "single sub-element")
	var d := s.to_dict()
	var s2 := LabSession.new()
	s2.from_dict(d)
	check(s2.god and s2.ai_kit == "fire" and s2.ai_sub == 1, "session round-trips")


# ---------------------------------------------------------------- panel

func test_panel_opens_pages_and_emits_actions() -> void:
	var panel := LabPanel.new()
	panel.session = LabSession.new()
	host.root.add_child(panel)
	track(panel)
	var log: Array[Dictionary] = []
	panel.action.connect(func(n: String, a: Dictionary) -> void: log.append({"n": n, "a": a}))
	check(not panel.is_open(), "closed at start")
	panel.open_panel()
	check(panel.is_open() and panel.visible, "opens")
	check_eq(LabPanel.PAGES.size(), 6, "six pages")
	for i in 6:
		panel.show_page(i)
		check(panel.page(i).visible and panel.page_index() == i, "page %d shows" % i)
	# Spawner page.
	var sp := panel.page(1) as LabPageSpawn
	sp._select("stone_45")
	sp.params["mass"] = 60.0
	sp.launch = true
	sp.do_spawn(true)
	check(not panel.is_open(), "Spawn and close closes the panel")
	check_eq(log.size(), 1, "one action")
	check_eq(log[0].n, "spawn", "spawn action")
	check_eq(log[0].a.id, "stone_45", "entry id")
	check_near(float(log[0].a.params.mass), 60.0, 1e-6, "mass param")
	check(log[0].a.launch, "launched by the rival")
	# Dev page: freeze and step through the session.
	panel.open_panel(0)
	var dev := panel.page(0) as LabPageDev
	dev._freeze_btn.button_pressed = true
	check(panel.session.frozen, "the Freeze switch freezes")
	panel.session.request_step(1)
	check(panel.session.consume_step(), "frame step")
	dev._preset_buttons[2].pressed.emit()
	check_eq(panel.session.ai_preset, "master", "difficulty picker")
	dev._kit_buttons[3].pressed.emit()
	check_eq(panel.session.ai_kit, "fire", "kit picker")
	check(log.any(func(e: Dictionary) -> bool: return e.n == "ai"), "AI changes are announced")
	# Moves page: Try plays through the game.
	var mv := panel.page(2) as LabPageMoves
	mv.do_try("thrust", 1)
	var tried: Dictionary = log.back()
	check(tried.n == "try" and tried.a.slot == "thrust" and tried.a.tier == 1, "Try action")
	# Escape closes.
	panel.open_panel()
	var esc := InputEventAction.new()
	esc.action = &"ui_cancel"
	esc.pressed = true
	panel._input(esc)
	check(not panel.is_open(), "ui_cancel closes")


# ---------------------------------------------------------------- spawner

func test_catalog_covers_every_material_of_the_design() -> void:
	var ids := SpawnCatalog.ids()
	for want in ["stone_20", "stone_45", "stone_80", "stone_200", "hot_rock", "magma_blob", "lava_wave", "metal_disc", "metal_lance",
			"metal_plate", "sand_slug", "sand_cloud", "sand_surge", "water_blob", "water_stream", "water_wave", "water_puddle", "ice_shard",
			"ice_wall", "mist", "steam", "vines", "fireball", "fire_field", "tornado", "vacuum_well", "sound_pulse", "bolt", "blast"]:
		check(ids.has(want), "catalogue has %s" % want)


func test_every_spawn_runs_and_conserves() -> void:
	for e in SpawnCatalog.entries():
		for launch in [true, false]:
			var w := CombatWorld.new(3)
			var p := w.add_actor("You", Vector3(0, 0, 7), 0, {}, 0)
			var o := w.add_actor("Rival", Vector3(0, 0, -7), 1, {}, 0)
			o.elements = [true, true, true, true]
			var e0 := w.system_energy()
			var s0 := w.earth_mass()
			var w0 := w.water_mass()
			var m0 := w.metal_mass()
			var r := SpawnCatalog.spawn(w, e.id, {}, p, o, p.pos + p.forward() * 5.0, launch)
			check(r.ok, "%s (%s) spawns: %s" % [e.id, "launch" if launch else "inert", r.msg])
			var it := {p.id: ActorIntent.new(), o.id: ActorIntent.new()}
			var script: LabScript = r.script
			for k in 150:
				if script != null and not script.is_done():
					var oi: ActorIntent = it[o.id]
					oi.clear()
					LabScript.apply_dict(oi, script.next())
				w.step(it)
				w.take_events()
			check_near(w.system_energy() - e0 - w.ledger_balance(), 0.0, 1.0, "%s: energy ledger" % e.id)
			check_near(w.earth_mass() - s0, 0.0, 1e-3, "%s: earth mass" % e.id)
			check_near(w.water_mass() - w0, 0.0, 1e-3, "%s: water mass" % e.id)
			check_near(w.metal_mass() - m0, 0.0, 1e-3, "%s: metal mass" % e.id)
			check(w.alive_count() <= Sim.MAX_BODIES, "%s: body cap" % e.id)


func test_spawn_parameters_and_ownership() -> void:
	var g := _lab_game()
	var r := g.lab_spawn("stone_80", {"mass": 100.0, "speed": 20.0}, true)
	var b: MatBody = r.bodies[0]
	check_near(b.mass, 100.0, 1e-3, "mass slider")
	check(b.attack_id != 0 and b.attack_owner == g.opponent.id, "launched by the rival as its attack")
	var to := (g.player.chest() - b.pos).normalized()
	check(b.vel.normalized().dot(to) > 0.8, "flying at the player")
	var hot: MatBody = g.lab_spawn("hot_rock", {"temp": 700.0}, false).bodies[0]
	check(hot.temp > 650.0 and hot.attack_id == 0, "inert hot rock at the requested temperature")
	var ice: MatBody = g.lab_spawn("ice_wall", {}, true).bodies[0]
	check(ice.form == Sim.Form.WALL and ice.phase == Sim.Phase.FROZEN, "inert-only entries ignore 'launch'")
	var n := g.world.alive_count()
	g.lab_clear()
	check(g.world.alive_count() < n and g.world.alive_count() <= 2, "Clear removes bodies (pool stays)")
	var bolt := g.lab_spawn("bolt", {"tier": 1}, true)
	check(bolt.ok and bolt.script != null and g._rival_script != null, "a volume is performed by the rival through its input")
	g.opponent.focus = 100.0
	var evs := _step(g, 40)
	check(evs.any(func(e: Dictionary) -> bool: return e.type == "action" and e.actor == g.opponent.id and e.move == "spark"), "the rival's Spark started")
	_free(g)


# ---------------------------------------------------------------- Try and the input path

func test_try_plays_every_slot_through_the_input_path() -> void:
	for el in 4:
		for sb in 4:
			var g := _lab_game()
			g.player.pos = Vector3(0, 0, 7)
			for slot in ["strike", "thrust", "ground", "sweep", "guard", "push", "sink", "tech", "evade"]:
				var id := Moves.resolve(el, sb, slot)
				if id == "":
					continue
				g.world.take_events()
				g.player.action = null
				g.player.stun = 0.0
				g.player.focus = 100.0
				g.lab_try(el, sb, slot, 0)
				var started := false
				var evs := _step(g, 30)
				for e in evs:
					if e.type == "action" and e.actor == g.player.id and e.phase == "startup":
						started = true
						if slot != "push" and slot != "sink" and slot != "guard":
							check_eq(e.move, id, "%s/%s %s starts %s" % [Sim.ELEMENT_NAMES[el], Sim.SUB_NAMES[el][sb], slot, id])
				check(started, "%s/%s %s starts an action via the scripted input" % [Sim.ELEMENT_NAMES[el], Sim.SUB_NAMES[el][sb], slot])
				_step(g, 120)
			check_eq(g.player.sub(), sb, "Try switched to sub %d" % sb)
			_free(g)


func test_try_with_a_tier_reaches_it() -> void:
	var g := _lab_game()
	g.lab_try(Sim.Element.FIRE, 1, "strike", 2)
	var tier := 0
	for e in _step(g, 140):
		if e.type == "charge" and e.actor == g.player.id:
			tier = maxi(tier, int(e.tier))
	check(tier >= 2, "holding a Blue strike for T2 reaches T2 (got T%d)" % tier)
	_free(g)


# ---------------------------------------------------------------- move list

func test_move_list_covers_every_sub_element_and_device() -> void:
	for e in 4:
		for s in 4:
			var rows := MoveListData.rows(e, s, "keyboard")
			check(rows.size() >= 6, "%s/%s lists %d moves" % [Sim.ELEMENT_NAMES[e], Sim.SUB_NAMES[e][s], rows.size()])
			for r in rows:
				check(r.name != "" and r.input != "" and r.frames.begins_with("S") and r.cost != "", "row %s is complete" % r.id)
	check(MoveListData.input_text("thrust", "touch") != MoveListData.input_text("thrust", "keyboard"), "inputs follow the device")
	check_eq(MoveListData.input_text("push", "keyboard"), "K + J", "keyboard push chord")
	check_eq(MoveListData.input_text("sink", "gamepad"), "RB + LT", "gamepad sink chord")
	var strike: Dictionary = MoveListData.rows(0, 0, "touch")[0]
	check_eq(strike.id, "earth_attack", "Stone strike is the legacy move")
	check(strike.tiers.size() == 3 and strike.tiers[0].hold > 0.0, "tier table with hold times")
	# New registry moves appear without touching the UI.
	var before := MoveListData.rows(2, 2, "keyboard").size()
	check(before > 0, "registry-driven")


# ---------------------------------------------------------------- combos

func test_combo_table_is_complete_and_resolvable() -> void:
	var all := LabCombos.all()
	check_eq(all.size(), 28, "all 28 showcase combos")
	var ids := {}
	for c in all:
		check(not ids.has(c.id), "unique id %s" % c.id)
		ids[c.id] = true
		check(c.steps.size() >= 1, "%s has steps" % c.id)
		for s in c.steps:
			if String(s.kind) != "shape":
				check(Moves.resolve(int(s.el), int(s.sub), String(s.slot)) != "", "%s: step %s resolves" % [c.id, LabCombos.step_text(s)])
		for sp in (c.setup as Dictionary).get("spawn", []):
			check(not SpawnCatalog.find(String(sp.id)).is_empty(), "%s: set-up entry %s exists" % [c.id, sp.id])


func _action(p: int, move: String) -> Dictionary:
	return {"type": "action", "actor": p, "move": move, "phase": "startup"}


func test_combo_tracker_follows_steps_windows_and_results() -> void:
	var c := LabCombos.find("mud_bog")
	var ids: Array[String] = []
	for s in c.steps:
		ids.append(Moves.resolve(int(s.el), int(s.sub), String(s.slot)))
	var tr := ComboTracker.new()
	tr.start(c, 1)
	check_eq(tr.state, ComboTracker.State.RUNNING, "running")
	tr.update(0.1, [_action(1, "unrelated_move")])
	check_eq(tr.step, 0, "an unrelated move does nothing")
	tr.update(0.1, [_action(1, ids[0])])
	check_eq(tr.step, 1, "step 1 done")
	tr.update(0.1, [_action(2, ids[1])])
	check_eq(tr.step, 1, "another actor's move does not count")
	tr.update(0.1, [_action(1, ids[1])])
	check_eq(tr.state, ComboTracker.State.SEQUENCE_DONE, "sequence done, waiting for the result")
	tr.update(0.1, [{"type": "interaction", "outcome": "transform", "to": "mud"}])
	check_eq(tr.state, ComboTracker.State.SUCCESS, "result event -> success")
	check_eq(tr.successes, 1, "counted")
	# Too slow: the second step's window closes.
	tr.start(c, 1)
	tr.update(0.1, [_action(1, ids[0])])
	tr.update(float(c.steps[1].within) + 0.5, [])
	check_eq(tr.step, 0, "timed out: back to the first step")
	check(tr.fail_reason.begins_with("too slow"), "reason given (%s)" % tr.fail_reason)
	check_eq(tr.attempts, 1, "attempt counted")
	# Tiered step completes at the charge event; shape step at the shape event.
	var t2 := LabCombos.find("flash_freeze")
	tr.start(t2, 1)
	var i0 := Moves.resolve(int(t2.steps[0].el), int(t2.steps[0].sub), String(t2.steps[0].slot))
	var i1 := Moves.resolve(int(t2.steps[1].el), int(t2.steps[1].sub), String(t2.steps[1].slot))
	var i2 := Moves.resolve(int(t2.steps[2].el), int(t2.steps[2].sub), String(t2.steps[2].slot))
	tr.update(0.1, [_action(1, i0)])
	tr.update(0.1, [_action(1, i1)])
	tr.update(0.1, [_action(1, i2)])
	check_eq(tr.step, 2, "a tier-1 step does not complete at the press")
	tr.update(0.1, [{"type": "charge", "actor": 1, "move": i2, "tier": 1}])
	check_eq(tr.state, ComboTracker.State.SEQUENCE_DONE, "completes at the tier")
	var t3 := LabCombos.find("split_return")
	tr.start(t3, 1)
	tr.update(0.1, [_action(1, Moves.resolve(0, 0, "tech"))])
	tr.update(0.1, [{"type": "split", "parent": 1, "child": 2}])
	check(tr.state == ComboTracker.State.SEQUENCE_DONE or tr.state == ComboTracker.State.SUCCESS, "shape step from a split event")
	check(tr.status_text() != "", "status text")


func test_combo_start_sets_up_and_arms_the_tracker() -> void:
	var g := _lab_game()
	g.lab_start_combo("swallow")
	check(g.combo_tracker.is_running(), "tracker running")
	check(g._lab_queue.size() >= 1, "the stone throw is queued")
	var evs := _step(g, 1)
	for k in 70:
		g._lab_tick()
	check(g.world.bodies.any(func(b: MatBody) -> bool: return b.alive and b.origin == "lab"), "the situation was spawned")
	check(evs != null, "ticks run")
	_free(g)


# ---------------------------------------------------------------- matrix

func test_matrix_prediction_scales_with_the_threat_and_the_counter() -> void:
	var small := MatrixQuery.predict("stone_20", {}, "swallow", 0, false)
	var big := MatrixQuery.predict("stone_200", {}, "swallow", 0, false)
	check(small.ok and big.ok, "predictions build")
	check(float(big.tp) > float(small.tp) * 2.0, "a 200 kg boulder carries far more threat power (%.1f vs %.1f)" % [big.tp, small.tp])
	check(float(big.ratio) < float(small.ratio), "so the same counter has a lower ratio")
	var heavy := MatrixQuery.predict("stone_20", {"mass": 80.0}, "swallow", 0, false)
	check(float(heavy.tp) > float(small.tp), "the mass slider raises TP")
	var hi := MatrixQuery.predict("stone_20", {"speed": 28.0}, "swallow", 0, false)
	check(float(hi.tp) > float(small.tp), "the speed slider raises TP")
	var t0 := MatrixQuery.predict("stone_45", {}, "swallow", 0, false)
	var t3 := MatrixQuery.predict("stone_45", {}, "swallow", 3, false)
	check(float(t3.cp) > float(t0.cp), "a higher counter tier has more counter power")
	var perfect := MatrixQuery.predict("stone_45", {}, "swallow", 0, true)
	check(float(perfect.cp_eff) > float(t0.cp_eff), "perfect timing multiplies counter power")
	check(["full", "partial", "fail"].has(t0.band) or String(t0.band) != "", "band reported")
	check(String(t0.outcome) != "", "outcome reported")
	check(float(t0.needs) > 0.0, "needed counter power reported")
	var lava_vs_wind := MatrixQuery.predict("lava_wave", {}, "gust_wall", 0, false)
	var lava_vs_wind3 := MatrixQuery.predict("lava_wave", {}, "gust_wall", 0, true)
	check(lava_vs_wind.ok and lava_vs_wind3.ok, "lava vs wind builds")
	check(float(lava_vs_wind.ratio) < 1.0, "a plain wind wall cannot stop a lava wave (ratio %.2f)" % float(lava_vs_wind.ratio))
	check(not MatrixQuery.predict("nope", {}, "swallow", 0, false).ok, "unknown threat is reported")
	check(MatrixQuery.counters().size() > 60, "every registry counter is selectable")
	var bolt := MatrixQuery.predict("bolt", {}, "wall_stone", 0, false)
	check(bolt.ok and String(bolt.threat_cls) == "lightning", "volume threats work (%s)" % str(bolt))


func test_matrix_page_updates_and_stages() -> void:
	var panel := LabPanel.new()
	panel.session = LabSession.new()
	host.root.add_child(panel)
	track(panel)
	var log: Array[Dictionary] = []
	panel.action.connect(func(n: String, a: Dictionary) -> void: log.append({"n": n, "a": a}))
	panel.open_panel(4)
	var mp := panel.page(4) as LabPageMatrix
	mp.threat_id = "stone_45"
	mp.counter_id = "swallow"
	mp.update()
	check(bool(mp.last.ok), "page computed a result")
	check(mp._result.text.contains("ratio"), "result text shows the ratio")
	mp.perfect = true
	mp.update()
	check(bool(mp.last.perfect), "perfect flag flows through")
	mp.stage()
	var st: Dictionary = log.back()
	check(st.n == "stage" and st.a.id == "stone_45" and st.a.counter == "swallow" and st.a.element == 0, "Stage it announces the threat and the counter's element")
	var g := _lab_game()
	g.lab_panel.free()
	g.lab_panel = panel
	g.lab_stage(st.a)
	check(g._player_script != null, "the player is switched to the counter's element")
	check(g.world.bodies.any(func(b: MatBody) -> bool: return b.alive and b.origin == "lab" and b.attack_owner == g.opponent.id), "the threat is launched at the player")
	g.lab_panel = LabPanel.new()
	_free(g)


# ---------------------------------------------------------------- tuning

func test_tuning_save_load_reset_and_export() -> void:
	LabTuning.reset_all()
	var orig := float(Moves.DEFS.earth_attack.damage)
	var ot2 := float(Moves.DEFS.fireball.tiers.t2.damage)
	check(LabTuning.set_value("earth_attack", "damage", orig * 2.0), "flat field")
	check(LabTuning.set_value("fireball", "tiers.t2.damage", ot2 + 5.0), "tier field")
	check(not LabTuning.set_value("earth_attack", "no_such_field", 1.0), "unknown field refused")
	check(not LabTuning.set_value("no_such_move", "damage", 1.0), "unknown move refused")
	check_near(float(Moves.DEFS.earth_attack.damage), orig * 2.0, 1e-6, "live value changed")
	check_near(float(Charge.pget(Moves.DEFS.fireball, 2, "damage", 0.0)), ot2 + 5.0, 1e-6, "tier ladder sees it")
	check(LabTuning.is_changed("earth_attack", "damage"), "marked changed")
	check_near(float(LabTuning.original_value("earth_attack", "damage")), orig, 1e-6, "original remembered")
	# A counter-rule threshold.
	var cell := LabTuning.rule_cells()[0]
	var rf := LabTuning.rule_fields(cell.key, cell.idx)
	check(rf.size() >= 5, "rule thresholds offered")
	var r0 := float(LabTuning.rule_fields(cell.key, cell.idx)[0].value)
	var rpath: String = LabTuning.rule_fields(cell.key, cell.idx)[0].path
	check(LabTuning.set_rule_value(cell.key, cell.idx, rpath, r0 + 0.25), "rule field set")
	check_eq(LabTuning.change_count(), 3, "three changes")
	var txt := LabTuning.export_text()
	check(txt.contains("earth_attack.damage") and txt.contains("fireball.tiers.t2.damage") and txt.contains("rule "), "export lists every change")
	var path := "user://test_tuning.cfg"
	check_eq(LabTuning.save(path), OK, "saved")
	LabTuning.reset_all()
	check_near(float(Moves.DEFS.earth_attack.damage), orig, 1e-6, "reset restores the move")
	check_near(float(Moves.DEFS.fireball.tiers.t2.damage), ot2, 1e-6, "reset restores the tier")
	check_near(float(LabTuning.rule_fields(cell.key, cell.idx)[0].value), r0, 1e-6, "reset restores the rule")
	check_eq(LabTuning.change_count(), 0, "log cleared")
	var n := LabTuning.load_file(path)
	check_eq(n, 3, "three fields loaded")
	check_near(float(Moves.DEFS.earth_attack.damage), orig * 2.0, 1e-6, "loaded move value")
	check_near(float(Moves.DEFS.fireball.tiers.t2.damage), ot2 + 5.0, 1e-6, "loaded tier value")
	check_near(float(LabTuning.rule_fields(cell.key, cell.idx)[0].value), r0 + 0.25, 1e-6, "loaded rule value")
	check_eq(LabTuning.load_file("user://does_not_exist.cfg"), -1, "missing file is reported")
	LabTuning.reset_all()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	check(LabTuning.numeric_fields("fireball").size() > 10, "every numeric field of a def is listed")
	var fields := LabTuning.numeric_fields("fireball")
	check(fields.any(func(f: Dictionary) -> bool: return f.path == "tiers.t3.impact_power"), "tier fields included")


func test_tuning_page_builds_fields_for_a_move() -> void:
	var panel := LabPanel.new()
	panel.session = LabSession.new()
	host.root.add_child(panel)
	track(panel)
	panel.open_panel(5)
	var tp := panel.page(5) as LabPageTuning
	tp.move_id = "fireball"
	tp._rebuild_fields()
	check(tp._fields.get_child_count() > 10, "a slider per numeric field")
	var hs: Array = []
	for row in tp._fields.get_children():
		for ch in row.get_children():
			if ch is HSlider:
				hs.append(ch)
	check(hs.size() > 10, "sliders present")
	var before := float(Moves.DEFS.fireball.speed)
	(hs[0] as HSlider).value = (hs[0] as HSlider).max_value
	check(LabTuning.change_count() >= 1, "moving a slider changes the move")
	LabTuning.reset_all()
	check_near(float(Moves.DEFS.fireball.speed), before, 1e-6, "restored")


# ---------------------------------------------------------------- HUD hints and rings

func test_hud_hints_follow_the_selected_sub_element() -> void:
	var w := CombatWorld.new(1)
	var p := w.add_actor("P", Vector3(0, 0, 5), 0, {}, Sim.Element.FIRE)
	var pet := Game.gesture_petals(p)
	check_eq(pet.up, "Fireball", "Flame thrust")
	p.subs[Sim.Element.FIRE] = 1
	pet = Game.gesture_petals(p)
	check_eq(pet.up, "Comet Flame", "Blue thrust")
	check_eq(pet.side, "Corona", "Blue sweep")
	var gp := Game.guard_petals(p)
	check(gp.has("up") and gp.has("down"), "push / sink names")
	check_eq(Game.shape_label(p), "", "no shape hint without a technique")
	check_eq(Game.charge_ring_context(p), {}, "no ring while idle")


func test_attack_ring_context_covers_every_registry_attack_move() -> void:
	var w := CombatWorld.new(1)
	var p := w.add_actor("P", Vector3(0, 0, 5), 0, {}, Sim.Element.EARTH)
	var o := w.add_actor("O", Vector3(0, 0, -5), 1, {}, Sim.Element.EARTH)
	o.is_dummy = true
	var seen := {}
	for sub in 4:
		for slot in Sim.ATTACK_SLOTS:
			var id := Moves.resolve(0, sub, slot)
			var it := ActorIntent.new()
			p.action = null
			p.stun = 0.0
			p.focus = 100.0
			p.subs[0] = sub
			it.attack_pressed = true
			it.attack_held = true
			it.attack_gesture = LabScript.GESTURE_OF_SLOT[slot]
			var intents := {p.id: it}
			var ok := false
			for k in 12:
				w.step(intents)
				w.take_events()
				it.attack_pressed = false
				it.attack_gesture = 0
				var ctx := Game.attack_ring_context(p)
				if p.action != null and p.action.id == id and ctx.attack_charge >= 0.0:
					ok = true
					check(ctx.attack_element == 0, "element of the running attack")
					if not TouchControls.ATTACK_MOVES.has(id):
						check(float(ctx.attack_decide) >= Moves.HOLD_THRESHOLD - 0.001, "%s carries its own decision time" % id)
			check(ok, "%s (%s/%s) drives the charge ring" % [id, Sim.SUB_NAMES[0][sub], slot])
			it.attack_held = false
			it.attack_released = true
			for k in 80:
				w.step(intents)
				w.take_events()
				it.attack_released = false


func test_charge_ring_context_reports_tiers() -> void:
	var g := _lab_game()
	g.lab_try(Sim.Element.FIRE, 1, "strike", 3)
	var best := {}
	for k in 160:
		_step(g, 1)
		var cr := Game.charge_ring_context(g.player)
		if not cr.is_empty() and int(cr.tier) >= int(best.get("tier", -1)):
			best = cr
	check(not best.is_empty() and best.slot == "attack" and best.max == 3, "attack charge ring with three tiers (%s)" % str(best))
	check(int(best.tier) >= 2, "tier progress is reported (T%d)" % int(best.tier))
	_free(g)


func test_hud_has_an_icon_for_every_status() -> void:
	for nm in Status.CORE:
		check(Hud.STATUS_STYLE.has(nm), "status %s has an icon" % nm)
		check(String(Hud.STATUS_STYLE[nm][0]).length() == 2, "two-letter code for %s" % nm)
