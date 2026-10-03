extends TestCase
## The sparring AI must counter a player-created lava wave in ordinary play
## (its own throws, its own reaction delay and resources), across seeds.

func _play(seed_value: int, counter: float, reaction: float = 0.28) -> Dictionary:
	var h := SimHarness.new(seed_value)
	var p := h.actor("player", Vector3(0, 0, 6), 0, {"magma": true}, Sim.Element.FIRE)
	var o := h.actor("opponent", Vector3(0, 0, -6), 1, {"heat_draw": true}, Sim.Element.EARTH)
	var ai := AiBrain.new(h.w, o, {"aggression": 0.8, "counter": counter, "reaction": reaction, "drill": "stone_rain", "interval": 3.5}, seed_value)
	var res := {"intercepted": false, "poured": false, "countered": false, "hit": false, "drew": false}
	var stone: MatBody = null
	var phase := "wait"
	for k in 1200:
		h.intents[o.id] = ai.think(Sim.DT)
		match phase:
			"wait":
				for b in h.w.bodies:
					if b.alive and b.is_projectile() and b.attack_owner == o.id and b.pos.distance_to(p.chest()) < 8.0:
						stone = b
						h.press(p, "tech")
						phase = "catch"
						break
			"catch":
				if stone.controller == p.id:
					res.intercepted = true
					phase = "melt"
				elif p.action == null:
					phase = "wait"
			"melt":
				if stone.phase == Sim.Phase.MOLTEN:
					h.aim(p, o.pos - p.pos)
					h.release(p, "tech")
					phase = "wave"
				elif stone.controller != p.id:
					phase = "wait"
			"wave":
				if stone.form == Sim.Form.WAVE:
					res.poured = true
				if res.poured and (stone.phase == Sim.Phase.SOLID or not stone.alive):
					break
		h.step()
		if stone != null and res.poured and h.events("hit").any(func(e): return e.actor == o.id and e.kind == "lava"):
			res.hit = true
			break
	res.drew = h.events("drawing").any(func(e): return e.actor == o.id)
	res.countered = res.poured and not res.hit and stone != null and stone.phase == Sim.Phase.SOLID
	res["dbg"] = "seed %d: %s ai=%s o.focus=%.0f reserve=%.0f stone=%s" % [seed_value, str(res), ai.debug_state, o.focus, o.heat_reserve, stone.describe() if stone else "-"]
	return res


func test_ai_counters_player_wave() -> void:
	var ok := 0
	var tried := 0
	for s in [3, 5, 8, 13, 21]:
		var r := _play(s, 1.0)
		note(r.dbg)
		if r.poured:
			tried += 1
			if r.countered and r.drew:
				ok += 1
	note("AI drew heat and solidified %d of %d player waves" % [ok, tried])
	check(tried >= 4, "player managed to pour in most runs (%d)" % tried)
	check(ok >= tried - 1 and ok > 0, "AI counters the wave in ordinary play (%d/%d)" % [ok, tried])


func test_ai_without_counter_gets_hit() -> void:
	var hits := 0
	var tried := 0
	for s in [3, 5, 8]:
		var r := _play(s, 0.0, 9.0)
		if r.poured:
			tried += 1
			if r.hit:
				hits += 1
	check(tried > 0 and hits >= 1, "with no reaction the wave connects (%d/%d)" % [hits, tried])
