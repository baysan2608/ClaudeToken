class_name FireFlame
extends RefCounted
## Fire / Flame (sub 0, docs/MOVESET.md §7.9). The legacy kit keeps its T0/T1 behaviour exactly (act_fire.gd: flare,
## blaze, the >= 0.65 s bolt of the legacy `lightning` kit flag, thermal HEAT / DRAW / VENT, pour). This file adds the
## data of the T2 Fire Column / T3 Inferno (run by ActFire), the SCORCH mode of the thermal technique (ActFire), and
## the new slots: Fireball (thrust), Fire Line / Fire Ring (ground), Fire Fan / Nova (sweep), Flame Guard with the
## extended Heat Sink (guard), Backdraft (push), Ground Heat (sink), Flare Dash (evade) and Rocket Hop (evade hold).

const E := 2
const SUB := 0
const HEAT_SINK_DRAW := 300.0     # a perfect Flame Guard draws this much out of a magma blob / hot rock (HU)
const HEAT_SINK_REACH := 2.4      # ... when it is this close and approaching (m)


static func _s(frames: float) -> float:
	return frames / 60.0


static func register() -> void:
	_extend_legacy()
	_thrust()
	_ground()
	_sweep()
	_guard()
	_push_sink()
	_mobility()
	for pair in [["thrust", "fireball"], ["ground", "fire_line"], ["sweep", "fire_fan"], ["guard", "flame_guard"],
			["push", "backdraft"], ["sink", "ground_heat"], ["evade", "flare_dash"], ["evade_hold", "rocket_hop"]]:
		Moves.bind(E, SUB, pair[0], pair[1])
	KitFire.handle("flame_guard", {"tick": Callable(FireFlame, "guard_tick")})
	KitFire.handle("rocket_hop", {"tick": Callable(FireFlame, "hop_tick")})
	CombatWorld.register_body_tick(&"fireball", Callable(FireFlame, "fireball_tick"))
	CombatWorld.register_body_tick(&"comet", Callable(FireFlame, "fireball_tick"))
	CombatWorld.register_body_tick(&"fire_line", Callable(FireFlame, "line_tick"))
	CombatWorld.register_zone_effect(&"fire_field", Callable(FireUtil, "field_tick"))


