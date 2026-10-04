extends TestCase
## Regressions for sparring-AI decision bugs: drawing waves it cannot set in time (or walking
## into them while drawing), dropped technique presses, pool-edge jitter, blind throws into
## cover, guards that drop during a visible charge, unreachable strike defence, and the
## per-tick contest roll.


func _wave(w: CombatWorld, owner: ActorState, mass: float, p: Vector3, dir: Vector3) -> MatBody:
	# A freshly poured, fully molten wave (the fields ActFire._pour sets).
	var d: Dictionary = Moves.DEFS.pour
	var b := w.spawn_body(Sim.Mat.STONE, Sim.Form.WAVE, mass, p, "test", Sim.STONE_MELT_C)
	b.liquid = 1.0
	b.phase = Sim.Phase.MOLTEN
	b.on_ground = true
	b.update_radius()
	b.wave_dir = dir
	b.wave_budget = float(d.base_budget) + float(d.budget_per_kg) * mass
	b.wave_width = 1.1 + mass * 0.025
	b.wave_path = PackedVector3Array([p])
	b.max_life = -1.0
	b.attack_id = w.new_attack_id()
	b.attack_owner = owner.id
	b.hit_set[owner.id] = true
	b.damage = float(d.damage)
	b.balance_damage = float(d.balance)
	return b


func _duel(seed_value: int, opp_pos: Vector3, cfg: Dictionary, player_kit: Dictionary = {"magma": true}) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 6), 0, player_kit, Sim.Element.FIRE)
	var o := h.actor("opponent", opp_pos, 1, {"heat_draw": true}, Sim.Element.EARTH)
	o.elements = [true, false, true, false]
	var c := {"aggression": 0.6, "counter": 1.0, "elements": [0, 2], "drill": "passive"}
	c.merge(cfg, true)
	var ai := AiBrain.new(h.w, o, c, seed_value)
	for k in 5:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
	return {"h": h, "p": p, "o": o, "ai": ai}


## Pours a wave of `mass` straight at the AI from `dist` m; returns its decision, whether it was
## hit and how far it moved while channelling a draw (before any hit).
func _wave_trial(mass: float, dist: float, seed_value: int, cfg: Dictionary = {}) -> Dictionary:
	var start := Vector3(0, 0, 4.7)
	var d := _duel(seed_value, start - Vector3(0, 0, dist), cfg)
	var h: SimHarness = d.h
	var ai: AiBrain = d.ai
	var o: ActorState = d.o
	ai._next_attack = 1e9   # free sparring: no throws of its own
	var b := _wave(h.w, d.p, mass, start, Vector3(0, 0, -1))
	var key := "b%d:%d" % [b.id, b.attack_id]
	var res := {"decision": "", "hit": false, "drift": 0.0}
	var at = null
	for k in 400:
		h.intents[o.id] = ai.think(Sim.DT)
		if res.decision == "" and ai._decided.has(key):
			res.decision = ai._decided[key]
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "hit" and e.actor == o.id and e.kind == "lava":
				res.hit = true
		var a := o.action
		if not res.hit and a != null and a.id == "fire_tech" and a.phase == ActionInst.P.CHANNEL:
			if at == null:
				at = o.pos
			res.drift = maxf(res.drift, o.pos.distance_to(at))
		if b.form != Sim.Form.WAVE:
			break
	return res


func test_ai_walls_a_wave_it_cannot_draw_in_time() -> void:
	# A 45 kg wave holds ~2.25x the heat of a 20 kg one: the draw cannot set it before
	# contact, so the AI must pick the wall (which always stops it) instead.
	# (A close 20 kg wave is just as hopeless for a draw.)
	var hits := 0
	var draws := 0
	var n := 0
	for md in [[45.0, 6.0], [45.0, 8.4], [45.0, 10.0], [20.0, 6.0]]:
		for s in [1, 2, 3]:
			var r := _wave_trial(md[0], md[1], s)
			n += 1
			hits += 1 if r.hit else 0
			draws += 1 if r.decision == "draw" else 0
	note("unwinnable waves: %d hits, %d draws of %d" % [hits, draws, n])
	check(hits == 0, "waves the draw cannot set are stopped another way (%d/%d hit)" % [hits, n])
	check(draws == 0, "the AI does not pick a draw it cannot finish (%d/%d)" % [draws, n])


