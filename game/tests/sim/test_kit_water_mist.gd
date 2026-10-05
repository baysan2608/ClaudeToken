extends WaterKitTest
## Water / Mist (sub 2, docs/MOVESET.md §7.7): steam = water + heat (paid, booked vapor), fog = a fog ZONE that
## conceals, dampens fire and conducts lightning at 60 %. Scald Puff, Steam Jet, Geyser, Boiling Pillars, Fog Lance,
## Creeping Fog, Veil, Steam Screen (+ Condense), Steam Blast, Dew Fall, Vapor Draw (+ Condense), Mist Step, Fog Walk.


func _mist_duel(dist: float = 8.0, rival_element: int = Sim.Element.EARTH, seed_value: int = 3) -> Array:
	var s := duel(2, rival_element, seed_value, dist)
	var w: ActorState = s[0]
	w.heat_reserve = 300.0      # steam moves pay heat from the reserve first (then Focus at 10 HU each)
	return s


func _tap(p: ActorState, gesture: int, hold_ticks: int = 2) -> void:
	h.flick(p, "attack", gesture)
	h.step(hold_ticks)
	h.release(p, "attack")


func _hold(p: ActorState, gesture: int, secs: float) -> void:
	h.flick(p, "attack", gesture)
	h.step(int(secs * 60.0))
	h.release(p, "attack")


func _fog_at(owner: ActorState, p: Vector3, kg: float = 2.0, radius: float = 4.0, life: float = 30.0) -> MatBody:
	owner.water_carried -= kg
	var z := WaterUtil.zone(h.w, "fog", p, radius, owner.id, life, {"actor_status": "concealed", "status_t": 0.4, "spare_owner": false,
		"height": 3.0, "rate": 0.15}, Sim.Mat.STEAM, kg, 6.0)
	return z


# ---------------------------------------------------------------- strike

func test_scald_puff_boils_half_a_kilo_and_scalds() -> void:
	var s := _mist_duel(2.5)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	var vapor0: float = h.w.ledger.vapor
	var skin0 := w.water_carried
	_tap(w, Sim.Gesture.NONE if false else Sim.Gesture.NONE)       # plain tap (no gesture): the strike
	h.step(50)
	check(h.has_event("action", "move", "scald_puff"), "Scald Puff started")
	near(skin0 - w.water_carried, 0.5, 0.01, "0.5 kg of water boiled")
	check(float(h.w.ledger.vapor) > vapor0, "booked in the vapor ledger")
	check(r.health < 100.0 or Status.has(r, "scalded") or h.has_event("hit"), "the rival is scalded (hp %.1f)" % r.health)
	check(bodies_of(Sim.Mat.STEAM).size() >= 1 or zones_tagged("steam").size() >= 1, "a steam cloud / obscuring zone")
	check(h.events("fx").any(func(e): return e.mat == "steam"), "steam fx cue")
	h.step(240)
	ledgers_ok(base, "Scald Puff")


