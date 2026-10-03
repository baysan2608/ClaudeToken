extends TestCase
## Water and ice: waterskin, ice lance (held water attack), shatter provenance, melting into
## puddles, merging and the puddle cap, drawing from the pool, and fire onto a water shield.
## The invariant behind most checks: CombatWorld.water_mass() never changes (pool + bodies +
## steam + waterskins + vapor/evaporated ledgers), except when a test spawns water itself.

var h: SimHarness
var w: ActorState      # water fighter
var e: ActorState      # target (Earth, idle)


func _setup(w_pos := Vector3(0, 0, 6), e_pos := Vector3(0, 0, -6), seed_value: int = 3) -> void:
	h = SimHarness.new(seed_value)
	w = h.actor("W", w_pos, 0, {}, Sim.Element.WATER)
	e = h.actor("E", e_pos, 1, {}, Sim.Element.EARTH)
	h.step(20)   # actors turn to face each other
	h.log.clear()


## Steps n ticks and reports (once) if water_mass() ever drifts from `base`.
func _step_conserved(n: int, base: float, what: String) -> bool:
	var ok := true
	for k in n:
		h.step()
		var d := h.w.water_mass() - base
		if absf(d) > 1e-6:
			check(false, "%s: water mass drifted by %.6f kg at tick %d" % [what, d, h.w.tick])
			ok = false
			break
	return ok


## Holds the water attack long enough for an ice lance and returns the shard (or null).
func _lance(hold_ticks: int = 40) -> MatBody:
	h.press(w, "attack")
	h.step(hold_ticks)
	h.release(w, "attack")
	var t := h.until(func(): return h.has_event("launch"), 60)
	if t <= 0:
		return null
	return h.w.get_body(int(h.last_event("launch").body))


func _water_bodies_except_pool() -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.mat == Sim.Mat.WATER and b.form != Sim.Form.POOL:
			out.append(b)
	return out


func _puddles() -> Array[MatBody]:
	var out: Array[MatBody] = []
	for b in h.w.bodies:
		if b.alive and b.form == Sim.Form.PUDDLE:
			out.append(b)
	return out


# ---------------------------------------------------------------- ice lance

func test_held_water_attack_creates_frozen_shard_from_waterskin() -> void:
	_setup()
	var base := h.w.water_mass()
	var e0 := h.w.system_energy()
	var focus0 := w.focus
	check(w.water_carried == 6.0, "waterskin starts full")
	var shard := _lance()
	check(shard != null, "a held water attack launches something")
	if shard == null:
		return
	check(h.last_event("launch").kind == "ice", "launch event says ice")
	check(shard.form == Sim.Form.SHARD and shard.phase == Sim.Phase.FROZEN, "the body is a frozen shard (%s/%s)" % [Sim.FORM_NAMES[shard.form], Sim.PHASE_NAMES[shard.phase]])
	check(shard.liquid == 0.0 and shard.temp < 0.0, "ice: liquid 0, below freezing (%.1f)" % shard.temp)
	near(shard.mass, float(Moves.DEFS.water_attack.shard_mass), 1e-9, "shard takes the configured mass")
	check(shard.origin == "waterskin:%d" % w.id, "provenance: drawn from W's waterskin (%s)" % shard.origin)
	near(w.water_carried, 6.0 - shard.mass, 1e-9, "waterskin lost exactly the shard mass")
	check(shard.attack_owner == w.id and shard.attack_id != 0, "shard is W's attack")
	check(shard.damage == float(Moves.DEFS.water_attack.heavy_damage), "heavy damage")
	near(h.w.water_mass(), base, 1e-9, "no water created or destroyed by freezing")
	# Cost: attack (5) + heavy surcharge (5) Focus; freezing dumps heat to the environment.
	var cost := float(Moves.DEFS.water_attack.heavy_cost)
	check(w.focus <= focus0 - cost + 1.0, "Focus was spent (%.1f -> %.1f)" % [focus0, w.focus])
	near(h.w.system_energy() - e0, h.w.ledger_balance(), 0.01, "energy ledger balances after freezing")
	check(h.w.ledger.freeze_dump < 0.0, "freezing is recorded as heat dumped to the environment")


