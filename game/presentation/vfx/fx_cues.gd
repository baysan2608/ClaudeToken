class_name FxCues
extends RefCounted
## The moveset half of FxDirector: turns the engine's cue events (docs/MOVESET.md §15.6) into pooled
## effects, sound and feedback, and keeps the per-fighter persistent cues (charge telegraphs, status
## visuals, stance / guard auras) alive while their state holds.
##
##   fx           cast / release / cone / beam / burst / ring / erupt / trail / splash / aura, by mat & shape
##   interaction  the outcome cues of §11.3 (block, deflect, reclaim, absorb, transform, shatter, sink,
##                ground / conduct, extinguish, amplify, weaken, overwhelm ...), perfect = flash + sting
##   charge       per element / sub tier telegraph (ChargeFX) + rising tone + a light haptic
##   status       burning, wet, frozen, shocked, blinded, rooted, concealed, anchored ...
##   zone         open / close puffs and rings; zone and body loops (one per body) in update()
##   clash, morph, chain, weave, counter_cancel, slump, convert, capture, ricochet, mode, stance
## Every sound name is from §12 (AudioDirector skips names it does not have yet).

const STING_GAP := 0.1                  # s between interaction stingers
const STATUS_TICK := 0.35               # s between periodic status puffs (drips, grit)

## threat / counter class -> material family (VfxPalette keys)
const CLS_MAT := {
	"stone": "stone", "stone_heavy": "stone", "boulder": "stone", "hot_rock": "magma", "magma": "magma",
	"lava_wave": "magma", "metal": "metal", "molten_metal": "metal", "sand": "sand", "sand_cloud": "sand",
	"sand_surge": "sand", "glass": "glass", "water": "water", "water_wave": "water", "puddle": "water", "pool": "water",
	"ice": "ice", "mist": "mist", "steam": "steam", "vine": "plant", "flame": "flame", "blue_fire": "blue",
	"fire_field": "flame", "ember": "blast", "lightning": "lightning", "blast": "blast", "gust": "wind",
	"tornado": "vortex", "vacuum": "vacuum", "sound": "sound", "frost": "ice",
	"guard_earth": "stone", "wall_stone": "stone", "wall_obsidian": "stone", "wall_glass": "glass",
	"wall_sand": "sand", "wall_mud": "stone", "wall_ice": "ice", "wall_vine": "plant", "plate_metal": "metal",
	"shield_water": "water", "screen_steam": "steam", "fog": "mist", "aura_flame": "flame", "aura_blue": "blue",
	"ward_static": "lightning", "guard_blast": "blast", "guard_wind": "wind", "wall_vortex": "vortex",
	"bubble_null": "vacuum", "barrier_sound": "sound", "spikes": "stone", "rod": "metal", "anchor": "stone",
	"swallow": "stone", "quicksand": "sand", "melt_pit": "magma", "heat_grip": "flame", "heat_ranged": "flame",
	"draw_heat": "flame", "freeze": "ice", "condense": "water", "wave_water": "water", "wave_sand": "sand",
	"wave_lava": "magma", "rime": "ice", "vacuum_well": "vacuum", "suction": "vacuum", "water_jet": "water",
	"spray": "water", "plate": "metal", "arena_wall": "stone", "ground": "stone",
}
## mat -> BurstFX style for impacts / puffs
const MAT_BURST := {
	"stone": "dust", "metal": "metal", "sand": "sand", "glass": "glass", "magma": "ember", "water": "water",
	"ice": "frost", "mist": "mist", "steam": "steam", "plant": "leaves", "flame": "ember", "blue": "blue_sparks",
	"lightning": "static", "blast": "ash", "wind": "dust", "vortex": "dust", "vacuum": "inflow", "sound": "dust",
}
## mat -> cast / release sound (§12 names, legacy fallbacks second)
const MAT_RELEASE_SFX := {
	"stone": "stone_launch", "metal": "metal_scrape", "sand": "sand_hiss", "glass": "glass_shatter",
	"magma": "magma_glob", "water": "water_whip", "ice": "frost_hiss", "mist": "steam_hiss", "steam": "steam_jet",
	"plant": "vine_creak", "flame": "fireball_whoosh", "blue": "blue_ignite", "lightning": "spark_snap",
	"blast": "explosion_small", "wind": "crescent_whoosh", "vortex": "crescent_whoosh", "vacuum": "vacuum_implode",
	"sound": "echo_ping",
}
## zone tag / body tag -> loop
const LOOPS := {
	"sand_cloud": "sandstorm_loop", "sandstorm": "sandstorm_loop", "quicksand": "quicksand_loop",
	"fire_field": "fire_field_loop", "tornado": "tornado_loop", "vortex_wall": "tornado_loop", "eddy": "tornado_loop",
	"vacuum_well": "vacuum_loop", "null_bubble": "vacuum_loop", "fog": "mist_loop", "mist": "mist_loop",
	"static_field": "static_crackle_loop", "corona": "blue_roar_loop", "disc": "disc_whirr_loop",
	"water_wave": "wave_rush_loop", "sand_surge": "sand_surge_loop", "rod": "rod_hum_loop", "geyser": "steam_jet",
	"flight_field": "flight_loop", "fuse": "fuse_tick",
}
## status -> {style, every}
const STATUS_FX := {
	"burning": "flames", "wet": "drip", "chilled": "frost", "frozen": "frost", "shocked": "crackle", "charged": "crackle",
	"blinded": "grit", "rooted": "vines", "concealed": "veil", "anchored": "dust", "armored": "aura", "muddy": "mud",
	"slowed": "mud", "levitating": "lift", "deafened": "ring",
	# kit statuses (docs/kits/*.md): reuse the families above or a periodic puff
	"overcharged": "crackle", "icegrip": "frostfeet", "windborne": "lift", "flight": "lift", "scalded": "steam",
	"fogbound": "fogpuff", "lava_wade": "embers",
}

