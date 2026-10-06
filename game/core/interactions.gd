class_name Interactions
extends RefCounted
## The counter rule (docs/MOVESET.md §5, §15.3; docs/COMBAT_SPEC.md "Engine" §E4).
## A threat Agent meets a counter Agent: threat power TP (rule-weighted channels K H C E P) vs
## counter power CP (barrier mass x hardness or the move's tier power, x1.5 perfect, x eff).
## ratio = CP_eff / TP picks the band (full >= 1, partial >= 0.5, fail) or a custom band; the band
## picks the outcome; the outcome handler (Outcomes) applies it through the ledgers.
##
## Rules live in one static table keyed "threat|counter" (classes, families or "*"), each key holding
## an ordered list (a rule with `tiers` only applies to those counter tiers). Lookup order:
##   t|c -> t|cfam -> tfam|c -> tfam|cfam -> *|c -> *|cfam -> t|* -> tfam|* -> default.
## Rule format (all keys optional):
##   outcome, perfect, partial, fail          outcomes per band (defaults: block / weaken / overwhelm)
##   bands: [[min_ratio, outcome], ...]       custom bands (ascending), replace outcome/partial/fail
##   eff, w: {K,H,C,E,P}                      efficacy, channel weights (C defaults to 1 only vs heat)
##   full_at 1.0, partial_at 0.5, absorb_on_fail 0.5, perfect_mult 1.5
##   when: {mass_lt, mass_max, mass_min, hostile, mats: [..], tags: [..], forms: [..]}, else: outcome
##   inert: outcome for a non-hostile threat body (inert_else when `when` fails)
##   by_form: {form_name: outcome}            form-specific outcome (checked first)
##   tiers: [..]                              counter tiers this rule applies to
##   aura: true                               acts regardless of the guard's facing (wind, auras)
##   fallback: outcome if the handler declines; to: transform target; legacy: true (core-owned, fixed)
##   plus handler parameters (chip, bal, knock, share, rate, factor, angle, side, up, verb, kind, ...).

const PLAIN_GUARD_CP := 10.0
const WIND_GUARD_CP := 12.0
const ENV_CP := 1.0e6
const HU_PER_PU := 20.0                 # 1 PU of H or C is 20 HU
const IX_EVENT_TICKS := 30              # continuous contacts emit `interaction` at most every 0.5 s per pair
## A partial (weaken / slow) is a one-time subtraction per contact (MOVESET §5.3): a body staying in a zone or
## grinding a wall is not re-weakened every resolve. A contact ends after this many ticks without a resolve.
const CONTACT_TICKS := 30
const PARTIAL_ONCE := ["weaken", "slow"]

const THREAT_CLASSES := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "metal", "molten_metal",
	"sand", "sand_cloud", "sand_surge", "glass", "water", "water_wave", "ice", "mist", "steam", "vine", "flame",
	"blue_fire", "fire_field", "gust", "tornado", "vacuum", "ember"]
const VOLUME_CLASSES := ["flame", "blue_fire", "lightning", "blast", "gust", "sound", "water", "sand", "steam", "vacuum", "frost"]
const COUNTER_CLASSES := ["guard", "guard_earth", "wall_stone", "wall_obsidian", "wall_glass", "wall_sand", "wall_mud", "wall_ice",
	"wall_vine", "plate_metal", "shield_water", "screen_steam", "fog", "aura_flame", "aura_blue", "ward_static", "guard_blast",
	"guard_wind", "wall_vortex", "bubble_null", "barrier_sound", "spikes", "rod", "anchor",
	"swallow", "quicksand", "melt_pit", "grip_stone", "grip_metal", "grip_sand", "grip_magma", "grip_water", "grip_ice",
	"grip_vapor", "grip_vine", "grip_wind", "heat_grip", "heat_ranged", "draw_heat", "freeze", "condense", "wave_water",
	"wave_sand", "wave_lava", "rime", "gust", "tornado", "vacuum_well", "suction", "flame", "blue_fire", "lightning", "blast",
	"sound", "water_jet", "spray", "sand_cloud", "frost", "pool", "puddle", "plate", "arena_wall", "ground"]

