extends TestCase
## Flagship encounter: stone -> lava -> ground wave -> cooled rock, with the
## opponent countering by drawing heat. Each test plays the rules, not a script.

var h: SimHarness
var p: ActorState
var o: ActorState


func _setup(p_elem: int = Sim.Element.FIRE, o_kit: Dictionary = {"heat_draw": true}) -> void:
	h = SimHarness.new(11)
	p = h.actor("player", Vector3(0, 0, 6), 0, {"magma": true, "heat_draw": true}, p_elem)
	o = h.actor("opponent", Vector3(0, 0, -6), 1, o_kit, Sim.Element.EARTH)


func _opponent_throws() -> MatBody:
	h.press(o, "attack")
	h.step()
	h.release(o, "attack")
	var t := h.until(func(): return h.has_event("launch"), 60)
	check(t > 0, "opponent launched a stone")
	return h.w.get_body(int(h.last_event("launch").body))


func _dist(b: MatBody, a: ActorState) -> float:
	return b.pos.distance_to(a.chest())


func _intercept(stone: MatBody, press_at: float) -> void:
	h.until(func(): return _dist(stone, p) <= press_at, 120)
	h.press(p, "tech")


func test_full_exchange_and_counter() -> void:
	_setup()
	var e0 := h.w.system_energy()
	var stone := _opponent_throws()
	var sid := stone.id
	check(stone.attack_owner == o.id and stone.mass == Sim.STONE_SHOT_MASS, "stone is the opponent's 20 kg attack")
	_intercept(stone, 8.5)
	var caught := h.until(func(): return stone.controller == p.id, 60)
	check(caught > 0, "magma grip caught the stone in flight")
	check(h.has_event("intercept"), "intercept event (momentum absorbed)")
	var melted := h.until(func(): return stone.phase == Sim.Phase.MOLTEN, 120)
	check(melted > 0, "held stone becomes molten (%d ticks)" % melted)
	check(stone.id == sid and stone.alive, "same logical body after melting")
	check(stone.form == Sim.Form.BLOB, "molten body is a held blob")
	near(stone.mass, 20.0, 1e-4, "mass preserved through melting")
	# Pour toward the opponent.
	h.aim(p, o.pos - p.pos)
	h.release(p, "tech")
	var poured := h.until(func(): return stone.form == Sim.Form.WAVE, 40)
	check(poured > 0, "release turns molten blob into a ground wave")
	check(stone.attack_owner == p.id and stone.controller == -1, "wave is the player's attack")
	# Opponent recognises the wave and draws its heat.
	h.element(o, Sim.Element.FIRE)
	h.step()
	h.until(func(): return stone.pos.distance_to(o.pos) < 8.5, 120)
	h.press(o, "tech")
	h.step()
	check(o.action != null and o.action.data.get("mode", "") == "DRAW", "opponent's thermal technique chose DRAW")
	var cooled := h.until(func(): return stone.phase == Sim.Phase.SOLID or h.has_event("hit"), 240)
	check(cooled > 0, "wave resolved")
	check(stone.phase == Sim.Phase.SOLID, "wave solidified before reaching the opponent")
	check(not h.events("hit").any(func(e): return e.actor == o.id), "opponent not hit by the wave")
	check(stone.form == Sim.Form.CHUNK and stone.id == sid, "cooled into rock with the same identity")
	check(stone.temp >= Sim.HOT_ROCK_C, "rock is still hot (%.0f °C)" % stone.temp)
	near(stone.mass, 20.0, 1e-4, "mass preserved through wave and cooling")
	check(o.heat_reserve > 50.0, "extracted heat went into the opponent's reserve (%.0f HU)" % o.heat_reserve)
	h.release(o, "tech")
	h.step(30)
	# Energy ledger balances: nothing created from nowhere.
	near(h.w.system_energy() - e0, h.w.ledger_balance(), 0.5, "energy ledger balances")
	# The rock is now a resource for either fighter: opponent seizes it with earth.
	h.element(o, Sim.Element.EARTH)
	h.step()
	h.press(o, "tech")
	var seized := h.until(func(): return stone.controller == o.id, 60)
	check(seized > 0, "opponent can seize the cooled rock")
	note("melt %d ticks, pour->rock %d ticks, reserve %.0f HU" % [melted, cooled, o.heat_reserve])


func test_wave_hits_when_not_countered() -> void:
	_setup()
	var stone := _opponent_throws()
	_intercept(stone, 8.5)
	h.until(func(): return stone.phase == Sim.Phase.MOLTEN, 180)
	h.aim(p, o.pos - p.pos)
	h.release(p, "tech")
	var r := h.until(func(): return h.events("hit").any(func(e): return e.actor == o.id), 240)
	check(r > 0, "uncountered wave hits the opponent")
	var hit := h.events("hit").filter(func(e): return e.actor == o.id)
	check(hit.size() == 1, "wave hits exactly once (dedup), got %d" % hit.size())
	check(hit.size() > 0 and hit[0].kind == "lava", "hit kind is lava")


