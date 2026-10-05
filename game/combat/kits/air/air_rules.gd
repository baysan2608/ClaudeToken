class_name AirRules
extends RefCounted
## The Air column of the counter matrix (docs/MOVESET.md §8.4): the rule cells of the Air counter classes, the
## custom outcomes they need (prefix "air_"), statuses, tag classes and the zone / body hooks that belong to the
## rules. The core owns the legacy cells (palm gust / cyclone T0-T1, the Wind Guard against today's threats, the
## plain guard, the arena) which are never touched here; every cell below is non-legacy.
##
## Counter classes encoded here: gust (tiers 2-3 and every tier for classes the legacy cells do not know),
## crescent, crosswind, guard_wind (non-legacy threats), grip_wind, wall_vortex, tornado, eddy, bubble_null,
## vacuum_well, suction, vacuum, barrier_sound, sound, ground_ping.
## Every heat / mass change goes through the CombatWorld ledger helpers (heat_body, decay_body, split_body,
## merge_bodies, convert_mat) or AirUtil's booked helpers; convective cooling by wind books `ledger.ambient`.

## Every cell registered by the kit: "threat|counter|tiers" -> {id, threat, counter, tiers, ref} (docs, Lab matrix
## viewer, tests). ref: {move, tier, perfect, expect, tp, mass} - the test checks that the move's counter at that tier
## meeting the reference threat predicts `expect`.
static var CELLS := {}

## Reference threats of MOVESET §8.4: threat power (PU) and mass (kg).
const REF := {
	"stone": {"tp": 17.0, "mass": 20.0}, "stone_heavy": {"tp": 31.5, "mass": 45.0}, "boulder": {"tp": 110.0, "mass": 200.0},
	"hot_rock": {"tp": 26.8, "mass": 20.0}, "magma": {"tp": 35.0, "mass": 20.0}, "lava_wave": {"tp": 27.3, "mass": 20.0},
	"metal": {"tp": 12.0, "mass": 6.0}, "sand": {"tp": 10.0, "mass": 8.0}, "sand_cloud": {"tp": 8.0, "mass": 6.0},
	"sand_surge": {"tp": 20.0, "mass": 14.0}, "water": {"tp": 9.6, "mass": 12.0}, "water_wave": {"tp": 24.0, "mass": 14.0},
	"ice": {"tp": 9.0, "mass": 6.0}, "mist": {"tp": 3.0, "mass": 2.0}, "steam": {"tp": 6.0, "mass": 1.0},
	"vine": {"tp": 14.0, "mass": 10.0}, "flame": {"tp": 8.0, "mass": 0.0}, "blue_fire": {"tp": 16.0, "mass": 0.0},
	"lightning": {"tp": 24.0, "mass": 0.0}, "blast": {"tp": 16.0, "mass": 0.0}, "gust": {"tp": 11.0, "mass": 0.0},
	"tornado": {"tp": 30.0, "mass": 0.0}, "vacuum": {"tp": 18.0, "mass": 0.0}, "sound": {"tp": 16.0, "mass": 0.0},
	"glass": {"tp": 9.0, "mass": 6.0}, "ember": {"tp": 4.0, "mass": 0.0}, "fire_field": {"tp": 8.0, "mass": 0.0},
	"puddle": {"tp": 6.0, "mass": 3.0},
}

const LIGHT_SOLIDS := ["stone", "hot_rock", "ice", "metal", "glass", "sand"]
## Counter-class families of the Air column, for the coverage test (every row has an answer in every column).
const COLUMNS := {
	"Gust": ["gust", "guard_wind", "crescent", "grip_wind"],
	"Vortex": ["wall_vortex", "tornado", "eddy"],
	"Vacuum": ["vacuum_well", "bubble_null", "suction", "vacuum"],
	"Sound": ["barrier_sound", "sound", "ground_ping"],
}


static func _s(frames: float) -> float:
	return frames / 60.0


