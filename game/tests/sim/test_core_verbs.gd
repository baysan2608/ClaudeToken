extends TestCase
## Moveset engine: a test kit registers one move per verb (every slot, sub-elements 1-3 of every
## element, tiers T0-T3) and two fighters play it with random input for 60 s. Invariants: finite
## state, every mass ledger and the energy ledger exact, body cap, nobody stuck, every fx/interaction
## key catalogued, deterministic for a seed (docs/COMBAT_SPEC.md "Engine" §E9).

const SOAK_TICKS := 3600
const TIERS := {"t1": {"power": 11.0, "damage": 10.0}, "t2": {"power": 18.0, "damage": 13.0}, "t3": {"power": 28.0, "damage": 16.0}}


static func _t(extra: Dictionary = {}) -> Dictionary:
	var t := TIERS.duplicate(true)
	for k in extra:
		t[k].merge(extra[k], true)
	return t


## The test kit: data-only moves on every verb.
static func register_test_kit() -> void:
	var D := {}
	# Earth-like (element 0, sub 1)
	D["tk_disc"] = {"element": 0, "verb": "projectile", "startup": 0.16, "active": 0.05, "recovery": 0.24, "cancel": 0.6, "chain": 0.25,
		"cost": 4.0, "metal": 0.0, "source": "metal", "mat": "metal", "mass": 2.0, "speed": 24.0, "homing": 10.0, "tag": "disc",
		"tiers": _t({"t1": {"count": 2}, "t2": {"count": 3, "ricochet": 1}, "t3": {"count": 4, "pierce": 1}}), "fx": {"mat": "metal"}}
	D["tk_spear"] = {"element": 0, "verb": "projectile", "startup": 0.2, "active": 0.05, "recovery": 0.3, "cost": 6.0, "source": "ground",
		"mat": "stone", "mass": 12.0, "speed": 26.0, "gravity": 0.4, "tag": "spear", "on_impact": "stick", "tiers": _t({"t3": {"on_impact": "shatter"}})}
	D["tk_surge"] = {"element": 0, "verb": "ground_line", "startup": 0.25, "active": 0.05, "recovery": 0.3, "cost": 8.0, "source": "ground",
		"mat": "sand", "mass": 10.0, "tag": "sand_surge", "speed": 9.0, "budget": 10.0, "width": 2.0, "leave_zone": "quicksand",
		"zone_life": 2.0, "tiers": _t({"t2": {"width": 2.5}}), "fx": {"mat": "sand", "release": "erupt"}}
	D["tk_fan"] = {"element": 0, "verb": "cone", "startup": 0.15, "active": 0.1, "recovery": 0.25, "cost": 5.0, "cls": "sand", "range": 5.0,
		"angle": 40.0, "power": 6.0, "status": "blinded", "status_t": 0.6, "tiers": _t(), "fx": {"mat": "sand"}}
	D["tk_dune"] = {"element": 0, "verb": "barrier", "barrier": "wall", "mat": "sand", "tag": "sand", "mass": 100.0, "cost": 7.0,
		"tiers": {"t1": {"mass": 130.0}, "t2": {"mass": 160.0}}, "counter": {"cls": "wall_sand"}}
	D["tk_push"] = {"element": 0, "verb": "burst", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 4.0, "at": "ahead",
		"distance": 2.5, "radius": 2.0, "power": 12.0, "fx": {"mat": "sand"}}
	D["tk_pit"] = {"element": 0, "verb": "zone", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 6.0, "tag": "quicksand",
		"radius": 2.5, "life": 2.5, "at": "ahead", "actor_status": "slowed", "status_t": 0.3, "power": 18.0, "tiers": _t({"t2": {"radius": 3.0}})}
	D["tk_seize"] = {"element": 0, "verb": "grip", "startup": 0.1, "active": 0.06, "recovery": 0.28, "cost": 4.0, "ccls": "grip_stone",
		"reach": 7.5, "rip_source": "ground", "rip_mass": 15.0, "shape": "split", "pieces": 3, "speed": 18.0}
	D["tk_burrow"] = {"element": 0, "verb": "dash", "startup": 0.0, "active": 0.25, "recovery": 0.1, "cost": 5.0, "distance": 3.5,
		"burrow": true, "trail": "dust", "trail_life": 0.8}
	D["tk_skin"] = {"element": 0, "verb": "stance", "startup": 0.0, "active": 0.0, "recovery": 0.1, "upkeep": 6.0, "stance": "stone_skin",
		"armor": 0.4, "anchored": true, "anchor_cp": 40.0}
	# Water-like (element 1, sub 1)
	D["tk_shard"] = {"element": 1, "verb": "projectile", "startup": 0.15, "active": 0.05, "recovery": 0.25, "cost": 4.0, "source": "moisture",
		"mat": "water", "frozen": true, "mass": 1.0, "speed": 26.0, "tag": "needle", "tiers": _t({"t1": {"mass": 4.0}, "t3": {"count": 3, "mass": 5.0}})}
	D["tk_jet"] = {"element": 1, "verb": "beam", "startup": 0.12, "active": 0.4, "recovery": 0.2, "cost": 4.0, "cls": "water", "range": 9.0,
		"pulse": 0.1, "power": 8.0, "wet": true, "tiers": _t()}
	D["tk_tide"] = {"element": 1, "verb": "ground_line", "startup": 0.2, "active": 0.05, "recovery": 0.3, "cost": 8.0, "source": "waterskin",
		"mat": "water", "mass": 4.0, "tag": "water_wave", "speed": 9.0, "budget": 10.0, "width": 2.0, "power": 18.0, "channel": "K", "tiers": _t()}
	D["tk_frost"] = {"element": 1, "verb": "cone", "startup": 0.12, "active": 0.1, "recovery": 0.2, "cost": 5.0, "cls": "frost", "range": 5.0,
		"angle": 45.0, "power": 6.0, "status": "chilled", "tiers": _t(), "fx": {"mat": "ice"}}
	D["tk_icewall"] = {"element": 1, "verb": "barrier", "barrier": "wall", "mat": "water", "tag": "ice", "source": "moisture", "mass": 50.0,
		"cost": 8.0, "tiers": {"t1": {"mass": 65.0}, "t2": {"mass": 80.0}}}
	D["tk_orb"] = {"element": 1, "verb": "projectile", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 4.0, "source": "waterskin",
		"mat": "water", "mass": 2.0, "speed": 14.0, "on_impact": "puddle"}
	D["tk_floor"] = {"element": 1, "verb": "zone", "startup": 0.1, "active": 0.05, "recovery": 0.2, "cost": 5.0, "tag": "ice_floor", "at": "self",
		"radius": 3.0, "life": 3.0, "walk_height": 0.0, "friction": 0.15, "surface": "ice"}
	D["tk_fog"] = {"element": 1, "verb": "summon", "startup": 0.15, "active": 0.0, "recovery": 0.2, "cost": 5.0, "upkeep": 3.0, "tag": "fog",
		"mat": "air", "radius": 3.0, "power": 4.0, "at": "aim", "range": 9.0, "steer_speed": 3.0, "linger": 2.0, "tiers": _t({"t2": {"radius": 4.0}})}
	D["tk_glide"] = {"element": 1, "verb": "dash", "startup": 0.0, "active": 0.25, "recovery": 0.08, "cost": 4.0, "distance": 5.0, "iframes": 0.15}
	D["tk_fly"] = {"element": 1, "verb": "mode", "startup": 0.0, "active": 0.0, "recovery": 0.1, "upkeep": 8.0, "kind": "hover", "height": 1.5}
	# Fire-like (element 2, sub 1)
	D["tk_needle"] = {"element": 2, "verb": "beam", "startup": 0.1, "active": 0.1, "recovery": 0.2, "heat": 60.0, "cls": "blue_fire",
		"range": 7.0, "tiers": _t(), "fx": {"mat": "blue"}}
	D["tk_ball"] = {"element": 2, "verb": "projectile", "startup": 0.12, "active": 0.05, "recovery": 0.25, "heat": 100.0, "source": "heat",
		"mat": "fire", "mass": 1.0, "speed": 18.0, "gravity": 0.3, "tag": "fireball", "on_impact": "burst", "impact_radius": 2.0, "tiers": _t()}
	D["tk_line"] = {"element": 2, "verb": "ground_line", "startup": 0.2, "active": 0.05, "recovery": 0.3, "heat": 120.0, "source": "heat",
		"mat": "fire", "mass": 1.0, "tag": "fire_line", "speed": 12.0, "budget": 10.0, "width": 1.2, "trail_zone": "fire_field", "trail_life": 1.0}
	D["tk_mine"] = {"element": 2, "verb": "burst", "startup": 0.1, "active": 0.05, "recovery": 0.2, "heat": 80.0, "at": "aim", "range": 8.0,
		"radius": 2.0, "power": 12.0, "fuse": 0.4, "tiers": _t({"t2": {"radius": 3.0}}), "fx": {"mat": "blast"}}
	D["tk_aegis"] = {"element": 2, "verb": "barrier", "barrier": "aura", "upkeep": 6.0, "counter": {"cls": "aura_blue", "power": [16, 20, 24]},
		"tiers": {"t1": {}, "t2": {}}}
	D["tk_flash"] = {"element": 2, "verb": "cone", "startup": 0.08, "active": 0.05, "recovery": 0.2, "heat": 80.0, "cls": "flame",
		"range": 4.0, "angle": 60.0, "fx": {"mat": "blue"}}
	D["tk_kiln"] = {"element": 2, "verb": "ranged_heat", "startup": 0.12, "active": 0.0, "recovery": 0.2, "cost": 6.0, "rate": 450.0,
		"range": 6.0, "target_temp": 600.0}
	D["tk_shimmer"] = {"element": 2, "verb": "dash", "startup": 0.0, "active": 0.12, "recovery": 0.1, "cost": 5.0, "distance": 3.0, "hidden": true}
	D["tk_hop"] = {"element": 2, "verb": "mode", "startup": 0.0, "active": 0.0, "recovery": 0.1, "upkeep": 6.0, "kind": "flight", "height": 2.0, "speed_mult": 1.2}
	# Air-like (element 3, sub 1)
	D["tk_clap"] = {"element": 3, "verb": "cone", "startup": 0.1, "active": 0.1, "recovery": 0.2, "cost": 4.0, "cls": "sound", "range": 6.0,
		"angle": 30.0, "power": 8.0, "tiers": _t(), "fx": {"mat": "sound"}}
	D["tk_rail"] = {"element": 3, "verb": "beam", "startup": 0.1, "active": 0.1, "recovery": 0.3, "cost": 10.0, "cls": "lightning",
		"range": 14.0, "damage": 14.0, "power": 14.0, "tiers": _t({"t2": {"power": 36.0, "damage": 20.0}}), "fx": {"mat": "lightning"}}
	D["tk_tremor"] = {"element": 3, "verb": "ground_line", "startup": 0.2, "active": 0.05, "recovery": 0.3, "cost": 7.0, "source": "none",
		"mat": "air", "mass": 2.0, "tag": "tremor", "speed": 18.0, "budget": 10.0, "width": 1.5, "power": 12.0, "tiers": _t()}
	D["tk_twister"] = {"element": 3, "verb": "zone", "startup": 0.12, "active": 0.05, "recovery": 0.2, "cost": 6.0, "tag": "tornado",
		"radius": 2.0, "life": 2.0, "at": "ahead", "distance": 4.0, "power": 12.0, "walk_speed": 3.0, "tiers": _t({"t2": {"radius": 2.5}})}
	D["tk_bubble"] = {"element": 3, "verb": "barrier", "barrier": "zone", "tag": "null_bubble", "upkeep": 6.0, "radius": 1.8,
		"counter": {"cls": "bubble_null", "power": [14, 18, 22]}, "tiers": {"t1": {}, "t2": {"radius": 2.2}}}
	D["tk_wave"] = {"element": 3, "verb": "burst", "startup": 0.08, "active": 0.05, "recovery": 0.2, "cost": 4.0, "at": "self", "radius": 3.0,
		"power": 16.0, "cls": "vacuum"}
	D["tk_anchor"] = {"element": 3, "verb": "stance", "startup": 0.0, "active": 0.6, "recovery": 0.1, "held": false, "stance": "anchor",
		"anchored": true, "anchor_cp": 35.0}
	D["tk_eye"] = {"element": 3, "verb": "summon", "startup": 0.14, "active": 0.0, "recovery": 0.18, "cost": 10.0, "upkeep": 10.0,
		"tag": "tornado", "radius": 2.5, "power": 25.0, "at": "aim", "range": 10.0, "steer_speed": 4.0, "linger": 2.0, "tiers": _t({"t2": {"radius": 3.0}})}
	D["tk_boom"] = {"element": 3, "verb": "dash", "startup": 0.0, "active": 0.2, "recovery": 0.1, "cost": 6.0, "distance": 6.0, "dir": "aim"}
	D["tk_glider"] = {"element": 3, "verb": "mode", "startup": 0.0, "active": 0.0, "recovery": 0.1, "upkeep": 6.0, "kind": "glide"}
	for id in D:
		Moves.register(id, D[id])
	var slots := {
		0: ["tk_disc", "tk_spear", "tk_surge", "tk_fan", "tk_dune", "tk_push", "tk_pit", "tk_seize", "tk_burrow", "tk_skin"],
		1: ["tk_shard", "tk_jet", "tk_tide", "tk_frost", "tk_icewall", "tk_orb", "tk_floor", "tk_fog", "tk_glide", "tk_fly"],
		2: ["tk_needle", "tk_ball", "tk_line", "tk_mine", "tk_aegis", "tk_flash", "tk_kiln", "tk_kiln", "tk_shimmer", "tk_hop"],
		3: ["tk_clap", "tk_rail", "tk_tremor", "tk_twister", "tk_bubble", "tk_wave", "tk_anchor", "tk_eye", "tk_boom", "tk_glider"],
	}
	for e in slots:
		for k in Sim.SLOTS.size():
			for s in [1, 2, 3]:
				Moves.bind(e, s, Sim.SLOTS[k], slots[e][(k + s - 1) % 10] if s > 1 and Sim.SLOTS[k] != "guard" else slots[e][k])
	Interactions.add_rule("stone", "wave_water", {"outcome": "capture", "partial": "slow", "release_speed": 9.0})
	Interactions.add_rule("solid_light", "quicksand", {"outcome": "sink", "partial": "slow"})
	Interactions.add_rule("flame", "fog", {"outcome": "weaken", "partial": "weaken", "fail": "pass"})
	Interactions.add_rule("solid_light", "tornado", {"outcome": "capture", "partial": "bend", "fail": "pass", "release_speed": 10.0})
	Interactions.add_rule("sound", "wall_ice", {"outcome": "pass", "partial": "pass", "fail": "shatter"})


