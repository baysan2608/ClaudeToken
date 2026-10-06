class_name EarthRules
extends RefCounted
## The Earth column of the counter matrix (docs/MOVESET.md §8.1): the rule cells of the Earth counter
## classes, plus the custom outcomes they need (prefix "earth_"). Owned by the Earth kit; the core owns
## the legacy cells (wall_stone x legacy threats, grip_stone, ...) and the environment.
##
## Counter classes encoded here: wall_stone (non-legacy rows only), wall_obsidian, wall_glass, wall_sand,
## wall_mud, plate_metal, spikes, rod, caltrops, swallow, quicksand, melt_pit, grip_stone (non-legacy rows),
## grip_metal, grip_sand, grip_magma, wave_sand, wave_lava, sand_cloud, plus the kit's own active classes
## ram (Ram Wall: K = wall mass x 9 / 20) and chain (Chain Arc), and the spike line as a threat (spikes).
## Every heat / mass change goes through the CombatWorld ledger helpers (heat_body, split_body,
## merge_bodies, decay_body, convert_mat) or books its own exact transfer.

## Every cell registered by the kit: "threat|counter" -> rule id (docs, Lab matrix, tests).
static var CELLS := {}

const SOLIDS := ["stone", "hot_rock", "metal", "ice", "glass", "sand"]
const VOLUMES := ["flame", "blue_fire", "lightning", "blast", "gust", "sound", "water", "sand", "steam", "vacuum", "frost"]


static func _add(t: String, c: String, r: Dictionary) -> void:
	var rr := r.duplicate(true)
	rr["owner"] = "earth"
	if not rr.has("id"):
		rr["id"] = "earth_%s_%s" % [c, t]
	if Interactions.add_rule(t, c, rr):
		CELLS["%s|%s" % [t, c]] = rr.id


static func register() -> void:
	CELLS.clear()
	_outcomes()
	_wall_stone()
	_wall_obsidian()
	_wall_sand()
	_wall_mud()
	_wall_glass()
	_plate()
	_spikes_and_rods()
	_sinks()
	_grips()
	_waves()
	_sand_cloud()
	_ram()
	_chain()


# ================================================================ cells