var d: FxDirector
var _sting_t := 0.0
var _charges := {}          # actor id -> {node, tier, mat, move}
var _status := {}           # "actor:status" -> {node, t, style}
var _auras := {}            # actor id -> {node, mat}
var _loop_keys := {}        # loop key -> last pos (fade-out where the body died)
var _rng := RandomNumberGenerator.new()


func _init(director: FxDirector) -> void:
	d = director
	_rng.seed = 1234


func clear() -> void:
	for c in _charges.values():
		_release(c.node)
	for s in _status.values():
		_release(s.get("node"))
	for au in _auras.values():
		_release(au.node)
	_charges.clear()
	_status.clear()
	_auras.clear()
	_loop_keys.clear()


func _release(n: Variant) -> void:
	if n != null and is_instance_valid(n) and d.views and d.views.pool:
		d.views.pool.release(n)


func _fx(key: String) -> Node:
	return d._fx(key)


func _hold(key: String) -> Node:
	return d._hold(key)


func _sfx(nm: String, pos: Vector3, vol: float = 0.0) -> void:
	if d.sfx_log is Array:
		(d.sfx_log as Array).append(nm)
	if d.audio:
		d.audio.play(nm, pos, vol)


func _burst(pos: Vector3, normal: Vector3, strength: float, style: String) -> void:
	var b := _fx("burst")
	if b:
		b.call("play", pos, normal, strength, style)


func _ring(pos: Vector3, normal: Vector3, r0: float, r1: float, dur: float, mat: String, opts: Dictionary = {}) -> void:
	var r := _fx("ring")
	if r:
		r.call("play", pos, normal, r0, r1, dur, VfxPalette.color(mat), opts)


func _ground(p: Vector3) -> float:
	if d.world and d.world.arena:
		return d.world.arena.ground_height(p.x, p.z, p.y + 0.3)
	return 0.0


func _vec(e: Dictionary, k: String, def: Vector3 = Vector3.ZERO) -> Vector3:
	var v: Variant = e.get(k, def)
	return v if v is Vector3 else def


# ------------------------------------------------------------------------------------- fx cues

func fx_event(e: Dictionary) -> void:
	var key := String(e.get("fx", ""))
	var mat := String(e.get("mat", ""))
	var shape := String(e.get("shape", ""))
	var tier := int(e.get("tier", 0))
	var pos := _vec(e, "pos")
	var dir := _vec(e, "dir", Vector3.FORWARD)
	if dir.length_squared() < 1e-6:
		dir = Vector3.FORWARD
	dir = dir.normalized()
	var actor := int(e.get("actor", -1))
	var radius := float(e.get("radius", 0.0))
	var length := float(e.get("length", 0.0))
	var power := float(e.get("power", 0.0))
	var k := clampf(0.55 + 0.2 * float(tier), 0.4, 1.4)
	match key:
		"cast":
			_ring(pos, Vector3.BACK, 0.08, 0.35 + 0.08 * tier, 0.28, mat, {"billboard": true, "width": 0.08, "cover": 0.3, "glow": 1.3, "alpha": 0.7})
			if tier >= 2:
				_burst(pos, Vector3.UP, 0.4 * k, MAT_BURST.get(mat, "dust"))
		"release":
			_release_cue(mat, shape, pos, dir, k, actor)
		"cone":
			_cone(mat, pos, dir, maxf(length, 2.5), maxf(radius, 0.8), k, actor)
		"beam":
			_beam(e, mat, shape, pos, dir, maxf(length, 4.0), k)
		"burst":
			_burst_cue(mat, pos, maxf(radius, 1.0), k, power)
		"ring":
			var gy := _ground(pos)
			_ring(Vector3(pos.x, gy + 0.04, pos.z), Vector3.UP, 0.3, maxf(radius, 1.5), 0.45 + 0.1 * tier, mat,
				{"count": 2 if mat == "sound" or tier >= 2 else 1, "width": 0.09, "cover": 0.5, "glow": 1.3})
			if mat == "sound" or mat == "stone":
				_burst(Vector3(pos.x, gy, pos.z), Vector3.UP, 0.6 * k, "dust")
			_sfx("sonic_boom" if mat == "sound" and tier >= 2 else MAT_RELEASE_SFX.get(mat, "air_push"), pos)
		"erupt":
			_erupt(mat, pos, maxf(radius, 0.8), k, tier)
		"trail":
			_trail(mat, actor, dir)
		"splash":
			match mat:
				"water", "ice":
					d._call(_fx("splash"), "play", [pos, Vector3.UP, 0.6 * k])
				"steam", "mist":
					d._call(_fx("steam"), "play", [pos, 0.7 * k])
				_:
					_burst(pos, Vector3.UP, 0.7 * k, MAT_BURST.get(mat, "dust"))
		"aura":
			_aura(actor, mat, bool(e.get("on", true)))


func _release_cue(mat: String, shape: String, pos: Vector3, dir: Vector3, k: float, actor: int) -> void:
	match mat:
		"flame", "blue":
			if shape != "fireball" and shape != "comet":
				d._call(_fx("fire_burst"), "play", [pos, dir, 1.2 * k, 0.5 * k])
			_burst(pos, dir, 0.4 * k, "ember" if mat == "flame" else "blue_sparks")
		"lightning":
			_burst(pos, dir, 0.6 * k, "static")
		"wind", "vortex":
			d._call(_fx("air_push"), "play", [pos, dir, 0.6, 2.5 * k])
		"sand":
			_burst(pos, dir, 0.6 * k, "sand")
		"metal":
			_burst(pos, dir, 0.5 * k, "metal")
		"sound":
			_ring(pos + dir * 0.4, dir, 0.15, 0.9 * k, 0.3, "sound", {"width": 0.12, "cover": 0.3, "glow": 1.4})
		"vacuum":
			_ring(pos, Vector3.BACK, 0.6, 0.1, 0.25, "vacuum", {"billboard": true, "ease": "in", "cover": 0.5})
		"water", "ice", "mist", "steam":
			_burst(pos, dir, 0.45 * k, MAT_BURST.get(mat, "water"))
		_:
			_burst(pos, dir, 0.4 * k, MAT_BURST.get(mat, "dust"))
	_sfx(MAT_RELEASE_SFX.get(mat, "whoosh_light"), pos, -2.0)
	if k > 1.0:
		_sfx("whoosh_heavy", pos, -4.0)
	d._haptic("light", actor)