## The legacy strike gains T2 Fire Column and T3 Inferno (ActFire runs them); T0/T1 values stay untouched (tests pin
## them), the T1 charge does not drain Focus (charge_drain 0 on t1, exactly like today).
static func _extend_legacy() -> void:
	var d: Dictionary = Moves.DEFS["fire_attack"]
	d["name"] = "Flare / Blaze / Fire Column / Inferno"
	d["desc"] = "Tap: a flare cone. Hold: the blaze (T1). Hold 1 s: Fire Column, a narrow 7 m jet that leaves a burning field. Hold 1.8 s: Inferno, a 9 m wide blaze and a 4 s field."
	d["element"] = E
	d["sub"] = SUB
	d["slot"] = "strike"
	d["chain"] = 0.25
	d["tiers"] = {
		"t1": {"charge_drain": 0.0},
		"t2": {"charge_drain": 8.0, "col_range": 7.0, "col_cone": 9.0, "col_hu": 300.0, "col_damage": 20.0, "col_balance": 40.0,
			"field_r": 1.5, "field_life": 2.0, "field_share": 0.35, "knock": 4.0},
		"t3": {"charge_drain": 8.0, "col_range": 9.0, "col_cone": 34.0, "col_hu": 480.0, "col_damage": 26.0, "col_balance": 55.0,
			"field_r": 2.6, "field_life": 4.0, "field_share": 0.4, "knock": 6.0},
	}
	d["counter"] = {"cls": "flame", "power": [3.0, 8.0, 15.0, 24.0]}
	d["threat"] = {"cls": "flame"}
	d["fx"] = {"mat": "flame", "shape": ""}
	d["anim_t2"] = "fire_release"
	d["anim_t3"] = "mv_push_two_hand"
	d["ai"] = {"role": "poke", "range": [0.0, 6.5], "tags": ["burn", "melt_ice", "boil", "field_t2"]}
	var t: Dictionary = Moves.DEFS["fire_tech"]
	t["name"] = "Thermal (HEAT / DRAW / VENT / SCORCH)"
	t["desc"] = "Hold on a stone: magma grip, melt it, release to pour a lava wave. On lava or hot rock: DRAW its heat into your reserve. On a wall or a body you can't grip: SCORCH its face at 300 HU/s (walls slump). Nothing in reach and heat in reserve: VENT."
	t["element"] = E
	t["sub"] = SUB
	t["slot"] = "tech"
	t["counter"] = {"cls": "heat_grip"}
	t["fx"] = {"mat": "flame", "shape": ""}
	t["scorch"] = {"verb": "ranged_heat", "range": 6.0, "cone": 40.0, "rate": 300.0, "slump_fraction": 0.25, "slump_at": 0.5,
		"fx": {"mat": "flame", "shape": ""}}
	t["ai"] = {"role": "counter", "range": [0.0, 9.0], "tags": ["reclaim", "melt", "draw", "scorch_wall"]}
	for id in ["lightning", "pour", "vent"]:
		var l: Dictionary = Moves.DEFS[id]
		l["element"] = E
		l["sub"] = SUB if id != "lightning" else 2
		l["fx"] = {"mat": "lightning" if id == "lightning" else "flame"}
	Moves.DEFS.lightning["name"] = "Bolt (legacy)"
	Moves.DEFS.pour["name"] = "Pour"
	Moves.DEFS.vent["name"] = "Vent"


# ------------------------------------------------------------------ thrust: Fireball -> twin -> big -> Sunfall

static func _thrust() -> void:
	Moves.register("fireball", {
		"element": E, "sub": SUB, "slot": "thrust", "name": "Fireball / Twin / Great / Sunfall",
		"desc": "A ball of flame (a body carrying 120 HU) that bursts 2 m on impact. Air can grip and feed it, water quenches it, vacuum snuffs it. Held: two, then a 240 HU ball bursting 3 m, then Sunfall: lobbed, it leaves a 4 m burning field.",
		"module": "verbs", "verb": "projectile",
		"startup": _s(12), "active": _s(4), "recovery": _s(18), "cancel": 0.6, "chain": 0.25,
		"heat": 120.0, "source": "heat", "mat": "fire", "tag": "fireball", "mass": 0.5, "speed": 18.0, "gravity": 0.15,
		"damage": 8.0, "balance": 14.0, "reach": 16.0, "life": 2.0,
		"on_impact": "burst", "impact_radius": 2.0, "impact_power": 6.0, "impact_damage": 6.0,
		"tiers": {
			"t1": {"count": 2, "spread": 14.0, "heat_add": 60.0, "impact_power": 9.0, "damage": 7.0},
			"t2": {"count": 1, "heat_add": 120.0, "impact_radius": 3.0, "impact_power": 12.0, "speed": 16.0, "damage": 12.0, "balance": 24.0},
			"t3": {"count": 1, "heat_add": 260.0, "impact_radius": 3.0, "impact_power": 20.0, "speed": 13.0, "arc": true, "gravity": 1.0,
				"damage": 16.0, "balance": 34.0, "field_r": 4.0, "field_life": 3.0, "field_share": 0.4},
		},
		"hook_impact": Callable(FireFlame, "fireball_impact"),
		"counter": {"cls": "fireball", "power": [6.0, 9.0, 12.0, 20.0]}, "threat": {"cls": "flame"},
		"anim": "fire_release", "anim_t3": "mv_overhead_slam", "fx": {"mat": "flame", "shape": "fireball"},
		"ai": {"role": "poke", "range": [4.0, 16.0], "tags": ["projectile", "burn", "field_t3"]},
	})


