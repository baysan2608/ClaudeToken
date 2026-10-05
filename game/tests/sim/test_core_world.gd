extends TestCase
## Moveset engine in the world: projectile clash, zones (ticks, rules, surfaces), statuses, hooks,
## event catalogue (docs/COMBAT_SPEC.md "Engine" §E7-E8).

var h: SimHarness


func _shot(owner: ActorState, pos: Vector3, vel: Vector3, mass: float) -> MatBody:
	var b := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, mass, pos, "test")
	h.w.mass_ledger.ground_taken += mass
	b.vel = vel
	b.gravity_scale = 0.0
	b.attack_id = h.w.new_attack_id()
	b.attack_owner = owner.id
	b.hit_set[owner.id] = true
	b.damage = 10.0
	return b


func test_projectiles_of_different_owners_clash() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 8), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0, 0, -8), 1, {}, Sim.Element.EARTH)
	a.is_dummy = true
	b.is_dummy = true
	var light := _shot(a, Vector3(3, 1.5, 2), Vector3(0, 0, -17), 20.0)
	var heavy := _shot(b, Vector3(3, 1.5, -2), Vector3(0, 0, 14), 45.0)
	h.until(func(): return h.has_event("clash"), 30)
	var ev := h.last_event("clash")
	check(not ev.is_empty() and ev.winner == heavy.id, "the heavier shot wins (%s)" % [ev])
	check(light.attack_id == 0, "the lighter one is knocked aside")
	check(heavy.attack_id != 0 and heavy.vel.z > 0.0 and heavy.vel.length() < 14.0, "the heavier keeps going, slower")
	check(h.events("interaction").any(func(e): return e.outcome == "clash"), "through the rules (clash default)")
	# Same owner never clash; near-equal powers both drop.
	var s1 := _shot(a, Vector3(5, 1.5, 2), Vector3(0, 0, -17), 20.0)
	var s2 := _shot(b, Vector3(5, 1.5, -2), Vector3(0, 0, 17), 20.0)
	h.until(func(): return s1.attack_id == 0 or s2.attack_id == 0, 30)

	check(s1.attack_id == 0 and s2.attack_id == 0, "even clash: both drop")


func test_zone_effects_rules_and_lifetime() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	var a := h.actor("A", Vector3(0, 0, 0), 0, {}, Sim.Element.EARTH)
	var o := h.actor("O", Vector3(6, 0, 0), 1, {}, Sim.Element.EARTH)
	o.is_dummy = true
	var calls := [0]
	CombatWorld.register_zone_effect(&"t_field", func(_w: CombatWorld, _z: MatBody, _dt: float) -> void: calls[0] += 1)
	Interactions.add_rule("stone", "t_field", {"bands": [[0.0, "sink"]], "full_at": 0.0})
	var z := h.spawn_zone("t_field", Vector3(6, 0, 0), 2.5, a)
	z.max_life = 0.5
	z.props["actor_status"] = "slowed"
	z.props["status_t"] = 0.3
	var loose := h.w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 10.0, Vector3(6.5, 0.3, 0.5), "test")
	h.w.mass_ledger.ground_taken += 10.0
	var m0 := h.w.stone_mass()
	h.step(3)
	check(h.events("zone").any(func(e): return e.phase == "open" and e.kind == "t_field"), "zone open event")
	check(calls[0] >= 3, "zone effect hook runs every tick (%d)" % calls[0])
	check(Status.has(o, "slowed") and not Status.has(a, "slowed"), "actor inside gets the zone status, the owner is spared")
	check(h.events("status").any(func(e): return e.actor == o.id and e.status == "slowed" and e.on), "status event")
	check(not loose.alive, "body <-> zone rule (sink) applied")
	near(h.w.stone_mass(), m0, 1e-9, "booked")
	h.step(40)
	check(not z.alive and h.events("zone").any(func(e): return e.phase == "close"), "expired: zone close")
	h.step(30)
	check(not Status.has(o, "slowed"), "status timed out")
	h.end_scope()


