class_name SpawnCatalog
extends RefCounted
## The Lab spawner's catalogue (docs/MOVESET.md section 14): every material and state of the moveset, spawned
## inert at the aim point or launched at the player by the rival, with mass / speed / temperature / tier
## parameters. Everything goes through the world's ledgers (ground_taken, moisture_taken, metal_taken,
## generated / freeze_dump heat) so a Lab session keeps the conservation checks balanced.
##
## Entry kinds (`build`):
##   body     a MatBody made directly (mat, form, tag, source, mass, speed, temp, frozen ...)
##   verb     one of the kit's own verbs (projectile / ground_line / zone) run for the owner with a synthetic
##            action of `move` at `tier`: kit conventions (tags, props, impact hooks) apply unchanged
##   zone     a field (zone body): tag, radius, power, life, mat, heat
##   perform  the owner performs element / sub / slot through the input path (volumes: bolt, blast, sound, cones);
##            launch only
## Param specs on an entry: "mass" "speed" "temp" "tier" = [default, min, max]; absent / null = not adjustable.

const GROUPS := ["Earth", "Water", "Fire", "Air", "Volumes"]

static var _cache: Array[Dictionary] = []


static func entries() -> Array[Dictionary]:
	if _cache.is_empty():
		Moves.ensure()
		_cache = _build_entries()
	return _cache


## Rebuild after kits register more moves (tests).
static func reset() -> void:
	_cache.clear()


static func find(id: String) -> Dictionary:
	for e in entries():
		if e.id == id:
			return e
	return {}


static func ids() -> Array[String]:
	var out: Array[String] = []
	for e in entries():
		out.append(e.id)
	return out


static func _b(id: String, label: String, group: String, mat: String, d: Dictionary) -> Dictionary:
	var e := {"id": id, "label": label, "group": group, "build": "body", "mat": mat, "form": "chunk", "tag": "",
		"source": "none", "damage": 10.0, "balance": 20.0, "inert_only": false, "launch_only": false}
	e.merge(d, true)
	return e


static func _v(id: String, label: String, group: String, move: String, d: Dictionary = {}) -> Dictionary:
	var e := {"id": id, "label": label, "group": group, "build": "verb", "move": move, "inert_only": false, "launch_only": false}
	e.merge(d, true)
	return e


static func _z(id: String, label: String, group: String, tag: String, mat: String, d: Dictionary) -> Dictionary:
	var e := {"id": id, "label": label, "group": group, "build": "zone", "tag": tag, "mat": mat, "radius": 3.0, "power": 8.0,
		"life": 7.0, "inert_only": true, "launch_only": false}
	e.merge(d, true)
	return e


static func _p(id: String, label: String, element: int, sub: int, slot: String, tier: int) -> Dictionary:
	return {"id": id, "label": label, "group": "Volumes", "build": "perform", "element": element, "sub": sub, "slot": slot,
		"tier": [tier, 0, 3], "inert_only": false, "launch_only": true}


