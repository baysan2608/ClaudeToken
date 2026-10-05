class_name AiPlanner
extends RefCounted
## Matrix-driven counter planner and offense chooser of the sparring AI (docs/AI.md, docs/MOVESET.md §13).
## Pure functions over observable state: it never steps the world, never reads the rival's intent and
## builds every prediction with Interactions.predict (no side effects). Everything is generic over the
## move registry: a kit move takes part as soon as its def carries `counter`, `threat` and `ai` metadata.
##
## Kit: {element: [subs]} (already limited to what the fighter has unlocked).
## Params (prm): reaction, counter, misjudge, timing_err, aggression, tiers, env, punish, weave, chain
## (AiPresets rows) + optional callables: draw_ok(body) -> bool (legacy heat-draw estimate).

const COUNTER_SLOTS := ["guard", "push", "sink", "tech", "strike", "thrust", "ground", "sweep", "evade_hold"]
const OFFENSE_SLOTS := ["strike", "thrust", "ground", "sweep", "tech"]
const PERFECT_LEAD := 0.09            # aim for the middle of the 0.18 s perfect window
const EVADE_VALUE := 0.25
const ACTIVE_PAD := 0.06              # a counter must be out this long before the threat arrives

## MOVESET §13 outcome values (REC 1.0 > DEF↩ 0.9 > XFM/SNK/CAP 0.8 > BLK/DEF 0.6 > partial 0.3 > evade 0.25 > fail -1).
const OUTCOME_VALUE := {
	"reclaim": 1.0, "redirect": 0.9, "reflect": 0.9,
	"capture": 0.8, "sink": 0.8, "transform": 0.8, "absorb": 0.8, "extinguish": 0.8, "ground": 0.8,
	"neutralize": 0.8, "disrupt": 0.8, "shatter": 0.75,
	"block": 0.6, "deflect": 0.6, "disperse": 0.6, "clash": 0.55, "push": 0.4, "heat": 0.2,
	"bend": 0.2, "weaken": 0.15, "slow": 0.15,
	"pass": -1.0, "amplify": -1.0, "overwhelm": -1.0, "conduct": -1.0, "fail": -1.0, "": -1.0,
}
## Kit outcome names (prefixes earth_ / water_ / fire_ / air_), valued from the COUNTER's side (the AI answers):
## the threat stopped / turned / taken = good, the counter itself burnt, smothered or fanned = bad.
const KIT_VALUE := {
	"air_catch": 1.0, "air_spit": 1.0, "air_compress": 0.8, "air_contest": 0.7, "air_cool": 0.55, "air_cut": 0.8,
	"air_infuse": 0.6, "air_pop": 0.75, "air_shatter": 0.75, "air_shrink": 0.5, "air_slow_bend": 0.3, "air_snuff": 0.8,
	"air_spatter": -0.5, "air_split": 0.5, "air_still": 0.8,
	"earth_absorb_face": 0.8, "earth_bolt_grit": 0.8, "earth_crust": 0.8, "earth_drag": 0.45, "earth_embed": 0.6,
	"earth_face_heat": 0.8, "earth_feed_face": 0.8, "earth_glass_beads": 0.8, "earth_glass_ground": 0.8,
	"earth_glassify": 0.8, "earth_glaze": 0.8, "earth_magnet_catch": 1.0, "earth_melt_in": 0.8, "earth_mud": 0.8,
	"earth_plate_bolt": 0.4, "earth_plate_heat": 0.45, "earth_quench": 0.8, "earth_ram_blocked": 0.3,
	"earth_ram_both": 0.3, "earth_ram_push": 0.9, "earth_rod_ground": 0.8, "earth_rod_melt": 0.4, "earth_set": 0.8,
	"earth_smother": 0.8, "earth_spike_stop": 0.8, "earth_stick": 0.8, "earth_wrap": 0.9,
	"fire_aegis_melt": 0.8, "fire_blown": 0.6, "fire_body_burst": -0.5, "fire_burn": 0.8, "fire_charge_body": 0.5,
	"fire_conduct_owner": 0.85, "fire_counter_blast": 0.9, "fire_dampen": 0.45, "fire_disrupt_zone": 0.8,
	"fire_evaporate": 0.8, "fire_fanned": -1.0, "fire_fill_void": 0.8, "fire_fulgurite": 0.8, "fire_glassify": 0.8,
	"fire_guard_absorb": 0.8, "fire_heat": 0.5, "fire_melt": 0.8, "fire_reactive": 0.6, "fire_smother": -1.0,
	"fire_snuffed": -1.0, "fire_static": 0.8, "fire_static_full": 0.6, "fire_suppressed": -1.0, "fire_tornado": -1.0,
	"water_brittle": 0.8, "water_burn": -1.0, "water_carry": 0.9, "water_condense_in": 0.8, "water_dampen": 0.45,
	"water_drown": 0.8, "water_feed": -1.0, "water_freeze": 0.8, "water_hot_block": 0.6, "water_melt": 0.8,
	"water_quench": 0.8, "water_ridge": 0.8, "water_skin": 0.9, "water_sling": 1.0,
}
## Unknown kit outcomes: keyword fallbacks, then the band.
const KIT_KEYWORDS := [
	["catch", 1.0], ["sling", 1.0], ["return", 0.9], ["burn", -1.0], ["fanned", -1.0], ["feed", -1.0],
	["smother", -1.0], ["snuff", -1.0], ["weak", 0.3], ["slow", 0.3], ["bend", 0.3],
]
const ROLE_WEIGHT := {"poke": 0.5, "zone": 0.42, "finisher": 0.55, "setup": 0.35, "counter": 0.2}
const CHANNEL_OF := {"flame": "H", "blue_fire": "H", "steam": "H", "lightning": "E", "frost": "C", "blast": "P",
	"gust": "P", "sound": "P", "water": "K", "sand": "K", "vacuum": "P", "stone": "K", "metal": "K", "ice": "K",
	"vine": "P", "lava_wave": "H", "fire_field": "H", "tornado": "P", "mist": "P", "ember": "H"}