func _cone(mat: String, pos: Vector3, dir: Vector3, length: float, radius: float, k: float, actor: int) -> void:
	match mat:
		"flame":
			d._call(_fx("fire_burst"), "play", [pos, dir, length, k])
			_sfx("fire_release", pos)
		"blue":
			var fb := _fx("fire_burst")
			if fb and fb.has_method("set_blue"):
				fb.call("set_blue", true)
			d._call(fb, "play", [pos, dir, length, k])
			_sfx("blue_roar_loop" if false else "blue_ignite", pos)
		"wind", "vortex":
			d._call(_fx("air_push"), "play", [pos, dir, radius, length])
			_sfx("air_gust" if k > 0.9 else "air_push", pos)
		"sand":
			d._call(_fx("air_push"), "play", [pos, dir, radius, length])
			for i in 2:
				_burst(pos + dir * length * (0.3 + 0.35 * i), dir, 0.8 * k, "sand")
			_sfx("sand_burst", pos)
		"water":
			d._call(_fx("splash"), "play", [pos + dir * 0.8, dir, 0.9 * k])
			_burst(pos + dir * length * 0.5, dir, 0.7 * k, "water")
			_sfx("water_splash", pos)
		"steam", "mist":
			d._call(_fx("steam"), "play", [pos + dir * length * 0.3, k])
			_burst(pos + dir * length * 0.6, dir, 0.8 * k, mat)
			_sfx("steam_jet", pos)
		"ice":
			_burst(pos + dir * length * 0.4, dir, 0.9 * k, "frost")
			_sfx("frost_hiss", pos)
		"sound":
			for i in 2:
				_ring(pos + dir * (0.6 + 1.2 * i), dir, 0.3 + 0.4 * i, radius * (0.8 + 0.6 * i), 0.35, "sound",
					{"width": 0.1, "cover": 0.3, "glow": 1.2})
			_sfx("roar_wave" if k > 0.9 else "echo_ping", pos)
		"vacuum":
			_burst(pos + dir * length * 0.5, -dir, k, "inflow")
			_sfx("vacuum_implode", pos)
		"lightning":
			var tip := pos + dir * length
			d._call(_fx("lightning_arc"), "strike", [PackedVector3Array([pos, pos.lerp(tip, 0.5) + Vector3(0, 0.3, 0), tip]), _rng.randi()])
			_sfx("spark_snap", pos)
		_:
			_burst(pos + dir, dir, 0.7 * k, MAT_BURST.get(mat, "dust"))
	d._haptic("light", actor)


func _beam(e: Dictionary, mat: String, shape: String, pos: Vector3, dir: Vector3, length: float, k: float) -> void:
	var path: Variant = e.get("path", null)
	var tip := pos + dir * length
	if path is PackedVector3Array and (path as PackedVector3Array).size() >= 2:
		tip = (path as PackedVector3Array)[(path as PackedVector3Array).size() - 1]
	match mat:
		"lightning":
			var pts := PackedVector3Array()
			if shape == "down":
				# skybreak: a vertical bolt from the sky onto the point
				pts = PackedVector3Array([pos + Vector3(_rng.randf_range(-1, 1), 14.0, _rng.randf_range(-1, 1)), pos + Vector3(0, 6.0, 0), pos])
				_ring(Vector3(pos.x, _ground(pos) + 0.05, pos.z), Vector3.UP, 0.3, 2.5, 0.4, "lightning", {"cover": 0.2, "glow": 3.0})
				_burst(pos, Vector3.UP, 1.2, "static")
				_sfx("thunderclap", pos)
				d.feel("t3", pos, Vector3.ZERO, -1)
			elif path is PackedVector3Array and (path as PackedVector3Array).size() >= 2:
				pts = path
			else:
				pts = PackedVector3Array([pos, pos.lerp(tip, 0.5), tip])
			d._call(_fx("lightning_arc"), "strike", [pts, _rng.randi()])
			if shape != "down":
				_sfx("lightning_strike", tip, -2.0)
			if d.hud and d.settings and d.settings.flashes > 0.05:
				d.hud.call("flash", "lightning")
		"blue", "flame", "sand", "water", "sound", "vacuum":
			var style := mat
			if mat == "blue" and (shape == "needles" or shape == "lance"):
				style = "needle"
			var bm := _fx("beam")
			if bm:
				bm.call("play", pos, tip, 0.3 + 0.12 * k, style)
			_sfx({"blue": "blue_ignite", "flame": "fire_release", "sand": "sand_hiss", "water": "water_jet_loop",
				"sound": "sonic_boom", "vacuum": "vacuum_implode"}[mat], pos, -2.0)
			if mat == "blue":
				_burst(tip, -dir, 0.6 * k, "blue_sparks")
		_:
			_burst(tip, -dir, 0.6 * k, MAT_BURST.get(mat, "dust"))