static func _build_entries() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	# ---------------------------------------------------------------- Earth
	for m in [[20.0, 15.0], [45.0, 13.0], [80.0, 11.0], [200.0, 9.0]]:
		out.append(_b("stone_%d" % int(m[0]), "Stone %d kg" % int(m[0]), "Earth", "stone",
			{"source": "ground", "mass": [m[0], 5.0, 300.0], "speed": [m[1], 3.0, 30.0], "damage": 12.0 * sqrt(m[0] / 20.0),
			"balance": minf(90.0, 24.0 * sqrt(m[0] / 20.0))}))
	out.append(_b("hot_rock", "Hot rock", "Earth", "stone", {"source": "ground", "mass": [20.0, 5.0, 120.0],
		"speed": [14.0, 3.0, 30.0], "temp": [450.0, 100.0, 900.0], "damage": 14.0}))
	out.append(_b("magma_blob", "Magma blob", "Earth", "stone", {"source": "ground", "form": "blob", "mass": [25.0, 5.0, 120.0],
		"speed": [13.0, 3.0, 30.0], "temp": [1000.0, 1000.0, 1500.0], "damage": 16.0, "molten": true}))
	out.append(_b("lava_wave", "Lava wave", "Earth", "stone", {"source": "ground", "form": "blob", "mass": [25.0, 8.0, 80.0],
		"speed": [7.5, 3.0, 14.0], "temp": [1000.0, 1000.0, 1500.0], "damage": 18.0, "balance": 55.0, "molten": true, "wave": true}))
	out.append(_v("metal_disc", "Metal disc", "Earth", "razor_disc"))
	out.append(_v("metal_lance", "Metal lance", "Earth", "iron_lance"))
	out.append(_b("metal_plate", "Metal plate", "Earth", "metal", {"source": "metal", "tag": "plate", "mass": [12.0, 3.0, 40.0],
		"speed": [20.0, 5.0, 30.0], "damage": 12.0, "balance": 20.0}))
	out.append(_v("sand_slug", "Sand slug", "Earth", "grit_shot"))
	out.append(_v("sand_cloud", "Sand cloud", "Earth", "veil_of_grit"))
	out.append(_v("sand_surge", "Sand surge", "Earth", "sand_surge", {"launch_only": true}))
	out.append(_v("spike_line", "Rising fangs", "Earth", "rising_fangs", {"launch_only": true}))
	# ---------------------------------------------------------------- Water
	out.append(_b("water_blob", "Water blob", "Water", "water", {"source": "moisture", "form": "blob", "mass": [8.0, 2.0, 60.0],
		"speed": [14.0, 3.0, 30.0], "temp": [20.0, 1.0, 95.0], "damage": 8.0}))
	out.append(_v("water_stream", "Water bullet", "Water", "water_bullet"))
	out.append(_v("water_wave", "Water wave", "Water", "tidal_rush", {"launch_only": true}))
	out.append(_b("water_puddle", "Puddle", "Water", "water", {"source": "moisture", "form": "puddle",
		"mass": [10.0, 2.0, 40.0], "inert_only": true}))
	out.append(_v("ice_shard", "Ice shard", "Water", "frost_shard"))
	out.append(_b("ice_wall", "Ice wall", "Water", "water", {"source": "moisture", "form": "wall", "tag": "ice", "frozen": true,
		"mass": [70.0, 20.0, 160.0], "inert_only": true}))
	out.append(_v("mist", "Mist bank", "Water", "creeping_fog"))
	out.append(_b("steam", "Steam cloud", "Water", "steam", {"source": "moisture", "form": "cloud", "mass": [3.0, 1.0, 12.0],
		"speed": [6.0, 0.0, 14.0], "damage": 4.0, "balance": 8.0, "life": 5.0}))
	out.append(_v("vines", "Burr seeds", "Water", "burr_shot", {"launch_only": true}))
	# ---------------------------------------------------------------- Fire
	out.append(_v("fireball", "Fireball", "Fire", "fireball"))
	out.append(_v("comet", "Blue comet", "Fire", "comet_flame"))
	out.append(_v("fire_line", "Fire line", "Fire", "fire_line", {"launch_only": true}))
	out.append(_z("fire_field", "Fire field", "Fire", "fire_field", "fire", {"radius": 3.0, "power": 6.0, "life": 8.0, "heat": 240.0}))
	# ---------------------------------------------------------------- Air
	out.append(_v("tornado", "Tornado", "Air", "vortex_twister", {"tier": [1, 0, 3]}))
	out.append(_v("wind_crescent", "Wind crescent", "Air", "gust_crescent"))
	out.append(_z("vacuum_well", "Vacuum well", "Air", "vacuum_well", "air", {"radius": 3.5, "power": 12.0, "life": 7.0}))
	out.append(_v("tremor", "Tremor line", "Air", "sound_tremor", {"launch_only": true}))
	# ---------------------------------------------------------------- Volumes: the rival performs the move
	out.append(_p("bolt", "Bolt (aimed)", 2, 2, "strike", 1))
	out.append(_p("blast", "Blast", 2, 3, "strike", 1))
	out.append(_p("flame_cone", "Flame cone", 2, 0, "strike", 1))
	out.append(_p("gust_cone", "Gust", 3, 0, "strike", 1))
	out.append(_p("sound_pulse", "Sound pulse", 3, 3, "strike", 1))
	out.append(_p("steam_jet", "Scald puff", 1, 2, "strike", 1))
	out.append(_p("frost_fan", "Frost fan", 1, 1, "sweep", 1))
	out.append(_p("sandblast", "Sandblast", 0, 2, "thrust", 1))
	return out.filter(func(e: Dictionary) -> bool: return _usable(e))