func test_ai_still_draws_a_wave_it_can_set() -> void:
	# The draw stays the counter of choice when it works (20 kg from 10 m).
	var ok := 0
	for s in [1, 2, 3, 4]:
		var r := _wave_trial(20.0, 10.0, s)
		if r.decision == "draw" and not r.hit:
			ok += 1
	check(ok == 4, "a settable wave is drawn and stopped (%d/4)" % ok)


func test_ai_stands_still_while_it_draws() -> void:
	# The draw estimate assumes the AI stays put. It used to keep walking toward the foe (and
	# so into their wave) while holding the draw, and to slide on from a run when it committed:
	# draws estimated to set the lava in time arrived too late (13 of 17 here were hit before).
	var draws := 0
	var bad := 0
	var drift := 0.0
	for c in [["passive", 45.0, 13.0], ["", 30.0, 10.5], ["", 30.0, 11.5], ["", 20.0, 10.0]]:
		for s in [1, 2, 3, 4, 5, 6]:
			var r := _wave_trial(c[1], c[2], s, {"drill": c[0]})
			if r.decision == "draw":
				draws += 1
				bad += 1 if r.hit else 0
				drift = maxf(drift, r.drift)
	note("draws %d, hit through the draw %d, max drift while drawing %.2f m" % [draws, bad, drift])
	check(draws >= 8, "setup: the draw is still chosen (%d/24)" % draws)
	check(bad == 0, "a chosen draw sets the wave before it arrives (%d/%d hit)" % [bad, draws])
	check(drift < 0.01, "no steps while channelling the draw (%.2f m)" % drift)


func test_wave_is_perceived_from_the_pour_wind_up() -> void:
	# The pour's 0.25 s wind-up is the wave's visible telegraph (same body): the reaction
	# clock starts there, so the decision lands well before a fresh look at the wave would.
	for s in [1, 2, 3]:
		var d := _duel(s, Vector3(0, 0, -3), {})
		var h: SimHarness = d.h
		var ai: AiBrain = d.ai
		var p: ActorState = d.p
		var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.BLOB, 20.0, p.hand_point(), "test", Sim.STONE_MELT_C)
		b.liquid = 0.85
		b.phase = Sim.Phase.MOLTEN
		b.update_radius()
		b.controller = p.id
		p.held_body = b.id
		var aim := Vector3(0, 0, -1)
		h.w.start_action(p, "pour", h.it(p), {"aim": aim, "face": aim, "body": b.id})
		var spawn_t := -1.0
		var decided_t := -1.0
		for k in 60:
			h.intents[d.o.id] = ai.think(Sim.DT)
			if spawn_t >= 0.0 and decided_t < 0.0 and ai._decided.has("b%d:%d" % [b.id, b.attack_id]):
				decided_t = ai._t
			h.step()
			if spawn_t < 0.0 and b.form == Sim.Form.WAVE:
				spawn_t = ai._t
		check(spawn_t >= 0.0 and decided_t >= 0.0, "seed %d: the poured wave is decided on" % s)
		check(decided_t - spawn_t < float(ai.cfg.reaction) - 0.1, "seed %d: decided %.2f s after the wave appeared (pour seen first)" % [s, decided_t - spawn_t])


func test_draw_pressed_mid_throw_starts_when_the_throw_ends() -> void:
	# The world buffers a press for 0.15 s only; a draw decided during the AI's own throw
	# recovery used to be dropped silently, leaving the AI idle in front of the wave.
	var started := 0
	var drew := 0
	for s in [1, 2, 3]:
		var d := _duel(s, Vector3(0, 0, -6), {})
		var h: SimHarness = d.h
		var ai: AiBrain = d.ai
		var o: ActorState = d.o
		h.w._try_start(o, "attack", ActorIntent.new())   # the AI's own light stone throw
		h.until(func() -> bool: return o.action != null and o.action.phase == ActionInst.P.ACTIVE, 60)
		# 7 m away and flowing across (inside draw range and the aim cone throughout),
		# so only the press handling is under test.
		var b := _wave(h.w, d.p, 20.0, Vector3(0, 0, 1.0), Vector3(1, 0, 0))
		h.intents[o.id] = ai.think(Sim.DT)
		ai._start_draw(b)
		var n0 := h.log.size()
		for k in 90:
			h.step()
			h.intents[o.id] = ai.think(Sim.DT)
		var tech := false
		var drawing := false
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "action" and e.actor == o.id and e.move == "fire_tech":
				tech = true
			if e.type == "drawing" and e.actor == o.id:
				drawing = true
		started += 1 if tech else 0
		drew += 1 if drawing else 0
	check(started == 3, "the buffered draw press is retried until the technique starts (%d/3)" % started)
	check(drew == 3, "the draw then actually extracts heat (%d/3)" % drew)