const VOLUME_VERBS := ["cone", "beam", "burst"]
const LEGACY_VOLUME := {"fire_attack": "flame", "air_attack": "gust", "water_attack": "water", "lightning": "lightning"}


# ================================================================ outcome values

static func outcome_value(outcome: String, band: String, rule: Dictionary = {}) -> float:
	var v := 0.0
	if OUTCOME_VALUE.has(outcome):
		v = float(OUTCOME_VALUE[outcome])
		if outcome == "block" and float(rule.get("chip", 0.0)) > 0.0:
			v = 0.35   # a plain guard: blocked with chip damage and balance loss
	elif KIT_VALUE.has(outcome):
		v = float(KIT_VALUE[outcome])
	else:
		var found := false
		for kw in KIT_KEYWORDS:
			if outcome.contains(String(kw[0])):
				v = float(kw[1])
				found = true
				break
		if not found:
			match band:
				"partial":
					v = 0.3
				"fail":
					v = -1.0
				_:
					v = 0.75
	# A rule's partial band is the threat continuing weakened, whatever its name.
	if band == "partial" and not rule.has("bands") and v > 0.45:
		v = 0.45
	return v


## Chance a press aimed at the middle of the perfect window lands inside it with a uniform ±err error.
static func perfect_chance(err: float) -> float:
	if err <= 1e-4:
		return 0.97
	return clampf(0.07 / err, 0.05, 0.97)


# ================================================================ threats (observable state only)

## A hostile body heading at `me`: {key, kind, body, agent, cls, tti, dist, closing} or {} (not a threat).
static func body_threat(w: CombatWorld, me: ActorState, b: MatBody) -> Dictionary:
	if not b.alive or b.controller >= 0 or b == w.pool:
		return {}
	var hostile_owner := b.attack_owner if b.attack_id != 0 else b.owner
	if hostile_owner == me.id or hostile_owner < 0:
		return {}
	var foe := w.get_actor(hostile_owner)
	if foe != null and foe.team == me.team:
		return {}
	var key := "b%d:%d" % [b.id, b.attack_id]
	var res := {"key": key, "kind": "body", "body": b, "dist": 0.0, "tti": 9.0, "closing": 0.0}
	if b.form == Sim.Form.WAVE:
		var to := me.pos - b.pos
		to.y = 0.0
		var d := to.length()
		var wdir := b.wave_dir if b.wave_dir.length() > 0.1 else Vector3(b.vel.x, 0, b.vel.z).normalized()
		if d > 16.0 or wdir.length() < 0.1 or to.normalized().dot(wdir) < 0.5:
			return {}
		var speed := Vector3(b.vel.x, 0, b.vel.z).length()
		if speed < 0.5:
			speed = float(b.props.get("speed", Moves.DEFS.pour.wave_speed))
			if b.mat == Sim.Mat.STONE:
				speed *= Thermal.flow_factor(b)
		speed = maxf(speed, 0.5)
		res.dist = d
		res.closing = speed
		res.tti = maxf(0.0, d - b.wave_width * 0.5 - Sim.ACTOR_RADIUS) / speed
	elif b.form == Sim.Form.ZONE or b.form == Sim.Form.CLOUD:
		var to2 := me.pos - b.pos
		to2.y = 0.0
		var gap := to2.length() - maxf(b.zone_radius, b.radius) - Sim.ACTOR_RADIUS
		var v := Vector3(b.vel.x, 0, b.vel.z)
		var closing := v.dot(to2.normalized()) if to2.length() > 0.01 else 0.0
		if gap > 0.8 and closing < 0.5:
			return {}
		if gap > 12.0:
			return {}
		res.dist = maxf(gap, 0.0)
		res.closing = maxf(closing, 0.0)
		res.tti = 0.0 if gap <= 0.0 else (gap / closing if closing > 0.5 else 0.6)
	elif b.is_projectile():
		var rel := me.chest() - b.pos
		var closing2 := b.vel.dot(rel.normalized())
		if closing2 < 2.0 or rel.length() > 18.0:
			return {}
		var tti := rel.length() / closing2
		# Ballistic bodies (thrown stones, blobs) fall along their arc: include gravity in the aim check.
		var miss := b.pos + b.vel * tti + Vector3(0.0, -0.5 * Sim.GRAVITY * b.gravity_scale * tti * tti, 0.0) - me.chest()
		miss.y *= 0.5
		if miss.length() > 1.6 + b.radius:
			return {}
		res.dist = rel.length()
		res.closing = closing2
		res.tti = tti
	else:
		return {}
	res["agent"] = Agent.of_body(w, b, me)
	res["cls"] = String(res.agent.cls)
	res["owner"] = hostile_owner
	return res