const THREAT_FAMILY := {
	"stone": "solid_light", "metal": "solid_light", "glass": "solid_light", "ice": "solid_light", "hot_rock": "solid_light",
	"stone_heavy": "solid_heavy", "boulder": "solid_heavy",
	"magma": "molten", "lava_wave": "molten", "molten_metal": "molten",
	"sand": "granular", "sand_cloud": "granular", "sand_surge": "granular",
	"water": "liquid", "water_wave": "liquid", "puddle": "liquid", "pool": "liquid",
	"mist": "vapor", "steam": "vapor",
	"vine": "plant",
	"flame": "heat", "blue_fire": "heat", "fire_field": "heat", "ember": "heat",
	"lightning": "electric",
	"gust": "pressure", "tornado": "pressure", "vacuum": "pressure", "blast": "pressure",
	"sound": "sonic", "frost": "cold",
	"wall_stone": "solid_heavy", "wall_obsidian": "solid_heavy", "wall_glass": "solid_heavy", "wall_sand": "granular",
	"wall_mud": "granular", "wall_ice": "solid_heavy", "wall_vine": "plant", "plate_metal": "solid_light",
}
const COUNTER_FAMILY := {
	"guard": "guard", "guard_earth": "guard", "shield_water": "liquid", "aura_flame": "heat", "guard_wind": "pressure",
	"wall_stone": "barrier_solid", "wall_obsidian": "barrier_solid", "wall_glass": "barrier_solid", "wall_ice": "barrier_solid",
	"plate_metal": "barrier_solid", "spikes": "barrier_solid", "rod": "barrier_solid",
	"wall_sand": "barrier_soft", "wall_mud": "barrier_soft", "wall_vine": "barrier_soft", "screen_steam": "barrier_soft",
	"fog": "barrier_soft", "sand_cloud": "barrier_soft",
	"aura_blue": "heat", "ward_static": "electric", "guard_blast": "pressure", "wall_vortex": "pressure",
	"bubble_null": "barrier_energy", "barrier_sound": "sonic", "anchor": "anchor",
	"swallow": "sink", "quicksand": "sink", "melt_pit": "sink",
	"grip_stone": "grip", "grip_metal": "grip", "grip_sand": "grip", "grip_magma": "grip", "grip_water": "grip",
	"grip_ice": "grip", "grip_vapor": "grip", "grip_vine": "grip", "grip_wind": "grip", "heat_grip": "grip", "draw_heat": "grip",
	"heat_ranged": "heat", "freeze": "cold", "rime": "cold", "frost": "cold", "condense": "liquid",
	"wave_water": "liquid", "wave_sand": "barrier_soft", "wave_lava": "heat",
	"gust": "pressure", "tornado": "pressure", "vacuum_well": "pressure", "suction": "pressure", "blast": "pressure",
	"flame": "heat", "blue_fire": "heat", "lightning": "electric", "sound": "sonic", "water_jet": "liquid", "spray": "liquid",
	"pool": "liquid", "puddle": "liquid", "plate": "barrier_solid", "arena_wall": "barrier_solid", "ground": "sink",
}
## hit_actor info.kind -> volume class (legacy sites); main channel per class.
const KIND_CLASS := {"stone": &"stone", "water": &"water", "lava": &"lava_wave", "fire": &"flame", "air": &"gust",
	"lightning": &"lightning", "blast": &"blast", "sound": &"sound", "ice": &"ice", "sand": &"sand", "steam": &"steam",
	"vacuum": &"vacuum", "frost": &"frost", "metal": &"metal", "plant": &"vine", "blue": &"blue_fire"}
const CLASS_CHANNEL := {&"flame": "H", &"blue_fire": "H", &"steam": "H", &"lightning": "E", &"frost": "C",
	&"blast": "P", &"gust": "P", &"sound": "P", &"water": "P", &"sand": "P", &"vacuum": "P", &"stone": "K", &"metal": "K",
	&"ice": "K", &"vine": "P", &"lava_wave": "H"}

const DEFAULT_RULE := {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "id": "default"}
const CLASH_RULE := {"outcome": "clash", "partial": "clash", "fail": "clash", "id": "clash_default"}
const PASS_RULE := {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "pass_default"}

