class_name LabCombos
extends RefCounted
## The combo trainer's data table (docs/MOVESET.md section 9.3): input steps and success events.
##
## A step names a move by (el, sub, slot) so renames never break it (Moves.resolve at run time):
##   el sub     Sim.Element / sub-element index      slot  strike thrust ground sweep guard push sink tech evade evade_hold
##   tier       charge tier the step must reach (0 = a tap / plain press; the step completes at the `charge` event)
##   kind       "move" (default) or "shape" (T+A tap while the technique is held: event `shape` or `split`)
##   hold       seconds the input is held (timing bar length; tech steps)
##   within     seconds after the previous step in which this one must start (default 4)
##   hint       the situation text ("rival raises a Bulwark first")
## `setup` (optional) is applied when the trainer starts: {"spawn": [{id, launch, params}], "player": {el, sub}}.
## `result` is {"any": [predicates], "within": s, "text"}: a predicate is a dictionary matched against sim events
## ({"type": "interaction", "outcome": "transform"}: every key must equal the event's field; "tier_min" style
## keys are not needed). No result = the combo is complete when its last step starts.

const E_ST := [0, 0]
const E_ME := [0, 1]
const E_SA := [0, 2]
const E_MG := [0, 3]
const W_WA := [1, 0]
const W_IC := [1, 1]
const W_MI := [1, 2]
const W_PL := [1, 3]
const F_FL := [2, 0]
const F_BL := [2, 1]
const F_LI := [2, 2]
const F_CO := [2, 3]
const A_GU := [3, 0]
const A_VO := [3, 1]
const A_VA := [3, 2]
const A_SO := [3, 3]


static func _s(sub: Array, slot: String, extra: Dictionary = {}) -> Dictionary:
	var d := {"el": sub[0], "sub": sub[1], "slot": slot, "tier": 0, "kind": "move", "hold": 0.0, "within": 4.0, "hint": ""}
	d.merge(extra, true)
	return d


static func _shape(sub: Array, extra: Dictionary = {}) -> Dictionary:
	return _s(sub, "tech", {"kind": "shape", "hint": "tap ATTACK while the technique is held"}.merged(extra, true))


static func _r(any: Array, text: String, within: float = 6.0) -> Dictionary:
	return {"any": any, "text": text, "within": within}


static func _ix(outcome: String, extra: Dictionary = {}) -> Dictionary:
	var d := {"type": "interaction", "outcome": outcome}
	d.merge(extra, true)
	return d


static var _table: Array[Dictionary] = []


static func all() -> Array[Dictionary]:
	if _table.is_empty():
		_table = _build()
	return _table


static func find(id: String) -> Dictionary:
	for c in all():
		if c.id == id:
			return c
	return {}


static func _c(n: int, id: String, name: String, desc: String, steps: Array, result: Dictionary, setup: Dictionary = {}) -> Dictionary:
	return {"n": n, "id": id, "name": name, "desc": desc, "steps": steps, "result": result, "setup": setup}