func test_tap_water_attack_is_a_lash_not_a_shard() -> void:
	_setup(Vector3(0, 0, -3.5), Vector3(0, 0, -6))
	h.press(w, "attack")
	h.step()
	h.release(w, "attack")
	h.step(40)
	check(h.has_event("lash"), "a tap lashes")
	check(not h.has_event("launch"), "a tap never launches a shard")
	check(_water_bodies_except_pool().is_empty(), "no ice body exists")
	near(w.water_carried, 6.0, 1e-9, "a lash does not consume the waterskin")


func test_ice_lance_needs_water() -> void:
	_setup()
	w.water_carried = 0.9   # below the 1 kg minimum, not standing in the pool
	h.press(w, "attack")
	h.step(40)
	h.release(w, "attack")
	h.step(60)
	check(h.events("insufficient").any(func(x): return x.actor == w.id and x.what == "water"), "told there is not enough water")
	check(not h.has_event("launch"), "no shard without water")
	near(w.water_carried, 0.9, 1e-9, "waterskin untouched")
	check(w.action == null, "no stuck action after the fizzle")


func test_ice_lance_takes_what_the_waterskin_has() -> void:
	_setup()
	w.water_carried = 1.0   # exactly the minimum: a 1 kg shard
	var base := h.w.water_mass()
	var shard := _lance()
	check(shard != null, "1.0 kg of water is enough for a lance")
	if shard == null:
		return
	near(shard.mass, 1.0, 1e-9, "shard takes all the water that was there")
	near(w.water_carried, 0.0, 1e-9, "waterskin empty")
	near(h.w.water_mass(), base, 1e-9, "conserved")
	# 1 kg is the smallest shard that still splits when it shatters.
	h.until(func(): return h.has_event("shatter"), 120)
	var split := h.last_event("split")
	check(not split.is_empty() and absf(float(split.mass) - 0.5) < 1e-9, "a 1 kg shard splits into 0.5 kg halves")
	_step_conserved(300, base, "1 kg shard fragments")


func test_repeated_lances_drain_waterskin_then_fizzle() -> void:
	_setup()
	var base := h.w.water_mass()
	var first := _lance()
	check(first != null and absf(first.mass - 4.0) < 1e-9, "first lance: 4 kg")
	h.until(func(): return w.action == null, 90)
	var second := _lance()
	check(second != null and absf(second.mass - 2.0) < 1e-9, "second lance: only 2 kg left")
	near(w.water_carried, 0.0, 1e-9, "waterskin empty after two lances")
	h.until(func(): return w.action == null, 90)
	var launches_before := h.events("launch").size()
	h.press(w, "attack")
	h.step(30)
	h.release(w, "attack")
	h.step(60)
	check(h.events("launch").size() == launches_before, "third lance fizzles (no water)")
	check(h.events("insufficient").any(func(x): return x.what == "water"), "and reports it")
	check(_step_conserved(200, base, "after three lances"), "water conserved throughout")


# ---------------------------------------------------------------- shatter & provenance