func _burst_cue(mat: String, pos: Vector3, radius: float, k: float, power: float) -> void:
	var gy := _ground(pos)
	match mat:
		"blast", "flame", "blue":
			if mat == "blast" or radius >= 1.2:
				var bl := _fx("blast")
				if bl:
					bl.call("play", pos, radius, clampf(0.6 + power / 40.0, 0.5, 1.4), mat == "blue")
					bl.call("set_ground", gy)
				_burst(pos, Vector3.UP, k, "ember")
				_sfx("explosion_large" if radius >= 2.0 or power >= 30.0 else "explosion_small", pos)
				d.feel("boom", pos, Vector3.ZERO, -1, radius)
			else:
				d._call(_fx("fire_burst"), "play", [pos, Vector3.UP, radius * 1.5, k])
				_sfx("fire_ignite", pos)
		"sand", "stone":
			_burst(pos, Vector3.UP, k, MAT_BURST[mat])
			_burst(pos, Vector3.UP, k * 0.8, "grit" if mat == "sand" else "dust")
			var sh := _fx("shards")
			if sh and mat == "stone":
				sh.call("play", pos, Vector3.UP, k, "stone", _rng.randi() % 997, gy)
			_ring(Vector3(pos.x, gy + 0.04, pos.z), Vector3.UP, 0.3, radius * 1.3, 0.4, mat, {"cover": 0.6, "glow": 1.0})
			_sfx("sand_burst" if mat == "sand" else "stone_impact_1", pos)
		"water":
			d._call(_fx("splash"), "play", [pos, Vector3.UP, k])
			_ring(Vector3(pos.x, gy + 0.04, pos.z), Vector3.UP, 0.3, radius * 1.2, 0.4, "water", {"cover": 0.5})
			_sfx("water_splash", pos)
		"steam", "mist":
			d._call(_fx("steam"), "play", [pos, k])
			_burst(pos, Vector3.UP, k, mat)
			_sfx("steam_jet", pos)
		"ice", "glass", "metal":
			var sh2 := _fx("shards")
			if sh2:
				sh2.call("play", pos, Vector3.UP, k, mat, _rng.randi() % 997, gy)
			_burst(pos, Vector3.UP, k * 0.7, MAT_BURST[mat])
			_sfx({"ice": "ice_shatter", "glass": "glass_shatter", "metal": "metal_clang"}[mat], pos)
		"sound":
			for i in 2:
				_ring(Vector3(pos.x, gy + 0.05, pos.z), Vector3.UP, 0.3 + 0.3 * i, radius * (1.0 + 0.4 * i), 0.4, "sound", {"cover": 0.4})
			_burst(Vector3(pos.x, gy, pos.z), Vector3.UP, k, "dust")
			_sfx("sonic_boom", pos)
		"vacuum":
			_ring(pos, Vector3.BACK, radius, 0.1, 0.3, "vacuum", {"billboard": true, "ease": "in", "cover": 0.6, "glow": 1.5})
			_burst(pos, Vector3.UP, k, "inflow")
			_sfx("vacuum_implode", pos)
		"lightning":
			for i in 3:
				var a := TAU * float(i) / 3.0 + _rng.randf()
				var tip := pos + Vector3(cos(a) * radius, -pos.y + gy + 0.05, sin(a) * radius)
				d._call(_fx("lightning_arc"), "strike", [PackedVector3Array([pos, pos.lerp(tip, 0.5) + Vector3(0, 0.3, 0), tip]), _rng.randi()])
			_sfx("thunderclap", pos, -3.0)
		_:
			_burst(pos, Vector3.UP, k, MAT_BURST.get(mat, "dust"))


func _erupt(mat: String, pos: Vector3, radius: float, k: float, tier: int) -> void:
	var gy := _ground(pos)
	var g := Vector3(pos.x, gy, pos.z)
	match mat:
		"stone", "magma":
			_burst(g, Vector3.UP, k, "dust")
			var sh := _fx("shards")
			if sh:
				sh.call("play", g + Vector3(0, 0.1, 0), Vector3.UP, k, "stone", _rng.randi() % 997, gy)
			if mat == "magma":
				d._call(_fx("ember"), "play", [g, Vector3.UP, k])
				_sfx("magma_surge", pos)
			else:
				_sfx("wall_raise", pos)
		"water", "steam", "mist":
			d._call(_fx("splash"), "play", [g, Vector3.UP, 1.2 * k])
			d._call(_fx("steam"), "play", [g + Vector3(0, 0.3, 0), k])
			_sfx("geyser", pos)
		"ice":
			var sh2 := _fx("shards")
			if sh2:
				sh2.call("play", g + Vector3(0, 0.1, 0), Vector3.UP, k, "ice", _rng.randi() % 997, gy)
			_burst(g, Vector3.UP, k, "frost")
			_sfx("ice_wall_raise", pos)
		"flame", "blue":
			var fb := _fx("fire_burst")
			if fb and fb.has_method("set_blue"):
				fb.call("set_blue", mat == "blue")
			d._call(fb, "play", [g, Vector3.UP, 2.0 + 0.5 * tier, k])
			_sfx("fire_release", pos)
		"plant":
			_burst(g, Vector3.UP, k, "leaves")
			_sfx("vine_creak", pos)
		"sand":
			_burst(g, Vector3.UP, k, "sand")
			_sfx("sand_burst", pos)
		_:
			_burst(g, Vector3.UP, k, MAT_BURST.get(mat, "dust"))
	_ring(g + Vector3(0, 0.04, 0), Vector3.UP, 0.2, radius * 1.4, 0.35, mat, {"cover": 0.6, "glow": 1.0})


func _trail(mat: String, actor: int, dir: Vector3) -> void:
	if mat in ["flame", "blue", "blast"]:
		var a := d.world.get_actor(actor) if d.world else null
		if a:
			d._call(_fx("ember"), "play", [a.pos, -dir + Vector3.UP * 0.3, 0.7])
			var fb := _fx("fire_burst")
			if fb and fb.has_method("set_blue"):
				fb.call("set_blue", mat == "blue")
			d._call(fb, "play", [a.pos + Vector3(0, 0.3, 0), (-dir + Vector3.DOWN * 0.5).normalized(), 1.2, 0.6])
	d._dash_trail(actor, dir)


