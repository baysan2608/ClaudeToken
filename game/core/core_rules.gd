class_name CoreRules
extends RefCounted
## The rule cells the core owns (docs/COMBAT_SPEC.md "Engine" §E4.4): every legacy behaviour with
## today's exact numbers (flag legacy: true, never replaceable), the environment cells (pool, puddle,
## plate, arena_wall, ground), the generic anchor stance cell and the lightning-vs-barrier defaults.
## Element kits add the cells of their own counter classes (MOVESET §15.4).

## Threat classes today's moves can produce (legacy cells are exact for these, so kit family cells
## never shadow them).
const LEGACY_THREATS := ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "water", "ice", "flame",
	"gust", "lightning", "puddle"]
## Sub-0 guards (Agent.of_guard): Earth, Water (with/without shield), Fire, Air.
const LEGACY_GUARDS := ["guard", "guard_earth", "shield_water", "aura_flame", "guard_wind"]
const STONES := ["stone", "stone_heavy", "boulder", "hot_rock", "magma"]
const LIGHT_SOLIDS := ["stone", "hot_rock", "ice", "metal", "glass", "sand"]


static func register_all() -> void:
	_guards()
	_walls()
	_gust()
	_lash()
	_flare()
	_lava()
	_lightning()
	_techniques()
	_environment()
	_anchor()


static func _add(t: String, c: String, r: Dictionary) -> void:
	r["legacy"] = r.get("legacy", true)
	r["owner"] = "core"
	Interactions.add_rule(t, c, r)


## Plain guard: always a block with chip 12 % damage / 55 % balance / 35 % knock (guard break at 0
## balance); perfect (pressed <= 0.18 s before contact) deflects, and an attacker within 3 m loses 18 balance.
static func _plain_guard() -> Dictionary:
	return {"bands": [[0.0, "block"]], "full_at": 0.0, "perfect": "deflect", "chip": 0.12, "bal": 0.55, "knock": 0.35,
		"perfect_balance": 18.0, "perfect_range": 3.0}


static func _guards() -> void:
	for g in LEGACY_GUARDS:
		for t in LEGACY_THREATS + ["*"]:
			if g == "shield_water" and t == "flame":
				continue   # the shield steams the flame away (CoreRules._flare)
			if g == "guard_wind" and ["stone", "stone_heavy", "boulder", "hot_rock", "ice", "water"].has(t):
				continue   # Wind Guard power rule below
			var r := _plain_guard()
			r["id"] = "legacy_guard"
			if g == "guard_earth":
				# Perfect Earth guard vs a stone it could control: ballistic return at the thrower (x1.05, >= 12 m/s).
				r["perfect"] = "redirect"
				r["fallback"] = "deflect"
				r["requires"] = "stone_control"
				r["aim"] = "sender"
				r["speed_mult"] = 1.05
				r["min_speed"] = 12.0
				r["id"] = "legacy_earth_redirect"
			if g == "aura_flame" and t == "flame":
				r["absorb_reserve"] = 0.5     # perfect fire guard keeps 50 % of the flame's heat
				r["id"] = "legacy_heat_sink"
			if g == "aura_flame" and t == "lightning":
				r["perfect"] = "redirect"      # redirect current: the bolt returns at 80 %
				r["fallback"] = "deflect"
				r["requires"] = "redirect_current"
				r["factor"] = 0.8
				r["id"] = "legacy_redirect_current"
			if g == "guard_wind" and t == "flame":
				# Air guard vs flare: blocked, no chip, regardless of facing (wind wraps the fighter).
				r = {"bands": [[0.0, "block"]], "full_at": 0.0, "chip": 0.0, "bal": 0.0, "knock": 0.0, "kind": "fire_air",
					"aura": true, "id": "legacy_air_vs_flare"}
			_add(t, g, r)
	# Wind Guard (sub-0 Air guard) vs light solids and streams: CP 12 x eff 1.5 deflects, perfect sends it
	# back to the thrower; too heavy = a plain guard block. Replaces the old "< 30 kg deflect" rule
	# (no test pinned it; documented in COMBAT_SPEC "Engine" §E10).
	for t in ["stone", "hot_rock", "ice", "metal", "glass", "sand", "water"]:
		var eff := 1.0 if t == "water" else (2.0 if t == "metal" else 1.5)
		_add(t, "guard_wind", {"eff": eff, "outcome": "deflect", "perfect": "reflect", "partial": "block", "fail": "block",
			"chip": 0.12, "bal": 0.55, "knock": 0.35, "id": "wind_guard_light"})
	for t in ["stone_heavy", "boulder"]:
		_add(t, "guard_wind", {"eff": 0.6, "outcome": "deflect", "perfect": "reflect", "partial": "block", "fail": "block",
			"chip": 0.12, "bal": 0.55, "knock": 0.35, "id": "wind_guard_heavy"})