func test_shard_impact_shatters_into_two_exact_halves_with_lineage() -> void:
	_setup()
	var base := h.w.water_mass()
	var shard := _lance()
	if shard == null:
		check(false, "no shard")
		return
	var sid := shard.id
	var shard_mass := shard.mass
	var t := h.until(func(): return h.has_event("shatter"), 150)
	check(t > 0, "the shard shatters on impact")
	check(h.events("shatter").size() == 1, "exactly one shatter event, got %d" % h.events("shatter").size())
	check(h.events("hit").any(func(x): return x.actor == e.id and x.body == sid), "the shard hit E")
	check(e.health < 100.0, "E took damage (%.1f)" % e.health)
	var splits := h.events("split")
	check(splits.size() == 1, "exactly one split, got %d" % splits.size())
	if splits.is_empty():
		return
	var parent := h.w.get_body(int(splits[0].parent))
	var child := h.w.get_body(int(splits[0].child))
	check(parent != null and child != null and parent.id == sid, "split of the shard that hit")
	check(parent.mass + child.mass == shard_mass, "mass is divided exactly (%.9f + %.9f)" % [parent.mass, child.mass])
	check(parent.mass == shard_mass * 0.5 and child.mass == shard_mass * 0.5, "two equal halves")
	check(child.parent_id == parent.id, "child.parent_id is the shard")
	check(child.lineage == [parent.id], "child.lineage == [parent] (%s)" % [child.lineage])
	check(child.origin == "split:%d" % parent.id, "child origin names its parent (%s)" % child.origin)
	check(parent.lineage.is_empty(), "the shard itself was created from the waterskin (no ancestors)")
	check(child.phase == Sim.Phase.FROZEN and child.temp == parent.temp and child.liquid == parent.liquid, "child keeps the intensive state of the parent")
	check(parent.form == Sim.Form.CHUNK and child.form == Sim.Form.CHUNK, "fragments are inert chunks")
	check(parent.attack_id == 0 and child.attack_id == 0, "fragments cannot hurt anyone")
	check(parent.id != child.id and parent.alive and child.alive, "two distinct living bodies")
	# A fragment can itself be split: lineage grows by one generation.
	var grand := h.w.split_body(child, child.mass * 0.5, child.pos)
	check(grand.lineage == [parent.id, child.id], "grandchild lineage lists both ancestors oldest first (%s)" % [grand.lineage])
	near(child.mass + grand.mass + parent.mass, shard_mass, 1e-12, "mass still adds up")
	near(h.w.water_mass(), base, 1e-6, "conserved through shatter and split")


func test_missed_shard_still_shatters_exactly_once() -> void:
	# A lone fighter with no target throws the lance straight ahead: it hits the arena instead.
	h = SimHarness.new(3)
	w = h.actor("W", Vector3(0, 0, -3), 0, {}, Sim.Element.WATER)
	h.step(5)
	var base := h.w.water_mass()
	var shard := _lance()
	check(shard != null, "lance fired without a target")
	h.step(200)
	check(h.events("shatter").size() == 1, "one shatter event for a shard that hit the arena, got %d" % h.events("shatter").size())
	check(h.events("hit").is_empty(), "nobody was hit")
	check(h.events("split").size() == 1, "one split")
	check(_water_bodies_except_pool().size() <= 2, "no duplicate fragments (%d bodies)" % _water_bodies_except_pool().size())
	near(h.w.water_mass(), base, 1e-6, "conserved")


func test_evaded_shard_passes_through_then_breaks_on_the_ground() -> void:
	_setup()
	e.iframes = 5.0   # E is untouchable for the whole flight
	var base := h.w.water_mass()
	var shard := _lance()
	check(shard != null, "lance fired")
	h.step(150)
	check(h.has_event("evaded"), "E evaded the shard")
	check(e.health == 100.0, "no damage while evading")
	check(h.events("shatter").size() == 1, "the shard still shatters exactly once, on the arena")
	near(h.w.water_mass(), base, 1e-6, "conserved")


# ---------------------------------------------------------------- melting & puddles