## One rule cell. A rule without `tiers` applies to every counter tier.
static func _cell(t: String, c: String, rule: Dictionary, ref: Dictionary = {}) -> void:
	var r := rule.duplicate(true)
	r["owner"] = "air"
	if not r.has("id"):
		var suffix := ""
		for tr in r.get("tiers", []):
			suffix += str(tr)
		r["id"] = "air_%s_%s%s" % [c, t, "_t" + suffix if suffix != "" else ""]
	if Interactions.add_rule(t, c, r):
		var tiers: Array = r.get("tiers", [])
		CELLS["%s|%s|%s" % [t, c, str(tiers)]] = {"id": r.id, "threat": t, "counter": c, "ref": ref, "tiers": tiers}
	else:
		push_warning("AirRules: cell %s|%s refused (legacy)" % [t, c])


## Same rule for several threat classes.
static func _cells(ts: Array, c: String, rule: Dictionary) -> void:
	for t in ts:
		_cell(String(t), c, rule)


static func register() -> void:
	CELLS.clear()
	_statuses()
	_tags()
	AirOutcomes.register()
	_gust_column()
	_guard_wind_column()
	_grip_wind_column()
	AirVortex.register_rules()
	AirVacuum.register_rules()
	AirSound.register_rules()


# ================================================================ statuses, tags

static func _statuses() -> void:
	CombatWorld.register_status("dazed", {"speed": 0.35, "no_lock": false})
	CombatWorld.register_status("revealed", {})
	CombatWorld.register_status("flight", {"speed": 1.1, "immune": ["ground"]})
	CombatWorld.register_status("windborne", {"speed": 0.7, "immune": ["ground"]})
	CombatWorld.register_status("slipstream", {"speed": 1.3})
	CombatWorld.register_status("sandblasted", {"no_lock": true, "dps": 2.0})
	CombatWorld.register_status("storm_tossed", {"speed": 0.8})


static func _tags() -> void:
	Interactions.register_tag_class(&"crescent", &"gust", &"crescent")
	Interactions.register_tag_class(&"spiral", &"spiral", &"gust")
	Interactions.register_tag_class(&"eddy", &"tornado", &"eddy")
	Interactions.register_tag_class(&"inrush", &"inrush", &"inrush")
	Interactions.register_tag_class(&"flight_field", &"flight", &"flight")
	Interactions.register_tag_class(&"tremor", &"sound", &"tremor")


# ================================================================ Gust column: gust (tiers 2-3), crescent