static var _rules := {}           # key -> Array[Dictionary] (registration order)
static var _handlers := {}        # outcome -> Callable(w, threat, counter, res, rule, ctx) -> bool
static var _tag_threat := {}      # body tag -> threat class
static var _tag_counter := {}     # body tag -> counter class
static var _channel_hooks := {}   # body tag -> Callable(w, b, agent)
static var _ready := false
static var _n := 0                # registration counter (deterministic ids)


static func ensure() -> void:
	if _ready:
		return
	_ready = true
	CoreRules.register_all()


# ================================================================ classes

## Threat class of a body (MOVESET §15.4). Overrides: props.cls, register_tag_class.
static func classify(b: MatBody) -> StringName:
	if b.props.has("cls"):
		return StringName(b.props.cls)
	if b.tag != &"" and _tag_threat.has(b.tag):
		return _tag_threat[b.tag]
	match b.form:
		Sim.Form.PUDDLE:
			return &"puddle"
		Sim.Form.POOL:
			return &"pool"
		Sim.Form.WALL:
			return _wall_class(b)
	match b.mat:
		Sim.Mat.STONE:
			if b.form == Sim.Form.WAVE and b.liquid > 0.0:
				return &"lava_wave"
			if b.phase == Sim.Phase.MOLTEN:
				return &"magma"
			if b.is_hot():
				return &"hot_rock"
			if b.mass > 80.0:
				return &"boulder"
			if b.mass > 30.0:
				return &"stone_heavy"
			return &"stone"
		Sim.Mat.METAL:
			return &"molten_metal" if b.liquid > 0.5 else &"metal"
		Sim.Mat.SAND:
			if b.form == Sim.Form.CLOUD or b.form == Sim.Form.ZONE:
				return &"sand_cloud"
			if b.form == Sim.Form.WAVE:
				return &"sand_surge"
			return &"sand"
		Sim.Mat.GLASS:
			return &"glass"
		Sim.Mat.WATER:
			if b.phase == Sim.Phase.FROZEN:
				return &"ice"
			if b.form == Sim.Form.WAVE:
				return &"water_wave"
			if b.form == Sim.Form.CLOUD or b.form == Sim.Form.ZONE:
				return &"mist"
			return &"water"
		Sim.Mat.STEAM:
			return &"mist" if b.tag == &"mist" or b.tag == &"fog" else &"steam"
		Sim.Mat.PLANT:
			return &"vine"
		Sim.Mat.FIRE:
			match String(b.tag):
				"fire_field", "fire_line":
					return &"fire_field"
				"comet", "corona":
					return &"blue_fire"
				"ember":
					return &"ember"
			return &"blue_fire" if b.props.get("blue", false) else &"flame"
		Sim.Mat.AIR:
			match String(b.tag):
				"tornado", "twister", "eddy", "funnel", "vortex_wall":
					return &"tornado"
				"vacuum_well", "null_bubble", "mine":
					return &"vacuum"
				"tremor", "sound_barrier":
					return &"sound"
			return &"gust"
	return &"stone"


static func _wall_class(b: MatBody) -> StringName:
	match String(b.tag):
		"obsidian":
			return &"wall_obsidian"
		"glass":
			return &"wall_glass"
		"sand":
			return &"wall_sand"
		"mud":
			return &"wall_mud"
		"ice", "ridge":
			return &"wall_ice"
		"vine":
			return &"wall_vine"
		"plate":
			return &"plate_metal"
		"spikes":
			return &"spikes"
	match b.mat:
		Sim.Mat.SAND:
			return &"wall_sand"
		Sim.Mat.GLASS:
			return &"wall_glass"
		Sim.Mat.WATER:
			return &"wall_ice"
		Sim.Mat.PLANT:
			return &"wall_vine"
		Sim.Mat.METAL:
			return &"plate_metal"
	return &"wall_stone"


const ZONE_COUNTER := {"fog": &"fog", "mist": &"fog", "steam": &"screen_steam", "steam_screen": &"screen_steam",
	"sand_cloud": &"sand_cloud", "sandstorm": &"sand_cloud", "quicksand": &"quicksand", "melt_pit": &"melt_pit",
	"tornado": &"tornado", "vacuum_well": &"vacuum_well", "null_bubble": &"bubble_null", "wind_guard": &"guard_wind",
	"vortex_wall": &"wall_vortex", "sound_barrier": &"barrier_sound", "caltrops": &"spikes", "fire_field": &"flame",
	"corona": &"aura_blue", "static_field": &"ward_static", "ice_floor": &"rime", "lava_pool": &"melt_pit"}