func test_fragments_melt_into_puddle_and_water_mass_is_conserved_every_tick() -> void:
	_setup()
	var base := h.w.water_mass()
	var shard := _lance()
	check(shard != null, "lance fired")
	var ok := _step_conserved(100, base, "flight and impact")
	check(h.has_event("shatter"), "shard shattered")
	# Fragments exist and are frozen.
	var frozen := _water_bodies_except_pool()
	check(frozen.size() == 2, "two fragments after the shatter (%d)" % frozen.size())
	for f in frozen:
		check(f.phase == Sim.Phase.FROZEN and f.form == Sim.Form.CHUNK, "fragment #%d is frozen ice" % f.id)
	# Let them thaw (ice warms from the air); check conservation every tick.
	var ticks := 0
	while ok and ticks < 1500:
		h.step()
		ticks += 1
		if absf(h.w.water_mass() - base) > 1e-6:
			ok = false
			check(false, "water mass drifted by %.6f kg while melting (tick %d)" % [h.w.water_mass() - base, ticks])
		var any_frozen := false
		for b in _water_bodies_except_pool():
			if b.phase == Sim.Phase.FROZEN:
				any_frozen = true
		if not any_frozen:
			break
	check(ticks < 1500, "the fragments thawed within 25 s (%d ticks)" % ticks)
	var melts := h.events("transform").filter(func(x): return x.from == "ice" and x.to == "water" and x.why == "melted")
	check(melts.size() == 2, "two melt transforms, got %d" % melts.size())
	var left := _water_bodies_except_pool()
	check(left.size() == 1 and left[0].form == Sim.Form.PUDDLE, "the two fragments became one merged puddle (%d bodies)" % left.size())
	if left.size() == 1:
		near(left[0].mass, shard.mass, 1e-9, "the puddle holds the whole shard mass")
		check(left[0].absorbed.size() == 1, "the merge recorded the absorbed fragment")
		check(left[0].phase == Sim.Phase.LIQUID, "puddle is liquid (conductive)")
	check(h.w.mass_ledger.evaporated == 0.0, "nothing evaporated")
	near(h.w.water_mass(), base, 1e-6, "conserved after melting")


func test_puddles_merge_when_touching_and_not_when_apart() -> void:
	h = SimHarness.new(2)
	var a1 := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(0.0, 1.0, 5.0), "scenario")
	var a2 := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(0.3, 1.0, 5.0), "scenario")
	var b1 := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(-8.0, 1.0, 5.0), "scenario")
	var base := h.w.water_mass()
	h.step(60)
	var ps := _puddles()
	check(ps.size() == 2, "two touching blobs merged, the far one stayed separate (%d puddles)" % ps.size())
	var merged: MatBody = null
	for p in ps:
		if p.absorbed.size() > 0:
			merged = p
	check(merged != null, "one puddle recorded an absorbed body")
	if merged != null:
		near(merged.mass, 6.0, 1e-9, "merged mass is the sum")
		check(merged.absorbed.size() == 1 and (merged.absorbed[0] == a1.id or merged.absorbed[0] == a2.id), "absorbed id is the other blob")
		check(merged.radius > 0.3, "merged puddle is larger (%.3f)" % merged.radius)
	check(b1.alive and b1.form == Sim.Form.PUDDLE and absf(b1.mass - 3.0) < 1e-9, "the distant puddle is untouched")
	near(h.w.water_mass(), base, 1e-9, "conserved")


func test_puddles_are_capped_and_evaporation_is_accounted() -> void:
	h = SimHarness.new(2)
	var n := 14
	var spawned := 0
	for i in n:
		var x := -14.0 + 3.0 * float(i % 7)
		var z := 7.0 if i < 7 else 5.0
		h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(x, 1.0, z), "scenario")
		spawned += 1
	var base := h.w.water_mass()
	var max_seen := 0
	for k in 120:
		h.step()
		max_seen = maxi(max_seen, _puddles().size())
		if absf(h.w.water_mass() - base) > 1e-6:
			check(false, "water mass drifted by %.6f kg at tick %d" % [h.w.water_mass() - base, h.w.tick])
			break
	check(max_seen <= Sim.MAX_PUDDLES, "never more than MAX_PUDDLES (%d) puddles, saw %d" % [Sim.MAX_PUDDLES, max_seen])
	check(_puddles().size() == Sim.MAX_PUDDLES, "exactly MAX_PUDDLES remain when more landed (%d)" % _puddles().size())
	var evaporated := h.events("despawn").filter(func(x): return x.reason == "evaporated")
	check(evaporated.size() == n - Sim.MAX_PUDDLES, "the overflow puddles evaporated (%d)" % evaporated.size())
	near(h.w.mass_ledger.evaporated, 3.0 * float(n - Sim.MAX_PUDDLES), 1e-9, "evaporated mass is recorded in the ledger")
	near(h.w.water_mass(), base, 1e-6, "water mass conserved with the evaporation ledger")