## A fireball / comet bursts where it hits (props.on_impact "burst"): its payload is the burst's heat; Sunfall
## leaves a share of it as a burning field.
static func fireball_impact(w: CombatWorld, b: MatBody, _what: String) -> bool:
	burst_fire_body(w, b, "impact")
	return true


## Bursts a fire body at its position: a flame / blue_fire volume carrying its payload (ledger: spent by the burst,
## or moved into the field it leaves). Used by impacts and clashes.
static func burst_fire_body(w: CombatWorld, b: MatBody, why: String) -> void:
	if b == null or not b.alive:
		return
	var owner := w.get_actor(b.attack_owner if b.attack_owner >= 0 else b.residual_owner)
	var d: Dictionary = Moves.DEFS.get(String(b.props.get("move", "")), {})
	var tier := b.tier
	var blue := b.tag == &"comet" or b.props.get("blue", false)
	var p := b.pos
	var g := w.arena.ground_height(p.x, p.z, p.y + 0.5)
	if p.y < g + 0.4:
		p.y = g + 0.4
	var heat := b.heat_payload
	b.heat_payload = 0.0
	var fshare := float(Charge.pget(d, tier, "field_share", 0.0))
	if fshare > 0.0 and heat > 0.0:
		FireUtil.spawn_field(w, owner.id if owner != null else -1, p, float(Charge.pget(d, tier, "field_r", 3.0)),
			float(Charge.pget(d, tier, "field_life", 3.0)), heat * fshare, blue, tier)
		heat *= 1.0 - fshare
	b.props.erase("on_impact")
	b.attack_id = 0
	VerbVolume.burst_at(w, owner, null, p, {"radius": float(b.props.get("impact_radius", 2.0)), "power": float(b.props.get("impact_power", 6.0)),
		"damage": float(b.props.get("impact_damage", 6.0)), "balance": 18.0, "knock": 4.0, "lift": 1.5,
		"cls": "blue_fire" if blue else "flame", "heat_hu": heat, "mat": "blue" if blue else "flame"})
	w.emit("fire_burst", {"body": b.id, "why": why, "blue": blue, "pos": p})
	w.decay_body(b, "burst")


## Fireballs and comets: quenched by the water they touch (booked boil), fed (+20 %) when a wind grip lets them go.
static func fireball_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.mat != Sim.Mat.FIRE:
		return false
	# Fed by wind: released from a wind grip (Air "grip_wind"), the fire takes a breath once.
	if not b.props.get("fed", false) and b.residual_owner >= 0 and b.controller < 0:
		var ra := w.get_actor(b.residual_owner)
		if ra != null and ra.action != null and String(Charge.pdef(ra.action).get("ccls", "")) == "grip_wind":
			b.props["fed"] = true
			var add := b.heat_payload * 0.2
			b.heat_payload += add
			w.ledger.generated += add     # fantasy oxygen (like a fanned flame): booked as created heat
			w.emit("fed", {"body": b.id, "by": ra.id, "add": add, "payload": b.heat_payload})
	# Quenched by water it touches (the pool, puddles, streams, shields, ice).
	for o in w.bodies:
		if o == b or not o.alive or not o.is_water() or o.form == Sim.Form.CLOUD:
			continue
		var touch := false
		if o.form == Sim.Form.POOL:
			touch = w.arena.in_pool(b.pos.x, b.pos.z) and b.pos.y < w.arena.pool_level + b.radius + 0.1
		elif o.form == Sim.Form.PUDDLE:
			touch = Vector2(b.pos.x - o.pos.x, b.pos.z - o.pos.z).length() < o.radius and b.pos.y < o.pos.y + b.radius + 0.15
		else:
			touch = b.pos.distance_to(o.pos) < b.radius + o.radius + 0.1
		if not touch:
			continue
		var src := Agent.of_body(w, b)
		var used := FireUtil.transfer(w, src, o, b.heat_payload)
		w.emit("steam_block", {"actor": o.controller, "body": o.id, "fire": b.id, "hu": used})
		w.emit("interaction", {"threat": String(Interactions.classify(b)), "counter": String(Interactions.counter_class(o, w)),
			"outcome": "extinguish", "band": "full", "ratio": 1.0, "tp": src.ch.H, "cp": o.mass, "perfect": false, "pos": b.pos,
			"dir": b.vel.normalized(), "threat_actor": b.attack_owner, "counter_actor": o.controller, "threat_body": b.id,
			"counter_body": o.id, "to": "steam", "rule": "fire_quench", "tier": b.tier})
		if b.heat_payload < 1.0:
			w.emit("extinguish", {"body": b.id, "by": "water"})
			w.decay_body(b, "quenched")
			return true
	return false