static func _usable(e: Dictionary) -> bool:
	match String(e.build):
		"verb":
			return Moves.DEFS.has(String(e.move))
		"perform":
			return Moves.DEFS.has(Moves.resolve(int(e.element), int(e.sub), String(e.slot)))
	return true


# ================================================================ parameters

## The adjustable parameters of an entry: {mass, speed, temp, tier: [default, min, max]} (only those that apply).
static func param_specs(e: Dictionary) -> Dictionary:
	var out := {}
	for k in ["mass", "speed", "temp", "tier"]:
		if e.get(k) is Array:
			out[k] = (e[k] as Array).duplicate()
	if String(e.build) == "verb":
		var def: Dictionary = Moves.DEFS.get(String(e.move), {})
		var tier := int((e.get("tier", [0]) as Array)[0]) if e.get("tier") is Array else 0
		if not out.has("tier"):
			out["tier"] = [0, 0, maxi(0, Charge.max_tier(def))]
		else:
			out["tier"][2] = maxi(int(out["tier"][2]), Charge.max_tier(def))
		var m: Variant = Charge.pget(def, tier, "mass", null)
		if m != null and float(m) > 0.0 and not out.has("mass"):
			out["mass"] = [float(m), float(m) * 0.25, float(m) * 4.0]
		var s: Variant = Charge.pget(def, tier, "speed", null)
		if s != null and float(s) > 0.0 and String(def.get("verb", "")) != "zone" and not out.has("speed"):
			out["speed"] = [float(s), float(s) * 0.4, float(s) * 2.0]
	return out


## Defaults of every adjustable parameter ({mass: 20.0, ...}).
static func defaults(e: Dictionary) -> Dictionary:
	var out := {}
	var sp := param_specs(e)
	for k in sp:
		out[k] = sp[k][0]
	return out


## One-line summary of an entry at `params` for the UI.
static func describe(e: Dictionary, params: Dictionary) -> String:
	var p := defaults(e)
	p.merge(params, true)
	var bits: Array[String] = []
	if p.has("mass"):
		bits.append("%.0f kg" % float(p.mass))
	if p.has("speed"):
		bits.append("%.0f m/s" % float(p.speed))
	if p.has("temp"):
		bits.append("%.0f °C" % float(p.temp))
	if p.has("tier") and int(p.tier) > 0:
		bits.append("T%d" % int(p.tier))
	return ", ".join(bits)


# ================================================================ owners and spawning

## Who throws things: the first living non-dummy rival, else a dummy, else null.
static func owner_of(w: CombatWorld, player: ActorState) -> ActorState:
	for a in w.actors:
		if a != player and not a.is_dummy and a.team != player.team:
			return a
	for a in w.actors:
		if a != player and a.is_dummy:
			return a
	return null


static func _book_mass(w: CombatWorld, b: MatBody) -> void:
	match b.mat:
		Sim.Mat.STONE, Sim.Mat.SAND, Sim.Mat.GLASS:
			w.mass_ledger.ground_taken += b.mass
		Sim.Mat.METAL:
			w.mass_ledger.metal_taken += b.mass
		Sim.Mat.WATER, Sim.Mat.STEAM:
			w.mass_ledger.moisture_taken += b.mass
		Sim.Mat.PLANT:
			w.mass_ledger.plant_from_ground += b.mass