func test_water_falling_into_the_pool_merges_into_it() -> void:
	h = SimHarness.new(2)
	var pool_before := h.w.pool.mass
	var blob := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.STREAM, 3.0, Vector3(10.0, 1.0, -1.0), "scenario")
	var base := h.w.water_mass()
	h.step(60)
	near(h.w.pool.mass, pool_before + 3.0, 1e-9, "the pool gained exactly the blob's mass")
	check(not blob.alive and blob.mass == 0.0, "the blob is gone")
	check(_puddles().is_empty(), "no puddle left behind")
	near(h.w.water_mass(), base, 1e-9, "conserved")


# ---------------------------------------------------------------- drawing water

func test_water_technique_draws_exactly_what_leaves_the_pool() -> void:
	_setup(Vector3(5, 0, -1), Vector3(-6, 0, -1))
	var pool := h.w.pool
	var pool0 := pool.mass
	var total0 := h.w.water_mass()
	h.press(w, "tech")
	var rate := float(Moves.DEFS.water_tech.draw_rate) * Sim.DT
	var prev := 0.0
	var drawing_ticks := 0
	var bad_step := false
	for k in 40:
		h.step()
		var held := h.w.held(w)
		if held == null:
			continue
		if not near(pool0 - pool.mass, held.mass, 1e-9, "tick %d: pool lost exactly what the stream gained" % h.w.tick):
			break
		if drawing_ticks > 0 and not bad_step:
			if absf((held.mass - prev) - rate) > 1e-9:
				bad_step = true
				check(false, "tick %d: draw increment %.6f, want rate %.6f" % [h.w.tick, held.mass - prev, rate])
		prev = held.mass
		drawing_ticks += 1
	check(drawing_ticks >= 10, "water was being drawn for %d ticks" % drawing_ticks)
	var held := h.w.held(w)
	check(held != null and held.is_water() and held.phase == Sim.Phase.LIQUID, "W holds a liquid water body")
	if held != null:
		check(held.origin == "draw:%d" % pool.id, "provenance: drawn from the pool (%s)" % held.origin)
		check(held.lineage.has(pool.id), "lineage includes the pool")
	near(h.w.water_mass(), total0, 1e-9, "conserved while drawing")
	check(h.has_event("draw_water"), "draw_water event emitted")
	check(w.water_carried == 6.0, "the waterskin was not touched while the pool was in reach")


func test_water_draw_never_exceeds_max_draw() -> void:
	_setup(Vector3(5, 0, -1), Vector3(-6, 0, -1))
	var pool0 := h.w.pool.mass
	h.press(w, "tech")
	h.step(150)   # well past the time needed to reach max_draw
	var held := h.w.held(w)
	check(held != null, "W is still holding water")
	if held == null:
		return
	var cap := float(Moves.DEFS.water_tech.max_draw)
	check(held.mass <= cap + 1e-6, "held water %.4f kg exceeds max_draw %.1f kg" % [held.mass, cap])
	near(pool0 - h.w.pool.mass, held.mass, 1e-9, "pool lost exactly the held mass")


func test_released_stream_hits_once_and_becomes_a_puddle_with_all_its_mass() -> void:
	_setup(Vector3(5, 0, -1), Vector3(-5, 0, -1))
	var total0 := h.w.water_mass()
	h.press(w, "tech")
	h.step(30)
	var stream := h.w.held(w)
	check(stream != null, "holding drawn water")
	if stream == null:
		return
	h.release(w, "tech")
	var tl := h.until(func(): return h.has_event("launch"), 30)
	check(tl > 0, "the stream is launched")
	var mass := stream.mass   # drawing continues until the release tick: measure at launch
	var t := h.until(func(): return h.has_event("hit"), 120)
	check(t > 0, "the stream hits E")
	h.step(40)
	check(h.events("hit").filter(func(x): return x.actor == e.id).size() == 1, "exactly one hit (dedup)")
	check(e.wetness > 0.9, "the target is soaked (%.2f)" % e.wetness)
	check(stream.form == Sim.Form.PUDDLE and stream.alive, "the stream landed as a puddle")
	near(stream.mass, mass, 1e-9, "the puddle holds the whole stream")
	near(h.w.water_mass(), total0, 1e-9, "conserved through the throw")