func test_threats_are_perceived_while_holding() -> void:
	# Holding a heavy throw or a seize must not blind the AI: the reaction clock runs during the
	# hold, so the decision comes as soon as the hold ends instead of a full reaction later.
	var d := _duel(4, Vector3(0, 0, -6), {})
	var h: SimHarness = d.h
	var ai: AiBrain = d.ai
	var o: ActorState = d.o
	ai._hold = "attack"
	ai._hold_until = ai._t + 0.6
	var b := _wave(h.w, d.p, 20.0, Vector3(0, 0, 6), Vector3(0, 0, -1))
	b.wave_budget = 40.0
	var key := "b%d:%d" % [b.id, b.attack_id]
	for k in 24:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
	check(ai._hold == "attack" and ai._seen.has(key), "the wave is seen during the hold")
	var ticks_after := -1
	for k in 40:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
		if ai._hold == "" and ticks_after < 0:
			ticks_after = 0
		elif ticks_after >= 0:
			ticks_after += 1
		if ai._decided.has(key):
			break
	check(ai._decided.has(key) and ticks_after <= 2, "decided right after the hold (%d ticks after)" % ticks_after)


# ------------------------------------------------------------------ movement

func _move_run(seed_value: int, ppos: Vector3, opos: Vector3, secs: float) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", ppos, 0, {}, Sim.Element.EARTH)
	var o := h.actor("opponent", opos, 1, {}, Sim.Element.EARTH)
	var ai := AiBrain.new(h.w, o, {"elements": [], "drill": "", "aggression": 0.6}, seed_value)
	var last := Vector3.ZERO
	var rev := 0
	var min_d := INF
	var wet := 0
	for k in int(secs * 60):
		var it := ai.think(Sim.DT)
		h.intents[o.id] = it
		h.step()
		if it.move.length() > 0.1 and last.length() > 0.1 and it.move.normalized().dot(last.normalized()) < -0.5:
			rev += 1
		last = it.move
		min_d = minf(min_d, o.pos.distance_to(p.pos))
		if h.w.arena.in_pool(o.pos.x, o.pos.z):
			wet += 1
	return {"rev": rev / secs, "min_d": min_d, "wet": wet, "end": o.pos}


func test_ai_walks_around_the_pool_without_jitter() -> void:
	var hi := 11.0 - 2.0 * 0.6
	for s in [1, 2, 3]:
		# Foe straight across the pool: the AI must go round it, not vibrate at the edge.
		var r := _move_run(s, Vector3(10, 0, -9), Vector3(10, 0, 6), 15.0)
		note("across pool seed %d: %s" % [s, str(r)])
		check(r.rev < 1.0, "seed %d: no direction flip-flop (%.1f reversals/s)" % [s, r.rev])
		check(r.min_d < hi + 0.5, "seed %d: reaches its range around the pool (closest %.1f m)" % [s, r.min_d])
		check(r.wet == 0, "seed %d: stays out of the pool (%d ticks in it)" % [s, r.wet])
	for s in [1, 2]:
		# Strafing along the west edge with the foe at its spawn.
		var r := _move_run(s, Vector3(0, 0, 7), Vector3(6.0, 0, -1), 15.0)
		note("edge strafe seed %d: %s" % [s, str(r)])
		check(r.rev < 1.0, "edge seed %d: no direction flip-flop (%.1f reversals/s)" % [s, r.rev])
		check(r.wet == 0, "edge seed %d: stays out of the pool (%d ticks in it)" % [s, r.wet])