# ------------------------------------------------------------------ ground: Fire Line -> double -> triple -> Fire Ring

static func _ground() -> void:
	Moves.register("fire_line", {
		"element": E, "sub": SUB, "slot": "ground", "name": "Fire Line / Double / Triple / Fire Ring",
		"desc": "Flames race 12 m/s along the ground for 10 m, leaving a burning trail. Stopped by water and sand; ignites vines; warms stones it passes. Held: two lines, three lines, then a ring of fire around the rival.",
		"module": "kit_fire", "verb": "ground_line",
		"startup": _s(16), "active": _s(6), "recovery": _s(20), "cancel": 0.6, "chain": 0.25,
		"heat": 150.0, "source": "heat", "mat": "fire", "tag": "fire_line", "mass": 1.0, "speed": 12.0, "budget": 10.0,
		"width": 1.3, "damage": 9.0, "balance": 20.0, "knock": 2.0, "lift": 2.5, "kind": "fire", "steer": 14.0,
		"hit_status": "burning", "hit_status_t": 1.2, "trail_r": 1.0, "trail_life": 2.5, "trail_every": 2.2, "trail_share": 0.18,
		"tiers": {
			"t1": {"lines": 2, "heat_add": 90.0, "spread": 22.0},
			"t2": {"lines": 3, "heat_add": 180.0, "spread": 40.0, "damage": 11.0},
			"t3": {"ring": true, "heat_add": 330.0, "ring_r": 3.0, "ring_n": 6, "ring_life": 3.0, "damage": 12.0},
		},
		"hook_execute": Callable(FireFlame, "line_execute"),
		"counter": {"cls": "fire_field", "power": [8.0, 12.0, 16.0, 24.0]}, "threat": {"cls": "fire_field"},
		"anim": "mv_ground_slap", "anim_active": "fire_release", "fx": {"mat": "flame", "shape": "ground"},
		"ai": {"role": "zone", "range": [3.0, 11.0], "tags": ["ground", "burn", "trap_t3", "blocked_by_water"]},
	})


static func line_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var heat := Verbs.take_heat(inst)
	if bool(Charge.param(inst, "ring", false)):
		_fire_ring(w, a, inst, heat)
		return true
	var n := maxi(1, int(Charge.param(inst, "lines", 1)))
	var spread := deg_to_rad(float(Charge.param(inst, "spread", 0.0)))
	var dir: Vector3 = inst.data.get("aim", inst.data.get("face", a.forward()))
	dir.y = 0.0
	dir = dir.normalized() if dir.length() > 0.01 else a.forward()
	var ids: Array = []
	for k in n:
		var ang := 0.0 if n == 1 else lerpf(-spread * 0.5, spread * 0.5, float(k) / float(n - 1))
		inst.data["aim"] = dir.rotated(Vector3.UP, ang)
		inst.data["heat_paid"] = heat / float(n)
		var b := VerbGroundLine.launch(w, a, inst, {"source": "heat"})
		if b != null:
			b.props["trail_every"] = float(Charge.param(inst, "trail_every", 2.2))
			b.props["trail_r"] = float(Charge.param(inst, "trail_r", 1.0))
			b.props["trail_life"] = float(Charge.param(inst, "trail_life", 2.5))
			b.props["trail_share"] = float(Charge.param(inst, "trail_share", 0.18))
			b.props["last_trail"] = b.pos
			b.charge = 0.0
			ids.append(b.id)
	inst.data["aim"] = dir
	inst.data["bodies"] = ids
	return true