func test_draw_without_pool_uses_the_waterskin() -> void:
	_setup(Vector3(-4, 0, 8), Vector3(-4, 0, -6))   # far from the pool, no puddles
	var total0 := h.w.water_mass()
	h.press(w, "tech")
	h.step(30)
	var held := h.w.held(w)
	check(held != null and held.origin == "waterskin:%d" % w.id, "draws from the waterskin when nothing else is in reach")
	if held != null:
		near(held.mass, 6.0, 1e-9, "takes the whole waterskin")
	near(w.water_carried, 0.0, 1e-9, "waterskin empty")
	near(h.w.water_mass(), total0, 1e-9, "conserved")
	h.cancel_tech(w)
	h.step(120)
	check(w.action == null and w.held_body == -1, "cancel leaves no stuck state")
	near(h.w.water_mass(), total0, 1e-6, "conserved after cancelling (the water falls as a puddle)")
	check(_puddles().size() == 1, "the dropped water became a puddle")


func test_draw_with_nothing_in_reach_fails_cleanly() -> void:
	_setup(Vector3(-4, 0, 8), Vector3(-4, 0, -6))
	w.water_carried = 0.0
	var total0 := h.w.water_mass()
	h.press(w, "tech")
	h.step(60)
	check(h.events("insufficient").any(func(x): return x.actor == w.id and x.what == "water"), "no water in reach is reported")
	check(h.w.held(w) == null, "nothing is held")
	h.release(w, "tech")
	h.step(60)
	check(w.action == null and w.stun == 0.0, "no stuck action")
	near(h.w.water_mass(), total0, 1e-9, "conserved")


func test_standing_in_the_pool_refills_the_waterskin_from_the_pool() -> void:
	_setup(Vector3(10, 0, -1), Vector3(-6, 0, -1))
	w.water_carried = 1.0
	var pool0 := h.w.pool.mass
	var total0 := h.w.water_mass()
	h.step(3)
	check(w.in_water, "W stands in the pool")
	near(w.water_carried, 6.0, 1e-9, "waterskin refilled")
	near(h.w.pool.mass, pool0 - 5.0, 1e-9, "the pool paid exactly what the waterskin gained")
	near(h.w.water_mass(), total0, 1e-9, "conserved")


# ---------------------------------------------------------------- fire vs water shield

func _fire_duel(shield: bool) -> Dictionary:
	h = SimHarness.new(3)
	w = h.actor("W", Vector3(0, 0, 3), 0, {}, Sim.Element.WATER)
	var f := h.actor("F", Vector3(0, 0, -1), 1, {}, Sim.Element.FIRE)
	e = f
	h.step(20)
	h.log.clear()
	if shield:
		h.press(w, "guard")
		h.step(20)
	var base := h.w.water_mass()
	var e0 := h.w.system_energy()
	var shield_body := h.w.held(w)
	var mass0 := shield_body.mass if shield_body != null else 0.0
	h.press(f, "attack")
	h.step()
	h.release(f, "attack")
	h.until(func(): return h.has_event("flare"), 60)
	h.step(2)
	return {"base": base, "e0": e0, "shield": shield_body, "mass0": mass0}