## Counter class of a body acting as a counter (barrier, zone, environment, held shield) - or the
## body's own threat class when it is just an obstacle. Overrides: props.ccls, register_tag_class.
static func counter_class(b: MatBody, w: CombatWorld = null) -> StringName:
	if b.props.has("ccls"):
		return StringName(b.props.ccls)
	if b.tag != &"" and _tag_counter.has(b.tag):
		return _tag_counter[b.tag]
	match b.form:
		Sim.Form.WALL:
			return _wall_class(b)
		Sim.Form.PUDDLE:
			return &"puddle"
		Sim.Form.POOL:
			return &"pool"
		Sim.Form.ZONE, Sim.Form.CLOUD:
			if ZONE_COUNTER.has(String(b.tag)):
				return ZONE_COUNTER[String(b.tag)]
			if b.form == Sim.Form.ZONE and b.tag != &"" and b.mat == Sim.Mat.AIR:
				return b.tag   # a kit zone: its tag is its counter class
		Sim.Form.WAVE:
			match b.mat:
				Sim.Mat.WATER:
					return &"wave_water"
				Sim.Mat.SAND:
					return &"wave_sand"
				Sim.Mat.STONE:
					return &"wave_lava"
	if w != null and b.controller >= 0 and b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.LIQUID:
		var h := w.get_actor(b.controller)
		if h != null and h.guarding and h.action != null and h.action.data.get("shield", false):
			return &"shield_water"
	if b.mat == Sim.Mat.METAL and b.tag == &"plate":
		return &"plate_metal"
	return classify(b)


## Barrier-type bodies counter the volumes that reach them; other bodies are threats a volume
## counters (the "who meets whom" rule of every volume site).
static func is_barrier(w: CombatWorld, b: MatBody) -> bool:
	if b.form == Sim.Form.WALL or b.form == Sim.Form.ZONE or b.form == Sim.Form.PUDDLE or b.form == Sim.Form.POOL:
		return true
	return counter_class(b, w) == &"shield_water" or b.props.get("barrier", false)


static func family(cls: StringName) -> StringName:
	return StringName(THREAT_FAMILY.get(String(cls), ""))


static func counter_family(ccls: StringName) -> StringName:
	var k := String(ccls)
	if COUNTER_FAMILY.has(k):
		return StringName(COUNTER_FAMILY[k])
	return StringName(THREAT_FAMILY.get(k, ""))


## Kits: classes for a body tag (threat and/or counter; "" = unchanged).
static func register_tag_class(tag: StringName, threat_cls: StringName, counter_cls: StringName = &"") -> void:
	if threat_cls != &"":
		_tag_threat[tag] = threat_cls
	if counter_cls != &"":
		_tag_counter[tag] = counter_cls


## Kits: extra channel computation per body tag: cb(w, b, agent) edits agent.ch / power.
static func register_channels(tag: StringName, cb: Callable) -> void:
	_channel_hooks[tag] = cb


static func channel_hook(tag: StringName) -> Callable:
	return _channel_hooks.get(tag, Callable())


## Kits: a new outcome name (or a refined handler for an existing one, used before the core one).
## cb(w, threat, counter, res, rule, ctx) -> bool (false = declined: the rule's fallback runs).
static func register_outcome(nm: String, cb: Callable) -> void:
	_handlers[nm] = cb


static func handler(nm: String) -> Callable:
	return _handlers.get(nm, Callable())


# ================================================================ rules

static func _rk(t: String, c: String) -> String:
	return t + "|" + c


## Whether add_rule(t, c, rule) would be accepted (a non-legacy rule may not replace a legacy cell
## whose tiers overlap).
static func can_add(threat_cls: String, counter_cls: String, rule: Dictionary) -> bool:
	if rule.get("legacy", false):
		return true
	for r in _rules.get(_rk(threat_cls, counter_cls), []):
		if r.get("legacy", false) and _tiers_overlap(r, rule):
			return false
	return true