class Soak:
	var h: SimHarness
	var rng := RandomNumberGenerator.new()
	var next := {}
	var hold_until := {}
	var base := {}
	var worst := {}
	var bad: Array[String] = []
	var busy := {}
	var fx_keys := {}
	var outcomes := {}
	var counts := {}
	var max_bodies := 0


func _snap(w: CombatWorld) -> Dictionary:
	return {"energy": w.system_energy() - w.ledger_balance(), "water": w.water_mass(), "earth": w.earth_mass(), "metal": w.metal_mass(),
		"plant": w.plant_mass()}


func _input(s: Soak, x: ActorState, other: ActorState) -> void:
	var h := s.h
	var it := h.it(x)
	var t := h.w.tick
	if t >= int(s.hold_until.get(x.id, 0)):
		it.attack_held = false
		it.guard_held = false
		it.tech_held = false
		it.evade_held = false
	if t < int(s.next.get(x.id, 0)):
		return
	s.next[x.id] = t + 4 + s.rng.randi() % 30
	var r := s.rng.randf()
	if s.rng.randf() < 0.5:
		h.aim(x, other.pos - x.pos)
	var hold := 0
	if r < 0.30:
		h.press(x, "attack")
		if s.rng.randf() < 0.5:
			it.attack_gesture = 1 + s.rng.randi() % 3
		hold = 0 if s.rng.randf() < 0.3 else 5 + s.rng.randi() % 130
	elif r < 0.45:
		h.press(x, "guard")
		hold = 6 + s.rng.randi() % 90
	elif r < 0.52 and x.action != null and x.action.id == "guard":
		it.guard_gesture = 1 + s.rng.randi() % 2
		it.guard_held = true
	elif r < 0.66:
		h.press(x, "tech")
		hold = 6 + s.rng.randi() % 120
	elif r < 0.76:
		h.press(x, "evade")
		it.evade_held = s.rng.randf() < 0.5
		it.move = Vector3(s.rng.randf() - 0.5, 0, s.rng.randf() - 0.5)
		hold = 2 + s.rng.randi() % 60
	elif r < 0.84:
		h.element(x, s.rng.randi() % 4)
	elif r < 0.92:
		h.sub(x, s.rng.randi() % 4)
	else:
		var a := s.rng.randf() * TAU
		it.move = Vector3(sin(a), 0, cos(a)) * s.rng.randf()
	s.hold_until[x.id] = t + hold