## The rival's visible wind-up of a volume move (cone, beam, burst, legacy strikes) that reaches `me`.
## Charge tiers are telegraphed (rings, sound), so the tier is perceived. {} when nothing threatens.
static func action_threat(w: CombatWorld, me: ActorState, foe: ActorState) -> Dictionary:
	if foe == null or foe.action == null or foe.stun > 0.0:
		return {}
	var act := foe.action
	if act.phase != ActionInst.P.STARTUP and act.phase != ActionInst.P.CHARGE and act.phase != ActionInst.P.CHANNEL:
		return {}
	var def := act.def
	var cls := String(def.get("threat", {}).get("cls", ""))
	var verb := String(def.get("verb", ""))
	var legacy := LEGACY_VOLUME.has(act.id)
	if not legacy and not VOLUME_VERBS.has(verb):
		return {}
	if legacy:
		cls = LEGACY_VOLUME[act.id]
		if act.id == "fire_attack" and foe.has("lightning") and act.phase == ActionInst.P.CHARGE:
			cls = "lightning"
	if cls == "":
		return {}
	var tier := act.tier()
	var charging := act.phase == ActionInst.P.CHARGE or act.phase == ActionInst.P.CHANNEL
	var seen_tier := tier
	if charging:
		tier = mini(tier + 1, maxi(Charge.max_tier(def), tier))   # a growing charge: answer the next tier
	var reach := reach_of(def, maxi(tier, 1 if charging and legacy else tier), "volume")
	if charging:
		for t in range(tier, Charge.max_tier(def) + 1):
			reach = maxf(reach, reach_of(def, t, "volume"))   # a charge can still grow: its longest reach counts
	if cls == "lightning" and legacy:
		reach = float(Moves.DEFS.lightning.range)
	var to := me.pos - foe.pos
	to.y = 0.0
	var d := to.length()
	if d > reach + 0.5:
		return {}
	var power := 0.0
	var tp: Variant = def.get("threat", {}).get("power", null)
	if tp is Array and not (tp as Array).is_empty():
		power = float(tp[clampi(tier, 0, (tp as Array).size() - 1)])
	elif tp != null:
		power = float(tp)
	else:
		power = maxf(Charge.counter_power(def, tier), 0.0)
	if cls == "lightning" and legacy:
		power = float(Moves.DEFS.lightning.damage)
	if power <= 0.0:
		power = float(Charge.pget(def, tier, "damage", 8.0))
	var chn := String(CHANNEL_OF.get(cls, "P"))
	var ag := Agent.of_volume(w, foe, null, StringName(cls), foe.chest(), to.normalized() if d > 0.01 else foe.forward(), {chn: power})
	ag.tier = tier
	ag.hostile = true
	var tti := 0.12
	if act.phase == ActionInst.P.STARTUP:
		tti = maxf(0.0, float(act.data.get("startup", def.get("startup", 0.0))) - act.total) + 0.02
	return {"key": "v%d" % act.attack_id, "kind": "volume", "agent": ag, "cls": cls, "dist": d, "tti": tti,
		"closing": 0.0, "charging": charging, "attack_id": act.attack_id, "owner": foe.id, "move": act.id, "tier": seen_tier}


## Perceived copy of a threat agent: every power channel × (1 + err) (power misjudgement).
static func perceived(agent: Agent, err: float) -> Agent:
	var g := Agent.new()
	g.kind = agent.kind
	g.cls = agent.cls
	g.ccls = agent.ccls
	g.body = agent.body
	g.actor = agent.actor
	g.inst = agent.inst
	g.def = agent.def
	g.pos = agent.pos
	g.dir = agent.dir
	g.mass = agent.mass * (1.0 + err)
	g.speed = agent.speed
	g.heat = agent.heat
	g.tier = agent.tier
	g.power = agent.power
	g.mat = agent.mat
	g.hostile = agent.hostile
	g.data = agent.data
	for k in agent.ch:
		g.ch[k] = float(agent.ch[k]) * (1.0 + err)
	return g


# ================================================================ kit

## Every (element, sub, slot, id) of the kit, deduplicated (an unbound slot falling back to the sub-0 move is
## listed once, under sub 0 when sub 0 is in the kit).
static func kit_moves(kit: Dictionary, slots: Array) -> Array:
	var out: Array = []
	for e in kit:
		var subs: Array = kit[e]
		var seen := {}
		for s in subs:
			for slot in slots:
				var id := Moves.resolve(int(e), int(s), slot)
				if id == "" or not Moves.DEFS.has(id):
					continue
				if int(s) != 0 and subs.has(0) and id == Moves.resolve(int(e), 0, slot):
					continue
				var k := "%s/%s" % [id, slot]
				if seen.has(k):
					continue
				seen[k] = true
				out.append({"id": id, "element": int(e), "sub": int(s), "slot": slot})
	return out


# ================================================================ costs / timing

## Seconds until a press of `press` can start (guards and evades cancel recoveries from the move's
## cancel fraction; held charges and channels are ours to release or cancel).
static func busy_time(me: ActorState, press: String) -> float:
	if me.stun > 0.0:
		return me.stun
	var a := me.action
	if a == null:
		return 0.0
	if a.id == "guard":
		return 0.0 if press != "guard" else Sim.DT
	if a.phase == ActionInst.P.CHARGE or a.phase == ActionInst.P.CHANNEL:
		return 0.0 if (press == "guard" or press == "evade") else 0.1
	var d := a.def
	var rec := float(d.get("recovery", 0.0)) * Status.recovery_mult(me)
	var cancel_at := rec
	if (press == "guard" or press == "evade") and d.has("cancel"):
		cancel_at = rec * float(d.cancel)
	if a.phase == ActionInst.P.RECOVERY:
		return maxf(0.0, cancel_at - a.t)
	var left := cancel_at + float(a.data.get("active", d.get("active", 0.0)))
	if a.phase == ActionInst.P.ACTIVE:
		return maxf(0.0, left - a.t)
	return left + maxf(0.0, float(a.data.get("startup", d.get("startup", 0.0))) - a.total)


## Hold time to reach `tier` with this def (0 for T0).
static func hold_for(def: Dictionary, tier: int) -> float:
	if tier <= 0:
		return 0.0
	var times := Charge.tier_times(def)
	return float(times[clampi(tier - 1, 0, 2)]) + 0.02