## Heat a body to `temp` (degrees C) through the ledgers (generated HU).
static func heat_to(w: CombatWorld, b: MatBody, temp: float) -> void:
	if temp <= b.temp + 0.5:
		return
	var need := 0.0
	if Materials.is_fusible(b.mat):
		var c := Sim.STONE_C if b.mat == Sim.Mat.STONE else Materials.c(b.mat)
		var melt := Sim.STONE_MELT_C if b.mat == Sim.Mat.STONE else Materials.melt(b.mat)
		var lat := Sim.STONE_LATENT if b.mat == Sim.Mat.STONE else Materials.latent(b.mat)
		if temp < melt:
			need = b.mass * c * (temp - b.temp)
		else:
			need = b.mass * c * maxf(melt - b.temp, 0.0) + b.mass * lat * (1.0 - b.liquid) + b.mass * c * (temp - melt)
	elif b.mat == Sim.Mat.WATER:
		need = b.mass * Sim.WATER_C * (minf(temp, 99.0) - b.temp)
	if need <= 0.0:
		return
	w.ledger.generated += w.heat_body(b, need)


## Where a thrown object starts: the owner's hand, or 9 m in front of the target when nobody throws.
static func launch_origin(target: ActorState, owner: ActorState) -> Vector3:
	if owner != null:
		return owner.hand_point() + owner.forward() * 0.4
	return target.chest() + target.forward() * 9.0


## Spawns an entry. params: only the keys changed from the defaults. launch = thrown at `target` (the
## player) by `owner` (null = neutral); else placed inert at `at` (ground point). Returns
## {ok, bodies: Array[MatBody], script: LabScript or null, msg}.
static func spawn(w: CombatWorld, id: String, params: Dictionary, target: ActorState, owner: ActorState, at: Vector3, launch: bool) -> Dictionary:
	var e := find(id)
	var res := {"ok": false, "bodies": [] as Array[MatBody], "script": null, "msg": ""}
	if e.is_empty():
		res.msg = "unknown entry " + id
		return res
	var p := defaults(e)
	p.merge(params, true)
	if bool(e.get("inert_only", false)) and launch:
		launch = false
	if bool(e.get("launch_only", false)) and not launch:
		launch = true
	match String(e.build):
		"body":
			var b := _spawn_body(w, e, p, target, owner, at, launch)
			if b != null:
				res.bodies.append(b)
		"zone":
			var z := _spawn_zone(w, e, p, owner, at, target, launch)
			if z != null:
				res.bodies.append(z)
		"verb":
			if owner == null:
				res.msg = "needs a rival to throw it"
				return res
			for b2 in _spawn_verb(w, e, p, target, owner, at, launch):
				res.bodies.append(b2)
		"perform":
			if owner == null:
				res.msg = "needs a rival to perform it"
				return res
			var s := LabScript.for_move(int(e.element), int(e.sub), String(e.slot), int(p.get("tier", 1)))
			res.script = s
			res.ok = true
			return res
	res.ok = not (res.bodies as Array).is_empty()
	if not res.ok and res.msg == "":
		res.msg = "nothing spawned"
	return res


static func _ground_point(w: CombatWorld, p: Vector3) -> Vector3:
	return Vector3(p.x, w.arena.ground_height(p.x, p.z, p.y + 0.6), p.z)