## Stance / guard aura on (or off) for an actor (one per actor, replaced on a new mat).
func _aura(actor: int, mat: String, on: bool) -> void:
	var cur: Dictionary = _auras.get(actor, {})
	if not on:
		if not cur.is_empty():
			_release(cur.node)
			_auras.erase(actor)
		return
	if not cur.is_empty() and cur.mat == mat:
		return
	if not cur.is_empty():
		_release(cur.node)
	var n := _hold("shell")
	if n == null:
		return
	n.call("configure", "aura", VfxPalette.color(mat), actor)
	n.call("set_shape", 0.75, 1.3)
	_auras[actor] = {"node": n, "mat": mat}


# ------------------------------------------------------------------------------- interactions

func interaction(e: Dictionary) -> void:
	var outcome := String(e.get("outcome", ""))
	var pos := _vec(e, "pos")
	var dir := _vec(e, "dir", Vector3.FORWARD)
	var tm := String(CLS_MAT.get(String(e.get("threat", "")), _body_mat(int(e.get("threat_body", -1)), "stone")))
	var cm := String(CLS_MAT.get(String(e.get("counter", "")), _body_mat(int(e.get("counter_body", -1)), _actor_mat(int(e.get("counter_actor", -1))))))
	var perfect := bool(e.get("perfect", false))
	var tp := float(e.get("tp", 0.0))
	var ca := int(e.get("counter_actor", -1))
	var ta := int(e.get("threat_actor", -1))
	var to := String(e.get("to", ""))
	var small := String(e.get("band", "")) == "partial"
	var st := 0.5 if small else 1.0
	var stinger := ""
	match outcome:
		"block":
			_burst(pos, -dir, 0.5 * st, MAT_BURST.get(tm, "dust"))
			stinger = "metal_clang" if tm == "metal" or cm == "metal" else ""
			d.hitstop(4 if tp >= 25.0 else 2)
			d.feel("block_heavy" if tp >= 25.0 else "block", pos, dir, ca)
		"deflect", "redirect", "reflect":
			var nd := -dir if outcome == "reflect" else dir.cross(Vector3.UP).normalized()
			_ring(pos, Vector3.BACK, 0.1, 0.7, 0.25, cm, {"billboard": true, "cover": 0.1, "glow": 3.0, "width": 0.1})
			_burst(pos, nd, 0.7, "sparks" if tm in ["stone", "metal", "glass"] else MAT_BURST.get(tm, "sparks"))
			stinger = "deflect"
			d.hitstop(3)
			d._haptic("deflect", ca)
		"reclaim":
			_ring(pos, Vector3.BACK, 0.5, 0.15, 0.35, cm, {"billboard": true, "ease": "in", "cover": 0.2, "glow": 2.5})
			stinger = "metal_recall" if tm == "metal" else "stone_rip"
			d._haptic("counter", ca)
		"absorb", "capture":
			_ring(pos, Vector3.BACK, 0.9, 0.1, 0.4, cm, {"billboard": true, "ease": "in", "cover": 0.3, "glow": 1.8, "count": 2})
			_burst(pos, Vector3.UP, 0.6 * st, "inflow" if cm == "vacuum" else MAT_BURST.get(tm, "mist"))
			stinger = "vacuum_implode" if cm == "vacuum" else "water_splash"
		"transform":
			_transform_cue(to, pos, st)
			stinger = {"steam": "steam_hiss", "glass": "glass_fuse", "ice": "frost_hiss", "snow": "frost_hiss",
				"rock": "lava_hiss_quench", "obsidian": "obsidian_set", "lava": "magma_surge", "molten_metal": "magma_surge",
				"mud": "water_splash", "ash": "vine_burn", "water": "ice_melt_drip", "mist": "steam_hiss",
				"hot_rock": "melt_rise", "sandstone": "sand_burst"}.get(to, "melt_rise")
			if not small:
				d.feel("transform", pos, Vector3.ZERO, ca)
		"shatter":
			var sh := _fx("shards")
			if sh:
				sh.call("play", pos, -dir + Vector3.UP * 0.5, st, tm if tm in ["ice", "glass", "metal", "plant"] else "stone", _rng.randi() % 997, _ground(pos))
			_burst(pos, Vector3.UP, 0.5 * st, MAT_BURST.get(tm, "dust"))
			stinger = "glass_shatter" if tm == "glass" else ("ice_shatter" if tm == "ice" else "stone_impact_2")
			d.hitstop(3)
			d.feel("shatter", pos, dir, ca)
		"sink":
			var gy := _ground(pos)
			_burst(Vector3(pos.x, gy, pos.z), Vector3.UP, 0.8 * st, "dust" if tm != "sand" else "sand")
			_ring(Vector3(pos.x, gy + 0.04, pos.z), Vector3.UP, 1.0, 0.2, 0.35, "stone", {"ease": "in", "cover": 0.7, "glow": 0.8})
			stinger = "stone_impact_1"
		"ground", "conduct":
			var gp := Vector3(pos.x, _ground(pos), pos.z)
			var tgt := gp
			if outcome == "conduct":
				var cb := d.world.get_body(int(e.get("counter_body", -1))) if d.world else null
				if cb:
					tgt = cb.pos
			d._call(_fx("lightning_arc"), "strike", [PackedVector3Array([pos, pos.lerp(tgt, 0.5) + Vector3(0.2, 0.1, 0), tgt]), _rng.randi()])
			stinger = "conduct_buzz"
		"extinguish", "neutralize", "disperse":
			_burst(pos, Vector3.UP, 0.8 * st, "smoke" if tm in ["flame", "blue", "blast", "magma"] else "mist")
			stinger = "steam_hiss" if tm in ["flame", "blue"] else ""
		"amplify":
			var fb := _fx("fire_burst")
			if fb and fb.has_method("set_blue"):
				fb.call("set_blue", tm == "blue")
			d._call(fb, "play", [pos, Vector3.UP, 2.2, 1.0])
			stinger = "fire_ignite"
		"weaken", "bend", "slow":
			_burst(pos, -dir, 0.35, MAT_BURST.get(cm, "dust"))
			_ring(pos, Vector3.BACK, 0.1, 0.35, 0.2, cm, {"billboard": true, "cover": 0.2, "glow": 1.5})
		"overwhelm":
			_break_cue(cm, pos, dir)
			stinger = "counter_fail"
			d.hitstop(4)
		"clash":
			pass   # the "clash" event carries the cue
		"disrupt":
			_ring(pos, Vector3.BACK, 0.1, 0.8, 0.3, "sound", {"billboard": true, "count": 2, "cover": 0.2})
			stinger = "echo_ping"
		"heat":
			d._call(_fx("ember"), "play", [pos, Vector3.UP, 0.5])
		"push":
			_burst(pos, dir, 0.5, "dust")
	if perfect:
		stinger = "counter_success"
		d.hitstop(6)
		d.feel("perfect", pos, dir, ca)
		_ring(pos, Vector3.BACK, 0.1, 1.1, 0.3, cm, {"billboard": true, "cover": 0.0, "glow": 4.0, "count": 2})
		if d.hud and (ca == d.player_id or ta == d.player_id) and (d.settings == null or d.settings.flashes > 0.05):
			d.hud.call("flash", "perfect")
	elif outcome in ["block", "deflect", "redirect", "reflect", "reclaim", "absorb", "capture", "transform", "shatter",
			"sink", "ground", "extinguish", "neutralize"] and not small:
		if ca == d.player_id:
			d._haptic("counter" if outcome != "block" else "block", ca)
	if stinger != "" and _sting_t <= 0.0:
		_sting_t = STING_GAP
		_sfx(stinger, pos)


