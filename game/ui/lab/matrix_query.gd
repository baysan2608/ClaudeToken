class_name MatrixQuery
extends RefCounted
## The Lab matrix viewer's engine (docs/MOVESET.md section 14): a threat (a SpawnCatalog entry at mass / speed /
## heat / tier) against a counter (a registry move at a tier, perfect or not, or an environment / legacy guard
## class) answered by Interactions.predict: TP, CP, ratio, band and outcome. Nothing is spawned in the live
## world: the threat is built in a scratch CombatWorld with the same code the spawner uses.

const ENV_COUNTERS := ["pool", "puddle", "plate", "arena_wall", "ground"]
const LEGACY_GUARDS := [["wall_stone", "Earth: Bulwark stone wall (120 kg)"], ["guard_earth", "Earth guard (plain)"], ["shield_water", "Water shield (held, 6 kg)"],
	["aura_flame", "Flame guard (aura)"], ["guard_wind", "Wind guard"]]


## Counter choices: [{id, label, kind: move|env|legacy, max_tier, element, sub, slot}], moves with a
## `counter` (cls) or a barrier verb, ordered by element / sub / slot.
static func counters() -> Array[Dictionary]:
	Moves.ensure()
	var out: Array[Dictionary] = []
	for g in LEGACY_GUARDS:
		out.append({"id": g[0], "label": g[1], "kind": "legacy", "max_tier": 0, "element": -1, "sub": 0, "slot": "guard"})
	var seen := {}
	for e in 4:
		for s in 4:
			for slot in Sim.SLOTS:
				var id := Moves.resolve(e, s, slot)
				if id == "" or seen.has(id) or not Moves.DEFS.has(id):
					continue
				var def: Dictionary = Moves.DEFS[id]
				var c: Dictionary = def.get("counter", {})
				if not (c.has("cls") or String(def.get("verb", "")) == "barrier"):
					continue
				seen[id] = true
				out.append({"id": id, "label": "%s / %s: %s" % [Sim.ELEMENT_NAMES[e], Sim.SUB_NAMES[e][s], String(def.get("name", id)).split(" / ")[0]],
					"kind": "move", "max_tier": Charge.max_tier(def), "element": e, "sub": s, "slot": slot})
	for k in ENV_COUNTERS:
		out.append({"id": k, "label": "Environment: %s" % k, "kind": "env", "max_tier": 0, "element": -1, "sub": 0, "slot": ""})
	return out


static func find_counter(id: String) -> Dictionary:
	for c in counters():
		if c.id == id:
			return c
	return {}


## The threat Agent of a catalogue entry (built in a scratch world). Returns {agent, world, summary} or {} when
## the entry cannot be represented.
static func make_threat(entry_id: String, params: Dictionary) -> Dictionary:
	var e := SpawnCatalog.find(entry_id)
	if e.is_empty():
		return {}
	var sw := CombatWorld.new(1)
	var p := sw.add_actor("You", Vector3(0, 0, 7), 0, {}, 0)
	var o := sw.add_actor("Rival", Vector3(0, 0, -7), 1, {}, 0)
	for a in sw.actors:
		a.elements = [true, true, true, true]
	if String(e.build) == "perform":
		var id := Moves.resolve(int(e.element), int(e.sub), String(e.slot))
		var def: Dictionary = Moves.DEFS.get(id, {})
		var tier := int(params.get("tier", (e.get("tier", [1]) as Array)[0]))
		var cls := StringName(String((def.get("threat", {}) as Dictionary).get("cls", (def.get("counter", {}) as Dictionary).get("cls", "blast"))))
		var power := Charge.counter_power(def, tier)
		if power < 0.0:
			power = 10.0
		var chn: String = Interactions.CLASS_CHANNEL.get(cls, "P")
		var ag := Agent.of_volume(sw, o, null, cls, o.hand_point(), (p.chest() - o.hand_point()).normalized(), {chn: power})
		ag.tier = tier
		return {"agent": ag, "world": sw, "player": p, "rival": o, "summary": "%s T%d, %s power %.0f" % [String(def.get("name", id)).split(" / ")[0], tier, chn, power]}
	var r := SpawnCatalog.spawn(sw, entry_id, params, p, o, p.pos + p.forward() * 5.0, true)
	if not r.ok or (r.bodies as Array).is_empty():
		return {}
	var b: MatBody = (r.bodies as Array)[0]
	var ag2 := Agent.of_body(sw, b, p)
	ag2.hostile = true
	return {"agent": ag2, "world": sw, "player": p, "rival": o, "body": b,
		"summary": "%s: %s %.0f kg at %.1f m/s, %.0f HU" % [String(e.label), String(ag2.cls), b.mass, b.vel.length(), ag2.heat]}


## Counter Agent for a counter choice at a tier (scratch world sw, defender p).
static func make_counter(sw: CombatWorld, p: ActorState, c: Dictionary, tier: int, perfect: bool) -> Agent:
	if String(c.id) == "wall_stone":
		var wall := sw.spawn_body(Sim.Mat.STONE, Sim.Form.WALL, Sim.WALL_MASS, p.pos + p.forward() * 2.0, "lab")
		wall.static_body = true
		wall.wall_rise = 1.0
		var wg := Agent.of_body(sw, wall)
		wg.perfect = perfect
		wg.hostile = false
		return wg
	match String(c.kind):
		"env":
			var g := Agent.of_env(sw, String(c.id), p.pos)
			g.perfect = false
			return g
		"legacy":
			var ag := Agent.new()
			ag.kind = "guard"
			ag.actor = p
			ag.perfect = perfect
			ag.pos = p.chest()
			ag.dir = p.forward()
			ag.ccls = StringName(String(c.id))
			ag.cls = ag.ccls
			ag.tier = tier
			match String(c.id):
				"shield_water":
					ag.power = 6.0 * 1.0
				"guard_wind":
					ag.power = Interactions.WIND_GUARD_CP
				_:
					ag.power = Interactions.PLAIN_GUARD_CP
			return ag
	return Agent.of_move(sw, p, String(c.id), tier, perfect)


## Full prediction. threat_params: {mass, speed, temp, tier} (SpawnCatalog.param_specs keys).
## Returns {ok, msg, tp, cp, cp_eff, ratio, band, outcome, rule_id, threat_cls, counter_cls, needs, summary}.
static func predict(entry_id: String, threat_params: Dictionary, counter_id: String, counter_tier: int, perfect: bool) -> Dictionary:
	var t := make_threat(entry_id, threat_params)
	if t.is_empty():
		return {"ok": false, "msg": "this threat cannot be built"}
	var c := find_counter(counter_id)
	if c.is_empty():
		return {"ok": false, "msg": "unknown counter"}
	var sw: CombatWorld = t.world
	var cg := make_counter(sw, t.player, c, counter_tier, perfect)
	var res := Interactions.predict(sw, t.agent, cg)
	var cp_eff := float(res.cp_eff)
	var tp := float(res.tp)
	var mult := cp_eff / maxf(float(res.cp), 1e-6)
	# Counter power needed for a full block (ratio 1): TP / (multipliers).
	var needs := tp / maxf(mult, 1e-6)
	return {"ok": true, "msg": "", "tp": tp, "cp": float(res.cp), "cp_eff": cp_eff, "ratio": float(res.ratio), "band": String(res.band), "perfect": perfect,
		"outcome": String(res.outcome), "rule_id": String(res.rule_id), "to": String(res.to), "threat_cls": String(t.agent.cls),
		"counter_cls": String(cg.ccls), "needs": needs, "summary": String(t.summary)}