## Resources a move costs at a tier: {focus, heat, water, metal}.
static func move_cost(def: Dictionary, tier: int, hold: float) -> Dictionary:
	var focus := float(Charge.pget(def, 0, "cost", 0.0))
	var heat := float(Charge.pget(def, 0, "heat", def.get("cost_hu", 0.0)))
	if tier >= 1:
		focus += float(Charge.pget(def, tier, "cost_add", maxf(0.0, float(def.get("heavy_cost", def.get("cost", 0.0))) - float(def.get("cost", 0.0)))))
		heat += float(Charge.pget(def, tier, "heat_add", maxf(0.0, float(def.get("heavy_cost_hu", 0.0)) - float(def.get("cost_hu", 0.0)))))
		if Charge.max_tier(def) >= 2:
			var t1 := float(Charge.tier_times(def)[0])
			focus += maxf(0.0, hold - t1) * float(Charge.pget(def, tier, "charge_drain", Charge.DEFAULT_DRAIN))
	var water := float(Charge.pget(def, tier, "water", 0.0))
	var metal := float(Charge.pget(def, tier, "metal", 0.0))
	# Projectiles drawn from a carried source spend it (waterskin / metal satchel).
	var src := String(Charge.pget(def, tier, "source", ""))
	if src == "waterskin" or src == "metal":
		var kg := float(Charge.pget(def, tier, "mass", 0.0)) * maxf(1.0, float(Charge.pget(def, tier, "count", 1)))
		if src == "waterskin":
			water = maxf(water, kg)
		else:
			metal = maxf(metal, kg)
	return {"focus": focus, "heat": heat, "water": water, "metal": metal}


static func can_afford(w: CombatWorld, me: ActorState, cost: Dictionary) -> bool:
	if me.focus + 1e-6 < float(cost.focus):
		return false
	var hu := float(cost.heat)
	if hu > 0.0 and not w.can_pay_heat(me, hu + float(cost.focus) * Sim.HU_PER_FOCUS):
		return false
	if float(cost.water) > 0.0 and me.water_carried + 1e-6 < float(cost.water) and not me.in_water:
		return false
	if float(cost.metal) > 0.0 and me.metal_carried + 1e-6 < float(cost.metal):
		return false
	return true


## How far a move reaches (m) at a tier: cone/beam/burst range, grip reach, guard contact 0, travelling
## projectiles / ground lines their ai range.
static func reach_of(def: Dictionary, tier: int, _kind: String = "") -> float:
	var verb := String(def.get("verb", ""))
	var ai_rng: Array = def.get("ai", {}).get("range", [0.0, 6.0])
	var ai_max := float(ai_rng[1]) if ai_rng.size() > 1 else 6.0
	if def.has("heavy_range") and tier >= 1:
		return float(def.heavy_range)
	match verb:
		"cone", "beam":
			return float(Charge.pget(def, tier, "range", ai_max))
		"burst":
			return float(Charge.pget(def, tier, "range", Charge.pget(def, tier, "distance", 0.0))) + float(Charge.pget(def, tier, "radius", 1.5))
		"grip":
			return float(Charge.pget(def, tier, "reach", ai_max))
		"zone", "summon":
			return float(Charge.pget(def, tier, "range", Charge.pget(def, tier, "distance", ai_max))) + float(Charge.pget(def, tier, "radius", 1.0))
		"projectile", "ground_line":
			return ai_max
	if def.has("range"):
		return float(Charge.pget(def, tier, "range", ai_max))
	if def.has("reach"):
		return float(def.reach)
	return ai_max


## How a counter meets a threat: contact (barriers, guards, stances), ranged (volumes, grips, sinks:
## released once the threat is in reach) or travel (projectiles, ground lines: released at once).
static func meet_kind(def: Dictionary, slot: String) -> String:
	if slot == "guard" or slot == "evade_hold":
		return "contact"
	var verb := String(def.get("verb", ""))
	match verb:
		"barrier", "stance", "mode":
			return "contact"
		"projectile", "ground_line", "pour":
			return "travel"
	return "ranged"


# ================================================================ counters

## Hypothetical counter agent of a kit move (pure). Legacy sub-0 guards get their element's barrier.
static func counter_agent(w: CombatWorld, me: ActorState, c: Dictionary, tier: int, perfect: bool, threat: Dictionary) -> Agent:
	var id := String(c.id)
	if c.slot == "guard" and id == "guard":
		var g := Agent.new()
		g.kind = "move"
		g.actor = me
		g.tier = 0
		g.perfect = perfect
		g.pos = me.chest()
		g.dir = me.forward()
		match int(c.element):
			Sim.Element.EARTH:
				g.ccls = &"wall_stone"
				g.power = Sim.WALL_MASS * 0.25
			Sim.Element.WATER:
				if me.water_carried >= 1.0:
					g.ccls = &"shield_water"
					g.power = me.water_carried * 1.0
				else:
					g.ccls = &"guard"
					g.power = Interactions.PLAIN_GUARD_CP
			Sim.Element.FIRE:
				g.ccls = &"aura_flame"
				g.power = Interactions.PLAIN_GUARD_CP
			_:
				g.ccls = &"guard_wind"
				g.power = Interactions.WIND_GUARD_CP
		g.cls = g.ccls
		return g
	var ag := Agent.of_move(w, me, id, tier, perfect)
	if c.slot == "tech":
		var mode := tech_mode(w, me, c, threat)
		if mode == "DRAW":
			ag.ccls = &"draw_heat"
			ag.cls = ag.ccls
	return ag


## Context mode of a technique on the threat (legacy Fire thermal: DRAW / HEAT; else the registered preview).
static func tech_mode(w: CombatWorld, me: ActorState, c: Dictionary, threat: Dictionary) -> String:
	var b: MatBody = threat.get("body")
	if int(c.element) == Sim.Element.FIRE and int(c.sub) == 0 and String(c.id) == "fire_tech":
		if b == null:
			return ""
		if b.is_stone() and (b.liquid > 0.0 or b.is_hot()):
			return "DRAW" if me.has("heat_draw") else ""
		if b.is_stone() or b.is_water():
			return "HEAT" if me.has("magma") or b.is_water() else ""
		return "HEAT"
	return "GRIP"