func test_ai_does_not_throw_blind_from_behind_cover() -> void:
	# Placed behind the low cover wall with the foe at spawn (where Stone Rain used to stall):
	# no throws into the wall, and it steps out so stones reach the player again.
	for s in [1, 2, 3]:
		var h := SimHarness.new(s)
		var p := h.actor("player", Vector3(0, 0, 7), 0, {}, Sim.Element.EARTH)
		var o := h.actor("opponent", Vector3(-4.0, 0, -1.8), 1, {}, Sim.Element.EARTH)
		o.elements = [true, false, false, false]
		var ai := AiBrain.new(h.w, o, {"drill": "stone_rain", "interval": 2.4, "counter": 0.3, "aggression": 0.5, "elements": [0]}, s)
		ai._next_attack = 0.4
		var throws := 0
		var blind := 0
		var reached := 0
		for k in 60 * 15:
			h.intents[o.id] = ai.think(Sim.DT)
			var n0 := h.log.size()
			h.step()
			for i in range(n0, h.log.size()):
				var e: Dictionary = h.log[i]
				if e.type == "launch" and e.actor == o.id:
					throws += 1
					if not h.w.arena.has_los(o.chest(), p.chest()):
						blind += 1
				elif (e.type == "hit" or e.type == "block") and e.get("actor", -1) == p.id:
					reached += 1
		note("seed %d: throws %d blind %d reached %d" % [s, throws, blind, reached])
		check(blind == 0, "seed %d: no throws without line of sight (%d of %d)" % [s, blind, throws])
		check(reached >= 3, "seed %d: stones reach the player (%d of %d)" % [s, reached, throws])


# ------------------------------------------------------------------ guards

func _bolt_trial(seed_value: int, hold: float) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 4), 0, {"lightning": true}, Sim.Element.FIRE)
	var o := h.actor("opponent", Vector3(0, 0, -4), 1, {"heat_draw": true}, Sim.Element.EARTH)
	o.elements = [true, false, true, false]
	var ai := AiBrain.new(h.w, o, {"counter": 0.7, "elements": [0, 2], "aggression": 0.6}, seed_value)
	ai._next_attack = 1e9
	for k in 30:
		h.intents[o.id] = ai.think(Sim.DT)
		h.step()
	h.press(p, "attack")
	var res := {"bolt": "none", "hold_after": ""}
	for k in int((hold + 1.0) * 60):
		h.intents[o.id] = ai.think(Sim.DT)
		if k == int(hold * 60):
			h.release(p, "attack")
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "lightning" and e.actor == p.id:
				res.bolt = "blocked" if e.blocked else ("hit" if e.hits.has(o.id) else "miss")
	res.hold_after = ai._hold
	return res


func test_bolt_guard_lasts_as_long_as_the_charge() -> void:
	# The charge has no time limit: a guard that drops after a fixed 0.9 s lets any
	# long charge land. The guard must stay up while the charge is visible, then drop.
	for hold in [1.5, 2.5]:
		var blocked := 0
		for s in 4:
			var r := _bolt_trial(700 + s, hold)
			if r.bolt == "blocked":
				blocked += 1
			check(r.hold_after == "", "hold %.1f s seed %d: guard released after the bolt" % [hold, s])
		check(blocked == 4, "a %.1f s charge is still blocked (%d/4)" % [hold, blocked])