## Earth wall (Bulwark, WALL body tag ""): blocks every legacy threat whatever its power (wall_damage +=
## momentum / 900, crumble at >= 1 into 2 x 20 kg rubble); perfect guard redirects a stone it could
## control; lava waves settle "blocked"; bolts ground when E <= CP (120 kg x 0.25 = 30), else the wall
## shatters and the bolt continues with E - 0.5 CP.
static func _walls() -> void:
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "water", "ice", "puddle", "*"]:
		_add(t, "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "perfect": "redirect", "fallback": "block",
			"requires": "stone_control", "aim": "sender", "speed_mult": 1.05, "min_speed": 12.0, "wall_push": 0.3,
			"residual": true, "dmg_div": 900.0, "id": "legacy_wall"})
	_add("lava_wave", "wall_stone", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "legacy_wall_wave"})
	_add("lightning", "wall_stone", {"bands": [[0.0, "shatter"], [1.0, "ground"]], "absorb_on_fail": 0.5,
		"id": "legacy_wall_bolt"})


## Palm gust / cyclone push (counter class "gust", tiers 0-1): hostile light (< 30 kg) projectiles are
## turned along the push (x0.8, +1.5 up) and change owner; heavy ones bend (60 / m); loose light bodies are
## pushed (knock x 12 / m); clouds disperse; waves, puddles, walls and the pool are untouched.
static func _gust() -> void:
	var r := {"bands": [[0.0, "redirect"]], "full_at": 0.0, "when": {"mass_lt": 30.0}, "else": "bend",
		"inert": "push", "inert_else": "pass", "aim": "dir", "speed_mult": 0.8, "up": 1.5, "verb": "gust",
		"bend_impulse": 60.0, "push_mult": 12.0, "tiers": [0, 1], "id": "legacy_gust",
		"by_form": {"cloud": "disperse", "wave": "pass", "puddle": "pass", "wall": "pass", "pool": "pass"}}
	for t in LEGACY_THREATS + ["steam", "mist", "*"]:
		var rt := r.duplicate(true)
		if t == "magma":
			# Molten rock is lava: a palm gust only bends it (MOVESET §5.4; the owner's rule "a simple air
			# attack cannot stop lava"). Gale / Hurricane (T2 / T3, Air kit cells) cool it to rock.
			rt.when = {"mass_lt": 4.0}
		_add(t, "gust", rt)


## Water lash (counter class "water_jet"): knocks hostile projectiles <= 25 kg aside (x0.45, +2 up).
static func _lash() -> void:
	for t in LEGACY_THREATS + ["*"]:
		_add(t, "water_jet", {"bands": [[0.0, "deflect"]], "full_at": 0.0, "when": {"mass_max": 25.0}, "else": "pass",
			"side": 0.45, "up": 2.0, "verb": "lash", "tiers": [0, 1], "id": "legacy_lash"})


## Flare / blaze cone (volume class "flame"): water in the cone takes 60 % of the remaining heat first
## (liquid boils into steam, ice melts; event steam_block; its holder is shielded); stones in the cone
## take 50 % of what is left.
static func _flare() -> void:
	var steam := {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "steam", "share": 0.6, "event": "steam_block",
		"guard_kind": "fire_water", "id": "legacy_flare_water"}
	for c in ["shield_water", "puddle", "water", "ice"]:
		_add("flame", c, steam.duplicate(true))        # barrier bodies counter the flame
	for t in ["water", "ice"]:
		_add(t, "flame", steam.duplicate(true))        # loose water met by the flame
	for t in STONES + ["lava_wave"]:
		_add(t, "flame", {"bands": [[0.0, "heat"]], "full_at": 0.0, "share": 0.5, "id": "legacy_flare_stone"})


## Lava (molten / softened stone) touching the pool or a puddle is quenched at 1400 HU/s; the water
## flash-boils (Thermal.boil, ledger vapor).
static func _lava() -> void:
	for t in ["magma", "lava_wave", "hot_rock"]:
		for c in ["pool", "puddle"]:
			_add(t, c, {"bands": [[0.0, "transform"]], "full_at": 0.0, "to": "rock", "rate": Sim.QUENCH_RATE,
				"requires": "liquid", "id": "legacy_quench"})


