extends TestCase
## Offense reads (MOVESET §13.6): wet / in the pool -> lightning, rival behind a barrier -> Storm Bolt,
## Melt & Return or a sound bank shot, charging rival -> Disrupt / interrupt, chains after contact.


func _setup(kit: Dictionary, foe_pos: Vector3 = Vector3(0, 0, 4), me_pos: Vector3 = Vector3(0, 0, -4), opts: Dictionary = {}) -> Dictionary:
	var h := SimHarness.new(6)
	var p := h.actor("player", foe_pos, 0, {}, Sim.Element.EARTH)
	var o := h.actor("opponent", me_pos, 1, {}, int(kit.keys()[0]))
	var ai := AiBrain.new(h.w, o, {}, 6)
	var c := {"preset": "master", "elements": kit.keys(), "subs": kit}
	c.merge(opts, true)
	ai.configure(c)
	h.step(2)
	return {"h": h, "p": p, "o": o, "ai": ai}


func _top(d: Dictionary, seed_value: int = 1) -> Dictionary:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed_value
	var ai: AiBrain = d.ai
	var opts := AiPlanner.offense(d.h.w, d.o, d.p, ai.kit, ai._planner_params(), rng)
	return opts[0] if not opts.is_empty() else {}


func _wall_between(d: Dictionary) -> MatBody:
	var w: CombatWorld = d.h.w
	var mid: Vector3 = (d.p.pos + d.o.pos) * 0.5
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, Sim.WALL_MASS, mid, "test")
	w.mass_ledger.ground_taken += Sim.WALL_MASS
	b.wall_yaw = 0.0
	b.wall_half = Vector3(1.6, 1.0, 0.28)
	b.wall_rise = 1.0
	b.static_body = true
	b.last_actor = d.p.id
	return b


func test_wet_target_draws_lightning() -> void:
	var kit := {1: [0], 2: [0, 2], 0: [0]}
	var n_wet := 0
	var n_dry := 0
	for s in 6:
		var d := _setup(kit)
		var dry := _top(d, s)
		n_dry += 1 if (dry.get("reasons", []) as Array).has("conduct") else 0
		(d.p as ActorState).wetness = 1.0
		var wet := _top(d, s)
		n_wet += 1 if (wet.get("reasons", []) as Array).has("conduct") else 0
		if s == 0:
			note("dry: %s, wet: %s %s" % [dry.get("label", "-"), wet.get("label", "-"), str(wet.get("reasons", []))])
	check(n_wet == 6, "a wet rival draws an electric attack (%d/6)" % n_wet)
	check(n_dry == 0, "a dry rival does not count as conductive (%d/6)" % n_dry)


func test_barrier_answers() -> void:
	# Lightning kit: Storm Bolt (spark T2) blasts through the wall.
	var dl := _setup({2: [2]})
	_wall_between(dl)
	var tl := _top(dl)
	note("lightning vs wall: %s %s" % [tl.get("label", "-"), str(tl.get("reasons", []))])
	check(String(tl.get("id", "")) == "spark" and int(tl.get("tier", 0)) >= 2 and (tl.reasons as Array).has("barrier"), "Storm Bolt through the wall (%s)" % tl.get("label", "-"))
	# Blue fire kit: Smelter heats the wall face (Melt & Return, step 1).
	var db := _setup({2: [1], 0: [3]})
	var wb := _wall_between(db)
	var tb := _top(db)
	note("blue/magma vs wall: %s %s" % [tb.get("label", "-"), str(tb.get("reasons", []))])
	check(String(tb.get("id", "")) == "smelter" and int(tb.get("aim_body", -1)) == wb.id, "Smelter on the wall face (%s)" % tb.get("label", "-"))
	# Sound kit: rival behind a Bulwark near the west arena wall -> a bank shot off the arena wall.
	var ds := _setup({3: [3]}, Vector3(-13.0, 0, -4.0), Vector3(-13.0, 0, 4.0))
	_wall_between(ds)
	var ts := _top(ds)
	note("sound vs cover: %s aim %s" % [ts.get("label", "-"), str(ts.get("aim", Vector3.ZERO))])
	check(String(ts.get("id", "")) == "sound_lance" and (ts.aim as Vector3) != Vector3.ZERO, "a banked Sound Lance (%s)" % ts.get("label", "-"))


func test_melt_and_return_in_play() -> void:
	# Rival hides behind a Bulwark; the AI (Fire/Blue + Earth/Magma) melts the face and sends lava back.
	var d := _setup({2: [1], 0: [3]}, Vector3(0, 0, 3.0), Vector3(0, 0, -3.0), {"aggression": 1.0})
	var h: SimHarness = d.h
	var ai: AiBrain = d.ai
	var p: ActorState = d.p
	p.facing = PI
	h.press(p, "guard")            # the rival raises a Bulwark and keeps it up
	h.step(20)
	var wall := h.w.get_body(p.wall_body)
	check(wall != null and wall.alive, "setup: the rival's wall is up")
	ai._next_attack = 0.0
	var slumped := false
	var surged := false
	for k in 60 * 8:
		h.intents[d.o.id] = ai.think(Sim.DT)
		h.it(p).guard_held = true
		p.health = 100.0
		p.focus = 100.0
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "slump":
				slumped = true
			if e.type == "action" and e.actor == d.o.id and e.move == "magma_surge":
				surged = true
		if surged:
			break
	note("slumped %s, magma surge %s, AI %s" % [slumped, surged, ai.debug_state])
	check(slumped, "the Smelter slumps the wall face")
	check(surged, "then Magma Surge sends the lava back (Melt & Return)")


func test_charging_rival_is_disrupted() -> void:
	var d := _setup({3: [3]}, Vector3(0, 0, 3), Vector3(0, 0, -3))
	var h: SimHarness = d.h
	var ai: AiBrain = d.ai
	var p: ActorState = d.p
	p.element = Sim.Element.EARTH
	ai._next_attack = 1e9
	h.press(p, "attack")   # a held heave: a long visible charge
	var interrupted := false
	var acted := ""
	for k in 90:
		h.intents[d.o.id] = ai.think(Sim.DT)
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "action" and e.actor == d.o.id and e.phase == "startup" and acted == "":
				acted = e.move
			if (e.type == "interrupt" or e.type == "hit") and e.get("actor", -1) == p.id:
				interrupted = true
	note("answer to the charge: %s (%s), interrupted %s" % [acted, ai.debug_state, interrupted])
	check(acted == "sound_clap" or acted == "sound_lance" or acted == "sound_echo_ring", "a sound move answers the charge (%s)" % acted)
	check(interrupted, "the charge is interrupted")


func test_chain_after_contact() -> void:
	var d := _setup({2: [0], 3: [0]}, Vector3(0, 0, 1.5), Vector3(0, 0, -1.5), {"aggression": 1.0})
	var h: SimHarness = d.h
	var ai: AiBrain = d.ai
	var chains := 0
	for k in 60 * 15:
		h.intents[d.o.id] = ai.think(Sim.DT)
		(d.p as ActorState).health = 100.0
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "chain" and e.actor == d.o.id:
				chains += 1
	check(chains >= 2, "the AI continues strings after contact (%d chains)" % chains)