func test_fire_flare_on_water_shield_makes_steam_and_blocks_damage() -> void:
	var d := _fire_duel(true)
	var shield: MatBody = d.shield
	check(shield != null, "W raised a water shield")
	if shield == null:
		return
	check(w.health == 100.0, "the shield blocked the flame (health %.1f)" % w.health)
	check(h.events("hit").filter(func(x): return x.actor == w.id).is_empty(), "no hit event on W")
	check(h.events("block").any(func(x): return x.actor == w.id and x.kind == "fire_water"), "block(fire_water) reported")
	check(h.has_event("steam_block"), "steam_block reported")
	var boiled: float = d.mass0 - shield.mass
	check(boiled > 0.5, "the shield lost water to steam (%.3f kg)" % boiled)
	check(boiled <= float(Moves.DEFS.fire_attack.heat) / Thermal.vapor_energy(1.0) + 1e-9, "no more water boiled than the flare's heat can vaporise")
	var clouds := h.w.bodies.filter(func(b): return b.alive and b.mat == Sim.Mat.STEAM)
	check(clouds.size() >= 1, "a steam cloud exists")
	var cloud_mass := 0.0
	for c in clouds:
		cloud_mass += c.mass
	near(cloud_mass, boiled, 1e-6, "steam carries the boiled mass (Vector2 float32 rounding allowed)")
	near(h.w.water_mass(), d.base, 1e-6, "water mass conserved while the cloud is alive")
	near(h.w.ledger.vapor, Thermal.vapor_energy(boiled), 1e-6, "vapour energy ledger matches the boiled mass")
	# The cloud dissipates; its mass moves into mass_ledger.vapor.
	check(h.w.mass_ledger.vapor == 0.0, "nothing dissipated yet")
	h.step(240)
	check(h.w.bodies.filter(func(b): return b.alive and b.mat == Sim.Mat.STEAM).is_empty(), "the cloud dissipated")
	near(h.w.mass_ledger.vapor, boiled, 1e-6, "mass_ledger.vapor accounts for the dissipated steam")
	near(h.w.water_mass(), d.base, 1e-6, "water mass conserved after dissipation")
	near(h.w.system_energy() - d.e0, h.w.ledger_balance(), 0.01, "energy ledger balances")
	# Letting go of the guard returns the remaining water to the waterskin.
	h.release(w, "guard")
	h.step(40)
	near(w.water_carried, d.mass0 - boiled, 1e-6, "the surviving shield water went back to the waterskin")
	near(h.w.water_mass(), d.base, 1e-6, "conserved after the guard ends")


func test_fire_flare_without_shield_hurts_the_same_target() -> void:
	var d := _fire_duel(false)
	check(d.shield == null, "no shield without guarding")
	check(w.health < 100.0, "unshielded W takes the flame (health %.1f)" % w.health)
	check(h.events("hit").any(func(x): return x.actor == w.id and x.kind == "fire"), "fire hit reported")
	check(not h.has_event("steam_block"), "no steam without water in the way")


func test_small_residue_of_a_boiled_puddle_is_not_lost() -> void:
	# A flare vaporises 36 HU / 26 = 1.385 kg of a 1.4 kg puddle; the 0.015 kg left over is below
	# the removal threshold. Whatever happens to the body, water_mass() must not drop.
	h = SimHarness.new(3)
	var f := h.actor("F", Vector3(0, 0, -1), 1, {}, Sim.Element.FIRE)
	h.step(5)
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 1.4, Vector3(0, 0, 2), "scenario")
	puddle.update_radius_puddle()
	var base := h.w.water_mass()
	h.press(f, "attack")
	h.step()
	h.release(f, "attack")
	h.until(func(): return h.has_event("flare"), 60)
	h.step(2)
	check(h.has_event("steam_block"), "the flame reached the puddle")
	check(not puddle.alive or puddle.mass < 1.4, "the puddle was boiled")
	near(h.w.water_mass(), base, 1e-9, "no water vanished (residue %.4f kg)" % (puddle.mass if puddle.alive else 0.0))
	h.step(240)
	near(h.w.water_mass(), base, 1e-9, "still conserved after the steam dissipates")


func test_small_residue_of_a_drained_puddle_is_not_lost() -> void:
	# Drawing 0.2333 kg per tick from a 0.5 kg puddle leaves 0.0333 kg, which is under the
	# "drained" threshold: that remainder must not disappear from the books.
	h = SimHarness.new(3)
	var d := h.actor("D", Vector3(-4, 0, 5), 0, {}, Sim.Element.WATER)
	h.step(5)
	var puddle := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 0.5, Vector3(-4, 0, 6.5), "scenario")
	puddle.update_radius_puddle()
	d.water_carried = 0.0   # force the draw to use the puddle, nothing else
	var base := h.w.water_mass()
	h.press(d, "tech")
	h.step(60)
	check(not puddle.alive, "the puddle was drained")
	check(h.w.held(d) != null, "D holds the drawn water")
	near(h.w.water_mass(), base, 1e-9, "water mass conserved when the puddle is drained")