static func _build() -> Array[Dictionary]:
	var t: Array[Dictionary] = []
	t.append(_c(1, "melt_return", "Melt & Return", "The rival's wall face slumps into lava; the lava runs back at the builder.",
		[_s(F_BL, "tech", {"hold": 1.0, "hint": "hold Smelter on the wall face", "within": 8.0}), _s(E_MG, "ground", {"hint": "Magma Surge"})],
		_r([{"type": "slump"}, _ix("transform"), {"type": "transform", "to": "lava"}], "the wall slumps into lava and runs back"), {"spawn": [{"id": "ice_wall", "launch": false}]}))
	t.append(_c(2, "thunder_through", "Thunder Through Stone", "Storm Bolt (T2) blasts through a Bulwark: E 36 beats grounding 30.",
		[_s(F_LI, "strike", {"tier": 2, "hint": "hold ATTACK to T2, release at the wall", "within": 8.0})],
		_r([{"type": "shatter"}, {"type": "wall_crumble"}, _ix("shatter")], "the wall shatters, the bolt lands"), {"spawn": [{"id": "ice_wall", "launch": false}]}))
	t.append(_c(3, "split_return", "Split Return", "Seize the incoming stone, split it, release aimed: three spikes fan back.",
		[_s(E_ST, "tech", {"hold": 0.6, "hint": "TECH on the incoming stone", "within": 8.0}), _shape(E_ST, {"within": 3.0})],
		_r([{"type": "split"}, {"type": "shape"}], "three spikes fan back at the thrower"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	t.append(_c(4, "swallow", "Swallow", "Sink the stone into the ground just before impact (CP 22 >= 17).",
		[_s(E_ST, "sink", {"hint": "GUARD flick down just before impact", "within": 8.0})],
		_r([_ix("sink"), {"type": "sink"}], "the stone sinks into the ground"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	t.append(_c(5, "wave_back", "Wave Back", "Tidal Rush captures the incoming stone and carries it back.",
		[_s(W_WA, "ground", {"hint": "ATTACK flick down as the stone arrives", "within": 8.0})],
		_r([_ix("capture"), _ix("reclaim"), {"type": "capture"}], "the wave carries the stone back"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	t.append(_c(6, "molten_exchange", "Molten Exchange", "Magma-grip the stone, hold to molten, pour a lava wave at the thrower.",
		[_s(F_FL, "tech", {"hold": 1.4, "hint": "TECH on the stone, hold until molten, release aimed", "within": 8.0})],
		_r([{"type": "transform", "to": "wave"}, {"type": "transform", "to": "lava"}], "the stone becomes a lava wave"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	t.append(_c(7, "turn_the_wind", "Turn the Wind", "A perfect Wind Guard (27 >= 17) sends the stone back.",
		[_s(A_GU, "guard", {"hint": "tap GUARD just before contact", "within": 8.0})],
		_r([{"type": "perfect_deflect"}, _ix("redirect"), _ix("reflect")], "the stone returns to the thrower"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	t.append(_c(8, "live_wire", "Live Wire", "A wet target chains the Spark through the spray's puddles.",
		[_s(W_WA, "sweep", {"hint": "Spray Fan wets the target"}), _s(F_LI, "strike", {"hint": "Spark", "within": 5.0})],
		_r([{"type": "conduct"}], "the arc chains through the wet ground")))
	t.append(_c(9, "flash_freeze", "Flash Freeze Lock", "Wet, then frozen and rooted: the heave cannot be evaded.",
		[_s(W_WA, "sweep"), _s(W_IC, "sweep", {"hint": "Hoarfrost"}), _s(E_ST, "strike", {"tier": 1, "hint": "Heave at T1", "within": 5.0})],
		_r([{"type": "status", "status": "frozen"}, {"type": "hit"}], "the rooted target takes the heave")))
	t.append(_c(10, "glass_shrapnel", "Glass Shrapnel", "A bolt turns the Dune Wall to glass; a Sound Lance shatters it into shards.",
		[_s(E_SA, "guard", {"hint": "Dune Wall takes the bolt", "within": 8.0}), _s(A_SO, "thrust", {"hint": "Sound Lance into the glass", "within": 6.0})],
		_r([{"type": "shatter"}, _ix("shatter")], "the glass wall shatters into shards"), {"spawn": [{"id": "bolt", "launch": true}]}))
	t.append(_c(11, "fire_tornado", "Fire Tornado", "A Fireball fed into a tornado makes a fire tornado you steer.",
		[_s(A_VO, "tech", {"hold": 1.0, "hint": "Eye of the Storm", "within": 8.0}), _s(F_FL, "thrust", {"hint": "Fireball into it", "within": 6.0})],
		_r([_ix("amplify"), {"type": "convert"}], "the tornado ignites")))
	t.append(_c(12, "sandstorm", "Sandstorm", "A tornado through a grit cloud walks a sandstorm at the rival.",
		[_s(E_SA, "sweep", {"hint": "Veil of Grit"}), _s(A_VO, "strike", {"tier": 2, "hint": "Tornado through the cloud", "within": 6.0})],
		_r([_ix("amplify"), {"type": "zone", "kind": "sandstorm"}], "a blinding sandstorm walks forward")))
	t.append(_c(13, "rod_bait", "Lightning Rod Bait", "An embedded lance relays the bolt through the rival's wall.",
		[_s(E_ME, "thrust", {"hint": "Iron Lance into the wall"}), _s(F_LI, "tech", {"hold": 0.8, "hint": "Conductor's Hand on the lance", "within": 6.0})],
		_r([{"type": "conduct"}], "the bolt relays through the lance")))
	t.append(_c(14, "recall_pincer", "Recall Pincer", "Three discs past the rival fly back through them.",
		[_s(E_ME, "strike", {"tier": 2, "hint": "three discs past the rival"}), _s(E_ME, "tech", {"hint": "Recall with no target", "within": 5.0})],
		_r([{"type": "hit"}], "the discs come back through the rival")))
	t.append(_c(15, "obsidian_fort", "Obsidian Fort", "Water on your own Magma Curtain sets the face to obsidian: CP 28 to 38.",
		[_s(E_MG, "guard", {"hint": "Magma Curtain"}), _s(W_WA, "thrust", {"hint": "Water Bullet onto it", "within": 5.0})],
		_r([_ix("transform", {"to": "obsidian"}), {"type": "transform", "to": "obsidian"}], "the face sets to obsidian")))
	t.append(_c(16, "mud_bog", "Mud Bog", "Quicksand plus spray makes mud: a longer, stronger slow.",
		[_s(E_SA, "sink", {"hint": "Quicksand"}), _s(W_WA, "sweep", {"hint": "Spray into it", "within": 5.0})],
		_r([_ix("transform", {"to": "mud"}), {"type": "status", "status": "muddy"}], "the bog turns to mud")))
	t.append(_c(17, "no_air_no_fire", "No Air, No Fire", "The Null Bubble snuffs the rival's flames at the shell (x2).",
		[_s(A_VA, "guard", {"hint": "Null Bubble as the flames arrive", "within": 8.0})],
		_r([_ix("extinguish"), _ix("neutralize")], "the flames die at the shell"), {"spawn": [{"id": "flame_cone", "launch": true}]}))
	t.append(_c(18, "echo_bank", "Echo Bank Shot", "A Sound Lance off a pillar reflects into the rival behind cover.",
		[_s(A_SO, "thrust", {"hint": "aim at the pillar"})],
		_r([{"type": "hit"}, _ix("reflect")], "the pulse reflects into the rival")))
	t.append(_c(19, "geyser_launcher", "Geyser Launcher", "Geyser launch, wind juggle, ground finisher on landing.",
		[_s(W_MI, "strike", {"tier": 2, "hint": "Geyser"}), _s(A_GU, "thrust", {"hint": "Wind Crescent while airborne", "within": 3.0}),
			_s(E_ST, "ground", {"hint": "Rising Fangs on landing", "within": 3.0})],
		_r([{"type": "hit"}], "an air juggle into a ground finisher")))
	t.append(_c(20, "ember_wade", "Ember Wade", "Advance inside your own lava wave while the rival must evade it.",
		[_s(E_MG, "ground", {"hint": "Magma Surge"}), _s(E_MG, "evade_hold", {"hint": "Lava Wade behind the wave", "within": 3.0})],
		_r([{"type": "stance"}, {"type": "mode"}], "you wade behind the wave")))
	t.append(_c(21, "implosion_ignition", "Implosion Ignition", "Burst at the centre of a collapsing Vacuum Well: combustion x1.5 in the inrush.",
		[_s(A_VA, "tech", {"hold": 1.0, "hint": "Vacuum Well pulls the rival, release", "within": 8.0}), _s(F_CO, "strike", {"tier": 1, "hint": "Burst at the centre", "within": 4.0})],
		_r([{"type": "hit"}, _ix("amplify")], "the burst is amplified in the inrush")))
	t.append(_c(22, "ice_bridge", "Ice Bridge", "Skate across the pool; frozen water will not conduct the rival's bolt.",
		[_s(W_IC, "evade_hold", {"hint": "hold EVADE toward the pool"})],
		_r([{"type": "mode"}, {"type": "stance"}], "you skate across the pool")))
	t.append(_c(23, "quench_seize", "Quench & Seize", "Quench the lava wave to hot rock, then catch it without burns.",
		[_s(W_WA, "ground", {"hint": "Tidal Rush into the lava", "within": 8.0}), _s(E_MG, "tech", {"hold": 0.8, "hint": "Magma Hold on the hot rock", "within": 5.0})],
		_r([{"type": "control_won"}, {"type": "transform"}], "you hold the hot rock"), {"spawn": [{"id": "lava_wave", "launch": true}]}))
	t.append(_c(24, "vine_slingshot", "Vine Slingshot", "Vinegrip a far stone, wrap it, release: it roots the rival on hit.",
		[_s(W_PL, "tech", {"hold": 0.8, "hint": "Vinegrip on a stone", "within": 8.0}), _shape(W_PL, {"within": 3.0})],
		_r([{"type": "status", "status": "rooted"}, {"type": "shape"}], "the wrapped stone roots the rival")))
	t.append(_c(25, "static_return", "Static Return", "The Static Ward absorbs the bolt; Static Burst sends it back as a stun cone.",
		[_s(F_LI, "guard", {"hint": "Static Ward as the bolt arrives", "within": 8.0}), _s(F_LI, "push", {"hint": "Static Burst", "within": 4.0})],
		_r([{"type": "status", "status": "shocked"}, {"type": "hit"}], "the bolt returns as a stun cone"), {"spawn": [{"id": "bolt", "launch": true}]}))
	t.append(_c(26, "frost_shatter", "Frost Shatter", "Chill, frozen crust, then a Clap shatters it: bonus damage and stagger.",
		[_s(W_IC, "strike", {"hint": "Frost Shard"}), _s(W_IC, "sweep", {"hint": "Hoarfrost", "within": 4.0}), _s(A_SO, "strike", {"hint": "Clap", "within": 4.0})],
		_r([{"type": "shatter"}, _ix("shatter")], "the crust shatters")))
	t.append(_c(27, "reverse_tide", "Reverse Tide", "Magma Hold on the rival's lava wave (contest won), release it back.",
		[_s(E_MG, "tech", {"hold": 1.0, "hint": "Magma Hold on the wave", "within": 8.0})],
		_r([{"type": "control_won"}, {"type": "transform"}], "the wave turns back"), {"spawn": [{"id": "lava_wave", "launch": true}]}))
	t.append(_c(28, "catch_unleash", "Catch & Unleash", "The Vortex Wall captures the volley; Unleash flies every captured projectile back.",
		[_s(A_VO, "guard", {"hint": "Vortex Wall as the volley arrives", "within": 8.0}), _s(A_VO, "push", {"hint": "Unleash", "within": 4.0})],
		_r([{"type": "release_captured"}, _ix("capture")], "every captured projectile flies back"), {"spawn": [{"id": "stone_20", "launch": true}]}))
	return t


## Short input text of a step on `device` ("A[T2]" style names would be cryptic: use the move name).
static func step_text(step: Dictionary) -> String:
	Moves.ensure()
	var id := Moves.resolve(int(step.el), int(step.sub), String(step.slot))
	var nm := String((Moves.DEFS.get(id, {}) as Dictionary).get("name", id)).split(" / ")[0]
	if String(step.get("kind", "move")) == "shape":
		return "%s: shape (tap ATTACK)" % Sim.SUB_NAMES[int(step.el)][int(step.sub)]
	var t := ""
	if int(step.get("tier", 0)) > 0:
		t = " T%d" % int(step.tier)
	return "%s / %s: %s%s" % [Sim.ELEMENT_NAMES[int(step.el)], Sim.SUB_NAMES[int(step.el)][int(step.sub)], nm, t]


## Input text of a step for a device ("ATTACK flick down", "K + N" ...).
static func step_input(step: Dictionary, device: String) -> String:
	if String(step.get("kind", "move")) == "shape":
		return "tap ATTACK" if device == "touch" else ("J" if device == "keyboard" else "X")
	return MoveListData.input_text(String(step.slot), device)