## T3 Fire Ring: six burning patches in a ring of 3 m around the rival (or the aim point).
static func _fire_ring(w: CombatWorld, a: ActorState, inst: ActionInst, heat: float) -> void:
	var c := FireUtil.aim_ground(w, a, inst, 11.0)
	var n := int(Charge.param(inst, "ring_n", 6))
	var r := float(Charge.param(inst, "ring_r", 3.0))
	for k in n:
		var ang := TAU * float(k) / float(n)
		var p := c + Vector3(cos(ang), 0.0, sin(ang)) * r
		var z := FireUtil.spawn_field(w, a.id, p, 1.25, float(Charge.param(inst, "ring_life", 3.0)), heat / float(n), false, inst.tier())
		z.props["dps"] = 4.0
	Verbs.fx(w, a, inst, "ring", {"pos": c, "radius": r, "power": float(Charge.counter_power(inst.def, inst.tier())), "dur": 3.0})
	w.emit("fire_ring", {"actor": a.id, "pos": c, "radius": r})


## Fire lines: drop burning trail patches as they run (each takes a share of the line's heat), go out on water.
static func line_tick(w: CombatWorld, b: MatBody, _dt: float) -> bool:
	if not b.alive or b.form != Sim.Form.WAVE:
		return false
	var wet := w.arena.in_pool(b.pos.x, b.pos.z) and b.pos.y < w.arena.pool_level + 0.4
	var pd := w.puddle_at(b.pos)
	if wet or (pd != null and pd.phase == Sim.Phase.LIQUID):
		var water := w.pool if wet else pd
		FireUtil.transfer(w, Agent.of_body(w, b), water, b.heat_payload)
		w.emit("extinguish", {"body": b.id, "by": "water"})
		w.emit("steam", {"body": b.id, "water": water.id, "kg": 0.0})
		b.attack_id = 0
		w.decay_body(b, "doused")
		return true
	var last: Vector3 = b.props.get("last_trail", b.pos)
	if Vector2(b.pos.x - last.x, b.pos.z - last.z).length() >= float(b.props.get("trail_every", 2.2)) and b.heat_payload > 6.0:
		b.props["last_trail"] = b.pos
		var share := b.heat_payload * float(b.props.get("trail_share", 0.18))
		b.heat_payload -= share
		var z := FireUtil.spawn_field(w, b.attack_owner, b.pos, float(b.props.get("trail_r", 1.0)), float(b.props.get("trail_life", 2.5)),
			share, false, b.tier)
		z.props["trail_of"] = b.id
	return false


# ------------------------------------------------------------------ sweep: Fire Fan -> Nova

static func _sweep() -> void:
	Moves.register("fire_fan", {
		"element": E, "sub": SUB, "slot": "sweep", "name": "Fire Fan / Wide Fan / Wheel / Nova",
		"desc": "A 120 degree sweep of flame at 3.5 m that clears vines and mist. Held: wider, then a full 360 degree Nova.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(6), "recovery": _s(16), "cancel": 0.6, "chain": 0.25,
		"heat": 90.0, "cls": "flame", "channel": "H", "range": 3.5, "angle": 60.0, "damage": 6.0, "balance": 14.0,
		"knock": 3.0, "lift": 0.8, "status": "burning", "status_t": 0.8,
		"tiers": {
			"t1": {"angle": 75.0, "heat_add": 60.0, "range": 4.0, "damage": 8.0},
			"t2": {"angle": 120.0, "heat_add": 140.0, "range": 4.5, "damage": 10.0, "balance": 22.0},
			"t3": {"angle": 180.0, "heat_add": 260.0, "range": 5.0, "damage": 13.0, "balance": 30.0, "knock": 6.0},
		},
		"counter": {"cls": "flame", "power": [4.0, 6.0, 9.0, 14.0]}, "threat": {"cls": "flame"},
		"anim": "mv_spin", "anim_active": "fire_release", "fx": {"mat": "flame", "shape": "fan"},
		"ai": {"role": "zone", "range": [0.0, 4.5], "tags": ["area", "clear_vines", "clear_mist", "anti_surround"]},
	})