func test_steam_jet_needs_water_and_a_geyser_launches() -> void:
	var s := _mist_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	w.water_carried = 0.0
	_hold(w, Sim.Gesture.NONE, 0.5)
	h.step(40)
	check(h.has_event("insufficient", "what", "water"), "no water: the puff fizzles")
	# Geyser (T2): erupts under the target and launches it.
	s = _mist_duel(8.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.press(w, "attack")
	h.step(66)
	h.release(w, "attack")
	h.step(10)
	check(zones_tagged("geyser").size() == 1, "a geyser pocket is buried at the target (%d)" % zones_tagged("geyser").size())
	var vy := 0.0
	for k in 80:
		h.step()
		vy = maxf(vy, r.vel.y)
	check(vy > 5.0, "the eruption launches the target (peak vy %.1f)" % vy)
	check(zones_tagged("geyser").is_empty(), "the pocket is spent")
	check(h.has_event("fx", "fx", "erupt"), "erupt cue")
	h.step(240)
	ledgers_ok(base, "Geyser")
	# Boiling Pillars (T3): three geysers in a line.
	s = _mist_duel(8.0)
	w = s[0]
	r = s[1]
	r.is_dummy = true
	base = snap(h.w)
	h.press(w, "attack")
	h.step(112)
	h.release(w, "attack")
	h.step(8)
	check(zones_tagged("geyser").size() == 3, "three pillars (%d)" % zones_tagged("geyser").size())
	h.step(300)
	ledgers_ok(base, "Boiling Pillars")


func test_fog_lance_wets_chills_and_blinds() -> void:
	var s := _mist_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	r.wetness = 0.0
	var base := snap(h.w)
	_tap(w, Sim.Gesture.UP)
	h.step(50)
	check(r.wetness > 0.9, "wet (%.2f)" % r.wetness)
	check(h.has_event("status", "status", "chilled"), "chilled")
	check(h.has_event("status", "status", "blinded"), "blinded 0.8 s")
	h.step(240)
	ledgers_ok(base, "Fog Lance")


# ---------------------------------------------------------------- ground / sweep: fog

func test_creeping_fog_rolls_settles_conceals_and_conducts_lightning() -> void:
	var s := _mist_duel(10.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.kit = {"lightning": true}
	r.is_dummy = true
	var base := snap(h.w)
	_tap(w, Sim.Gesture.DOWN)
	h.step(40)
	var fog := zones_tagged("fog")
	check(fog.size() == 1, "one fog zone")
	if fog.is_empty():
		return
	var z := fog[0]
	check(z.mat == Sim.Mat.STEAM and z.mass > 1.0 and z.zone_radius >= 4.0, "a STEAM zone of booked water (r %.1f, %.1f kg)" % [z.zone_radius, z.mass])
	var p0 := z.pos
	h.step(60)
	check(z.pos.distance_to(p0) > 0.5, "it rolled forward (%.1f m)" % z.pos.distance_to(p0))
	h.step(30)
	var p1 := z.pos
	h.step(30)
	check(z.pos.distance_to(p1) < 0.5, "and settled")
	check(Materials.conducts(z) and is_equal_approx(Materials.conduction_factor(z), 0.6), "conducts at 60 %")
	# Fighters inside are concealed: lock-on breaks beyond 2 m.
	var a := h.w.add_actor("A", z.pos + Vector3(1.0, 0, 0), 1, {}, Sim.Element.EARTH)
	h.intents[a.id] = ActorIntent.new()
	a.is_dummy = true
	var b := h.w.add_actor("B", z.pos + Vector3(-1.0, 0, 0.5), 1, {}, Sim.Element.EARTH)
	h.intents[b.id] = ActorIntent.new()
	b.is_dummy = true
	var outsider := h.w.add_actor("O", z.pos + Vector3(7.0, 0, 0), 1, {}, Sim.Element.EARTH)
	h.intents[outsider.id] = ActorIntent.new()
	outsider.is_dummy = true
	h.step(10)
	check(Status.hidden(a) and Status.has(a, "fogbound"), "inside the fog: concealed and fogbound")
	check(not h.w._lockable(w, a), "lock-on breaks beyond 2 m")
	check(not Status.hidden(outsider), "outside: not concealed")
	# Lightning cast into the fog hits everyone inside at 60 %.
	var def := {"range": 14.0, "damage": 24.0, "balance": 40.0, "conduct_budget": 26.0, "max_hops": 4}
	r.pos = z.pos + Vector3(0, 0, -9.0)
	r.lock_target = -1
	var hp_a := a.health
	var hp_b := b.health
	var hp_o := outsider.health
	var out := Conduction.discharge(h.w, r, z.pos, def, h.w.new_attack_id(), true)
	h.step()
	check(a.health < hp_a and b.health < hp_b, "everyone inside is hit (%.1f, %.1f)" % [hp_a - a.health, hp_b - b.health])
	near(hp_a - a.health, 13.0 * 0.6, 0.5, "A takes 60% of its share (13)")
	check(outsider.health == hp_o, "the outsider is not hit")
	h.step(600)
	ledgers_ok(base_after_actors(base), "Creeping Fog")


## The fog test adds actors (each carries a waterskin and a satchel): extend the baseline by them.
func base_after_actors(base: Dictionary) -> Dictionary:
	var b := base.duplicate()
	var n := h.w.actors.size() - 2
	b.water = float(b.water) + 6.0 * float(n)
	b.metal = float(b.metal) + 12.0 * float(n)
	return b


func test_veil_wraps_the_caster_in_mist() -> void:
	var s := _mist_duel(8.0)
	var w: ActorState = s[0]
	var base := snap(h.w)
	_tap(w, Sim.Gesture.SIDE)
	h.step(40)
	var mist := zones_tagged("mist")
	check(mist.size() == 1 and int(mist[0].props.get("attach", -1)) == w.id, "a mist zone attached to the caster")
	check(Status.hidden(w), "hard to target (concealed)")
	h.step(300)
	check(zones_tagged("mist").is_empty(), "it dissolves after 3 s")
	ledgers_ok(base, "Veil")


# ---------------------------------------------------------------- guard: Steam Screen

func test_steam_screen_dampens_a_flame_slows_solids_and_scalds() -> void:
	var s := _mist_duel(8.0, Sim.Element.FIRE)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(14)
	var sc := zones_tagged("steam_screen")
	check(sc.size() == 1 and int(sc[0].props.get("attach", -1)) == w.id, "a steam screen zone around the fighter")
	if sc.is_empty():
		return
	check(Interactions.counter_class(sc[0], h.w) == &"screen_steam", "counter class screen_steam")
	# A flame ball passing through loses heat (x1.5 eff, k 0.5).
	var f := h.w.spawn_body(Sim.Mat.FIRE, Sim.Form.CHUNK, 1.0, w.pos + Vector3(0, 1.0, -1.2), "test")
	f.heat_payload = 160.0
	f.vel = Vector3(0, 0, 6.0)
	f.gravity_scale = 0.0
	f.attack_id = h.w.new_attack_id()
	f.attack_owner = r.id
	f.hit_set[r.id] = true
	f.max_life = 3.0
	h.w.ledger.generated += 160.0       # the test made this heat: book it
	var e0 := f.heat_payload
	h.step(12)
	check(f.heat_payload < e0 * 0.8, "the flame lost heat in the steam (%.0f -> %.0f HU)" % [e0, f.heat_payload])
	check(h.events("interaction").any(func(e): return e.counter == "screen_steam" and e.threat == "flame"), "interaction flame x screen_steam")
	# A stone is slowed.
	var st := h.launch_at(w, "stone", 10.0, 12.0, Sim.AMBIENT_C, "", r, 3.0)
	var v0 := st.vel.length()
	h.step(10)
	check(st.vel.length() < v0 * 0.95 or not st.alive or st.attack_id == 0, "a stone slows in the screen (%.1f -> %.1f)" % [v0, st.vel.length()])
	# A rival walking in is scalded.
	r.is_dummy = false
	r.pos = w.pos + Vector3(0, 0, -1.0)
	h.step(30)
	check(Status.has(r, "scalded") or h.has_event("status", "status", "scalded"), "walkers are scalded")
	h.release(w, "guard")
	h.step(60)
	check(zones_tagged("steam_screen").is_empty(), "the screen ends with the guard")
	h.step(300)
	ledgers_ok(base, "Steam Screen")


func test_condense_perfect_guard_pulls_steam_into_the_waterskin() -> void:
	# Condense: a perfect Steam Screen absorbs water / steam / mist into the waterskin.
	var s := _mist_duel(8.0, Sim.Element.WATER)
	var w: ActorState = s[0]
	w.water_carried = 1.0
	h.step(2)
	var base := snap(h.w)
	var steam := h.w.spawn_body(Sim.Mat.STEAM, Sim.Form.CLOUD, 2.0, w.pos + Vector3(0, 1.2, -2.0), "test")
	h.w.mass_ledger.moisture_taken += 2.0
	steam.max_life = 30.0
	var g := Interactions.predict(h.w, Agent.of_body(h.w, steam), Agent.of_move(h.w, w, "steam_screen", 0, true))
	check(String(g.outcome) == "water_skin", "predicted: perfect screen x steam -> water_skin (%s)" % g.outcome)
	var res := Interactions.resolve(h.w, Agent.of_body(h.w, steam), _screen_agent(w, true))
	check(not steam.alive, "the steam is gone")
	near(w.water_carried, 3.0, 0.01, "its 2 kg are in the waterskin")
	h.step(5)
	ledgers_ok(base, "Condense")


func _screen_agent(w: ActorState, perfect: bool) -> Agent:
	var a := Agent.of_move(h.w, w, "steam_screen", 0, perfect)
	a.actor = w
	return a


func test_steam_blast_bursts_the_screen_forward() -> void:
	var s := _mist_duel(4.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var base := snap(h.w)
	h.press(w, "guard")
	h.step(14)
	h.flick(w, "guard", Sim.Gesture.UP)
	h.step(40)
	h.release(w, "guard")
	h.step(60)
	check(zones_tagged("steam_screen").is_empty(), "the screen is gone (burst forward)")
	check(h.has_event("action", "move", "steam_blast"), "Steam Blast ran")
	check(r.health < 100.0 or Status.has(r, "scalded") or h.has_event("status", "status", "scalded"), "the cone scalds / hits (hp %.1f)" % r.health)
	h.step(240)
	ledgers_ok(base, "Steam Blast")


# ---------------------------------------------------------------- sink: Dew Fall

func test_dew_fall_rains_every_vapour_within_6_m() -> void:
	var s := _mist_duel(10.0)
	var w: ActorState = s[0]
	var fog := _fog_at(w, w.pos + Vector3(0, 0, -3.0), 2.0, 3.0, 30.0)
	h.w._spawn_steam(w.pos + Vector3(2.0, 1.0, 0.0), 1.5)
	h.w.mass_ledger.moisture_taken += 1.5
	var far := _fog_at(w, w.pos + Vector3(0, 0, -9.0), 1.0, 2.0, 30.0)
	h.step(3)
	var base := snap(h.w)
	var pud0 := bodies_of(Sim.Mat.WATER, Sim.Form.PUDDLE).size()
	h.press(w, "guard")
	h.step(12)
	h.flick(w, "guard", Sim.Gesture.DOWN)
	h.step(30)
	h.release(w, "guard")
	h.step(20)
	check(not fog.alive, "the fog within 6 m is gone")
	check(far.alive, "the far fog is not touched")
	check(bodies_of(Sim.Mat.STEAM).filter(func(b): return b.form == Sim.Form.CLOUD and b.pos.distance_to(w.pos) < 6.0).is_empty(), "no steam within 6 m")
	check(bodies_of(Sim.Mat.WATER, Sim.Form.PUDDLE).size() > pud0, "it fell as puddles")
	check(h.has_event("dew"), "dew event")
	h.step(120)
	ledgers_ok(base, "Dew Fall")


# ---------------------------------------------------------------- technique

func test_vapor_draw_pulls_fog_into_a_ball_and_condense_makes_water() -> void:
	var s := _mist_duel(8.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var fog := _fog_at(w, w.pos + Vector3(0, 0, -4.0), 3.0, 2.5, 30.0)
	h.step(3)
	var base := snap(h.w)
	h.press(w, "tech")
	h.step(70)
	var held := h.w.held(w)
	check(held != null and held.mat == Sim.Mat.STEAM and held.mass > 1.0, "a held vapour ball (%s)" % [held.describe() if held else "none"])
	check(fog.mass < 2.0 or not fog.alive, "the fog gave its water (%.2f kg left)" % [fog.mass if fog.alive else 0.0])
	if held == null:
		return
	# T+A: condense it into a water blob.
	h.press(w, "attack")
	h.step(3)
	h.release(w, "attack")
	h.step(3)
	held = h.w.held(w)
	check(held != null and held.is_water() and held.phase == Sim.Phase.LIQUID, "condensed into a water blob")
	h.release(w, "tech")
	h.step(200)
	ledgers_ok(base, "Vapor Draw")


func test_vapor_ball_is_a_steam_bomb() -> void:
	var s := _mist_duel(6.0)
	var w: ActorState = s[0]
	var r: ActorState = s[1]
	r.is_dummy = true
	var fog := _fog_at(w, w.pos + Vector3(0, 0, -2.5), 3.0, 2.5, 30.0)
	h.step(3)
	var base := snap(h.w)
	h.press(w, "tech")
	h.step(60)
	h.release(w, "tech")
	h.step(120)
	check(h.has_event("launch"), "released: the ball is thrown")
	check(r.health < 100.0 or Status.has(r, "scalded") or h.has_event("status", "status", "scalded"), "the steam bomb scalds (hp %.1f)" % r.health)
	h.step(300)
	ledgers_ok(base, "steam bomb")


# ---------------------------------------------------------------- evade

func test_mist_step_hides_and_fog_walk_conceals() -> void:
	var s := _mist_duel(10.0)
	var w: ActorState = s[0]
	var p0 := w.pos
	var base := snap(h.w)
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.step(1)
	h.it(w).move = Vector3.ZERO
	h.step(5)
	check(Status.hidden(w) and w.iframes > 0.0, "dissolved: hidden with i-frames")
	h.step(40)
	var d := Vector2(w.pos.x - p0.x, w.pos.z - p0.z).length()
	check(d > 2.5 and d < 4.8, "reformed about 4 m away (%.2f)" % d)
	check(zones_tagged("mist").size() >= 1, "fog puffs at the ends")
	h.step(120)
	ledgers_ok(base, "Mist Step")
	# Fog Walk: hold evade.
	s = _mist_duel(10.0)
	w = s[0]
	h.it(w).move = Vector3(1, 0, 0)
	h.press(w, "evade")
	h.it(w).evade_held = true
	h.step(40)
	check(w.action != null and w.action.id == "fog_walk", "morphed into Fog Walk (%s)" % [w.action.id if w.action else "none"])
	check(Status.has(w, "fogwalk") and Status.hidden(w), "untargetable by lock-on (fogwalk)")
	check(zones_tagged("mist").size() >= 1, "inside its own mist")
	h.it(w).evade_held = false
	h.step(60)
	check(zones_tagged("mist").is_empty(), "the mist is gone when you stop")