## The strike: cells for tiers 2-3 (the legacy cells keep T0 / T1). Reference: air_attack (Palm Gust) T2 Gale 18, T3 Hurricane 28.
static func _gust_column() -> void:
	var t23 := [2, 3]
	# Default: the strong wind leaves walls, pools, puddles and anything without a cell alone.
	_cell("*", "gust", {"outcome": "pass", "partial": "pass", "fail": "pass", "tiers": t23, "id": "air_gust_default"})
	# Light solids (x1.5; metal x2): deflected when CP_eff >= TP, bent below.
	for t in ["stone", "hot_rock", "ice", "metal", "glass"]:
		var eff := 2.0 if t == "metal" else (2.0 if t == "ice" else 1.5)
		var rule := {"outcome": "deflect", "partial": "bend", "fail": "bend", "eff": eff, "side": 0.45, "up": 2.0, "verb": "gust", "tiers": t23}
		if t == "hot_rock":
			# Convective cooling: the wind crusts the rock (booked ambient), then turns it.
			rule = {"outcome": "air_cool", "partial": "air_cool", "fail": "air_cool", "eff": 1.5, "cool": 0.5, "then_full": "deflect",
				"then_partial": "bend", "then_fail": "bend", "side": 0.45, "up": 2.0, "verb": "gust", "tiers": t23}
		_cell(t, "gust", rule, {"move": "air_attack", "tier": 2, "expect": "air_cool" if t == "hot_rock" else "deflect", "tp": float(REF[t].tp)})
	_cell("sand", "gust", {"outcome": "deflect", "partial": "bend", "fail": "bend", "eff": 2.0, "side": 0.6, "up": 2.0, "tiers": t23},
		{"move": "air_attack", "tier": 2, "expect": "deflect"})
	# Heavy stones only bend (x0.6, the legacy bend impulse); boulders do not care.
	_cell("stone_heavy", "gust", {"bands": [[0.0, "bend"], [1.0, "deflect"]], "eff": 0.6, "bend_impulse": 60.0, "tiers": t23},
		{"move": "air_attack", "tier": 2, "expect": "bend"})
	_cell("boulder", "gust", {"bands": [[0.0, "pass"], [1.0, "deflect"]], "eff": 0.6, "tiers": t23},
		{"move": "air_attack", "tier": 3, "expect": "pass"})
	# THE LAVA RULE (MOVESET §5.4): a Gale crusts and slows a 20 kg wave, a Hurricane Palm stalls it and sets it
	# into rock; a 45 kg wave (61.5) survives. Magma blobs (35) are only weakened at T3.
	# Convective cooling is slow: a Gale (ratio 0.66) takes ~45 % of the latent heat (the front crusts, the flow slows),
	# a Hurricane Palm (ratio 1.03) takes it all (15 HU per PU >= the 396 HU of a 20 kg wave): the front sets into rock.
	_cell("molten", "gust", {"outcome": "transform", "to": "rock", "partial": "weaken", "fail": "pass", "tiers": t23, "id": "air_gust_molten",
		"hu_per_pu": 15.0, "heat_mult": 0.35}, {"move": "air_attack", "tier": 3, "expect": "transform", "threat": "lava_wave"})
	_cell("magma", "gust", {"outcome": "transform", "to": "rock", "partial": "weaken", "fail": "pass", "tiers": t23, "id": "air_gust_magma",
		"hu_per_pu": 15.0, "heat_mult": 0.35}, {"move": "air_attack", "tier": 3, "expect": "weaken"})
	# Granular: a cloud is blown away, a slug deflected (x2 above), a surge only weakened.
	_cell("sand_cloud", "gust", {"outcome": "disperse", "partial": "disperse", "fail": "disperse", "tiers": t23},
		{"move": "air_attack", "tier": 2, "expect": "disperse"})
	_cell("sand_surge", "gust", {"outcome": "weaken", "partial": "weaken", "fail": "pass", "tiers": t23},
		{"move": "air_attack", "tier": 2, "expect": "weaken"})
	# Water: a stream scatters (x1), a wave is weakened and, at T3, split.
	_cell("water", "gust", {"outcome": "deflect", "partial": "bend", "fail": "bend", "eff": 1.0, "side": 0.5, "up": 2.0, "tiers": t23},
		{"move": "air_attack", "tier": 2, "expect": "deflect"})
	_cell("water_wave", "gust", {"outcome": "weaken", "partial": "weaken", "fail": "pass", "tiers": [2]},
		{"move": "air_attack", "tier": 2, "expect": "weaken"})
	_cell("water_wave", "gust", {"bands": [[0.0, "pass"], [0.5, "weaken"], [1.0, "air_split"]], "tiers": [3], "angle": 28.0},
		{"move": "air_attack", "tier": 3, "expect": "air_split"})
	# Vapour is blown away; vines and the pool do not mind; bolts pass; blasts are weakened (x0.8).
	_cells(["mist", "steam"], "gust", {"outcome": "disperse", "partial": "disperse", "fail": "disperse", "tiers": t23})
	_cell("vine", "gust", {"outcome": "pass", "partial": "pass", "fail": "pass", "tiers": t23}, {"move": "air_attack", "tier": 2, "expect": "pass"})
	_cell("lightning", "gust", {"outcome": "pass", "partial": "pass", "fail": "pass", "tiers": t23}, {"move": "air_attack", "tier": 3, "expect": "pass"})
	_cell("blast", "gust", {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.8}, {"move": "air_attack", "tier": 2, "expect": "weaken"})
	# Fire bands (MOVESET §8.4): below 1 the wind fans it (+30 % heat), 1-2 blows it aside, >= 2 puts it out.
	# Blue fire needs more wind (x0.7). The legacy air guard still blocks the flare cleanly (legacy cell).
	var fire := {"bands": [[0.0, "amplify"], [1.0, "deflect"], [2.0, "extinguish"]], "amp": 1.3, "side": 0.5, "up": 1.5, "tiers": t23}
	_cell("flame", "gust", fire, {"move": "air_attack", "tier": 2, "expect": "extinguish", "tp": 8.0})
	var blue := fire.duplicate(true)
	blue["eff"] = 0.7
	_cell("blue_fire", "gust", blue, {"move": "air_attack", "tier": 2, "expect": "amplify"})
	_cell("ember", "gust", fire, {"move": "air_attack", "tier": 2, "expect": "extinguish"})
	# Wind on wind: a stronger counter-wind turns it; sound passes through at half strength.
	_cell("gust", "gust", {"bands": [[0.0, "pass"], [0.5, "weaken"], [1.0, "deflect"]], "tiers": t23, "side": 0.5, "up": 0.5},
		{"move": "air_attack", "tier": 3, "expect": "deflect"})
	_cell("sound", "gust", {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.5}, {"move": "air_attack", "tier": 2, "expect": "weaken"})
	# Vortex and vacuum: a gale that out-pushes a tornado dissipates it; the wind fills a vacuum (x1.2).
	_cell("tornado", "gust", {"bands": [[0.0, "pass"], [0.5, "air_shrink"], [1.0, "neutralize"]]},
		{"move": "air_attack", "tier": 3, "expect": "neutralize", "tp": 25.0})
	_cell("vacuum", "gust", {"bands": [[0.0, "pass"], [0.5, "air_shrink"], [1.0, "neutralize"]], "eff": 1.2},
		{"move": "air_attack", "tier": 3, "expect": "neutralize", "tp": 18.0})
	# Wind Crescent (x1.5 on light solids, x2 on vines): turns the light shots it meets, cuts vines loose or woven.
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water"]:
		_cell(t, "crescent", {"outcome": "deflect", "partial": "bend", "fail": "bend", "eff": 1.5, "side": 0.45, "up": 2.0, "verb": "crescent"},
			{"move": "gust_crescent", "tier": 2, "expect": "deflect"} if t == "stone" else {})
	_cell("stone_heavy", "crescent", {"bands": [[0.0, "bend"], [1.0, "deflect"]], "eff": 0.6, "bend_impulse": 60.0})
	_cell("boulder", "crescent", {"bands": [[0.0, "pass"], [1.0, "deflect"]], "eff": 0.6}, {"move": "gust_crescent", "tier": 3, "expect": "pass"})
	_cell("vine", "crescent", {"outcome": "air_cut", "partial": "weaken", "fail": "pass", "eff": 2.0},
		{"move": "gust_crescent", "tier": 0, "expect": "air_cut"})
	_cell("wall_vine", "crescent", {"outcome": "air_cut", "partial": "air_cut", "fail": "air_cut", "eff": 2.0, "wall_k": 0.6},
		{"move": "gust_crescent", "tier": 1, "expect": "air_cut", "tp": 1.0})
	_cell("*", "crescent", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_crescent_default"})
	# Crosswind (lateral): curves projectiles in flight by up to 40 deg, scaled by how well the wind out-powers them.
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water", "stone_heavy", "flame", "blue_fire", "ember"]:
		var eff2 := 0.6 if t == "stone_heavy" else 1.5
		_cell(t, "crosswind", {"bands": [[0.0, "pass"], [0.5, "bend"]], "eff": eff2, "angle": 40.0, "id": "air_crosswind_" + t},
			{"move": "gust_crosswind", "tier": 1, "expect": "bend"} if t == "stone" else {})
	_cell("*", "crosswind", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "air_crosswind_default"})