## Lightning: arena solids always stop a bolt; the grounded stance (Earth guard on stone) takes 40 %;
## barriers ground E <= CP and shatter below (continue with E - 0.5 CP); insulators (ice, glass, sand,
## vacuum) count 1.5 x CP.
static func _lightning() -> void:
	_add("lightning", "arena_wall", {"bands": [[0.0, "ground"]], "full_at": 0.0, "factor": 0.0, "id": "legacy_arena_bolt"})
	_add("lightning", "ground", {"bands": [[0.0, "ground"]], "full_at": 0.0, "factor": 0.4, "event": "grounded",
		"id": "legacy_grounded_stance"})
	for c in ["barrier_solid", "barrier_soft", "barrier_energy"]:
		_add("electric", c, {"bands": [[0.0, "shatter"], [1.0, "ground"]], "absorb_on_fail": 0.5, "eff_insulator": 1.5,
			"legacy": false, "id": "bolt_vs_barrier"})
	for c in ["pool", "puddle", "plate"]:
		_add("lightning", c, {"bands": [[0.0, "conduct"]], "full_at": 0.0, "id": "env_conduct"})


## Technique legality (grip / draw / heat contexts) - Interactions.allows(body, counter_class).
static func _techniques() -> void:
	for t in ["stone", "stone_heavy", "boulder", "hot_rock"]:
		_add(t, "grip_stone", {"outcome": "reclaim", "bands": [[0.0, "reclaim"]], "full_at": 0.0, "id": "legacy_seize"})
	_add("*", "grip_stone", {"outcome": "pass", "bands": [[0.0, "pass"]], "id": "legacy_seize_other"})
	for t in ["hot_rock", "magma", "lava_wave"]:
		_add(t, "draw_heat", {"outcome": "absorb", "bands": [[0.0, "absorb"]], "full_at": 0.0, "id": "legacy_draw"})
	_add("*", "draw_heat", {"outcome": "pass", "bands": [[0.0, "pass"]], "id": "legacy_draw_other"})
	for t in ["stone", "stone_heavy", "boulder", "hot_rock", "magma", "lava_wave", "water", "ice", "puddle", "water_wave"]:
		_add(t, "heat_grip", {"outcome": "transform", "bands": [[0.0, "transform"]], "full_at": 0.0, "to": "lava", "id": "legacy_heat"})
	_add("*", "heat_grip", {"outcome": "pass", "bands": [[0.0, "pass"]], "id": "legacy_heat_other"})
	_add("*", "heat_ranged", {"outcome": "heat", "bands": [[0.0, "heat"]], "full_at": 0.0, "legacy": false, "id": "ranged_heat_any"})
	_add("puddle", "grip_water", {"outcome": "reclaim", "bands": [[0.0, "reclaim"]], "full_at": 0.0, "id": "legacy_draw_water"})
	_add("*", "grip_water", {"outcome": "pass", "bands": [[0.0, "pass"]], "id": "legacy_draw_water_other"})


## Environment: arena solids stop projectiles (legacy impact), water absorbs water.
static func _environment() -> void:
	_add("*", "arena_wall", {"bands": [[0.0, "block"]], "full_at": 0.0, "id": "env_arena"})
	for c in ["pool", "puddle"]:
		_add("liquid", c, {"outcome": "absorb", "partial": "absorb", "fail": "absorb", "legacy": false, "id": "env_water_absorb"})
		_add("*", c, {"outcome": "pass", "partial": "pass", "fail": "pass", "legacy": false, "id": "env_water_pass"})
	_add("*", "ground", {"outcome": "pass", "partial": "pass", "fail": "pass", "legacy": false, "id": "env_ground"})
	_add("*", "plate", {"outcome": "pass", "partial": "pass", "fail": "pass", "legacy": false, "id": "env_plate"})


## Anchor stance (Stone Skin, Iron Stance, Anchor, Deep Roots): pressure that would lift, pull or knock
## the fighter is nullified when CP >= P, scaled on a partial, ignored on a fail.
static func _anchor() -> void:
	_add("pressure", "anchor", {"outcome": "block", "partial": "weaken", "fail": "pass", "legacy": false, "id": "anchor"})
	_add("*", "anchor", {"outcome": "pass", "partial": "pass", "fail": "pass", "legacy": false, "id": "anchor_other"})