# ------------------------------------------------------------------ guard: Flame Guard (Heat Sink)

static func _guard() -> void:
	Moves.register("flame_guard", {
		"element": E, "sub": SUB, "slot": "guard", "name": "Flame Guard / Heat Sink",
		"desc": "A guard of heat. Perfect: absorbs a flame's heat into the reserve (Heat Sink); against a magma blob or hot rock it draws 300 HU out instantly (it crusts mid-air); ice melts before it lands; returns a bolt with Return Current.",
		"module": "kit_fire", "verb": "barrier", "barrier": "aura",
		"counter": {"cls": "aura_flame", "power": [10.0, 10.0, 10.0, 10.0]}, "move_channel": 0.35,
		"anim": "guard", "anim_active": "deflect", "fx": {"mat": "flame", "shape": ""},
		"ai": {"role": "counter", "range": [0.0, 3.0], "tags": ["heat_sink", "perfect_draw", "melt_ice", "return_current"]},
	})


## Flame Guard: the generic barrier lifecycle plus the extended Heat Sink, resolved just before contact inside the
## perfect window: a hostile magma blob / hot rock loses 300 HU into the reserve (it crusts), ice melts.
static func guard_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action != inst or inst.phase != ActionInst.P.CHANNEL or not w.perfect_guard(a):
		return
	for b in w.bodies:
		if not b.alive or not b.is_projectile() or b.attack_owner == a.id or b.props.get("heat_sunk", false):
			continue
		var rel := a.chest() - b.pos
		if rel.length() > HEAT_SINK_REACH + b.radius or b.vel.dot(rel) <= 0.0:
			continue
		var cls := String(Interactions.classify(b))
		if cls == "magma" or cls == "hot_rock" or cls == "molten_metal":
			b.props["heat_sunk"] = true
			var room := Sim.RESERVE_MAX - a.heat_reserve
			var take := minf(minf(HEAT_SINK_DRAW, room), maxf(0.0, b.thermal_energy()))
			var got := -Thermal.heat(b, -take)
			a.heat_reserve += got
			w.emit("heat_sink", {"actor": a.id, "body": b.id, "gain": got, "cls": cls, "perfect": true})
			_ix(w, b, a, cls, "absorb", "hot_rock" if b.is_stone() else "")
			FxEvents.fx(w, "aura", "flame", {"actor": a.id, "pos": a.chest(), "body": b.id, "power": got / 20.0, "on": true, "shape": "small"})
		elif cls == "ice":
			b.props["heat_sunk"] = true
			var used := FireUtil.pay_into(w, a, b, FireUtil.melt_need(b, 1.0) * 0.6)
			w.emit("heat_sink", {"actor": a.id, "body": b.id, "gain": -used, "cls": cls, "perfect": true})
			_ix(w, b, a, cls, "transform", "water")


static func _ix(w: CombatWorld, b: MatBody, a: ActorState, cls: String, outcome: String, to: String) -> void:
	w.emit("interaction", {"threat": cls, "counter": "aura_flame", "outcome": outcome, "band": "full", "ratio": 1.5, "tp": b.thermal_energy() / 20.0,
		"cp": 15.0, "perfect": true, "pos": b.pos, "dir": b.vel.normalized(), "threat_actor": b.attack_owner, "counter_actor": a.id,
		"threat_body": b.id, "counter_body": -1, "to": to, "rule": "fire_heat_sink", "tier": b.tier})