func test_early_press_whiffs() -> void:
	_setup()
	h.press(o, "attack")
	h.step()
	h.release(o, "attack")
	h.step(3)
	h.press(p, "tech")   # during the telegraph: nothing in flight to grip yet
	var r := h.until(func(): return h.events("hit").any(func(e): return e.actor == p.id), 120)
	check(h.has_event("whiff") or h.has_event("insufficient"), "early grip fails")
	check(r > 0, "stone then hits the player")
	check(not h.has_event("intercept"), "no interception")


func test_late_press_is_hit() -> void:
	_setup()
	var stone := _opponent_throws()
	_intercept(stone, 1.6)
	h.until(func(): return h.has_event("hit"), 60)
	check(h.events("hit").any(func(e): return e.actor == p.id), "late press: player is hit")
	check(not h.has_event("intercept"), "late press: no interception")
	check(p.action == null, "player's technique was interrupted, no stuck state")


func test_boulder_too_heavy() -> void:
	_setup()
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 200.0, Vector3(0, 1.4, -2), "scenario")
	b.vel = Vector3(0, 2, 15)
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = o.id
	b.damage = 25
	b.balance_damage = 60
	b.hit_set[o.id] = true
	h.press(p, "tech")
	h.until(func(): return h.has_event("hit") or h.has_event("control_fail"), 90)
	var cf := h.last_event("control_fail")
	check(not cf.is_empty() and cf.reason == "mass", "huge boulder cannot be gripped (mass)")
	check(b.controller != p.id and b.phase == Sim.Phase.SOLID, "boulder unchanged")


func test_insufficient_focus_partial_conversion() -> void:
	_setup()
	p.focus = 16.0
	var stone := _opponent_throws()
	_intercept(stone, 8.5)
	h.until(func(): return stone.controller == p.id, 60)
	h.step(90)
	check(h.events("insufficient").any(func(e): return e.actor == p.id), "player told Focus ran out")
	check(stone.phase != Sim.Phase.MOLTEN, "not enough energy to fully melt (liquid %.2f)" % stone.liquid)
	h.aim(p, o.pos - p.pos)
	h.release(p, "tech")
	h.step(3)
	check(stone.form != Sim.Form.WAVE, "a not-molten stone is thrown, not poured")
	check(stone.attack_owner == p.id, "hot stone thrown as an attack")


func test_interrupted_conversion_keeps_state() -> void:
	_setup()
	var stone := _opponent_throws()
	_intercept(stone, 8.5)
	h.until(func(): return stone.controller == p.id, 60)
	h.until(func(): return stone.liquid > 0.3, 120)
	var liq := stone.liquid
	# Interrupt the player with a direct hit.
	h.w.hit_actor(p, {"attacker": o.id, "attack_id": h.w.new_attack_id(), "damage": 5.0, "balance": 30.0,
		"kind": "fire", "from": o.pos})
	h.step()
	check(stone.controller == -1, "interruption drops the body")
	check(h.has_event("conversion_interrupted"), "interruption reported")
	check(stone.alive and absf(stone.liquid - liq) < 0.05, "partial melt state preserved (%.2f)" % stone.liquid)
	check(stone.phase == Sim.Phase.SOFTENED, "phase is softened, not snapped to solid/molten")
	# It cools passively without flickering between labels.
	var changes := 0
	var last := stone.phase
	for k in 900:
		h.step()
		if stone.phase != last:
			changes += 1
			last = stone.phase
	check(stone.phase == Sim.Phase.SOLID, "cooled back to solid")
	check(changes == 1, "single clean phase change while cooling (%d)" % changes)


func test_partial_draw_slows_wave() -> void:
	_setup()
	var stone := _opponent_throws()
	_intercept(stone, 8.5)
	h.until(func(): return stone.phase == Sim.Phase.MOLTEN, 180)
	# Overheat a little so the wave stays fluid long enough.
	h.step(10)
	h.aim(p, o.pos - p.pos)
	h.release(p, "tech")
	h.until(func(): return stone.form == Sim.Form.WAVE, 40)
	h.step(2)
	var v0 := stone.vel.length()
	h.element(o, Sim.Element.FIRE)
	h.step()
	h.until(func(): return stone.pos.distance_to(o.pos) < 8.5, 120)
	h.press(o, "tech")
	h.until(func(): return stone.liquid < 0.5, 120)
	h.release(o, "tech")
	h.step(2)
	if stone.form == Sim.Form.WAVE:
		check(stone.vel.length() < v0 * 0.85, "partially cooled wave is slower (%.1f -> %.1f)" % [v0, stone.vel.length()])
	else:
		check(stone.phase != Sim.Phase.MOLTEN, "wave settled while partially cooled")
	check(stone.phase != Sim.Phase.MOLTEN, "label left MOLTEN after partial cooling")