func _transform_cue(to: String, pos: Vector3, st: float) -> void:
	match to:
		"steam", "mist":
			d._call(_fx("steam"), "play", [pos, st])
			_burst(pos, Vector3.UP, 0.7 * st, "steam")
		"glass":
			_burst(pos, Vector3.UP, 0.7 * st, "glass")
			d._call(_fx("ember"), "play", [pos, Vector3.UP, 0.5 * st])
		"ice", "snow", "rime":
			_burst(pos, Vector3.UP, 0.8 * st, "frost")
		"rock", "obsidian", "sandstone":
			d._call(_fx("steam"), "play", [pos + Vector3(0, 0.2, 0), 0.5 * st])
			_burst(pos, Vector3.UP, 0.5 * st, "dust")
		"lava", "molten_metal", "hot_rock":
			d._call(_fx("ember"), "play", [pos, Vector3.UP, st])
			_burst(pos, Vector3.UP, 0.4 * st, "ember")
		"mud":
			_burst(pos, Vector3.UP, 0.6 * st, "dust")
		"ash":
			_burst(pos, Vector3.UP, 0.8 * st, "ash")
		"water":
			d._call(_fx("splash"), "play", [pos, Vector3.UP, 0.4 * st])
		_:
			_burst(pos, Vector3.UP, 0.5 * st, "dust")


## A counter that broke (overwhelm / fail): its own break effect.
func _break_cue(cm: String, pos: Vector3, dir: Vector3) -> void:
	match cm:
		"stone", "sand", "metal":
			_burst(pos, dir, 1.2, "dust" if cm != "sand" else "sand")
			_sfx("wall_crumble", pos)
		"water":
			d._call(_fx("splash"), "play", [pos, dir, 0.9])
		"ice", "glass":
			var sh := _fx("shards")
			if sh:
				sh.call("play", pos, dir, 1.0, cm, _rng.randi() % 997, _ground(pos))
		"plant":
			_burst(pos, dir, 1.0, "leaves")
			_sfx("vine_snap", pos)
		_:
			_burst(pos, dir, 0.8, MAT_BURST.get(cm, "dust"))
			_ring(pos, Vector3.BACK, 0.8, 0.2, 0.25, cm, {"billboard": true, "cover": 0.4})


func _body_mat(id: int, fallback: String) -> String:
	var b := d.world.get_body(id) if d.world and id >= 0 else null
	return FxEvents.mat_of(b) if b else fallback


func _actor_mat(id: int) -> String:
	var a := d.world.get_actor(id) if d.world and id >= 0 else null
	return VfxPalette.mat_of(a.element, a.sub()) if a else "wind"


# ---------------------------------------------------------------------------------- charge / status

func charge(e: Dictionary) -> void:
	var a := int(e.get("actor", -1))
	var tier := int(e.get("tier", 0))
	var mat := VfxPalette.mat_of(int(e.get("element", 0)), int(e.get("sub", 0)))
	if tier < 1:
		return
	var c: Dictionary = _charges.get(a, {})
	if c.is_empty():
		var n := _hold("charge")
		if n == null:
			return
		c = {"node": n}
		_charges[a] = c
	c.tier = tier
	c.mat = mat
	c.move = String(e.get("move", ""))
	var pos := d._hand(a)
	_sfx("charge_t%d" % clampi(tier, 1, 3), pos, -3.0)
	if tier >= 3:
		_sfx("perfect_deflect", pos, -12.0)   # the T3 sting until a dedicated one exists
	d._haptic("charge", a)