## Is the technique legal on this threat at all (kit techniques: Interactions.allows on the counter class)?
static func tech_legal(w: CombatWorld, me: ActorState, c: Dictionary, threat: Dictionary, ag: Agent) -> bool:
	var b: MatBody = threat.get("body")
	if b == null:
		return false
	if String(c.id) == "fire_tech" and int(c.sub) == 0:
		return tech_mode(w, me, c, threat) != ""
	if b.form == Sim.Form.ZONE:
		return false
	return Interactions.allows(b, ag.ccls)


## Scores every feasible counter of the kit against a perceived threat. Returns options sorted best first:
## {id, element, sub, slot, press, tier, hold, perfect, p, outcome, band, ratio, utility, value, kind,
##  press_in, release_in, guard_for, gesture, label}. Always includes evade (if not rooted) and a plain guard.
static func counters(w: CombatWorld, me: ActorState, threat: Dictionary, kit: Dictionary, prm: Dictionary, err: float) -> Array:
	var out: Array = []
	var ag := perceived(threat.agent, err)
	var tti := float(threat.tti)
	var is_volume: bool = threat.kind == "volume"
	var b: MatBody = threat.get("body")
	var only: Array = prm.get("only", [])
	for c in kit_moves(kit, COUNTER_SLOTS):
		if not only.is_empty() and not only.has(c.id):
			continue
		var def: Dictionary = Moves.DEFS[c.id]
		var slot := String(c.slot)
		if slot != "guard" and not def.has("counter"):
			continue
		if slot == "guard" and c.id != "guard" and not def.has("counter") and String(def.get("verb", "")) != "barrier":
			continue
		var mk := meet_kind(def, slot)
		if is_volume and mk != "contact" and slot != "sink" and slot != "push":
			continue   # instant volumes are answered where they land: barriers, guards, stances
		if slot == "tech" and (b == null or c.id == "air_tech" and b.mass > 30.0):
			continue
		var maxt := Charge.max_tier(def) if c.id != "guard" else 0
		if slot == "evade_hold" and String(def.get("verb", "")) != "stance":
			continue
		var tried_perfect := false
		for tier in range(0, maxt + 1):
			var opt := _evaluate(w, me, threat, ag, c, def, tier, false, prm)
			if not opt.is_empty():
				out.append(opt)
			if slot == "guard" and not tried_perfect and not is_volume:
				tried_perfect = true
				var po := _evaluate(w, me, threat, ag, c, def, tier, true, prm)
				if not po.is_empty():
					out.append(po)
	# Evade with the current element's evade (no switch).
	if not Status.rooted(me):
		var p := 0.85
		match String(threat.kind):
			"volume":
				p = 0.7 if not threat.get("charging", false) else 0.8
			"body":
				if b != null and (b.form == Sim.Form.WAVE or b.form == Sim.Form.ZONE or b.form == Sim.Form.CLOUD):
					p = 0.55
		var busy := busy_time(me, "evade")
		if busy > tti - 0.05:
			p *= 0.3
		out.append({"id": Moves.resolve(me.element, me.sub(), "evade"), "element": me.element, "sub": me.sub(), "slot": "evade",
			"press": "evade", "tier": 0, "hold": 0.0, "perfect": false, "p": p, "outcome": "evade", "band": "evade", "ratio": 0.0,
			"value": EVADE_VALUE, "utility": EVADE_VALUE * p - 0.01, "kind": "evade", "press_in": maxf(0.0, minf(tti - 0.3, busy)),
			"release_in": 0.0, "guard_for": 0.0, "gesture": 0, "label": "evade"})
	out.sort_custom(func(x, y):
		if absf(float(x.utility) - float(y.utility)) > 1e-6:
			return float(x.utility) > float(y.utility)
		if absf(float(x.value) - float(y.value)) > 1e-6:
			return float(x.value) > float(y.value)
		return String(x.id) + String(x.slot) + str(x.tier) < String(y.id) + String(y.slot) + str(y.tier))
	return out