## Adds a rule cell. Kits add the cells of THEIR counter classes; a later non-legacy rule with the
## same key and tiers replaces the earlier one; a legacy cell can never be replaced (refused, with
## an assert in debug builds). Returns false when refused.
static func add_rule(threat_cls: String, counter_cls: String, rule: Dictionary) -> bool:
	var k := _rk(threat_cls, counter_cls)
	if not can_add(threat_cls, counter_cls, rule):
		push_error("Interactions.add_rule: %s is a legacy cell and cannot be replaced" % k)
		assert(false, "legacy interaction cell %s replaced" % k)
		return false
	var r := rule.duplicate(true)
	_n += 1
	if not r.has("id"):
		r["id"] = k
	r["key"] = k
	r["seq"] = _n
	var list: Array = _rules.get(k, [])
	var keep: Array = []
	for o in list:
		if o.get("legacy", false) or not _same_tiers(o, r):
			keep.append(o)
	keep.append(r)
	# Narrow tier rules first so they win over unrestricted ones; stable otherwise.
	keep.sort_custom(func(x, y):
		var xr: bool = x.has("tiers")
		var yr: bool = y.has("tiers")
		if xr != yr:
			return xr
		return int(x.seq) < int(y.seq))
	_rules[k] = keep
	return true


static func remove_rule(threat_cls: String, counter_cls: String, include_legacy: bool = false) -> void:
	var k := _rk(threat_cls, counter_cls)
	if not _rules.has(k):
		return
	var keep: Array = []
	for o in _rules[k]:
		if o.get("legacy", false) and not include_legacy:
			keep.append(o)
	if keep.is_empty():
		_rules.erase(k)
	else:
		_rules[k] = keep


static func _same_tiers(a: Dictionary, b: Dictionary) -> bool:
	return a.get("tiers", []) == b.get("tiers", [])


static func _tiers_overlap(a: Dictionary, b: Dictionary) -> bool:
	if not a.has("tiers") or not b.has("tiers"):
		return true
	for t in a.tiers:
		if (b.tiers as Array).has(t):
			return true
	return false


static func _tier_ok(r: Dictionary, tier: int) -> bool:
	if not r.has("tiers") or tier < 0:
		return true
	return (r.tiers as Array).has(tier)


## Rule for (threat class, counter class) at a counter tier; `fallback_rule` replaces the default.
static func rule(threat_cls: StringName, counter_cls: StringName, tier: int = -1, fallback_rule: Dictionary = DEFAULT_RULE) -> Dictionary:
	ensure()
	var t := String(threat_cls)
	var c := String(counter_cls)
	var tf := String(family(threat_cls))
	var cf := String(counter_family(counter_cls))
	for k in [_rk(t, c), _rk(t, cf), _rk(tf, c), _rk(tf, cf), _rk("*", c), _rk("*", cf), _rk(t, "*"), _rk(tf, "*")]:
		if k.begins_with("|") or k.ends_with("|"):
			continue
		var list: Array = _rules.get(k, [])
		for r in list:
			if _tier_ok(r, tier):
				return r
	return fallback_rule


## True when a rule exists for the pair (any level except the default).
static func has_rule(threat_cls: StringName, counter_cls: StringName, tier: int = -1) -> bool:
	return rule(threat_cls, counter_cls, tier, {}).size() > 0


## Every rule cell (Lab matrix viewer, docs): key -> Array of rules.
static func all_rules() -> Dictionary:
	ensure()
	return _rules


static func save_state() -> Dictionary:
	return {"rules": _rules.duplicate(true), "handlers": _handlers.duplicate(), "tag_threat": _tag_threat.duplicate(),
		"tag_counter": _tag_counter.duplicate(), "channels": _channel_hooks.duplicate(), "n": _n}


static func load_state(st: Dictionary) -> void:
	_rules = st.rules.duplicate(true)
	_handlers = st.handlers.duplicate()
	_tag_threat = st.tag_threat.duplicate()
	_tag_counter = st.tag_counter.duplicate()
	_channel_hooks = st.channels.duplicate()
	_n = int(st.n)