func _strike_run(el: int, hold_ticks: int, secs: float, gap: float = 3.0) -> Dictionary:
	var h := SimHarness.new(3)
	var p := h.actor("player", Vector3(0, 0, gap * 0.5), 0, {}, el)
	var o := h.actor("opponent", Vector3(0, 0, -gap * 0.5), 1, {"heat_draw": true}, Sim.Element.EARTH)
	o.elements = [true, false, true, false]
	var ai := AiBrain.new(h.w, o, {"counter": 1.0, "elements": [0, 2], "aggression": 0.6, "drill": "passive"}, 3)
	var res := {"strikes": 0, "melee": 0, "defended": 0, "landed": 0, "max_seen": 0}
	var held := 0
	for k in int(secs * 60):
		h.intents[o.id] = ai.think(Sim.DT)
		p.focus = 100.0
		p.heat_reserve = 500.0
		o.health = 100.0
		if p.action == null and p.stun <= 0.0 and held == 0:
			h.press(p, "attack")
			res.strikes += 1
			held = 1
		elif held > 0:
			held += 1
			if held > hold_ticks:
				h.release(p, "attack")
				held = 0
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "action" and e.actor == o.id and (e.move == "guard" or e.move == "evade") and e.phase == "startup":
				res.defended += 1
			if e.type == "hit" and e.actor == o.id:
				res.landed += 1
		res.max_seen = maxi(res.max_seen, ai._seen.size())
	for key in ai._decided:
		if ai._decided[key] == "melee":
			res.melee += 1
	return res


func test_ai_defends_a_held_close_strike() -> void:
	# Held blaze / gust charges are long, visible telegraphs; taps (< 0.2 s) stay unreactable.
	for el in [Sim.Element.FIRE, Sim.Element.AIR]:
		var r := _strike_run(el, 50, 12.0)
		note("%s held: %s" % [Sim.ELEMENT_NAMES[el], str(r)])
		check(r.melee > 0 and r.defended > 0, "%s: held strikes are guarded or evaded (%d decisions)" % [Sim.ELEMENT_NAMES[el], r.melee])
		check(r.landed * 2 < r.strikes, "%s: most held strikes are stopped (%d of %d landed)" % [Sim.ELEMENT_NAMES[el], r.landed, r.strikes])
	# A held blaze reaches 6.5 m: it is answered from beyond plain flare range too.
	var far := _strike_run(Sim.Element.FIRE, 50, 12.0, 5.8)
	note("Fire held from 5.8 m: %s" % str(far))
	check(far.melee > 0 and far.landed * 2 < far.strikes, "a held blaze from 5.8 m is defended (%d decisions, %d of %d landed)" % [far.melee, far.landed, far.strikes])


func test_perceived_strike_keys_stay_bounded() -> void:
	# Each undecided tap used to leave a key in _seen forever.
	var r := _strike_run(Sim.Element.WATER, 2, 50.0)
	note("water taps: %s" % str(r))
	check(r.strikes > 70, "enough strikes to overflow the table (%d)" % r.strikes)
	check(r.max_seen <= 65, "perception table stays bounded (max %d)" % r.max_seen)


# ------------------------------------------------------------------ opportunities

func _contest_trial(seed_value: int, counter: float) -> bool:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 4), 0, {"magma": true}, Sim.Element.FIRE)
	var o := h.actor("opponent", Vector3(0, 0, -3), 1, {"heat_draw": true}, Sim.Element.FIRE)
	p.facing = PI
	o.facing = 0.0
	var st := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 20.0, Vector3(0, 0.25, 2.8), "test")
	st.on_ground = true
	var ai := AiBrain.new(h.w, o, {"counter": counter, "elements": [0, 2], "aggression": 0.0}, seed_value)
	ai._next_attack = 1e9
	h.press(p, "tech")
	for k in 240:
		h.intents[o.id] = ai.think(Sim.DT)
		h.intents[o.id].move = Vector3.ZERO
		var n0 := h.log.size()
		h.step()
		for i in range(n0, h.log.size()):
			var e: Dictionary = h.log[i]
			if e.type == "thermal" and e.actor == o.id and e.mode == "DRAW":
				return true
	return false


func test_contest_chance_is_rolled_once_per_hold() -> void:
	# counter * 0.35 is the chance per held stone, not per tick (which compounded to ~100%).
	var low := 0
	var high := 0
	var n := 40
	for s in n:
		low += 1 if _contest_trial(1000 + s, 0.2) else 0
		high += 1 if _contest_trial(1000 + s, 1.0) else 0
	note("contests: counter 0.2 -> %d/%d, counter 1.0 -> %d/%d" % [low, n, high, n])
	check(low <= 8, "counter 0.2 (~7%%) rarely contests (%d/%d)" % [low, n])
	check(high >= 5 and high <= 26, "counter 1.0 (~35%%) contests about a third of the time (%d/%d)" % [high, n])