static func _evaluate(w: CombatWorld, me: ActorState, threat: Dictionary, ag: Agent, c: Dictionary, def: Dictionary, tier: int, perfect: bool, prm: Dictionary) -> Dictionary:
	var slot := String(c.slot)
	var b: MatBody = threat.get("body")
	var tti := float(threat.tti)
	var counter := counter_agent(w, me, c, tier, perfect, threat)
	if slot == "tech" and not tech_legal(w, me, c, threat, counter):
		return {}
	if slot == "tech" and b != null:
		if b.mass > float(Charge.pget(def, tier, "max_mass", me.max_control_mass)):
			return {}
		if tech_mode(w, me, c, threat) == "HEAT" and b.is_stone():
			# Magma grip: melting it costs ~19.8 HU/kg (reserve first, then Focus).
			var need := b.mass * (Sim.STONE_C * (Sim.STONE_MELT_C - b.temp) + Sim.STONE_LATENT)
			if me.heat_reserve + me.focus * Sim.HU_PER_FOCUS < need * 0.9 + 60.0:
				return {}
	# Legacy Earth wall: needs ground in front and the threat far enough to meet the risen wall.
	if slot == "guard" and c.id == "guard" and int(c.element) == Sim.Element.EARTH:
		if not me.grounded or me.focus < 8.0:
			return {}
	var pred := Interactions.predict(w, ag, counter)
	var value := outcome_value(String(pred.outcome), String(pred.band), pred.rule)
	if float(pred.rule.get("chip", 0.0)) > 0.0 and (b == null or not b.is_projectile()):
		value = minf(value, 0.35)   # a plain guard only chips waves, zones and volumes away
	if value <= -0.99:
		return {}
	# ---- timing
	var press := "attack"
	var gesture := 0
	match slot:
		"guard", "push", "sink":
			press = "guard"
		"tech":
			press = "tech"
		"evade_hold":
			press = "evade"
		"thrust":
			gesture = Sim.Gesture.UP
		"ground":
			gesture = Sim.Gesture.DOWN
		"sweep":
			gesture = Sim.Gesture.SIDE
	var switch := 0.0 if (int(c.element) == me.element and int(c.sub) == me.sub_of(int(c.element))) else Sim.DT
	var busy := busy_time(me, press)
	var startup := float(def.get("startup", 0.0))
	var hold := hold_for(def, tier) if c.id != "guard" else 0.0
	var t_fire := 0.0
	match slot:
		"guard":
			t_fire = hold
		"push", "sink":
			t_fire = hold + 2.0 * Sim.DT + startup
		"tech":
			t_fire = startup + hold
			if tech_mode(w, me, c, threat) == "DRAW":
				t_fire = float(Moves.DEFS.fire_tech.draw_startup)
		"evade_hold":
			t_fire = 0.2
		_:
			t_fire = maxf(startup, hold)
	var t_ready := switch + busy + t_fire
	var kind := meet_kind(def, slot)
	var release_in := t_ready
	var press_in := switch + busy
	var dist := float(threat.dist)
	var closing := float(threat.closing)
	if kind == "contact":
		if t_ready > tti - 0.02:
			return {}
	elif kind == "ranged":
		var reach := reach_of(def, tier)
		if slot == "tech" and tech_mode(w, me, c, threat) == "DRAW":
			reach = float(Moves.DEFS.fire_tech.draw_range) - 0.5
		if reach < 1.0:
			return {}
		var in_reach := 0.0 if dist <= reach else (dist - reach) / maxf(closing, 0.5)
		release_in = maxf(t_ready, in_reach)
		if release_in + ACTIVE_PAD > tti:
			return {}
		# Press late enough that the release lands with the threat in reach (charge holds included).
		press_in = maxf(press_in, release_in - t_fire)
		if b != null and slot != "sink" and not w.los(me.chest(), b.pos + Vector3(0, 0.3, 0)):
			return {}
	else:
		if t_ready + 0.15 > tti:
			return {}
	if slot == "tech" and tech_mode(w, me, c, threat) == "DRAW":
		var draw_ok: Callable = prm.get("draw_ok", Callable())
		if b == null or (draw_ok.is_valid() and not bool(draw_ok.call(b))):
			return {}
	# ---- resources
	var cost := move_cost(def, tier, hold)
	if slot == "guard" and c.id == "guard" and int(c.element) == Sim.Element.EARTH:
		cost.focus = float(cost.focus) + 8.0
	if not can_afford(w, me, cost):
		return {}
	# ---- success probability
	var p := 1.0
	var plain_value := value
	if perfect:
		p = perfect_chance(float(prm.get("timing_err", 0.07)))
		var plain := Interactions.predict(w, ag, counter_agent(w, me, c, tier, false, threat))
		plain_value = outcome_value(String(plain.outcome), String(plain.band), plain.rule)
		if float(plain.rule.get("chip", 0.0)) > 0.0 and (b == null or not b.is_projectile()):
			plain_value = minf(plain_value, 0.35)
		if value <= plain_value + 1e-6:
			return {}   # perfect adds nothing here
		if tti - t_ready < PERFECT_LEAD + 0.05:
			return {}
	var ev := p * value + (1.0 - p) * plain_value
	var cost_term := float(cost.focus) / 100.0 * 0.5 + float(cost.heat) / 1000.0 + (0.02 if switch > 0.0 else 0.0) + hold * 0.04
	var guard_for := 0.0
	if press == "guard" and slot == "guard":
		guard_for = tti + 0.3
		if b != null and (b.form == Sim.Form.WAVE or b.form == Sim.Form.ZONE):
			guard_for = tti + 0.8
	var label := "%s %s%s T%d" % [Sim.SUB_NAMES[int(c.element)][int(c.sub)], c.id, "*" if perfect else "", tier]
	return {"id": c.id, "element": int(c.element), "sub": int(c.sub), "slot": slot, "press": press, "tier": tier,
		"hold": hold, "perfect": perfect, "p": p, "outcome": String(pred.outcome), "band": String(pred.band),
		"ratio": float(pred.ratio), "value": value, "utility": ev - cost_term, "kind": kind, "press_in": press_in,
		"release_in": release_in, "guard_for": guard_for, "gesture": gesture, "label": label,
		"mode": tech_mode(w, me, c, threat) if slot == "tech" else ""}


## Picks an option: with probability `counter` the best, else a random feasible one (skill misses).
static func choose(options: Array, prm: Dictionary, rng: RandomNumberGenerator) -> Dictionary:
	if options.is_empty():
		return {}
	if rng.randf() < float(prm.get("counter", 0.75)):
		return options[0]
	var pool: Array = []
	for o in options:
		if float(o.value) > -0.5:
			pool.append(o)
	if pool.is_empty():
		return options[0]
	return pool[rng.randi_range(0, pool.size() - 1)]


# ================================================================ offense

## What the AI can see of the target: {dist, wet, in_water, on_plate, near_wall, airborne, charging,
## recovering, behind_barrier, wall_body, cover, hidden}.
static func observe(w: CombatWorld, me: ActorState, foe: ActorState) -> Dictionary:
	var to := foe.pos - me.pos
	to.y = 0.0
	var d := to.length()
	var dir := to / d if d > 0.01 else me.forward()
	var st := {"dist": d, "dir": dir, "wet": foe.wetness > Status.WET_AT or Status.has(foe, "wet"), "in_water": foe.in_water,
		"on_plate": foe.surface == "metal", "airborne": not foe.grounded, "charging": false, "recovering": false,
		"behind_barrier": false, "wall_body": -1, "cover": not w.arena.has_los(me.chest(), foe.chest()),
		"hidden": Status.hidden(foe) and d > 2.0, "charge_tier": 0, "near_wall": false, "puddle": foe.surface == "puddle"}
	if foe.action != null:
		var ph := foe.action.phase
		st.charging = ph == ActionInst.P.CHARGE or (ph == ActionInst.P.CHANNEL and foe.action.id != "guard")
		st.recovering = ph == ActionInst.P.RECOVERY
		st.charge_tier = foe.action.tier()
	var t := w.wall_hit(me.chest(), foe.chest())
	if t >= 0.0:
		for b in w.bodies:
			if b.alive and b.form == Sim.Form.WALL and b.wall_rise > 0.5 and w.wall_segment_t(me.chest(), foe.chest(), b) >= 0.0:
				st.behind_barrier = true
				st.wall_body = b.id
				break
	var probe := foe.chest() + dir * 2.2
	st.near_wall = not w.arena.has_los(foe.chest(), probe)
	return st