func _run(seed_value: int, ticks: int) -> Soak:
	var s := Soak.new()
	s.h = SimHarness.new(seed_value)
	s.rng.seed = seed_value * 31 + 7
	var a := s.h.actor("A", Vector3(-2, 0, 6), 0, {"magma": true, "heat_draw": true, "lightning": true, "glide": true}, s.rng.randi() % 4)
	var b := s.h.actor("B", Vector3(2, 0, -6), 1, {"magma": true, "heat_draw": true, "lightning": true, "glide": true}, s.rng.randi() % 4)
	a.subs = [1, 1, 1, 1]
	b.subs = [1, 1, 1, 1]
	var w := s.h.w
	s.base = _snap(w)
	for k in ticks:
		for x in w.actors:
			_input(s, x, b if x == a else a)
			if x.health < 25.0:
				x.health = Sim.HEALTH_MAX
		var pre_e := w.system_energy() - w.ledger_balance()
		s.h.step()
		var post_e := w.system_energy() - w.ledger_balance()
		if absf(post_e - pre_e) > 1e-3 and s.bad.size() < 6:
			var types := []
			for e in s.h.log:
				if not (e.type in ["fx", "spawn"]):
					types.append("%s:%s" % [e.type, e.get("move", e.get("outcome", e.get("phase", e.get("reason", ""))))])
			_bad(s, "energy jump %.2f: %s" % [post_e - pre_e, ", ".join(types)])
		for e in s.h.log:
			s.counts[e.type] = int(s.counts.get(e.type, 0)) + 1
			if e.type == "fx":
				s.fx_keys["%s/%s" % [e.fx, e.mat]] = true
			elif e.type == "interaction":
				s.outcomes[e.outcome] = true
		s.h.log.clear()
		var alive := 0
		for bd in w.bodies:
			if bd.alive:
				alive += 1
				if not (is_finite(bd.pos.x) and is_finite(bd.pos.y) and is_finite(bd.mass) and is_finite(bd.temp)) or bd.mass < -1e-9:
					_bad(s, "body %s" % bd.describe())
		s.max_bodies = maxi(s.max_bodies, alive)
		for x in w.actors:
			if not (is_finite(x.pos.x) and is_finite(x.pos.z)) or absf(x.pos.x) > 16.0 or absf(x.pos.z) > 16.0:
				_bad(s, "%s out of the arena %s" % [x.name, x.pos])
			if x.focus < -1e-9 or x.focus > 100.0 + 1e-9 or x.water_carried < -1e-9 or x.metal_carried < -1e-9:
				_bad(s, "%s resources focus %.3f water %.3f metal %.3f" % [x.name, x.focus, x.water_carried, x.metal_carried])
			var it := s.h.it(x)
			var holding := it.attack_held or it.guard_held or it.tech_held or it.evade_held
			s.busy[x.id] = int(s.busy.get(x.id, 0)) + 1 if (x.action != null and not holding) else 0
			if int(s.busy[x.id]) == 300:
				_bad(s, "%s stuck in %s (%s)" % [x.name, x.action.id, x.action.phase_name()])
		if k % 60 == 59:
			var now := _snap(w)
			for key in s.base:
				var d := absf(float(now[key]) - float(s.base[key]))
				s.worst[key] = maxf(float(s.worst.get(key, 0.0)), d)
	return s


