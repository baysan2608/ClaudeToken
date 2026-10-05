class_name FxEvents
extends RefCounted
## Event contract catalogue (docs/MOVESET.md §15.6, docs/COMBAT_SPEC.md "Engine" §E8) and emit
## helpers. Sim -> VFX / audio / AI / HUD / Lab. One-shot cues only: persistent visuals come from
## body state (mat, form, tag, heat, zone_radius, tier, spin), never from events.

## One-shot fx keys (event "fx", field fx). aura = on/off for stances and guards (field on).
const FX := ["cast", "release", "cone", "beam", "burst", "ring", "erupt", "trail", "splash", "aura"]
## Material keys (field mat).
const MATS := ["stone", "metal", "sand", "glass", "magma", "water", "ice", "mist", "steam", "plant",
	"flame", "blue", "lightning", "blast", "wind", "vortex", "vacuum", "sound"]
## Optional shape keys (field shape).
const SHAPES := ["spear", "fan", "disc", "lance", "rod", "plate", "fireball", "comet", "ember", "crescent",
	"spiral", "needles", "seed", "ground", "down", "small", "open", "short",
	# technique shaping (T+A) and stance / movement-mode auras (fx cast / aura `shape`)
	"split", "freeze", "condense", "compress", "cool", "reforge", "retag", "flight", "glide", "surf", "skate", "hover",
	"burrow", "run", "walk", "roots", "stone_skin", "iron", "anchor", "grounding", "stance", "wind", "storm", "sound", "vacuum"]
## MatBody.tag values by form family.
const BODY_TAGS := {
	"projectile": ["spear", "rubble", "crag", "disc", "lance", "rod", "plate", "caltrops", "slug", "glob", "bomb",
		"fireball", "comet", "ember", "crescent", "twister", "spiral", "needle", "seed", "orb", "block"],
	"wave": ["", "water_wave", "sand_surge", "rime", "fire_line", "ground_current", "roots", "tremor", "dust_line",
		"wind_wall", "funnel", "magma_rift", "spike_line"],
	"wall": ["", "obsidian", "glass", "sand", "mud", "ice", "vine", "plate", "ridge", "spikes"],
	"zone": ["fog", "mist", "steam", "sand_cloud", "sandstorm", "fire_field", "quicksand", "ice_floor", "mud",
		"caltrops", "tornado", "vacuum_well", "null_bubble", "mine", "melt_pit", "corona", "eddy", "briar", "geyser",
		"static_field", "wind_guard", "vortex_wall", "sound_barrier", "steam_screen", "lava_pool", "fuse", "flight_field", "inrush"],
}
## New event types of the engine (existing events keep their names and fields).
const EVENTS := ["charge", "fx", "interaction", "status", "zone", "morph", "clash"]
## Every outcome name an `interaction` event can carry.
const OUTCOMES := ["block", "deflect", "redirect", "reflect", "reclaim", "capture", "absorb", "transform", "shatter",
	"sink", "conduct", "ground", "pass", "amplify", "extinguish", "weaken", "bend", "slow", "overwhelm", "clash",
	"disrupt", "neutralize", "heat", "push", "disperse"]


## kind: "fx" | "mat" | "shape" | "tag" | "event" | "outcome".
static func is_known(kind: String, key: String) -> bool:
	match kind:
		"fx":
			return FX.has(key)
		"mat":
			return MATS.has(key)
		"shape":
			return key == "" or SHAPES.has(key)
		"tag":
			for k in BODY_TAGS:
				if (BODY_TAGS[k] as Array).has(key):
					return true
			return false
		"event":
			return EVENTS.has(key)
		"outcome":
			return OUTCOMES.has(key)
	return false


## One-shot cue. d may hold any of: shape actor body element sub move tier pos dir radius length
## angle height path dur power on. Missing keys get neutral defaults so consumers can read them blindly.
static func fx(w: CombatWorld, fx_key: String, mat: String, d: Dictionary = {}) -> void:
	if not w._record_events:
		return
	var e := {"fx": fx_key, "mat": mat, "shape": "", "actor": -1, "body": -1, "element": -1, "sub": 0, "move": "",
		"tier": 0, "pos": Vector3.ZERO, "dir": Vector3.ZERO, "radius": 0.0, "length": 0.0, "angle": 0.0,
		"height": 0.0, "dur": 0.0, "power": 0.0, "seed": (w.tick * 2654435761 + d.get("body", 0) * 97 + d.get("actor", 0)) & 0x7fffffff}
	e.merge(d, true)
	w.emit("fx", e)


## fx cue for an action (fills actor, element, sub, move, tier, pos, dir from the action).
static func fx_for(w: CombatWorld, a: ActorState, inst: ActionInst, fx_key: String, mat: String, d: Dictionary = {}) -> void:
	var base := {"actor": a.id, "element": inst.element, "sub": inst.sub, "move": inst.id, "tier": inst.tier(),
		"pos": a.hand_point(), "dir": inst.data.get("face", a.forward())}
	base.merge(d, true)
	fx(w, fx_key, mat, base)


static func charge(w: CombatWorld, a: ActorState, inst: ActionInst, tier: int, ready: bool) -> void:
	w.emit("charge", {"actor": a.id, "move": inst.id, "element": inst.element, "sub": inst.sub, "tier": tier,
		"ready": ready, "slot": inst.slot})


static func zone(w: CombatWorld, b: MatBody, phase: String) -> void:
	w.emit("zone", {"body": b.id, "kind": String(b.tag), "phase": phase, "radius": b.zone_radius, "owner": b.owner,
		"pos": b.pos, "tier": b.tier})


static func status(w: CombatWorld, a: ActorState, nm: String, on: bool, t: float, mag: float) -> void:
	w.emit("status", {"actor": a.id, "status": nm, "on": on, "t": t, "mag": mag})


## fx `mat` key for a body (material family + state).
static func mat_of(b: MatBody) -> String:
	match b.mat:
		Sim.Mat.STONE:
			if b.tag == &"obsidian":
				return "stone"
			return "magma" if b.liquid > 0.0 else "stone"
		Sim.Mat.WATER:
			if b.phase == Sim.Phase.FROZEN:
				return "ice"
			if b.form == Sim.Form.CLOUD or b.form == Sim.Form.ZONE:
				return "mist"
			return "water"
		Sim.Mat.STEAM:
			return "steam"
		Sim.Mat.METAL:
			return "metal"
		Sim.Mat.SAND:
			return "sand"
		Sim.Mat.GLASS:
			return "glass"
		Sim.Mat.PLANT:
			return "plant"
		Sim.Mat.FIRE:
			return "blue" if b.tag == &"comet" or b.props.get("blue", false) else "flame"
		Sim.Mat.AIR:
			match String(b.tag):
				"tornado", "twister", "eddy", "funnel", "vortex_wall":
					return "vortex"
				"vacuum_well", "null_bubble", "mine":
					return "vacuum"
				"tremor", "sound_barrier":
					return "sound"
			return "wind"
	return "stone"