## Bulwark (legacy wall) refinements for threats today's game never threw at it. The legacy cells
## (stones, magma, water, ice, lava waves, lightning, "*") stay the core's.
static func _wall_stone() -> void:
	# Iron lances embed and stay as a conductive rod; discs and other metal bounce off.
	_add("metal", "wall_stone", {"outcome": "earth_embed", "when": {"tags": ["lance"]}, "else": "block", "fallback": "block",
		"id": "bulwark_metal"})
	_add("sand", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_sand"})
	_add("glass", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_glass"})
	_add("vine", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_vine"})
	_add("steam", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_steam"})
	# Flames splash on the wall and heat it; blue fire heats it harder (Smelter / White Core slump it).
	_add("flame", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "heat_share": 0.5, "id": "bulwark_flame"})
	_add("blue_fire", "wall_stone", {"outcome": "block", "partial": "block", "fail": "block", "heat_share": 0.9, "id": "bulwark_blue"})
	_add("blast", "wall_stone", {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "id": "bulwark_blast"})
	_add("gust", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_gust"})
	_add("tornado", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_tornado"})
	_add("vacuum", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "bulwark_vacuum"})
	# Sound echoes back at its sender.
	_add("sound", "wall_stone", {"outcome": "reflect", "partial": "reflect", "fail": "overwhelm", "partial_at": 0.4, "factor": 0.6,
		"id": "bulwark_echo"})


## Magma Curtain (WALL tag obsidian, CP 28; set obsidian 38).
static func _wall_obsidian() -> void:
	var c := "wall_obsidian"
	# Small solids stick in the molten face and heat; a perfect guard fuses them into the wall.
	for t in ["stone", "hot_rock", "metal", "ice", "glass"]:
		var r := {"outcome": "earth_stick", "perfect": "earth_stick", "partial": "weaken", "fail": "overwhelm", "hu": 60.0,
			"when": {"mass_max": 20.0}, "else": "block", "fallback": "block"}
		if t == "ice":
			r = {"outcome": "earth_face_heat", "partial": "earth_face_heat", "fail": "earth_face_heat", "eff": 3.0, "to": "steam",
				"hu": 120.0, "fallback": "block"}
		_add(t, c, r)
	_add("stone_heavy", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "dmg_div": 600.0})
	_add("boulder", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	# Lava is absorbed into the face (the wall keeps it molten).
	_add("magma", c, {"bands": [[0.0, "earth_absorb_face"]], "full_at": 0.0, "fallback": "block"})
	_add("lava_wave", c, {"bands": [[0.0, "earth_absorb_face"]], "full_at": 0.0, "fallback": "block"})
	_add("sand", c, {"bands": [[0.0, "earth_glass_beads"]], "full_at": 0.0, "fallback": "block"})
	_add("sand_surge", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	# Water: steam burst and the face sets to obsidian (harder).
	for t in ["water", "water_wave", "puddle"]:
		_add(t, c, {"bands": [[0.0, "earth_set"]], "full_at": 0.0, "fallback": "block"})
	_add("steam", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("mist", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_add("vine", c, {"outcome": "transform", "to": "ash", "partial": "transform", "fail": "block", "eff": 3.0, "fallback": "block"})
	# Flames feed the face (heat into the wall); blue fire too.
	_add("flame", c, {"bands": [[0.0, "earth_feed_face"]], "full_at": 0.0, "share": 1.0, "fallback": "block"})
	_add("blue_fire", c, {"bands": [[0.0, "earth_feed_face"]], "full_at": 0.0, "share": 1.0, "fallback": "block"})
	_add("blast", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_add("gust", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("tornado", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	# Obsidian is brittle: strong sound shatters it (eff 0.6).
	_add("sound", c, {"outcome": "block", "partial": "block", "fail": "overwhelm", "eff": 0.6})


## Dune Wall (WALL tag sand, CP 25/32/40): captures solids (x1.2), Engulf (perfect) spits a <= 30 kg
## projectile back, smothers fire (x2), absorbs blasts and sound (x1.5), grounds bolts up to 60 and
## turns to glass, water makes it mud.
static func _wall_sand() -> void:
	var c := "wall_sand"
	_sand_family(c, 0.0)


static func _wall_mud() -> void:
	_sand_family("wall_mud", 5.0)


static func _sand_family(c: String, _mud: float) -> void:
	for t in ["stone", "hot_rock", "metal", "ice", "glass"]:
		_add(t, c, {"outcome": "capture", "perfect": "reflect", "partial": "weaken", "fail": "overwhelm", "eff": 1.2,
			"max_captured": 6, "fallback": "block"})
	# Railspike (tier 3 lance, K 25) partly pierces a plain dune: metal is captured at eff 1.0 only.
	_add("metal", c, {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "eff": 1.0, "max_captured": 6, "fallback": "block"})
	_add("stone_heavy", c, {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "eff": 1.0, "fallback": "block"})
	_add("boulder", c, {"outcome": "capture", "partial": "weaken", "fail": "overwhelm", "eff": 1.0, "fallback": "block"})
	_add("magma", c, {"outcome": "earth_crust", "partial": "earth_crust", "fail": "overwhelm", "eff": 1.5, "crust": 1.0, "stops": true,
		"fallback": "block"})
	_add("lava_wave", c, {"outcome": "earth_crust", "partial": "earth_crust", "fail": "overwhelm", "eff": 1.5, "crust": 1.0,
		"stops": true, "fallback": "block"})
	_add("sand", c, {"bands": [[0.0, "absorb"]], "full_at": 0.0, "fallback": "block"})
	_add("sand_surge", c, {"outcome": "absorb", "partial": "absorb", "fail": "overwhelm", "fallback": "block"})
	for t in ["water", "puddle"]:
		_add(t, c, {"bands": [[0.0, "earth_mud"]], "full_at": 0.0, "fallback": "block"})
	_add("water_wave", c, {"outcome": "earth_mud", "partial": "earth_mud", "fail": "overwhelm", "eff": 1.5, "fallback": "block"})
	_add("mist", c, {"outcome": "absorb", "partial": "weaken", "fail": "pass"})
	_add("steam", c, {"outcome": "block", "partial": "weaken", "fail": "pass"})
	_add("vine", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	# Smother: a sand barrier stops flame volumes (their heat is booked as spent by the volume).
	_add("flame", c, {"outcome": "block", "partial": "block", "fail": "weaken", "eff": 2.0, "heat_share": 0.3})
	_add("blue_fire", c, {"bands": [[0.0, "earth_glassify"]], "full_at": 0.0, "heat_share": 0.5, "fallback": "block"})
	_add("lightning", c, {"bands": [[0.0, "shatter"], [1.0, "earth_glass_ground"]], "eff": 2.4, "absorb_on_fail": 0.5,
		"fallback": "ground"})
	_add("blast", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 1.5})
	_add("sound", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 1.5})
	_add("gust", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("tornado", c, {"outcome": "block", "partial": "amplify", "fail": "amplify"})


## Fused sand (WALL tag glass, CP 30): solids and bolts (insulator); brittle to sound (x2.5) and blasts (x2).
static func _wall_glass() -> void:
	var c := "wall_glass"
	for t in ["stone", "hot_rock", "ice", "glass"]:
		_add(t, c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	# Sand (slug or Sandblast volume) abrades glass x2.
	_add("sand", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 0.5})
	_add("metal", c, {"outcome": "earth_embed", "when": {"tags": ["lance"]}, "else": "block", "partial": "weaken", "fail": "overwhelm",
		"fallback": "block"})
	_add("stone_heavy", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_add("boulder", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm"})
	_add("lava_wave", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("magma", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("flame", c, {"bands": [[0.0, "block"]], "full_at": 0.0, "heat_share": 0.3})
	_add("blue_fire", c, {"outcome": "block", "partial": "block", "fail": "block", "heat_share": 0.8})
	_add("sound", c, {"outcome": "block", "partial": "block", "fail": "overwhelm", "eff": 0.4})
	_add("blast", c, {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "eff": 0.5})
	_add("water_wave", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("sand_surge", c, {"bands": [[0.0, "block"]], "full_at": 0.0})
	_add("gust", c, {"bands": [[0.0, "block"]], "full_at": 0.0})


## Aegis Plate (held 6 kg metal, CP 20) as the fighter's guard (and as a loose plate wall).
static func _plate() -> void:
	var c := "plate_metal"
	var chip := {"chip": 0.06, "bal": 0.35, "knock": 0.25}
	var r := {"outcome": "block", "perfect": "deflect", "partial": "weaken", "fail": "overwhelm"}
	r.merge(chip)
	for t in ["stone", "ice", "glass", "sand", "water"]:
		_add(t, c, r)
	var rh := r.duplicate()
	rh["perfect"] = "block"
	_add("stone_heavy", c, rh)
	_add("boulder", c, rh)
	# Magnet Catch: a perfect plate pulls a metal shot into the satchel.
	var rm := r.duplicate()
	rm["perfect"] = "earth_magnet_catch"
	_add("metal", c, rm)
	# Heat: hot rock and flames heat the plate (>= 300 °C it is dropped, red-hot); magma and blue fire fail.
	var hot := {"bands": [[0.0, "earth_plate_heat"]], "full_at": 0.0, "share": 0.5, "fallback": "block"}
	hot.merge(chip)
	for t in ["hot_rock", "flame", "steam"]:
		_add(t, c, hot)
	var melt := {"bands": [[0.0, "earth_plate_heat"]], "full_at": 0.0, "share": 1.0, "melts": true, "fallback": "weaken"}
	melt.merge(chip)
	_add("magma", c, melt)
	_add("blue_fire", c, melt)
	_add("lava_wave", c, {"outcome": "overwhelm", "partial": "overwhelm", "fail": "overwhelm"})
	_add("sand_surge", c, {"outcome": "overwhelm", "partial": "overwhelm", "fail": "overwhelm"})
	_add("water_wave", c, {"outcome": "overwhelm", "partial": "overwhelm", "fail": "overwhelm"})
	# Lightning: grounded through the fighter on stone / sand; conducted into them in water / on the plate.
	_add("lightning", c, {"bands": [[0.0, "earth_plate_bolt"]], "full_at": 0.0, "aura": true, "fallback": "pass"})
	var rb := {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "knock": 0.9, "chip": 0.08, "bal": 0.5}
	_add("blast", c, rb)
	_add("gust", c, {"bands": [[0.0, "block"]], "full_at": 0.0, "chip": 0.0, "bal": 0.2, "knock": 0.3})
	_add("vine", c, r)
	_add("sound", c, {"outcome": "reflect", "perfect": "reflect", "partial": "block", "fail": "overwhelm", "eff": 1.2, "factor": 0.8,
		"chip": 0.06, "bal": 0.35, "knock": 0.25})
	_add("mist", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_add("tornado", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})
	_add("vacuum", c, {"bands": [[0.0, "pass"]], "full_at": 0.0})


## Rising Fangs spikes (low WALL tag spikes, CP 20): stop ground lines (every wave form) and low shots;
## bolts use the core barrier rule (ground E <= 20, else blasted). Rod Plant (zone props.ccls rod):
## bolts within its field are drawn to it and grounded up to its capacity (60). Caltrops (zone): a
## conductor node that lets everything through (statuses come from the zone effect).
static func _spikes_and_rods() -> void:
	for t in ["lava_wave", "water_wave", "sand_surge", "fire_field", "puddle"]:
		_add(t, "spikes", {"bands": [[0.0, "earth_spike_stop"]], "full_at": 0.0, "fallback": "block"})
	# The spike line itself meeting an enemy ground line (clash site, either order).
	for c2 in ["wave_lava", "wave_sand"]:
		_add("spikes", c2, {"bands": [[0.0, "earth_spike_stop"]], "full_at": 0.0, "fallback": "clash", "id": "spike_line_stop"})
	_add("*", "spikes", {"outcome": "block", "partial": "weaken", "fail": "overwhelm", "by_form": {"wave": "block"}, "id": "spikes_any"})
	_add("lightning", "rod", {"bands": [[0.0, "earth_rod_melt"], [1.0, "earth_rod_ground"]], "absorb_on_fail": 0.5, "fallback": "pass"})
	# Fields that only answer bolts: everything else passes (explicit cells, so no family rule shadows them).
	for t in Interactions.THREAT_CLASSES + Interactions.VOLUME_CLASSES + ["puddle", "*"]:
		if t != "lightning":
			_add(t, "rod", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "rod_pass"})
		_add(t, "caltrops", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "caltrops_pass"})


## Swallow (move counter, CP 22/30/40/55), Quicksand and Melt Pit (zones).
static func _sinks() -> void:
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "metal", "ice", "glass", "sand", "sand_surge"]:
		_add(t, "swallow", {"outcome": "sink", "partial": "weaken", "fail": "pass"})
	_add("magma", "swallow", {"outcome": "sink", "partial": "weaken", "fail": "pass"})
	_add("lava_wave", "swallow", {"outcome": "sink", "partial": "weaken", "fail": "pass"})
	_add("water_wave", "swallow", {"outcome": "weaken", "partial": "weaken", "fail": "pass"})
	_add("*", "swallow", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "swallow_any"})
	# Quicksand: landing solids sink (heavy ones bog), lava crusts to glass and stalls, water makes a bog.
	for t in ["stone", "hot_rock", "metal", "ice", "glass", "stone_heavy"]:
		_add(t, "quicksand", {"outcome": "sink", "partial": "slow", "fail": "slow", "factor": 0.4})
	_add("boulder", "quicksand", {"bands": [[0.0, "slow"], [1.0, "sink"]], "factor": 0.3})
	_add("sand", "quicksand", {"bands": [[0.0, "absorb"]], "full_at": 0.0})
	_add("magma", "quicksand", {"outcome": "earth_crust", "partial": "earth_crust", "fail": "pass", "eff": 1.5, "stops": true, "crust": 1.0})
	_add("lava_wave", "quicksand", {"outcome": "earth_crust", "partial": "earth_crust", "fail": "pass", "eff": 1.5, "stops": true,
		"crust": 1.0})
	for t in ["water", "water_wave", "puddle"]:
		_add(t, "quicksand", {"bands": [[0.0, "earth_mud"]], "full_at": 0.0, "eff": 1.5, "fallback": "pass"})
	_add("*", "quicksand", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "quicksand_any"})
	# Melt Pit: solids melt into it while its heat budget lasts; ice and water boil; sand surges glaze.
	_add("sand_surge", "melt_pit", {"outcome": "earth_glaze", "partial": "earth_glaze", "fail": "slow", "factor": 0.5})
	for t in ["stone", "stone_heavy", "hot_rock", "metal", "glass", "sand", "ice", "water", "water_wave", "vine"]:
		_add(t, "melt_pit", {"outcome": "earth_melt_in", "partial": "slow", "fail": "pass", "factor": 0.5, "rate_hu": 120.0})
	_add("boulder", "melt_pit", {"outcome": "earth_melt_in", "partial": "slow", "fail": "pass", "factor": 0.5, "rate_hu": 120.0})
	_add("*", "melt_pit", {"outcome": "pass", "partial": "pass", "fail": "pass", "id": "melt_pit_any"})


## Technique legality (Interactions.allows): what Lodestone Grip, Sandform and Magma Hold may seize.
static func _grips() -> void:
	_add("glass", "grip_stone", {"bands": [[0.0, "reclaim"]], "full_at": 0.0, "id": "seize_glass"})
	_add("metal", "grip_metal", {"bands": [[0.0, "reclaim"]], "full_at": 0.0})
	_add("*", "grip_metal", {"bands": [[0.0, "pass"]], "id": "grip_metal_any"})
	for t in ["sand", "sand_surge", "sand_cloud"]:
		_add(t, "grip_sand", {"bands": [[0.0, "reclaim"]], "full_at": 0.0})
	_add("*", "grip_sand", {"bands": [[0.0, "pass"]], "id": "grip_sand_any"})
	for t in ["magma", "lava_wave", "hot_rock", "molten_metal"]:
		_add(t, "grip_magma", {"bands": [[0.0, "reclaim"]], "full_at": 0.0})
	_add("*", "grip_magma", {"bands": [[0.0, "pass"]], "id": "grip_magma_any"})


## Ground lines as counters: a Sand Surge carries loose solids back, buries puddles (mud), smothers fire
## fields and crusts lava (x1.5). A lava wave (Magma Surge) meets other waves by clash; water quenches it.
static func _waves() -> void:
	for t in ["stone", "hot_rock", "metal", "ice", "glass", "stone_heavy"]:
		_add(t, "wave_sand", {"outcome": "capture", "partial": "slow", "fail": "pass", "release_speed": 9.0, "factor": 0.6})
	_add("sand", "wave_sand", {"bands": [[0.0, "absorb"]], "full_at": 0.0})
	_add("puddle", "wave_sand", {"bands": [[0.0, "earth_mud"]], "full_at": 0.0, "fallback": "pass"})
	for t in ["fire_field", "flame", "ember", "blue_fire"]:
		_add(t, "wave_sand", {"outcome": "earth_smother", "partial": "weaken", "fail": "pass", "eff": 2.0})
	_add("lava_wave", "wave_sand", {"outcome": "earth_crust", "partial": "earth_crust", "fail": "clash", "eff": 1.5, "crust": 1.0,
		"stops": true})
	_add("magma", "wave_sand", {"outcome": "earth_crust", "partial": "earth_crust", "fail": "pass", "eff": 1.5, "crust": 1.0})
	_add("water_wave", "wave_sand", {"outcome": "earth_mud", "partial": "earth_mud", "fail": "clash", "target": "counter"})
	_add("*", "wave_sand", {"bands": [[0.0, "pass"]], "full_at": 0.0, "by_form": {"wave": "clash"}, "id": "wave_sand_any"})
	# Lava waves as counters (wave <-> wave clashes; the bigger wave wins / merges by default).
	_add("sand_surge", "wave_lava", {"outcome": "earth_crust", "partial": "earth_crust", "fail": "clash", "target": "counter",
		"eff": 1.0, "crust": 1.5})
	_add("water_wave", "wave_lava", {"bands": [[0.0, "earth_quench"]], "full_at": 0.0, "fallback": "clash"})


## Veil of Grit / sandstorm (zones sand_cloud): drags projectiles (-30 % K), smothers fire (x2), absorbs
## mist and steam, halves bolts that cross it and fuses a glass bead.
static func _sand_cloud() -> void:
	var c := "sand_cloud"
	for t in ["stone", "stone_heavy", "hot_rock", "metal", "ice", "glass", "sand", "magma"]:
		_add(t, c, {"bands": [[0.0, "earth_drag"]], "full_at": 0.0, "factor": 0.7, "when": {"hostile": true}, "else": "pass"})
	_add("sound", c, {"outcome": "absorb", "partial": "weaken", "fail": "pass", "eff": 1.5})
	for t in ["flame", "fire_field", "ember", "blue_fire"]:
		_add(t, c, {"outcome": "earth_smother", "partial": "weaken", "fail": "pass", "eff": 2.0})
	for t in ["mist", "steam"]:
		_add(t, c, {"outcome": "absorb", "partial": "weaken", "fail": "pass", "eff": 1.5})
	_add("lightning", c, {"bands": [[0.0, "earth_bolt_grit"]], "full_at": 0.0, "fallback": "pass"})
	_add("*", c, {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "sand_cloud_any"})


## Ram Wall (counter class "ram", power = K of the sliding wall): bodies and waves on its face are
## shoved back along it (and become the rammer's); too strong a threat breaks the ram. A wall it touches
## is a push contest: the rammer's K is the threat, the other wall's CP the counter.
static func _ram() -> void:
	_add("*", "ram", {"outcome": "earth_ram_push", "partial": "weaken", "fail": "overwhelm", "id": "ram_push"})
	for c in ["wall_stone", "wall_obsidian", "wall_glass", "wall_sand", "wall_mud", "plate_metal", "spikes"]:
		_add("wall_stone", c, {"outcome": "earth_ram_blocked", "partial": "earth_ram_both", "fail": "overwhelm", "dmg_div": 900.0,
			"id": "ram_contest"})


## Chain Arc (counter class "chain"): light solids <= 20 kg are wrapped and yanked to the swinger's
## feet; heavier ones bent; ice deflected; vines cut.
static func _chain() -> void:
	for t in ["stone", "metal", "glass", "hot_rock", "sand"]:
		_add(t, "chain", {"outcome": "earth_wrap", "partial": "bend", "fail": "pass", "when": {"mass_max": 20.0}, "else": "bend",
			"inert": "pass", "eff": 1.2, "id": "chain_wrap"})
	_add("stone_heavy", "chain", {"outcome": "bend", "partial": "bend", "fail": "pass", "inert": "pass"})
	_add("ice", "chain", {"outcome": "deflect", "partial": "bend", "fail": "pass", "inert": "pass", "verb": "chain", "kind": "ice"})
	_add("vine", "chain", {"outcome": "shatter", "partial": "weaken", "fail": "pass", "eff": 2.0, "pieces": 2})
	_add("*", "chain", {"bands": [[0.0, "pass"]], "full_at": 0.0, "id": "chain_any"})


# ================================================================ outcomes

static func _outcomes() -> void:
	Interactions.register_outcome("earth_embed", Callable(EarthRules, "_o_embed"))
	Interactions.register_outcome("earth_stick", Callable(EarthRules, "_o_stick"))
	Interactions.register_outcome("earth_face_heat", Callable(EarthRules, "_o_face_heat"))
	Interactions.register_outcome("earth_absorb_face", Callable(EarthRules, "_o_absorb_face"))
	Interactions.register_outcome("earth_feed_face", Callable(EarthRules, "_o_feed_face"))
	Interactions.register_outcome("earth_set", Callable(EarthRules, "_o_set"))
	Interactions.register_outcome("earth_glass_beads", Callable(EarthRules, "_o_glass_beads"))
	Interactions.register_outcome("earth_crust", Callable(EarthRules, "_o_crust"))
	Interactions.register_outcome("earth_mud", Callable(EarthRules, "_o_mud"))
	Interactions.register_outcome("earth_glassify", Callable(EarthRules, "_o_glassify"))
	Interactions.register_outcome("earth_glass_ground", Callable(EarthRules, "_o_glass_ground"))
	Interactions.register_outcome("earth_plate_heat", Callable(EarthRules, "_o_plate_heat"))
	Interactions.register_outcome("earth_plate_bolt", Callable(EarthRules, "_o_plate_bolt"))
	Interactions.register_outcome("earth_magnet_catch", Callable(EarthRules, "_o_magnet_catch"))
	Interactions.register_outcome("earth_rod_ground", Callable(EarthRules, "_o_rod_ground"))
	Interactions.register_outcome("earth_rod_melt", Callable(EarthRules, "_o_rod_melt"))
	Interactions.register_outcome("earth_melt_in", Callable(EarthRules, "_o_melt_in"))
	Interactions.register_outcome("earth_bolt_grit", Callable(EarthRules, "_o_bolt_grit"))
	Interactions.register_outcome("earth_quench", Callable(EarthRules, "_o_quench"))
	Interactions.register_outcome("earth_smother", Callable(EarthRules, "_o_smother"))
	Interactions.register_outcome("earth_ram_push", Callable(EarthRules, "_o_ram_push"))
	Interactions.register_outcome("earth_ram_blocked", Callable(EarthRules, "_o_ram_blocked"))
	Interactions.register_outcome("earth_ram_both", Callable(EarthRules, "_o_ram_both"))
	Interactions.register_outcome("earth_wrap", Callable(EarthRules, "_o_wrap"))
	Interactions.register_outcome("earth_spike_stop", Callable(EarthRules, "_o_spike_stop"))
	Interactions.register_outcome("earth_glaze", Callable(EarthRules, "_o_glaze"))
	Interactions.register_outcome("earth_drag", Callable(EarthRules, "_o_drag"))


static func _aid(x: Agent) -> int:
	return x.actor.id if x != null and x.actor != null else -1


## Stops a projectile body against a barrier (no longer an attack).
static func _stop_body(b: MatBody) -> void:
	b.vel = Vector3.ZERO
	b.attack_id = 0
	b.on_ground = false


## Lance into a stone / glass wall: it embeds and stays as a conductive rod.
static func _o_embed(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or c.body == null or t.body.mat != Sim.Mat.METAL:
		return false
	var b := t.body
	_stop_body(b)
	b.static_body = true
	b.gravity_scale = 0.0
	b.tag = &"rod"
	b.props["embedded_in"] = c.body.id
	b.props.erase("on_impact")
	b.max_life = Sim.REMNANT_LIFETIME
	c.body.wall_damage_add(b.mass * 4.0 / 900.0)
	w.emit("stick", {"body": b.id, "on": "wall", "wall": c.body.id})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## A small solid sticks in the Magma Curtain's molten face: stones fuse into the wall (mass merged),
## other solids heat in the face and drop at its foot. Heat comes from the face (wall heat_payload).
static func _o_stick(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or c.body == null or not c.body.alive:
		return false
	var b := t.body
	var wall := c.body
	var hu := minf(float(r.get("hu", 60.0)), wall.heat_payload)
	if hu > 0.0:
		wall.heat_payload -= hu
		var used := w.heat_body(b, hu)
		wall.heat_payload += hu - used
	res.stopped = true
	res.pass_scale = 0.0
	if b.mat == wall.mat and b.mass <= 20.0 + 1e-6:
		w.emit("stick", {"body": b.id, "on": "wall", "wall": wall.id})
		b.attack_id = 0
		w.merge_bodies(wall, b)
		if c.perfect:
			wall.props["cp_bonus"] = float(wall.props.get("cp_bonus", 0.0)) + 5.0   # the wall hardens
		return true
	_stop_body(b)
	b.vel = Vector3(0, -1.0, 0)
	w.emit("stick", {"body": b.id, "on": "wall", "wall": wall.id})
	return true


## Ice against the molten face: melted / boiled with the face's heat (x3).
static func _o_face_heat(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or c.body == null:
		return false
	var hu := minf(float(r.get("hu", 120.0)), c.body.heat_payload)
	c.body.heat_payload -= hu
	var used := w.heat_body(t.body, hu)
	c.body.heat_payload += hu - used
	if t.body.alive:
		_stop_body(t.body)
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Lava meeting the Curtain is absorbed into the face: its heat joins the face (heat_payload, exact),
## its stone joins the wall (merge, booked).
static func _o_absorb_face(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or c.body == null or not c.body.alive or t.body.mat != c.body.mat:
		return false
	var b := t.body
	var e := b.thermal_energy() - b.heat_payload
	var got := -Thermal.heat(b, -maxf(0.0, e))
	c.body.heat_payload += got
	b.attack_id = 0
	if b.form == Sim.Form.WAVE:
		w.emit("wave_blocked", {"body": b.id, "at": b.pos})
	w.emit("absorb", {"body": b.id, "into": c.body.id, "hu": got})
	w.merge_bodies(c.body, b)
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Flames feed the Curtain's face (the flame's heat budget goes into the wall).
static func _o_feed_face(_w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if c.body == null or not c.body.alive:
		return false
	res.stopped = true
	res.pass_scale = 0.0
	var hu := 0.0
	if t.body != null and t.body.alive and t.body.mat == Sim.Mat.FIRE:
		hu = t.body.heat_payload * float(r.get("share", 1.0))
		t.body.heat_payload -= hu
	elif t.heat > 0.0:
		hu = t.heat * float(r.get("share", 1.0))
		t.heat -= hu
	c.body.heat_payload += hu
	res.heat_used = float(res.heat_used) + hu
	return true


## Water on the Curtain: the face's heat boils it (steam burst, vapour booked) and the face sets to
## obsidian (CP 28 -> 38, brittle to sound).
static func _o_set(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var wall := c.body
	if wall == null or not wall.alive:
		return false
	var water := t.body
	var hu := wall.heat_payload
	if water != null and water.alive and water.is_water() and hu > 0.0:
		wall.heat_payload = 0.0
		var kg := w.boil_water(water, hu, wall.pos + Vector3(0, 0.8, 0))
		w.emit("steam", {"body": wall.id, "water": water.id, "kg": kg})
		if water.alive and water.mass <= 0.05:
			w.decay_body(water, "boiled")
		elif water.alive and water.form != Sim.Form.PUDDLE and water.form != Sim.Form.POOL:
			_stop_body(water)
			if water.form == Sim.Form.WAVE:
				w._settle_wave(water, "blocked")
	if not wall.props.get("set", false):
		wall.props["set"] = true
		wall.hardness = 0.38
		w.emit("transform", {"body": wall.id, "at": wall.pos, "from": "molten_face", "to": "obsidian", "why": "quench"})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Sand thrown at the molten face fuses into glass beads (booked conversion) and drops.
static func _o_glass_beads(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if t.body == null or not t.body.alive or t.body.mat != Sim.Mat.SAND:
		return false
	var b := t.body
	var hu := minf(c.body.heat_payload if c.body != null else 0.0, 40.0)
	if c.body != null:
		c.body.heat_payload -= hu
	var used := w.heat_body(b, hu)
	if c.body != null:
		c.body.heat_payload += hu - used
	w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
	_stop_body(b)
	b.vel = Vector3(0, -1.0, 0)
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": "molten_face"})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Sand vs lava: the lava crusts (heat removed = CP_eff x 20 x crust, moved into the sand body when
## there is one, else lost to the air: booked) and a wave stalls when `stops`.
static func _o_crust(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var lava := t.body
	var sand := c.body
	if String(r.get("target", "")) == "counter":
		lava = c.body
		sand = t.body
	if lava == null or not lava.alive or not lava.is_stone():
		return false
	var want := float(res.cp_eff) * Interactions.HU_PER_PU * float(r.get("crust", 1.0))
	want = minf(want, maxf(0.0, lava.thermal_energy() - lava.heat_payload))
	var got := -Thermal.heat(lava, -want)
	if sand != null and sand.alive and sand != lava and sand.mass > 0.0 and Materials.is_fusible(sand.mat):
		var used := w.heat_body(sand, got)
		w.ledger.ambient -= got - used
	else:
		w.ledger.ambient -= got
	res.heat_used = float(res.heat_used) + got
	w.emit("transform", {"body": lava.id, "at": lava.pos, "from": "lava", "to": "crust", "why": "sand"})
	var stops := bool(r.get("stops", false)) and String(res.band) == "full"
	if lava.form == Sim.Form.WAVE:
		if stops or lava.liquid <= 0.05:
			w.emit("wave_blocked", {"body": lava.id, "at": lava.pos})
			w._settle_wave(lava, "crusted")
		else:
			lava.wave_budget *= 0.6
	elif stops and lava == t.body:
		_stop_body(lava)
	res.stopped = stops
	res.pass_scale = 0.0 if stops else _left(res)
	return true


static func _left(res: Dictionary) -> float:
	var tp := float(res.tp)
	if tp <= 1e-6:
		return 0.0
	return clampf((tp - float(res.cp_eff)) / tp, 0.0, 1.0)


## Water and sand meet: the sand side becomes mud (a mud wall +5 CP that collapses after 4 s, a mud
## bog quicksand, a slower muddy surge); the water body is soaked into it (booked as evaporated).
static func _o_mud(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var sand: MatBody = null
	var water: MatBody = null
	for b in [t.body, c.body]:
		if b == null or not b.alive:
			continue
		if b.mat == Sim.Mat.SAND and sand == null:
			sand = b
		elif b.is_water() and water == null:
			water = b
	if sand == null:
		return false
	if not sand.props.get("mud", false):
		sand.props["mud"] = true
		sand.props["wet"] = true
		sand.props["mud_tick"] = w.tick
		match sand.form:
			Sim.Form.WALL:
				sand.tag = &"mud"
				sand.props["cp_bonus"] = float(sand.props.get("cp_bonus", 0.0)) + 5.0
			Sim.Form.ZONE:
				sand.power *= 1.3
				if sand.max_life > 0.0:
					sand.max_life += 3.0
			Sim.Form.WAVE:
				sand.props["speed"] = float(sand.props.get("speed", 9.0)) * 0.75
				sand.power += 5.0
		w.emit("transform", {"body": sand.id, "at": sand.pos, "from": "sand", "to": "mud", "why": "water"})
	if water != null and water.form != Sim.Form.POOL:
		w.mass_ledger.evaporated += water.mass
		w.ledger.removed += water.thermal_energy()
		water.mass = 0.0
		w.remove_body(water, "soaked")
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Blue fire on a sand wall: the wall fuses to glass (blocks what hits it), part of the heat goes in.
static func _o_glassify(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if c.body == null or not c.body.alive:
		return false
	res.stopped = true
	res.pass_scale = 0.0
	if t.heat > 0.0:
		# Outcomes.move_heat takes the heat from the threat body's real energy (or a volume's budget).
		var used := Outcomes.move_heat(w, t, c.body, t.heat * float(r.get("heat_share", 0.5)))
		res.heat_used = float(res.heat_used) + used
	_to_glass(w, c.body, "blue_fire")
	return true


static func _to_glass(w: CombatWorld, b: MatBody, why: String) -> void:
	if b.mat != Sim.Mat.SAND:
		return
	w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
	if b.form == Sim.Form.WALL:
		b.tag = &"glass"
		b.props.erase("mud")
		b.props.erase("cp_bonus")
		b.hardness = -1.0
	w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": why})


## A bolt grounded by a sand wall fuses it into glass.
static func _o_glass_ground(w: CombatWorld, _t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.pass_scale = 0.0
	res.stopped = true
	w.emit("grounded", {"actor": _aid(c)})
	if c.body != null and c.body.alive:
		_to_glass(w, c.body, "lightning")
	return true


## The Aegis plate takes the heat (flame / hot rock / steam): a clean block while it holds; >= 300 °C it
## is dropped (EarthMetal plate tick). `melts`: magma / blue fire overwhelm it - most of the hit lands.
static func _o_plate_heat(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var plate := c.body
	var share := float(r.get("share", 0.5))
	if plate != null and plate.alive:
		if t.body != null and t.body.alive and t.body.mat != Sim.Mat.FIRE:
			var take := maxf(0.0, t.body.thermal_energy()) * share
			var got := -Thermal.heat(t.body, -take)
			var used := w.heat_body(plate, got)
			w.ledger.ambient -= got - used
		elif t.body != null and t.body.alive:
			var hp := t.body.heat_payload * share
			t.body.heat_payload -= hp
			var used2 := w.heat_body(plate, hp)
			w.ledger.ambient -= hp - used2
		elif t.heat > 0.0:
			var used3 := w.heat_body(plate, t.heat * share)
			t.heat -= used3
			res.heat_used = float(res.heat_used) + used3
	if r.get("melts", false):
		res.pass_scale = 0.6
		res.stopped = false
		res.knock_scale = 0.6
		return true
	res.stopped = true
	res.pass_scale = 0.0
	if c.kind == "guard" and c.actor != null:
		res.result = w.guard_chip(c.actor, ctx.get("info", {}), r, t)
	elif t.body != null and t.body.alive and t.body.mat != Sim.Mat.FIRE:
		t.body.vel = -t.body.vel * 0.12 + Vector3(0, 1.0, 0)
		t.body.attack_id = 0
	return true


## Lightning on an Aegis guard: grounded through a fighter standing on stone or sand (clean block);
## in water, on a puddle or on the metal plate the plate conducts it into them (x1.2).
static func _o_plate_bolt(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var a := c.actor
	if a == null:
		return false
	var s := a.surface
	if a.grounded and (s == "stone" or s.begins_with("zone:") and not s.contains("ice")):
		res.stopped = true
		res.pass_scale = 0.0
		w.emit("grounded", {"actor": a.id, "via": "plate"})
		if c.kind == "guard":
			var rr := {"chip": 0.0, "bal": 0.0, "knock": 0.0, "kind": "grounded"}
			res.result = w.guard_chip(a, ctx.get("info", {}), rr, t)
		return true
	res.pass_scale = float(r.get("conduct_mult", 1.2))
	res.stopped = false
	res["conduct"] = true
	w.emit("conduct", {"actor": _aid(t), "nodes": ["plate:%d" % (c.body.id if c.body != null else -1)], "victims": [a.id]})
	return true


## Magnet Catch: a perfect Aegis pulls a metal shot into the satchel (mass booked: field -> satchel).
static func _o_magnet_catch(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var a := c.actor
	if a == null or t.body == null or not t.body.alive or t.body.mat != Sim.Mat.METAL:
		return false
	var b := t.body
	EarthMetal.to_satchel(w, a, b, "caught")
	w.emit("perfect_deflect", {"actor": a.id, "body": b.id, "verb": "magnet", "kind": "metal"})
	res.stopped = true
	res.pass_scale = 0.0
	res.result = "perfect"
	return true


## Rod Plant field: a bolt within it is drawn to the rod and grounded (E <= capacity). A rod touching a
## puddle / the pool / the plate passes the charge into that water / metal (half of E, split).
static func _o_rod_ground(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	var rod := c.body
	w.emit("grounded", {"actor": _aid(c), "via": "rod", "body": rod.id if rod != null else -1})
	if rod != null and rod.alive:
		rod.props["struck"] = float(rod.props.get("struck", 0.0)) + float(t.ch.E)
		FxEvents.fx(w, "burst", "lightning", {"actor": _aid(c), "body": rod.id, "pos": rod.pos + Vector3(0, 1.2, 0), "radius": 0.8,
			"power": float(t.ch.E), "shape": "ground"})
		EarthMetal.rod_spread(w, t, rod, float(t.ch.E) * 0.5)
	return true


## Above the rod's capacity the rod melts (scrap leaves the field, booked) and the bolt continues.
static func _o_rod_melt(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var rod := c.body
	if rod != null and rod.alive:
		w.emit("shatter", {"body": rod.id, "mass": rod.mass, "by": "lightning"})
		w.close_zone(rod, "melted")   # the rod melts away (metal_returned)
	res.counter_broken = true
	var tp := float(res.tp)
	res.pass_scale = clampf((tp - float(r.get("absorb_on_fail", 0.5)) * float(res.cp_eff)) / maxf(tp, 1e-6), 0.0, 1.0)
	res.stopped = res.pass_scale <= 0.0
	return true


## Melt Pit: a body in the pit takes heat from the pit's budget (zone heat_payload, exact) - stones and
## metal melt into it (lava to re-pour), ice and water boil; it stops moving there.
static func _o_melt_in(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var pit := c.body
	var b := t.body
	if pit == null or not pit.alive or b == null or not b.alive:
		return false
	var hu := minf(pit.heat_payload, float(r.get("rate_hu", 120.0)))
	if hu > 0.0:
		pit.heat_payload -= hu
		var used := w.heat_body(b, hu)
		pit.heat_payload += hu - used
		res.heat_used = float(res.heat_used) + used
	elif pit.is_stone() and pit.liquid > 0.0:
		# A lava pool melts what lands in it with its own heat (exact transfer).
		var take := minf(float(r.get("rate_hu", 120.0)), maxf(0.0, pit.thermal_energy() - pit.mass * Sim.STONE_C * (Sim.STONE_MELT_C - Sim.AMBIENT_C) * 0.5))
		if take > 0.0:
			var got := -Thermal.heat(pit, -take)
			var used2 := w.heat_body(b, got)
			w.ledger.ambient -= got - used2
			res.heat_used = float(res.heat_used) + used2
	if b.alive and b.form != Sim.Form.WAVE:
		b.vel *= 0.2
		b.attack_id = 0
	elif b.alive and b.form == Sim.Form.WAVE:
		b.wave_budget = minf(b.wave_budget, 0.5)
	res.stopped = true
	res.pass_scale = 0.0
	if pit.mat == Sim.Mat.AIR and pit.heat_payload <= 0.5:
		w.close_zone(pit, "spent")
	return true


## A bolt through a sand cloud loses half its power and fuses a glass bead (1 kg of the cloud, booked).
static func _o_bolt_grit(w: CombatWorld, _t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.pass_scale = 0.5
	res.stopped = false
	var z := c.body
	if z != null and z.alive and z.mat == Sim.Mat.SAND and z.mass > 1.5:
		var g := w.split_body(z, 1.0, z.pos + Vector3(0, 0.6, 0))
		g.form = Sim.Form.CHUNK
		g.zone_radius = 0.0
		g.tag = &""
		g.vel = Vector3(0, -1.0, 0)
		g.max_life = Sim.REMNANT_LIFETIME
		w.convert_mat(g, Sim.Mat.GLASS, "sand_to_glass")
		w.emit("transform", {"body": g.id, "at": g.pos, "from": "sand", "to": "glass", "why": "lightning"})
	return true


## A water wave meets a lava wave: the lava is quenched by the water (flash boil, booked) - both stall.
static func _o_quench(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var water := t.body
	var lava := c.body
	if water == null or lava == null or not water.alive or not lava.alive or not water.is_water() or not lava.is_stone():
		return false
	w.quench_energy(lava, water, minf(lava.thermal_energy(), water.mass * 30.0))
	if lava.alive and lava.form == Sim.Form.WAVE and lava.liquid <= 0.4:
		w._settle_wave(lava, "quenched")
	if water.alive and water.form == Sim.Form.WAVE:
		w._settle_wave(water, "quenched")
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Sand smothers fire: a fire body (fireball, fire field, ember) is put out (decay, booked); a flame
## volume is stopped (its heat budget is booked as spent by the volume site).
static func _o_smother(w: CombatWorld, t: Agent, _c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	res.stopped = true
	res.pass_scale = 0.0
	if t.body != null and t.body.alive and t.body.mat == Sim.Mat.FIRE:
		w.emit("extinguish", {"body": t.body.id, "by": "sand"})
		if t.body.form == Sim.Form.ZONE:
			w.close_zone(t.body, "smothered")
		else:
			w.decay_body(t.body, "smothered")
	return true


## Ram Wall face: a body is shoved along the ram (a hostile shot becomes the rammer's attack); a wave
## turns around along it and is now the rammer's.
static func _o_ram_push(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or c.actor == null:
		return false
	var a := c.actor
	var dir := c.dir
	if b.form == Sim.Form.WAVE:
		b.wave_dir = dir
		b.wave_budget = maxf(b.wave_budget, 5.0)
		b.pos += dir * 0.4
	else:
		b.vel = dir * maxf(10.0, b.vel.length() * 0.6) + Vector3(0, 2.0, 0)
		b.on_ground = false
	if b.attack_id != 0 or b.form == Sim.Form.WAVE:
		b.attack_id = w.new_attack_id()
		b.attack_owner = a.id
		b.hit_set.clear()
		b.hit_set[a.id] = true
		b.damage = maxf(b.damage, 10.0)
		b.balance_damage = maxf(b.balance_damage, 25.0)
	b.touch(a.id, "ram", w.tick)
	w.emit("deflect", {"actor": a.id, "body": b.id, "verb": "ram", "kind": FxEvents.mat_of(b)})
	res.stopped = true
	res.pass_scale = 0.0
	res.result = "deflect"
	return true


## Ram contest lost: the other wall holds (it takes the momentum as damage); the ram stops.
static func _o_ram_blocked(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	if c.body != null and c.body.alive and t.body != null:
		# The wall that held takes damage in proportion to how close the contest was.
		c.body.wall_damage_add(0.5 * float(res.tp) / maxf(float(res.cp_eff), 1e-3))
		w.emit("block", {"actor": _aid(c), "body": t.body.id, "kind": "ram", "wall": c.body.id, "power": res.tp, "mat": FxEvents.mat_of(t.body),
			"tier": t.tier, "dir": t.dir})
		if c.body.wall_damage >= 1.0:
			w._crumble_wall(c.body)
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Ram contest even: both walls break.
static func _o_ram_both(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	if c.body != null and c.body.alive and c.body.form == Sim.Form.WALL:
		w._crumble_wall(c.body)
	res.counter_broken = true
	res.stopped = true
	res.pass_scale = 0.0
	if t.body != null:
		w.emit("clash", {"a": t.body.id, "b": c.body.id if c.body != null else -1, "winner": -1, "pos": t.body.pos, "power": res.tp,
			"mat": FxEvents.mat_of(t.body)})
	return true


## Chain Arc wraps a light solid and yanks it to the swinger's feet (no longer an attack; the swinger
## keeps the residual authority, so it is theirs to seize or reuse).
static func _o_wrap(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	var a := c.actor
	if b == null or not b.alive or a == null:
		return false
	var land := a.pos + a.forward() * 0.9
	land.y = w.arena.ground_height(land.x, land.z, a.pos.y + 0.4) + b.radius
	b.gravity_scale = 1.0
	b.vel = Verbs.launch_vel(b.pos, land, maxf(2.0, KitEarth.flat_dist(b.pos, land) / 0.45), 1.0)
	b.attack_id = 0
	b.on_ground = false
	b.residual_owner = a.id
	b.residual_authority = 0.9
	b.touch(a.id, "wrap", w.tick)
	w.emit("capture", {"body": b.id, "by": -1, "actor": a.id, "verb": "chain"})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## Spikes meet a ground line: the line is stopped where it is; a travelling spike line erupts there.
## Iron filings (metal spike line) don't stop anything.
static func _o_spike_stop(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, _r: Dictionary, _ctx: Dictionary) -> bool:
	var spike: MatBody = null
	var other: MatBody = null
	for x in [t.body, c.body]:
		if x == null or not x.alive:
			continue
		if spike == null and (x.tag == &"spike_line" or x.tag == &"spikes"):
			spike = x
		else:
			other = x
	if spike == null or other == null:
		return false
	if spike.mat == Sim.Mat.METAL:
		res.pass_scale = 1.0
		return true
	if other.form == Sim.Form.WAVE:
		w.emit("block", {"actor": spike.attack_owner if spike.attack_owner >= 0 else spike.last_actor, "body": other.id, "kind": "spikes",
			"wall": spike.id, "power": res.tp, "mat": FxEvents.mat_of(other), "tier": other.tier, "dir": other.wave_dir})
		w.emit("wave_blocked", {"body": other.id, "at": other.pos})
		w._settle_wave(other, "blocked")
	elif other.form == Sim.Form.ZONE and other.mat == Sim.Mat.FIRE:
		w.close_zone(other, "blocked")
	if spike.form == Sim.Form.WAVE:
		EarthStone._erupt(w, spike)
	res.stopped = true
	res.pass_scale = 0.0
	return true


## A Melt Pit glazes the front of a sand surge: the surge stalls in it and fuses to glass (the pit's
## heat goes into it; sand -> glass booked).
static func _o_glaze(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or b.mat != Sim.Mat.SAND:
		return false
	_o_melt_in(w, t, c, res, r, ctx)
	if b.alive and b.form == Sim.Form.WAVE:
		w._settle_wave(b, "glazed")
	if b.alive and b.mat == Sim.Mat.SAND:
		w.convert_mat(b, Sim.Mat.GLASS, "sand_to_glass")
		b.props.erase("settle")
		w.emit("transform", {"body": b.id, "at": b.pos, "from": "sand", "to": "glass", "why": "melt_pit"})
	res.stopped = true
	res.pass_scale = 0.0
	return true


## A hostile projectile crossing a sand cloud is dragged once (K x factor: speed x factor).
static func _o_drag(w: CombatWorld, t: Agent, c: Agent, res: Dictionary, r: Dictionary, _ctx: Dictionary) -> bool:
	var b := t.body
	if b == null or not b.alive or c.body == null:
		return false
	var key := "dragged_%d" % c.body.id
	res.pass_scale = 1.0
	if b.props.has(key):
		return true
	b.props[key] = true
	var f := float(r.get("factor", 0.7))
	b.vel *= f
	res.pass_scale = f
	w.emit("bend", {"actor": _aid(c), "body": b.id, "verb": "grit"})
	return true