func _bad(s: Soak, msg: String) -> void:
	if s.bad.size() < 8:
		s.bad.append("t%d: %s" % [s.h.w.tick, msg])


func _hash(w: CombatWorld) -> int:
	var parts: Array = [w.tick, w.ledger.duplicate(), w.mass_ledger.duplicate()]
	for x in w.actors:
		parts.append([x.pos, x.vel, x.health, x.focus, x.balance, x.water_carried, x.metal_carried, x.stance, x.status.keys(),
			x.action.id if x.action else "", x.subs])
	for b in w.bodies:
		if b.alive:
			parts.append([b.id, b.mat, b.form, b.tag, b.mass, b.temp, b.liquid, b.pos, b.vel, b.tier, b.zone_radius, b.heat_payload])
	return hash(parts)


func test_verbs_soak_keeps_every_invariant() -> void:
	var holder := SimHarness.new(1)
	holder.begin_scope()
	register_test_kit()
	var s := _run(17, SOAK_TICKS)
	check(s.bad.is_empty(), "violations: %s" % "; ".join(s.bad))
	near(float(s.worst.get("energy", 0.0)), 0.0, 1e-4, "energy ledger")
	for key in ["water", "earth", "metal", "plant"]:
		near(float(s.worst.get(key, 0.0)), 0.0, 1e-5, "%s mass ledger" % key)
	check(s.max_bodies <= Sim.MAX_BODIES + 2, "body count bounded (%d)" % s.max_bodies)
	for fk in s.fx_keys:
		var parts: PackedStringArray = String(fk).split("/")
		check(FxEvents.is_known("fx", parts[0]) and FxEvents.is_known("mat", parts[1]), "catalogued fx key %s" % fk)
	for o in s.outcomes:
		check(FxEvents.is_known("outcome", String(o)), "catalogued outcome %s" % o)
	for ev in ["charge", "fx", "interaction", "zone", "status", "launch", "morph"]:
		check(int(s.counts.get(ev, 0)) > 0, "the soak exercised '%s' (%d)" % [ev, int(s.counts.get(ev, 0))])
	note("events: charge %d fx %d interaction %d zone %d status %d morph %d clash %d; max bodies %d; worst %s" % [
		int(s.counts.get("charge", 0)), int(s.counts.get("fx", 0)), int(s.counts.get("interaction", 0)), int(s.counts.get("zone", 0)),
		int(s.counts.get("status", 0)), int(s.counts.get("morph", 0)), int(s.counts.get("clash", 0)), s.max_bodies, s.worst])
	note("outcomes seen: %s" % [s.outcomes.keys()])
	holder.end_scope()