func status(e: Dictionary) -> void:
	var a := int(e.get("actor", -1))
	var nm := String(e.get("status", ""))
	var key := "%d:%s" % [a, nm]
	var on := bool(e.get("on", true))
	var style := String(STATUS_FX.get(nm, ""))
	var cur: Dictionary = _status.get(key, {})
	if not on:
		if not cur.is_empty():
			_release(cur.get("node"))
			_status.erase(key)
		return
	if style == "" or not cur.is_empty():
		return
	var ac := d.world.get_actor(a) if d.world else null
	if ac == null:
		return
	var n: Node = null
	match style:
		"flames":
			n = _hold("flame_field")
			if n:
				n.set("use_light", false)
				n.call("setup", "burning", a, 0.32, 0.5)
		"frost":
			n = _hold("shell")
			if n:
				n.call("configure", "frost")
				n.call("set_shape", 0.5, 1.0 if nm == "frozen" else 0.6)
			_burst(ac.pos + Vector3(0, 0.4, 0), Vector3.UP, 0.7, "frost")
			_sfx("frost_hiss", ac.pos)
		"crackle":
			n = _hold("crackle")
			if n:
				n.call("setup", "actor", 0.6, a)
			_sfx("spark_snap", ac.chest())
		"vines":
			n = _hold("vine")
			if n:
				n.call("setup", "rooted", a, Vector3.ONE * 0.38)
			_sfx("vine_creak", ac.pos)
		"veil":
			n = _hold("cloud")
			if n:
				n.call("configure", "veil", a)
				n.call("set_shape", 0.7, 1.9)
		"aura":
			n = _hold("shell")
			if n:
				n.call("configure", "aura", VfxPalette.color("stone"), a)
				n.call("set_shape", 0.7, 1.3)
		"dust":
			_burst(ac.pos, Vector3.UP, 0.8, "dust")
			_ring(ac.pos + Vector3(0, 0.04, 0), Vector3.UP, 0.3, 1.2, 0.4, "stone", {"cover": 0.6, "glow": 0.8})
		"ring":
			_ring(ac.chest() + Vector3(0, 0.6, 0), Vector3.BACK, 0.1, 0.6, 0.3, "sound", {"billboard": true, "count": 2})
		"frostfeet":
			_ring(ac.pos + Vector3(0, 0.03, 0), Vector3.UP, 0.15, 0.7, 0.35, "ice", {"cover": 0.7, "glow": 1.0})
			_burst(ac.pos, Vector3.UP, 0.5, "frost")
			_sfx("ice_crack", ac.pos, -4.0)
		"steam":
			_burst(ac.chest(), Vector3.UP, 0.4, "steam")
			_sfx("steam_hiss", ac.chest(), -6.0)
	_status[key] = {"node": n, "t": 0.0, "style": style, "actor": a}


# --------------------------------------------------------------------------------- zones & misc

func zone(e: Dictionary) -> void:
	var kind := String(e.get("kind", ""))
	var pos := _vec(e, "pos")
	var r := float(e.get("radius", 1.0))
	var open := String(e.get("phase", "open")) == "open"
	var gy := _ground(pos)
	var g := Vector3(pos.x, gy, pos.z)
	var mat := "wind"
	var style := "dust"
	match kind:
		"sand_cloud", "sandstorm", "quicksand":
			mat = "sand"
			style = "sand"
		"fog", "mist", "steam", "steam_screen", "geyser":
			mat = "mist"
			style = "mist" if kind != "geyser" else "steam"
		"fire_field", "melt_pit", "lava_pool", "mine", "fuse":
			mat = "flame"
			style = "ember"
		"ice_floor":
			mat = "ice"
			style = "frost"
		"vacuum_well", "null_bubble":
			mat = "vacuum"
			style = "inflow"
		"static_field", "corona":
			mat = "lightning" if kind == "static_field" else "blue"
			style = "static"
		"sound_barrier":
			mat = "sound"
		"briar":
			mat = "plant"
			style = "leaves"
		"caltrops":
			mat = "metal"
			style = "metal"
	if open:
		_ring(g + Vector3(0, 0.04, 0), Vector3.UP, 0.2, maxf(r, 0.5), 0.4, mat, {"cover": 0.5, "glow": 1.2})
		_burst(g, Vector3.UP, clampf(r / 2.0, 0.4, 1.2), style)
		if int(e.get("owner", -1)) == d.player_id:
			d._haptic("zone", d.player_id)
	else:
		_burst(g, Vector3.UP, clampf(r / 2.5, 0.3, 1.0), "smoke" if mat == "flame" else ("mist" if mat in ["mist", "ice", "vacuum"] else style))


func clash(e: Dictionary) -> void:
	var pos := _vec(e, "pos")
	var mat := String(e.get("mat", "stone"))
	var energy := mat in ["flame", "blue", "lightning", "blast", "vacuum", "sound", "wind", "vortex"]
	_burst(pos, Vector3.UP, 0.8, "sparks" if not energy else MAT_BURST.get(mat, "static"))
	_ring(pos, Vector3.BACK, 0.1, 0.8, 0.22, mat, {"billboard": true, "cover": 0.1, "glow": 3.0})
	_sfx("clash_energy" if energy else "clash_solid", pos)
	d.hitstop(4)
	d.feel("clash", pos, Vector3.ZERO, -1)