static func _spawn_body(w: CombatWorld, e: Dictionary, p: Dictionary, target: ActorState, owner: ActorState, at: Vector3, launch: bool) -> MatBody:
	var mat: int = Sim.MAT_NAMES.find(String(e.mat))
	var form: int = Sim.FORM_NAMES.find(String(e.form))
	var mass := float(p.get("mass", 5.0))
	var pos := launch_origin(target, owner) if launch else _ground_point(w, at) + Vector3(0, 0.35, 0)
	var b := w.spawn_body(mat, form, mass, pos, "lab")
	_book_mass(w, b)
	b.tag = StringName(String(e.tag))
	if bool(e.get("frozen", false)) and b.is_water():
		WaterUtil.freeze_body(w, b)
	elif p.has("temp"):
		if b.is_water():
			var e0 := b.thermal_energy()
			b.temp = float(p.temp)
			var d := b.thermal_energy() - e0
			if d >= 0.0:
				w.ledger.generated += d
			else:
				w.ledger.freeze_dump += d
		else:
			heat_to(w, b, float(p.temp))
	if bool(e.get("molten", false)):
		Thermal.update_phase(b)
	b.update_radius()
	match form:
		Sim.Form.PUDDLE:
			b.pos = _ground_point(w, at)
			w._water_to_puddle(b)
			return b if b.alive else null
		Sim.Form.WALL:
			b.pos = _ground_point(w, at)
			b.wall_yaw = target.facing if target != null else 0.0
			b.wall_half = Vector3(1.1, 0.7, 0.25)
			b.static_body = true
			b.wall_rise = 1.0
			b.props["standing"] = 45.0
			return b
	if mat == Sim.Mat.STEAM:
		b.max_life = float(e.get("life", 5.0))
	elif b.max_life < 0.0:
		b.max_life = Sim.REMNANT_LIFETIME
	if launch:
		_arm_and_throw(w, b, e, p, target, owner)
	else:
		b.on_ground = true
		b.vel = Vector3.ZERO
		b.attack_id = 0
	return b


static func _arm_and_throw(w: CombatWorld, b: MatBody, e: Dictionary, p: Dictionary, target: ActorState, owner: ActorState) -> void:
	var speed := float(p.get("speed", 14.0))
	b.attack_id = w.new_attack_id()
	b.attack_owner = owner.id if owner != null else -1
	b.hit_set.clear()
	if owner != null:
		b.hit_set[owner.id] = true
	b.damage = float(e.get("damage", 10.0))
	b.balance_damage = float(e.get("balance", 20.0))
	b.on_ground = false
	if bool(e.get("wave", false)):
		# A poured wave: the molten body becomes a ground wave aimed at the target.
		var dir := (target.pos - b.pos)
		dir.y = 0.0
		dir = dir.normalized() if dir.length() > 0.01 else Vector3.FORWARD
		b.form = Sim.Form.WAVE
		b.pos = _ground_point(w, b.pos)
		b.vel = Vector3.ZERO
		b.wave_dir = dir
		b.wave_budget = 14.0 + 0.2 * b.mass
		b.wave_width = 1.1 + b.mass * 0.025
		b.wave_path = PackedVector3Array([b.pos])
		b.max_life = -1.0
		return
	var gs := 0.0 if (b.mat == Sim.Mat.FIRE or b.mat == Sim.Mat.AIR or b.mat == Sim.Mat.STEAM) else 1.0
	b.gravity_scale = gs
	b.vel = Verbs.launch_vel(b.pos, target.chest(), maxf(speed, 0.5), gs)


static func _spawn_zone(w: CombatWorld, e: Dictionary, p: Dictionary, owner: ActorState, at: Vector3, target: ActorState, launch: bool) -> MatBody:
	var mat: int = Sim.MAT_NAMES.find(String(e.mat))
	var pos := _ground_point(w, at)
	if launch and target != null:
		pos = _ground_point(w, target.pos)
	var tier := int(p.get("tier", 0))
	var z := w.spawn_zone(StringName(String(e.tag)), pos, float(e.radius) * (1.0 + 0.15 * tier), owner.id if owner != null else -1,
		float(e.power) * (1.0 + 0.4 * tier), mat, 0.0, float(e.life))
	z.tier = tier
	if mat == Sim.Mat.FIRE and float(e.get("heat", 0.0)) > 0.0:
		z.heat_payload = float(e.heat)
		w.ledger.generated += z.heat_payload
	return z