# ------------------------------------------------------------------ push / sink: Backdraft, Ground Heat

static func _push_sink() -> void:
	Moves.register("backdraft", {
		"element": E, "sub": SUB, "slot": "push", "name": "Backdraft",
		"desc": "From the guard: release the whole heat reserve as a 5 m cone (H = reserve / 20, up to 25). Absorbed heat becomes the counter-attack.",
		"module": "verbs", "verb": "cone",
		"startup": _s(10), "active": _s(6), "recovery": _s(18), "cancel": 0.6,
		"cls": "flame", "channel": "H", "range": 5.0, "angle": 28.0, "damage": 4.0, "balance": 12.0, "knock": 4.0, "lift": 1.0,
		"status": "burning", "status_t": 1.0,
		"hook_execute": Callable(FireFlame, "backdraft_execute"),
		"counter": {"cls": "flame"}, "threat": {"cls": "flame"},
		"anim": "mv_push_two_hand", "anim_active": "fire_release", "fx": {"mat": "flame", "shape": "open"},
		"ai": {"role": "counter", "range": [0.0, 5.0], "tags": ["after_heat_sink", "reserve"]},
	})
	Moves.register("ground_heat", {
		"element": E, "sub": SUB, "slot": "sink", "name": "Ground Heat",
		"desc": "From the guard: vent the heat reserve into the ground; it boils the puddles within 2 m and melts an ice floor. Your footing becomes lightning-safe.",
		"module": "verbs", "verb": "burst", "startup": _s(8), "active": _s(10), "recovery": _s(14), "cancel": 0.6,
		"cost": 4.0, "radius": 2.0, "at": "self",
		"hook_execute": Callable(FireFlame, "ground_heat_execute"),
		"anim": "mv_stomp", "anim_active": "fire_release", "fx": {"mat": "flame", "shape": "ground"},
		"ai": {"role": "setup", "range": [0.0, 2.0], "tags": ["dry_footing", "anti_lightning", "vent"]},
	})


static func backdraft_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var hu := minf(a.heat_reserve, 500.0)
	a.heat_reserve -= hu
	inst.data["heat_paid"] = float(inst.data.get("heat_paid", 0.0)) + hu
	var p := clampf(hu / Interactions.HU_PER_PU, 1.0, 25.0)
	FireUtil.with_params(inst, {"power": p, "damage": 3.0 + p * 0.9, "balance": 8.0 + p * 1.6, "knock": 2.0 + p * 0.25})
	VerbVolume.cone(w, a, inst)
	w.emit("backdraft", {"actor": a.id, "hu": hu, "power": p})
	return true


static func ground_heat_execute(w: CombatWorld, a: ActorState, inst: ActionInst) -> bool:
	var r := float(Charge.param(inst, "radius", 2.0))
	var boiled := 0
	for b in w.bodies.duplicate():
		if not b.alive or not b.is_water():
			continue
		var flat := Vector2(b.pos.x - a.pos.x, b.pos.z - a.pos.z).length()
		if flat > r + b.radius:
			continue
		if b.form == Sim.Form.PUDDLE or (b.form == Sim.Form.ZONE and b.tag == &"ice_floor") or (b.form == Sim.Form.CHUNK and b.on_ground):
			var need := FireUtil.melt_need(b) + b.mass * Sim.WATER_LATENT_VAPOR
			var from_res := minf(a.heat_reserve, need)
			a.heat_reserve -= from_res
			var rest := need - from_res
			var paid := from_res + (w.pay_heat(a, minf(rest, 60.0)) if rest > 0.0 else 0.0)
			var used := w.heat_body(b, paid) if b.phase == Sim.Phase.FROZEN else 0.0
			if b.alive and b.is_water() and paid - used > 0.0:
				w.boil_water(b, paid - used, b.pos)
				used = paid
			w.ledger.spent += paid - used
			if b.alive and b.mass <= 0.05:
				if b.form == Sim.Form.ZONE:
					w.close_zone(b, "boiled")
				else:
					w.decay_body(b, "boiled")
			boiled += 1
	var vented := a.heat_reserve
	a.heat_reserve = 0.0
	w.ledger.vented += vented
	w.emit("vent", {"actor": a.id, "amount": vented, "ground": true, "boiled": boiled})
	Verbs.fx(w, a, inst, "burst", {"pos": a.pos + Vector3(0, 0.1, 0), "radius": r, "power": vented / 20.0, "shape": "ground"})
	return true