func misc(e: Dictionary) -> void:
	var a := int(e.get("actor", -1))
	match String(e.type):
		"morph", "chain":
			var mat := _actor_mat(a)
			_ring(d._hand(a), Vector3.BACK, 0.06, 0.3, 0.18, mat, {"billboard": true, "cover": 0.1, "glow": 2.0})
		"weave":
			_ring(d._hand(a), Vector3.BACK, 0.06, 0.5, 0.25, _actor_mat(a), {"billboard": true, "cover": 0.1, "glow": 2.5, "count": 2})
			_sfx("charge_t1", d._hand(a), -6.0)
		"counter_cancel":
			_ring(d._apos(a), Vector3.BACK, 0.2, 1.0, 0.25, _actor_mat(a), {"billboard": true, "cover": 0.0, "glow": 3.0})
			d.hitstop(2)
		"slump":
			var p := d._bpos(int(e.get("body", -1)))
			d._call(_fx("ember"), "play", [p, Vector3.UP, 1.0])
			_burst(p, Vector3.UP, 0.8, "smoke")
			_sfx("melt_rise", p)
			d.feel("transform", p, Vector3.ZERO, a)
		"convert":
			var p2 := d._bpos(int(e.get("body", -1)))
			var to := String(e.get("to", ""))
			_transform_cue(to.to_lower(), p2, 1.0)
			if to.to_lower() == "glass":
				_sfx("glass_fuse", p2)
				d.feel("transform", p2, Vector3.ZERO, -1)
		"capture":
			_ring(d._bpos(int(e.get("body", -1))), Vector3.BACK, 0.6, 0.1, 0.3, "vortex", {"billboard": true, "ease": "in", "cover": 0.3})
		"ricochet":
			var at := _vec(e, "at")
			_burst(at, _vec(e, "dir", Vector3.UP), 0.6, "metal")
			_sfx("metal_clang", at)
		"inrush":
			# Air slams back into a collapsed vacuum: rings closing in + an inward puff.
			var ip := _vec(e, "pos")
			var g := Vector3(ip.x, _ground(ip), ip.z)
			var rr := maxf(float(e.get("radius", 1.5)), 0.6)
			_ring(g + Vector3(0, 0.05, 0), Vector3.UP, rr * 1.2, 0.15, 0.32, "vacuum", {"ease": "in", "cover": 0.55, "glow": 1.4, "count": 2})
			_burst(g + Vector3(0, 0.6, 0), Vector3.UP, clampf(rr / 2.0, 0.5, 1.2), "inflow")
			_sfx("vacuum_implode", g, -3.0)
		"extinguish":
			var xp := d._bpos(int(e.get("body", -1)))
			_burst(xp, Vector3.UP, 0.7, "smoke")
			_sfx("lava_hiss_quench" if String(e.get("by", "")) != "time" else "steam_hiss", xp, -6.0)
		"current_grounded":
			var cg := _vec(e, "at")
			_burst(cg, Vector3.UP, 0.6, "static")
			_ring(Vector3(cg.x, _ground(cg) + 0.04, cg.z), Vector3.UP, 0.1, 0.9, 0.25, "lightning", {"cover": 0.3, "glow": 2.5})
		"fork":
			_burst(_vec(e, "at"), Vector3.UP, 0.5, "static")
		"stick":
			var sp := d._bpos(int(e.get("body", -1)))
			_burst(sp, Vector3.UP, 0.35, MAT_BURST.get(_body_mat(int(e.get("body", -1)), "stone"), "dust"))
		"stance", "mode":
			var on := bool(e.get("on", true))
			_aura(a, _actor_mat(a), on and String(e.get("stance", e.get("kind", ""))) != "")


# ------------------------------------------------------------------------------- per frame

func update(dt: float) -> void:
	_sting_t = maxf(0.0, _sting_t - dt)
	if d.world == null:
		return
	# charge telegraphs: live while the action charges / channels
	for a_id in _charges.keys():
		var c: Dictionary = _charges[a_id]
		var a := d.world.get_actor(a_id)
		var inst := a.action if a else null
		var holding := inst != null and (inst.phase == ActionInst.P.CHARGE or inst.phase == ActionInst.P.CHANNEL) and a.stun <= 0.0
		if not holding or not is_instance_valid(c.node):
			_release(c.node)
			_charges.erase(a_id)
			continue
		var n: Node = c.node
		n.call("set_anchor", d._hand(a_id), a.pos)
		n.call("set_charge", int(c.tier), Charge.progress(inst).y, String(c.mat))
	# status visuals follow their fighter; periodic puffs
	for key in _status.keys():
		var s: Dictionary = _status[key]
		var a2 := d.world.get_actor(int(s.actor))
		if a2 == null:
			_release(s.get("node"))
			_status.erase(key)
			continue
		var n2: Node3D = s.get("node")
		if n2 != null and is_instance_valid(n2):
			match String(s.style):
				"crackle":
					n2.call("set_target", a2.pos)
					n2.call("set_intensity", 0.8)
				"veil", "flames", "vines":
					n2.position = a2.pos
				"frost", "aura":
					n2.position = a2.pos + Vector3(0, 0.6 if s.style == "frost" else 0.95, 0)
		s.t = float(s.t) + dt
		if float(s.t) >= STATUS_TICK:
			s.t = 0.0
			match String(s.style):
				"drip":
					_burst(a2.pos + Vector3(0, 0.9, 0), Vector3.DOWN, 0.15, "water")
				"grit":
					_burst(a2.chest() + Vector3(0, 0.45, 0), Vector3.UP, 0.25, "grit")
				"mud":
					_burst(a2.pos, Vector3.UP, 0.2, "dust")
				"lift":
					_ring(a2.pos + Vector3(0, 0.05, 0), Vector3.UP, 0.2, 0.8, 0.35, "wind", {"cover": 0.4})
				"frostfeet":
					_burst(a2.pos, Vector3.UP, 0.2, "frost")
				"steam":
					_burst(a2.chest(), Vector3.UP, 0.22, "steam")
				"fogpuff":
					_burst(a2.chest(), Vector3.UP, 0.25, "mist")
				"embers":
					_burst(a2.pos, Vector3.UP, 0.3, "ember")
	# stance / guard auras follow their fighter
	for a_id in _auras.keys():
		var au: Dictionary = _auras[a_id]
		var a3 := d.world.get_actor(a_id)
		if a3 == null or not is_instance_valid(au.node):
			_release(au.node)
			_auras.erase(a_id)
			continue
		(au.node as Node3D).position = a3.pos + Vector3(0, 0.95, 0)
	# zone / body loops (one per body): on while alive, faded out where it died
	if d.audio == null:
		return
	var live := {}
	for b in d.world.bodies:
		if not b.alive or b.tag == &"":
			continue
		var nm := String(LOOPS.get(String(b.tag), ""))
		if nm == "":
			continue
		var key2 := "z%d" % b.id
		live[key2] = b.pos
		d.audio.loop(key2, nm, true, b.pos, -4.0)
	for key3 in _loop_keys:
		if not live.has(key3):
			d.audio.loop(key3, "", false, _loop_keys[key3])
	_loop_keys = live