func test_statuses_modify_movement_hits_and_targeting() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 0), 0, {}, Sim.Element.EARTH)
	var b := h.actor("B", Vector3(0, 0, -6), 1, {}, Sim.Element.EARTH)
	b.is_dummy = true
	h.it(a).move = Vector3(1, 0, 0)
	h.step(60)
	var free_x := a.pos.x
	a.pos = Vector3(0, 0, 0)
	a.vel = Vector3.ZERO
	Status.apply(h.w, a, "slowed", 2.0)
	h.step(60)
	check(a.pos.x < free_x * 0.75, "slowed: %.2f m vs %.2f m" % [a.pos.x, free_x])
	Status.remove(h.w, a, "slowed")
	a.pos = Vector3(0, 0, 0)
	a.vel = Vector3.ZERO
	Status.apply(h.w, a, "rooted", 1.0)
	h.step(30)
	check(a.pos.length() < 0.05, "rooted: no walking")
	h.it(a).move = Vector3.ZERO
	# Anchored: no knockback; armored: less kinetic damage.
	b.anchored = true
	Status.apply(h.w, b, "armored", 5.0)
	var hp := b.health
	h.w.hit_actor(b, {"attacker": a.id, "attack_id": h.w.new_attack_id(), "damage": 10.0, "balance": 5.0,
		"knock": Vector3(0, 0, -6), "kind": "stone", "from": a.chest()})
	check(Vector2(b.vel.x, b.vel.z).length() < 1e-6, "anchored: no knockback")
	near(hp - b.health, 6.0, 1e-6, "armored 40 %: 10 -> 6")
	b.anchored = false
	# Targeting
	Status.apply(h.w, a, "blinded", 0.5)
	h.step()
	check(a.lock_target == -1, "blinded: no lock-on")
	h.step(40)
	check(a.lock_target == b.id, "lock returns after the blind")
	Status.apply(h.w, b, "concealed", 1.0)
	h.step()
	check(a.lock_target == -1, "concealed beyond 2 m: not lockable")
	# Burning damage over time, wet mirrors wetness.
	var hb := b.health
	Status.apply(h.w, b, "burning", 1.0)
	h.step(60)
	check(hb - b.health > 2.0, "burning hurts (%.2f)" % (hb - b.health))
	b.wetness = 1.0
	h.step()
	check(Status.has(b, "wet") and h.events("status").any(func(e): return e.status == "wet" and e.on), "wet status from wetness")
	check(Status.immune(b, "lift") == false and Status.spec("anchored").immune.has("pull"), "immunity flags")


func test_zone_surface_walkable_over_the_pool() -> void:
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(5.5, 0, -1), 0, {}, Sim.Element.WATER)
	var z := h.spawn_zone("ice_floor", Vector3(10, h.w.arena.pool_level, -1), 4.0, a)
	z.props["walk_height"] = 0.0
	z.props["friction"] = 0.2
	z.props["surface"] = "ice"
	h.it(a).move = Vector3(1, 0, 0)
	h.step(70)
	check(h.w.arena.in_pool(a.pos.x, a.pos.z), "walked over the pool (x %.2f)" % a.pos.x)
	check(not a.in_water and absf(a.pos.y - h.w.arena.pool_level) < 0.05, "on the ice floor, not in the water (y %.2f)" % a.pos.y)
	check(a.surface == "zone:ice", "surface reports the zone (%s)" % a.surface)
	h.it(a).move = Vector3.ZERO
	h.w.close_zone(z, "test")
	h.step(30)
	check(a.in_water, "without it: in the water")


func test_hooks_body_tick_and_tech_preview() -> void:
	h = SimHarness.new(1)
	h.begin_scope()
	var a := h.actor("A", Vector3(0, 0, 0), 0, {}, Sim.Element.EARTH)
	CombatWorld.register_body_tick(&"t_hover", func(_w: CombatWorld, b: MatBody, dt: float) -> bool:
		b.spin += 3.0 * dt
		return true)
	var b := h.w.spawn_body(Sim.Mat.AIR, Sim.Form.CHUNK, 1.0, Vector3(0, 3, 2), "test")
	b.tag = &"t_hover"
	h.step(30)
	check(absf(b.pos.y - 3.0) < 1e-6 and b.spin > 1.4, "custom body tick replaced the default motion")
	CombatWorld.register_tech_preview(0, 2, func(_w: CombatWorld, _a: ActorState, _d: Vector3) -> Dictionary:
		return {"mode": "SANDFORM", "body": -1, "ok": true, "reason": ""})
	a.subs[0] = 2
	check(h.w.tech_preview(a, a.forward()).mode == "SANDFORM", "registered preview")
	a.subs[0] = 0
	check(h.w.tech_preview(a, a.forward()).is_empty(), "nothing registered for Earth/Stone")
	a.element = Sim.Element.FIRE
	check(h.w.tech_preview(a, a.forward()).has("mode"), "legacy Fire thermal preview")
	h.end_scope()