## Legality of a technique on a body (grip/draw/heat contexts): the cell's full outcome is not pass/fail.
static func allows(b: MatBody, counter_cls: StringName) -> bool:
	var r := rule(classify(b), counter_cls, -1, PASS_RULE)
	var o := String(r.get("outcome", "pass"))
	if r.has("bands") and not (r.bands as Array).is_empty():
		o = String((r.bands as Array)[(r.bands as Array).size() - 1][1])
	return o != "pass" and o != "fail" and o != ""


## Residual authority a released charged body keeps against grips/reclaims (MOVESET §5.1).
static func cohesion(tier: int) -> float:
	return 0.6 + 0.1 * float(tier)


## Disrupt (sound) succeeds when P >= the charge's cohesion 6 + 4·tier (MOVESET §4).
static func disrupt_threshold(tier: int) -> float:
	return 6.0 + 4.0 * float(tier)


# ================================================================ power

static func threat_power(threat: Agent, r: Dictionary, counter_fam: StringName = &"") -> float:
	var w: Dictionary = r.get("w", {})
	var wc := float(w.get("C", 1.0 if counter_fam == &"heat" else 0.0))
	return float(threat.ch.K) * float(w.get("K", 1.0)) + float(threat.ch.H) * float(w.get("H", 1.0)) \
		+ float(threat.ch.C) * wc + float(threat.ch.E) * float(w.get("E", 1.0)) + float(threat.ch.P) * float(w.get("P", 1.0))


## Counter power before perfect and efficacy: explicit power, else barrier mass x hardness (+ props.cp_bonus).
static func counter_power(counter: Agent) -> float:
	if counter.power >= 0.0:
		return counter.power
	if counter.body != null:
		return counter.body.mass * Materials.hardness(counter.body) + float(counter.body.props.get("cp_bonus", 0.0))
	return PLAIN_GUARD_CP


static func _cond(cond: Dictionary, threat: Agent) -> bool:
	var m := threat.mass
	if cond.has("mass_lt") and not (m < float(cond.mass_lt)):
		return false
	if cond.has("mass_max") and not (m <= float(cond.mass_max)):
		return false
	if cond.has("mass_min") and not (m >= float(cond.mass_min)):
		return false
	if cond.has("hostile") and bool(cond.hostile) != threat.hostile:
		return false
	if threat.body != null:
		if cond.has("mats") and not (cond.mats as Array).has(Sim.MAT_NAMES[threat.body.mat]):
			return false
		if cond.has("tags") and not (cond.tags as Array).has(String(threat.body.tag)):
			return false
		if cond.has("forms") and not (cond.forms as Array).has(Sim.FORM_NAMES[threat.body.form]):
			return false
	return true


## Pure prediction: {outcome, band, ratio, tp, cp, cp_eff, eff, perfect, rule}. No side effects.
static func predict(w: CombatWorld, threat: Agent, counter: Agent, fallback_rule: Dictionary = DEFAULT_RULE) -> Dictionary:
	var r := rule(threat.cls, counter.ccls, counter.tier, fallback_rule)
	var cfam := counter_family(counter.ccls)
	var tp := threat_power(threat, r, cfam)
	var cp := counter_power(counter)
	var eff := float(r.get("eff", 1.0))
	if r.has("eff_insulator") and counter.body != null and Materials.insulates(counter.body):
		eff *= float(r.eff_insulator)
	var mult := float(r.get("perfect_mult", 1.5)) if counter.perfect else 1.0
	var cpe := cp * mult * eff
	var ratio := cpe / tp if tp > 1e-6 else 1.0e6
	var full_at := float(r.get("full_at", 1.0))
	var partial_at := float(r.get("partial_at", 0.5))
	var band := "full" if ratio >= full_at else ("partial" if ratio >= partial_at else "fail")
	var outcome := ""
	if r.has("by_form") and threat.body != null and (r.by_form as Dictionary).has(Sim.FORM_NAMES[threat.body.form]):
		outcome = String(r.by_form[Sim.FORM_NAMES[threat.body.form]])
		band = "form"
	elif threat.kind == "body" and not threat.hostile and r.has("inert"):
		outcome = String(r.inert) if _cond(r.get("when", {}), threat) else String(r.get("inert_else", "pass"))
		band = "inert"
	elif r.has("when") and not _cond(r.when, threat):
		outcome = String(r.get("else", "pass"))
		band = "cond"
	elif r.has("bands") and not (r.bands as Array).is_empty():
		for bd in r.bands:
			if ratio >= float(bd[0]) - 1e-9:
				outcome = String(bd[1])
		if outcome == "":
			outcome = String(r.bands[0][1])
	else:
		match band:
			"full":
				outcome = String(r.get("outcome", "block"))
			"partial":
				outcome = String(r.get("partial", "weaken"))
			_:
				outcome = String(r.get("fail", "overwhelm"))
	if counter.perfect and r.has("perfect") and ratio >= full_at and band != "cond" and band != "inert" and band != "form":
		outcome = String(r.perfect)
	return {"outcome": outcome, "band": band, "ratio": ratio, "tp": tp, "cp": cp, "cp_eff": cpe, "eff": eff,
		"perfect": counter.perfect, "rule": r, "rule_id": String(r.get("id", "")), "to": String(r.get("to", ""))}