## Scored offensive options for the kit against the target's visible state, best first:
## {id, element, sub, slot, press, gesture, tier, hold, score, label, aim}.
static func offense(w: CombatWorld, me: ActorState, foe: ActorState, kit: Dictionary, prm: Dictionary, rng: RandomNumberGenerator) -> Array:
	var st := observe(w, me, foe)
	var out: Array = []
	var d := float(st.dist)
	var lava_near := false
	for b in w.bodies:
		if b.alive and b.is_stone() and b.liquid > 0.5 and b.controller < 0 and b.pos.distance_to(me.pos) < 8.0:
			lava_near = true
	for c in kit_moves(kit, OFFENSE_SLOTS):
		var def: Dictionary = Moves.DEFS[c.id]
		var ai: Dictionary = def.get("ai", {})
		var role := String(ai.get("role", ""))
		var tags: Array = ai.get("tags", [])
		if role == "" or role == "mobility":
			continue
		if role == "counter" and not (tags.has("melt_wall") or tags.has("scorch_wall")) :
			continue
		if c.slot == "tech" and (c.id == "air_tech" or c.id == "water_tech" or c.id == "fire_tech"):
			continue   # legacy techniques need a target body (handled as opportunities)
		var skip := false
		for tg in tags:
			var ts := String(tg)
			if ts == "needs_lava" and not lava_near:
				skip = true
			elif (ts.begins_with("needs_") and ts != "needs_lava") or ts.begins_with("after_"):
				skip = true
		if skip:
			continue
		var rng_arr: Array = ai.get("range", [0.0, 8.0])
		var lo := float(rng_arr[0])
		var hi := float(rng_arr[1]) if rng_arr.size() > 1 else 8.0
		var melts := tags.has("melt_wall") or tags.has("scorch_wall")
		var aim_body := -1
		var aim := Vector3.ZERO
		var dd := d
		if melts:
			if not st.behind_barrier:
				continue
			var wb := w.get_body(int(st.wall_body))
			if wb == null:
				continue
			dd = Vector3(wb.pos.x - me.pos.x, 0, wb.pos.z - me.pos.z).length()   # heat the wall face, not the rival
			aim_body = wb.id
		if (st.cover or st.behind_barrier) and tags.has("bank_shot"):
			aim = bank_aim(w, me, foe, hi)
			if aim == Vector3.ZERO:
				continue
		var max_t := mini(Charge.max_tier(def), int(prm.get("tiers", 1)))
		var tier := 0
		if max_t > 0 and rng.randf() < 0.25 + 0.4 * float(prm.get("aggression", 0.5)):
			tier = rng.randi_range(1, max_t)
		var score := float(ROLE_WEIGHT.get(role, 0.3))
		var reasons: Array = []
		var reach_hi := hi + (1.5 if tier >= 1 else 0.0)
		if dd < lo - 0.5 or dd > reach_hi:
			continue
		if VOLUME_VERBS.has(String(def.get("verb", ""))) or LEGACY_VOLUME.has(c.id):
			# Cones, beams and bursts reach by tier: hold to the first tier that reaches (if allowed).
			var need := -1
			for t in range(0, Charge.max_tier(def) + 1):
				if reach_of(def, t) + 0.3 >= dd:
					need = t
					break
			if need < 0 or need > maxi(max_t, 0):
				continue
			tier = maxi(tier, need)
		var env := bool(prm.get("env", false))
		var cls := String(def.get("threat", {}).get("cls", ""))
		var electric := cls == "lightning" or tags.has("conducts") or tags.has("punish_wet")
		if env and electric and (st.wet or st.in_water or st.on_plate or st.puddle):
			score += 0.7
			reasons.append("conduct")
		if env and st.near_wall and (tags.has("knockback") or tags.has("knockdown") or tags.has("push") or tags.has("shove") or c.id == "air_attack"):
			score += 0.4
			reasons.append("splat")
		if st.airborne and float(def.get("startup", 0.2)) <= 0.18:
			score += 0.35
			reasons.append("juggle")
		if bool(prm.get("punish", false)) and st.charging:
			if tags.has("disrupt") or tags.has("interrupt_channel") or tags.has("vs_charge"):
				score += 0.9
				reasons.append("disrupt")
			elif float(def.get("startup", 0.2)) <= 0.14:
				score += 0.4
				reasons.append("interrupt")
		if bool(prm.get("punish", false)) and st.recovering and float(def.get("startup", 0.2)) <= 0.17:
			score += 0.3
			reasons.append("punish")
		if st.behind_barrier or st.cover:
			var answers := false
			if tags.has("blast_through_t2") and max_t >= 2:
				tier = maxi(tier, 2)
				answers = true
			if tags.has("from_above_t3") and Charge.max_tier(def) >= 3 and int(prm.get("tiers", 1)) >= 3:
				tier = 3
				answers = true
			if melts or ((tags.has("melt_wall_t3") or tags.has("softens_walls")) and st.behind_barrier):
				answers = st.behind_barrier
				if melts and _kit_has_tag(kit, "needs_lava"):
					score += 0.5    # Melt & Return: the slumped face becomes our lava
					reasons.append("melt_return")
				if tags.has("melt_wall_t3") and max_t >= 3:
					tier = 3
			if aim != Vector3.ZERO or tags.has("around_cover") or tags.has("relay"):
				answers = true
			if c.id == "magma_surge" and lava_near:
				answers = true
			if answers:
				score += 0.8
				reasons.append("barrier")
			elif cls != "" or tags.has("projectile"):
				score -= 0.6   # straight into the wall
		var hold := hold_for(def, tier)
		var cost := move_cost(def, tier, hold)
		if not can_afford(w, me, cost):
			if tier > 0:
				tier = 0
				hold = 0.0
				cost = move_cost(def, 0, 0.0)
			if not can_afford(w, me, cost):
				continue
		if me.focus - float(cost.focus) < 12.0:
			score -= 0.25   # keep a guard's worth of Focus
		if int(c.element) != me.element or int(c.sub) != me.sub_of(int(c.element)):
			score -= 0.03
		score -= 0.12 * float((prm.get("recent", {}) as Dictionary).get(c.id, 0.0))   # vary the offense
		score += rng.randf_range(0.0, 0.3)
		var gesture := 0
		var press := "attack"
		match String(c.slot):
			"thrust":
				gesture = Sim.Gesture.UP
			"ground":
				gesture = Sim.Gesture.DOWN
			"sweep":
				gesture = Sim.Gesture.SIDE
			"tech":
				press = "tech"
				if String(def.get("verb", "")) == "ranged_heat":
					hold = maxf(hold, 1.5)   # Smelter / Scorch: about a second to slump a wall face
				elif hold <= 0.0:
					hold = maxf(0.35, float(def.get("startup", 0.2)) + 0.1)
		out.append({"id": c.id, "element": int(c.element), "sub": int(c.sub), "slot": String(c.slot), "press": press,
			"gesture": gesture, "tier": tier, "hold": hold, "score": score, "reasons": reasons, "aim_body": aim_body, "aim": aim,
			"label": "%s %s T%d" % [Sim.SUB_NAMES[int(c.element)][int(c.sub)], c.id, tier]})
	out.sort_custom(func(x, y):
		if absf(float(x.score) - float(y.score)) > 1e-6:
			return float(x.score) > float(y.score)
		return String(x.id) < String(y.id))
	return out