func test_engine_is_deterministic_for_a_seed() -> void:
	var holder := SimHarness.new(1)
	holder.begin_scope()
	register_test_kit()
	var a := _run(5, 900)
	var b := _run(5, 900)
	var c := _run(6, 900)
	check(_hash(a.h.w) == _hash(b.h.w), "same seed: same state hash")
	check(_hash(a.h.w) != _hash(c.h.w), "a different seed plays differently")
	holder.end_scope()


func test_every_verb_runs_at_every_tier() -> void:
	var holder := SimHarness.new(1)
	holder.begin_scope()
	register_test_kit()
	var verbs := {}
	for id in Moves.REGISTERED:
		if not id.begins_with("tk_"):
			continue
		var d: Dictionary = Moves.DEFS[id]
		for tier in 4:
			var h := SimHarness.new(3)
			var p := h.actor("P", Vector3(0, 0, 3), 0, {}, int(d.element))
			var o := h.actor("O", Vector3(0, 0, -4), 1, {}, Sim.Element.EARTH)
			o.is_dummy = true
			h.step(5)
			var e0 := _snap(h.w)
			var it := h.it(p)
			var slot := Moves.slot_of(int(d.element), 1, id)
			p.subs = [1, 1, 1, 1]
			var inst := h.w.start_action(p, id if slot != "guard" else "guard", it, {"slot": slot, "tier": tier, "charge_frozen": true,
				"spec": id if slot == "guard" else ""})
			it.attack_held = true
			it.guard_held = true
			it.tech_held = true
			it.evade_held = true
			h.step(40)
			it.attack_held = false
			it.guard_held = false
			it.tech_held = false
			it.evade_held = false
			h.step(120)
			var e1 := _snap(h.w)
			for k in e0:
				near(float(e1[k]), float(e0[k]), 1e-5, "%s T%d: %s ledger" % [id, tier, k])
			check(p.action == null or p.action.id == "guard" or p.action != inst, "%s T%d ends cleanly" % [id, tier])
			verbs[String(d.verb)] = true
	check(verbs.size() == 13, "all 13 verbs exercised (%s)" % [verbs.keys()])
	holder.end_scope()