## Applies the predicted outcome (Outcomes handlers, ledgers) and emits `interaction`.
## Result adds: result (actor-guard result string), stopped, pass_scale, counter_broken, knock_scale,
## heat_used, absorbed. ctx: site data (info for hits, continuous for per-tick contacts, ...).
static func resolve(w: CombatWorld, threat: Agent, counter: Agent, ctx: Dictionary = {}, fallback_rule: Dictionary = DEFAULT_RULE) -> Dictionary:
	var res := predict(w, threat, counter, fallback_rule)
	res["result"] = ""
	res["stopped"] = false
	res["pass_scale"] = 1.0
	res["counter_broken"] = false
	res["knock_scale"] = 1.0
	res["heat_used"] = 0.0
	res["absorbed"] = 0.0
	var r: Dictionary = res.rule
	if threat.body != null and counter.body != null \
			and (ctx.get("continuous", false) or ["wall", "wave_wall"].has(String(ctx.get("site", "")))):
		var key := "%d>%d" % [threat.body.id, counter.body.id]
		if w._partial_pairs.has(key) and w.tick - int(w._partial_pairs[key]) <= CONTACT_TICKS:
			# A partial already applied during this contact: what is left passes on (it is not re-weakened, nor
			# caught once the first weaken made it small enough).
			w._partial_pairs[key] = w.tick
			res.outcome = "pass"
			res.band = "contact"
			return res
		if PARTIAL_ONCE.has(String(res.outcome)):
			w._partial_pairs[key] = w.tick
	var ok := Outcomes.apply(w, String(res.outcome), threat, counter, res, r, ctx)
	if not ok and r.has("fallback"):
		res.outcome = String(r.fallback)
		Outcomes.apply(w, String(res.outcome), threat, counter, res, r, ctx)
	_emit(w, threat, counter, res, ctx)
	return res


static func _emit(w: CombatWorld, threat: Agent, counter: Agent, res: Dictionary, ctx: Dictionary) -> void:
	if not w._record_events:
		return
	if ctx.get("continuous", false):
		var key := "%d|%d|%s" % [threat.body.id if threat.body != null else -1, counter.body.id if counter.body != null else -1, String(counter.ccls)]
		var last := int(w._ix_last.get(key, -1000000))
		if w.tick - last < IX_EVENT_TICKS:
			return
		w._ix_last[key] = w.tick
	w.emit("interaction", {"threat": String(threat.cls), "counter": String(counter.ccls), "outcome": res.outcome,
		"band": res.band, "ratio": res.ratio, "tp": res.tp, "cp": res.cp_eff, "perfect": res.perfect,
		"pos": threat.body.pos if threat.body != null else (counter.pos if threat.pos == Vector3.ZERO else threat.pos),
		"dir": threat.dir, "threat_actor": threat.actor.id if threat.actor != null else -1,
		"counter_actor": counter.actor.id if counter.actor != null else -1,
		"threat_body": threat.body.id if threat.body != null else -1,
		"counter_body": counter.body.id if counter.body != null else -1, "to": res.to, "rule": res.rule_id,
		"tier": maxi(threat.tier, counter.tier)})