static func _synthetic(w: CombatWorld, owner: ActorState, move_id: String, tier: int, dir: Vector3, def: Dictionary) -> ActionInst:
	var inst := ActionInst.new()
	inst.id = move_id
	inst.def = def
	inst.element = int(def.get("element", owner.element))
	inst.sub = int(def.get("sub", 0))
	inst.slot = String(def.get("slot", "strike"))
	inst.attack_id = w.new_attack_id()
	inst.data = {"tier": tier, "aim": dir, "face": dir, "aim_active": false, "aim_point": owner.chest() + dir * 8.0}
	return inst


## A copy of a def with mass / speed scaled by the sliders' ratio (def and every tier).
static func _scaled_def(def: Dictionary, key: String, ratio: float) -> Dictionary:
	var d := def.duplicate(true)
	if d.has(key):
		d[key] = float(d[key]) * ratio
	var tiers: Dictionary = d.get("tiers", {})
	for t in tiers:
		if (tiers[t] as Dictionary).has(key):
			tiers[t][key] = float(tiers[t][key]) * ratio
	return d


static func _spawn_verb(w: CombatWorld, e: Dictionary, p: Dictionary, target: ActorState, owner: ActorState, at: Vector3, launch: bool) -> Array[MatBody]:
	var out: Array[MatBody] = []
	var move_id := String(e.move)
	var def: Dictionary = Moves.DEFS[move_id]
	var tier := clampi(int(p.get("tier", 0)), 0, 3)
	var dir := (target.pos - owner.pos) if launch else (at - owner.pos)
	dir.y = 0.0
	dir = dir.normalized() if dir.length() > 0.01 else owner.forward()
	owner.lock_target = target.id
	owner.facing = atan2(dir.x, dir.z)
	top_up(w, owner)
	var dflt := defaults(e)
	for key in ["mass", "speed"]:
		if p.has(key) and dflt.has(key) and float(dflt[key]) > 0.0 and absf(float(p[key]) - float(dflt[key])) > 1e-4:
			def = _scaled_def(def, key, float(p[key]) / float(dflt[key]))
	var inst := _synthetic(w, owner, move_id, tier, dir, def)
	var first_id: int = w._next_body
	Verbs.execute(w, owner, inst)
	for b in w.bodies:
		if b.alive and b.id >= first_id and b != w.pool:
			out.append(b)
	if launch:
		return out
	# Inert: shots are frozen where the aim point is; fields are placed there.
	var ground := _ground_point(w, at)
	var k := 0
	for b2 in out:
		match b2.form:
			Sim.Form.WAVE:
				pass
			Sim.Form.ZONE, Sim.Form.CLOUD:
				b2.pos = ground
			_:
				_freeze_in_place(w, b2, ground + Vector3(0.0, 0.4, 0.0) + Vector3(0.6 * k, 0.0, 0.0))
				k += 1
	return out


static func _freeze_in_place(_w: CombatWorld, b: MatBody, p: Vector3) -> void:
	b.pos = p
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.on_ground = true
	b.gravity_scale = 1.0


## Refill an actor's resources with ledger-exact bookkeeping (Lab "infinite resources", spawner owner).
static func top_up(w: CombatWorld, a: ActorState) -> void:
	a.focus = Sim.FOCUS_MAX
	var dh := Sim.RESERVE_MAX - a.heat_reserve
	if dh > 0.0:
		w.ledger.generated += dh
		a.heat_reserve = Sim.RESERVE_MAX
	var dw := 6.0 - a.water_carried
	if dw > 0.0:
		w.mass_ledger.moisture_taken += dw
		a.water_carried = 6.0
	var dm := 12.0 - a.metal_carried
	if dm > 0.0:
		w.mass_ledger.metal_taken += dm
		a.metal_carried = 12.0
	a.static_charge = maxf(a.static_charge, 30.0)