## A follow-up for a chain (same element/sub, unused slot) or a weave (another kit sub), or {}.
static func chain_follow(w: CombatWorld, me: ActorState, foe: ActorState, kit: Dictionary, prm: Dictionary, rng: RandomNumberGenerator) -> Dictionary:
	var used: Array = me.chain.get("slots", [])
	var d := me.pos.distance_to(foe.pos)
	var weave_ok := bool(prm.get("weave", false)) and not bool(me.chain.get("weaved", false)) and me.focus > 20.0
	var best := {}
	var best_s := -INF
	for c in kit_moves(kit, Sim.ATTACK_SLOTS):
		if used.has(c.slot):
			continue
		var same := int(c.element) == me.action.element and int(c.sub) == me.action.sub
		if not same and not weave_ok:
			continue
		var def: Dictionary = Moves.DEFS[c.id]
		var ai: Dictionary = def.get("ai", {})
		var role := String(ai.get("role", ""))
		if role == "" or role == "mobility" or role == "counter":
			continue
		var rr: Array = ai.get("range", [0.0, 8.0])
		if d < float(rr[0]) - 0.5 or d > float(rr[1]):
			continue
		var cost := move_cost(def, 0, 0.0)
		if not can_afford(w, me, cost) or me.focus - float(cost.focus) < 8.0:
			continue
		var s := float(ROLE_WEIGHT.get(role, 0.3)) + rng.randf_range(0.0, 0.3) + (0.1 if same else 0.0)
		if float(def.get("startup", 0.2)) <= 0.17:
			s += 0.15
		if s > best_s:
			best_s = s
			var g := 0
			match String(c.slot):
				"thrust":
					g = Sim.Gesture.UP
				"ground":
					g = Sim.Gesture.DOWN
				"sweep":
					g = Sim.Gesture.SIDE
			best = {"id": c.id, "element": int(c.element), "sub": int(c.sub), "slot": String(c.slot), "press": "attack",
				"gesture": g, "tier": 0, "hold": 0.0, "weave": not same, "label": "chain %s" % c.id}
	return best


## Bank shot (sound): aim at the rival's mirror image across an arena boundary wall when both legs of the
## path are clear and the path fits the range. Returns a flat unit aim direction or ZERO.
static func bank_aim(w: CombatWorld, me: ActorState, foe: ActorState, max_range: float) -> Vector3:
	var hs := w.arena.half_size
	var best := Vector3.ZERO
	var best_len := INF
	var p0 := me.chest()
	var f := foe.chest()
	for k in 4:
		var img := f
		match k:
			0:
				img.x = 2.0 * hs - f.x
			1:
				img.x = -2.0 * hs - f.x
			2:
				img.z = 2.0 * hs - f.z
			3:
				img.z = -2.0 * hs - f.z
		var dir := img - p0
		var tl := 0.0
		if k < 2:
			var wx := hs if k == 0 else -hs
			if absf(dir.x) < 1e-3:
				continue
			tl = (wx - p0.x) / dir.x
		else:
			var wz := hs if k == 2 else -hs
			if absf(dir.z) < 1e-3:
				continue
			tl = (wz - p0.z) / dir.z
		if tl <= 0.0 or tl >= 1.0:
			continue
		var hit := p0 + dir * tl
		var inside := hit - Vector3(dir.x, 0, dir.z).normalized() * 0.4
		if dir.length() > max_range or not w.los(p0, inside) or not w.los(inside, f):
			continue
		if dir.length() < best_len:
			best_len = dir.length()
			best = Vector3(dir.x, 0, dir.z).normalized()
	return best


static func _kit_has_tag(kit: Dictionary, tag: String) -> bool:
	for c in kit_moves(kit, OFFENSE_SLOTS):
		if (Moves.DEFS[c.id].get("ai", {}).get("tags", []) as Array).has(tag):
			return true
	return false