# ------------------------------------------------------------------ evade: Flare Dash, Rocket Hop

static func _mobility() -> void:
	Moves.register("flare_dash", {
		"element": E, "sub": SUB, "slot": "evade", "name": "Flare Dash",
		"desc": "A 4 m dash on a jet of flame (7 i-frames) that leaves a 1 s burning trail.",
		"module": "verbs", "verb": "dash", "startup": 0.0, "active": _s(14), "recovery": _s(8),
		"cost": 5.0, "heat": 20.0, "distance": 4.0, "iframes": _s(7), "dir": "stick",
		"hook_tick": Callable(FireFlame, "dash_tick"),
		"anim": "air_dash", "fx": {"mat": "flame", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 4.0], "tags": ["dash", "trail_fire"]},
	})
	Moves.register("rocket_hop", {
		"element": E, "sub": SUB, "slot": "evade_hold", "name": "Rocket Hop",
		"desc": "Hold evade: hop 2.5 m on flame and hover for 0.6 s (reach the high ledge, dodge ground lines).",
		"module": "kit_fire", "verb": "mode", "kind": "hover", "height": 2.5, "upkeep": 6.0, "speed_mult": 0.8, "hover_t": 0.85,
		"startup": 0.0, "active": 0.0, "recovery": _s(10),
		"anim": "jump", "anim_hold": "glide", "fx": {"mat": "flame", "shape": "small"},
		"ai": {"role": "mobility", "range": [0.0, 3.0], "tags": ["hover", "ledge", "dodge_ground"]},
	})


## Flare Dash: burning patches along the dash (each takes 5 HU of the paid 20).
static func dash_tick(w: CombatWorld, a: ActorState, inst: ActionInst, _it: ActorIntent) -> void:
	if inst.phase != ActionInst.P.ACTIVE:
		return
	var n := int(inst.data.get("trail_n", 0))
	if n >= 3 or inst.t < float(n) * 0.07:
		return
	inst.data["trail_n"] = n + 1
	var hu := minf(5.0, float(inst.data.get("heat_paid", 0.0)))
	inst.data["heat_paid"] = float(inst.data.get("heat_paid", 0.0)) - hu
	var z := FireUtil.spawn_field(w, a.id, a.pos, 0.7, 1.0, hu)
	z.props["dps"] = 2.0


## Rocket Hop: the hover mode ends by itself after hover_t (the flame runs out); then the fighter drops.
static func hop_tick(w: CombatWorld, a: ActorState, inst: ActionInst, it: ActorIntent) -> void:
	Verbs.on_tick(w, a, inst, it)
	if a.action == inst and inst.phase == ActionInst.P.CHANNEL and inst.t >= float(Charge.param(inst, "hover_t", 0.85)):
		w.set_phase(a, inst, ActionInst.P.RECOVERY)
	if a.action == inst and inst.phase == ActionInst.P.CHANNEL and w.tick % 8 == 0:
		Verbs.fx(w, a, inst, "trail", {"pos": a.pos, "dir": Vector3.DOWN, "length": 1.2})