# ================================================================ Gust column: Wind Guard (guard_wind, non-legacy threats)

## The legacy Wind Guard cells (CP 12: light solids x1.5, metal x2, heavy stones x0.6, flame = clean block, plain guard
## for the rest) are the core's. These are the threats the legacy cells do not know. Perfect = Return Wind.
static func _guard_wind_column() -> void:
	# Fire bands for the other heat classes (the legacy flame cell is a clean block): feeds below 1, deflects at 1-2,
	# extinguishes at >= 2; a perfect guard sends the flame back.
	var fire := {"bands": [[0.0, "amplify"], [1.0, "deflect"], [2.0, "extinguish"]], "amp": 1.3, "perfect": "reflect", "side": 0.5, "up": 1.5}
	_cell("ember", "guard_wind", fire, {"move": "wind_guard", "tier": 0, "expect": "extinguish", "tp": 4.0})
	var blue := fire.duplicate(true)
	blue["eff"] = 0.75
	_cell("blue_fire", "guard_wind", blue, {"move": "wind_guard", "tier": 0, "expect": "amplify", "tp": 16.0})
	_cell("fire_field", "guard_wind", fire, {"move": "wind_guard", "tier": 0, "expect": "deflect", "tp": 8.0})
	# Sand clouds and vapour are blown aside (x2); a perfect guard returns them to the sender.
	_cell("sand_cloud", "guard_wind", {"outcome": "deflect", "partial": "block", "fail": "block", "perfect": "reflect", "eff": 2.0, "side": 0.6, "up": 1.5,
		"chip": 0.0, "bal": 0.0, "knock": 0.0}, {"move": "wind_guard", "tier": 0, "expect": "deflect"})
	_cell("sand_surge", "guard_wind", {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 1.0, "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "wind_guard", "tier": 0, "expect": "weaken"})
	_cell("water_wave", "guard_wind", {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 0.8, "chip": 0.12, "bal": 0.55, "knock": 0.35},
		{"move": "wind_guard", "tier": 0, "expect": "overwhelm"})
	_cells(["mist", "steam"], "guard_wind", {"outcome": "disperse", "partial": "disperse", "fail": "disperse", "perfect": "reflect", "eff": 2.0})
	_cell("vine", "guard_wind", {"outcome": "block", "partial": "block", "fail": "block", "eff": 1.0, "chip": 0.12, "bal": 0.55, "knock": 0.35})
	# Counter-wind turns a gust; strong sound is only weakened (x0.5); the wind is sucked into a vacuum (plain guard).
	_cell("sound", "guard_wind", {"outcome": "weaken", "partial": "weaken", "fail": "weaken", "eff": 0.5}, {"move": "wind_guard", "tier": 0, "expect": "weaken"})
	_cell("tornado", "guard_wind", {"bands": [[0.0, "overwhelm"], [1.0, "block"]], "chip": 0.12, "bal": 0.55, "knock": 0.35})


# ================================================================ Gust column: Wind Grip (grip_wind)

## Technique legality: the grip may take light bodies only; heavy stones, boulders, lava and zones stay out of reach.
static func _grip_wind_column() -> void:
	var yes := {"outcome": "reclaim", "bands": [[0.0, "reclaim"]], "full_at": 0.0}
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water", "sand_cloud", "mist", "steam", "vine", "flame", "blue_fire", "ember"]:
		_cell(t, "grip_wind", yes, {"move": "gust_grip", "tier": 0, "expect": "reclaim"} if t in ["stone", "flame", "water", "steam"] else {})
	_cell("*", "grip_wind", {"outcome": "pass", "bands": [[0.0, "pass"]], "id": "air_grip_other"})