func test_event_catalogue() -> void:
	for k in ["cast", "release", "cone", "beam", "burst", "ring", "erupt", "trail", "splash", "aura"]:
		check(FxEvents.is_known("fx", k), "fx %s" % k)
	check(FxEvents.is_known("mat", "vacuum") and FxEvents.is_known("shape", "crescent") and FxEvents.is_known("tag", "quicksand"), "mats, shapes, tags")
	check(not FxEvents.is_known("fx", "explode") and not FxEvents.is_known("mat", "plasma"), "unknown keys rejected")
	for k in ["charge", "fx", "interaction", "status", "zone"]:
		check(FxEvents.is_known("event", k), "event %s" % k)
	h = SimHarness.new(1)
	var a := h.actor("A", Vector3(0, 0, 4), 0, {}, Sim.Element.FIRE)
	h.actor("B", Vector3(0, 0, 1), 1, {}, Sim.Element.EARTH)
	h.step(10)
	h.press(a, "attack")
	h.step(1)
	h.release(a, "attack")
	h.step(20)
	var fx := h.events("fx")
	check(not fx.is_empty() and fx[0].fx == "cone" and fx[0].mat == "flame" and fx[0].has("seed"), "legacy flare emits a cone fx")
	var hit := h.last_event("hit")
	for k in ["power", "mat", "tier", "dir"]:
		check(hit.has(k), "hit gains %s" % k)


func test_lightning_into_fog_hits_everyone_inside_at_60_percent() -> void:
	h = SimHarness.new(1)
	var c := h.actor("C", Vector3(0, 0, 9), 0, {"lightning": true}, Sim.Element.FIRE)
	var v1 := h.actor("V1", Vector3(1, 0, 0), 0, {}, Sim.Element.EARTH)
	var v2 := h.actor("V2", Vector3(-1, 0, 1), 0, {}, Sim.Element.EARTH)
	var outside := h.actor("V3", Vector3(6, 0, 0), 0, {}, Sim.Element.EARTH)
	h.spawn_zone("fog", Vector3(0, 0, 0), 3.0, null, 4.0)
	h.step(5)
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	var out := Conduction.discharge(h.w, c, Vector3(0, 0, 0.5), def, h.w.new_attack_id(), false)
	check(out.hits.has(v1.id) and out.hits.has(v2.id) and not out.hits.has(outside.id), "everyone inside the fog (hits %s)" % [out.hits])
	near(100.0 - v1.health, 13.0 * 0.6, 1e-6, "budget shared, x0.6 in fog")
	check(outside.health == 100.0, "outside the fog: untouched")


func test_held_water_conducts_into_its_holder_from_a_puddle() -> void:
	h = SimHarness.new(1)
	var c := h.actor("C", Vector3(0, 0, 9), 0, {"lightning": true}, Sim.Element.FIRE)
	var holder := h.actor("H", Vector3(3, 0, 0), 0, {}, Sim.Element.WATER)
	var pd := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 6.0, Vector3(0, 0.0, 0), "test")
	pd.update_radius_puddle()
	var blob := h.w.spawn_body(Sim.Mat.WATER, Sim.Form.BLOB, 4.0, Vector3(0.4, 0.2, 0), "test")
	h.w.take_control(holder, blob, 0.9, "test")
	h.step()
	blob.pos = Vector3(0.4, 0.2, 0)
	var out := Conduction.discharge(h.w, c, Vector3(0, 0, 0), {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4},
		h.w.new_attack_id(), false)
	check(out.hits.has(holder.id), "the water in hand touched the puddle: the holder is shocked (%s)" % [out.hits])
