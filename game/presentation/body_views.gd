class_name BodyViews
extends Node3D
## Keeps one pooled view per live MatBody and drives it from the body's state each
## frame. A view is chosen by representation (form) but the logical body never
## changes: a stone that heats, melts, pours and cools keeps its id while its
## view crossfades stone -> molten blob -> lava wave -> crusted ridge.
##
## Moveset bodies (docs/MOVESET.md §15.6) map (mat, form, tag) to a pooled view the same way: metal
## discs / lances / plates / caltrop fields, sand slugs / clouds / surges / quicksand, glass and ice
## crystals, magma globs / obsidian / lava pools, water waves, mist / steam / fog / geysers, vines,
## fireballs / flame fields, crackling charged bodies and ground currents, wind blades, vortices,
## vacuum shells, sound barriers. Every view is driven every frame from body state only (heat,
## liquid, charge, radius, zone_radius, tier, spin, power, age / max_life, owner).

## The Mobile renderer specialises every lit surface's pipeline by how many omni lights touch it
## (none / one / several). Until a variant has compiled the surface draws through the fallback
## ubershader, which flashed the arena dark for a frame or two on the session's first flare (then
## first lava glow, first bolt). After each scenario load two imperceptible arena-wide lights touch
## everything in view, one and then both, so both variants compile during load instead.
const WARM_LIGHT_FRAMES := 4
## Puddle water lens: fraction of the puddle radius (the wet mark's dark rim shows around it) and
## its half-thickness (a thin flattened orb, mostly above the floor).
const PUDDLE_LENS := 0.78
const PUDDLE_LENS_H := 0.025

var pool: VfxPool
var world: CombatWorld
## body id -> {kind: String, node: Node3D, prev: Vector3, curr: Vector3, trail: PackedVector3Array}, plus
## the ridge path/state last built (sig, st: "wave"), the radius the view was sized for (r: "blob",
## "puddle": the wet mark's placed radius) and the puddle's water lens and fitted state (surf, fit).
var _views := {}
var _tmp_w := PackedFloat32Array()
var _warm_lights: Array[OmniLight3D] = []
var _warm := 0


func _init() -> void:
	pool = VfxPool.new()
	pool.name = "VfxPool"


func _ready() -> void:
	add_child(pool)


func bind(w: CombatWorld) -> void:
	clear()
	world = w
	_warm = WARM_LIGHT_FRAMES


func clear() -> void:
	for id in _views.keys():
		_release(id)
	_views.clear()


func push_state() -> void:
	## Once per sim tick: record positions for interpolation, create/remove views.
	var alive := {}
	for b in world.bodies:
		if not b.alive or b.form == Sim.Form.POOL:
			continue
		alive[b.id] = true
		var kind := _kind_for(b)
		var style := _style_of(b, kind)
		var vkey := kind if style == "" else kind + ":" + style
		var v: Dictionary = _views.get(b.id, {})
		if v.is_empty() or v.get("key", "") != vkey:
			var keep_prev: Vector3 = v.get("curr", b.pos)
			if not v.is_empty():
				_release(b.id)
			v = _make(b, kind)
			v.key = vkey
			v.style = style
			if not v.has("kind_made"):
				_make_moveset(b, kind, style, v)
			elif kind == "wall" and v.node != null and v.node.has_method("set_material_style"):
				v.node.call("set_material_style", style)
			v.prev = keep_prev
			v.curr = b.pos
			_views[b.id] = v
		v.prev = v.curr
		v.curr = b.pos
		if kind == "ribbon" or (kind == "vine" and style == "lash") or kind == "cloud" and style == "slug":
			var tr: PackedVector3Array = v.trail
			tr.append(b.pos)
			if tr.size() > 6:
				tr.remove_at(0)
			v.trail = tr
	for id in _views.keys():
		if not alive.has(id):
			_release(id)
			_views.erase(id)


func render(alpha: float) -> void:
	if _warm > 0:
		_warm_light_variants()
	var dt := clampf(get_process_delta_time(), 0.0, 0.1) if is_inside_tree() else Sim.DT
	for id in _views:
		var b := world.get_body(id)
		if b == null:
			continue
		var v: Dictionary = _views[id]
		var p: Vector3 = (v.prev as Vector3).lerp(v.curr, alpha)
		var n: Node3D = v.node
		if n == null:
			# Viewless fronts (tremor, sound flight field) still pulse their one-shot rings.
			if String(v.kind) == "rings":
				_emit_rings(b, v, dt)
			continue
		match String(v.kind):
			"stone":
				# Pooled nodes live under the pool (a plain Node): local transform == world transform.
				n.position = p
				var heat := Thermal.heat01(b)
				n.call("set_thermal", heat, b.liquid)
				n.call("set_crust", _crust(b))
				if b.controller >= 0:
					n.rotate_y(0.02)
				else:
					_tumble(b, n, v, dt)
			"wave":
				_update_wave(b, n, p, v)
			"wall":
				n.position = b.pos
				n.rotation.y = b.wall_yaw
				n.call("set_rise", b.wall_rise)
				n.call("set_damage", b.wall_damage)
				if n.has_method("set_heat"):
					n.call("set_heat", Thermal.heat01(b))
			"blob":
				n.position = p
				# Drawn water grows from a first sip to 12 kg (and a shield shrinks as it boils off).
				if b.radius != float(v.get("r", 0.0)):
					v.r = b.radius
					n.call("setup", b.radius)
				n.call("set_state", 1.0 - b.liquid)
			"ribbon":
				var tr: PackedVector3Array = v.trail
				var pts := PackedVector3Array()
				for q in tr:
					pts.append(q)
				pts.append(p)
				var rad := PackedFloat32Array()
				rad.resize(pts.size())
				for k in pts.size():
					rad[k] = b.radius * lerpf(0.35, 1.0, float(k) / maxf(1.0, pts.size() - 1))
				n.call("set_points", pts, rad)
				n.call("set_state", 1.0 - b.liquid)
			"puddle":
				var fit := Vector4(b.pos.x, b.pos.y, b.pos.z, b.radius)
				if fit != v.get("fit", Vector4.INF):
					v.fit = fit
					_fit_puddle(b, n, v)
				if v.get("surf") != null and b.liquid != float(v.get("liq", -1.0)):
					v.liq = b.liquid
					(v.surf as Node3D).call("set_state", 1.0 - b.liquid)
			_:
				_render_moveset(b, n, p, v, dt)
		_render_aux(b, v, p, dt)


func _crust(b: MatBody) -> float:
	if not b.is_stone():
		return 0.0
	if b.liquid > 0.0:
		return clampf(1.0 - b.liquid, 0.0, 1.0) * 0.8
	return 1.0 if b.temp > 200.0 else 0.0


func _kind_for(b: MatBody) -> String:
	var tag := String(b.tag)
	if b.form == Sim.Form.ZONE:
		return _zone_kind(b, tag)
	if b.is_stone():
		match tag:
			"spikes", "spike_line":
				return "spikes"
			"tremor":
				return "rings"
		match b.form:
			Sim.Form.WALL:
				return "wall"
			Sim.Form.WAVE:
				return "wave"
			_:
				# A settled/cooled wave keeps its ridge shape until someone lifts it.
				if b.wave_path.size() >= 2 and b.controller < 0:
					return "wave"
				return "stone"
	if b.is_water():
		if tag == "spikes":
			return "spikes"
		match b.form:
			Sim.Form.PUDDLE:
				return "puddle"
			Sim.Form.CLOUD:
				return "cloud"
			Sim.Form.WALL:
				return "crystal_wall"
			Sim.Form.WAVE:
				return "strip"
			Sim.Form.STREAM, Sim.Form.SHARD:
				if tag == "needle":
					return "crystal"
				return "ribbon" if b.controller < 0 else "blob"
			_:
				if tag == "needle":
					return "crystal"
				return "blob"
	match b.mat:
		Sim.Mat.STEAM:
			return "cloud"
		Sim.Mat.METAL:
			if tag == "spikes" or tag == "spike_line":
				return "spikes"
			return "metal"
		Sim.Mat.SAND:
			match b.form:
				Sim.Form.WAVE:
					return "strip"
				Sim.Form.WALL:
					return "wall"
				Sim.Form.CLOUD:
					return "cloud"
			return "cloud"
		Sim.Mat.GLASS:
			if tag == "spikes":
				return "spikes"
			return "crystal_wall" if b.form == Sim.Form.WALL else "crystal"
		Sim.Mat.PLANT:
			return "vine"
		Sim.Mat.FIRE:
			if b.form == Sim.Form.WAVE:
				return "flames"
			if b.props.get("mine", false) or tag == "bomb":
				return "shell"
			return "fireball"
		Sim.Mat.AIR:
			match tag:
				"crescent":
					return "blade"
				"wind_wall":
					return "blade"
				"twister", "funnel", "spiral", "tornado", "eddy", "vortex_wall":
					return "vortex"
				"dust_line":
					return "cloud"
				"tremor", "sound_barrier":
					return "rings" if b.form == Sim.Form.WAVE else "shell"
				"ground_current":
					return "crackle"
	if tag == "ground_current" or tag == "fire_line":
		return "crackle" if tag == "ground_current" else "flames"
	return "none"


## Zones by tag (the tag carries the meaning; the mat only tints).
func _zone_kind(b: MatBody, tag: String) -> String:
	match tag:
		"fog", "mist", "steam", "sand_cloud", "sandstorm", "steam_screen", "geyser":
			return "cloud"
		"quicksand", "ice_floor", "mud", "melt_pit":
			return "decal"
		"fire_field":
			return "flames"
		"tornado", "eddy", "vortex_wall":
			return "vortex"
		"vacuum_well", "null_bubble", "mine", "corona", "wind_guard", "sound_barrier", "fuse", "static_field":
			return "shell"
		"caltrops", "rod":
			return "metal"
		"briar", "snare":
			return "vine"
		"flight_field":
			return "rings"
		"inrush":
			return "shell"
		"lava_pool":
			return "lava_pool"
	if b.mat == Sim.Mat.WATER or b.mat == Sim.Mat.STEAM:
		return "cloud"
	return "none"


## Style key of a view kind for this body (a change re-makes the view, e.g. a sand wall fusing to glass).
func _style_of(b: MatBody, kind: String) -> String:
	var tag := String(b.tag)
	match kind:
		"cloud":
			if b.form == Sim.Form.ZONE:
				return tag if CloudView.STYLES.has(tag) else ("steam" if b.mat == Sim.Mat.STEAM else "mist")
			if b.mat == Sim.Mat.SAND:
				return "slug"
			if tag == "dust_line":
				return "dust_line"
			return "steam" if b.mat == Sim.Mat.STEAM else "mist"
		"strip":
			if b.mat == Sim.Mat.SAND:
				return "sand"
			return "rime" if tag == "rime" or b.phase == Sim.Phase.FROZEN and tag != "water_wave" else "water"
		"decal":
			return tag
		"shell":
			if tag == "" and b.mat == Sim.Mat.FIRE:
				return "mine"
			if tag == "fuse" or tag == "bomb" or b.props.get("mine", false):
				return "mine"
			return tag
		"metal":
			if b.form == Sim.Form.ZONE:
				return "planted" if tag == "rod" else "caltrops"
			if b.form == Sim.Form.WALL or tag == "plate":
				return "plate"
			if tag in ["disc", "lance", "rod"]:
				return tag
			return "orb"
		"vine":
			if b.form == Sim.Form.WALL:
				return "lattice"
			if b.form == Sim.Form.WAVE:
				return "roots"
			if b.form == Sim.Form.ZONE:
				return "briar"
			return "lash"
		"crystal", "crystal_wall":
			return "glass" if b.mat == Sim.Mat.GLASS else "ice"
		"spikes":
			if b.mat == Sim.Mat.METAL:
				return "metal"
			return "glass" if b.mat == Sim.Mat.GLASS else ("ice" if b.is_water() else "stone")
		"flames":
			return "field" if b.form == Sim.Form.ZONE else "line"
		"blade":
			return "wall" if tag == "wind_wall" else "crescent"
		"vortex":
			return tag if tag != "" else "twister"
		"wall":
			if tag == "obsidian" or b.mat == Sim.Mat.SAND or tag == "mud" or tag == "sand":
				return "sand" if b.mat == Sim.Mat.SAND or tag == "sand" else tag
	return ""


func _make(b: MatBody, kind: String) -> Dictionary:
	var v := {"kind": kind, "node": null, "prev": b.pos, "curr": b.pos, "trail": PackedVector3Array()}
	var n: Node3D = null
	match kind:
		"stone":
			n = acquire("stone")
			if n:
				n.call("setup", b.id * 7919 + 13, b.radius)
		"wave":
			n = acquire("lava_wave")
		"wall":
			n = acquire("earth_wall")
			if n and n.has_method("setup"):
				n.call("setup", b.id, b.wall_half.x * 2.0, b.wall_half.y * 2.0, b.wall_half.z * 2.0)
		"blob":
			n = acquire("water_blob")
			if n:
				n.call("setup", b.radius)
				v.r = b.radius
		"ribbon":
			n = acquire("water_ribbon")
		"puddle":
			n = acquire("scorch_decal")
			if n and n.has_method("place"):
				n.call("place", b.pos, b.radius, "wet", 9999.0)
				v.r = b.radius
			# The dark wet mark alone all but vanishes under its glossy sheen at a low camera angle:
			# a calm, thin water lens over it makes the puddle read as water.
			var s := acquire("water_blob")
			if s:
				s.call("set_wobble", 0.2)
				# Draw the lens before other transparent water/ice so shards resting in the puddle stay
				# visible (the lens refracts only the opaque scene behind it).
				_set_sorting_offset(s, -2.0)
				v.surf = s
			if n:
				v.fit = Vector4(b.pos.x, b.pos.y, b.pos.z, b.radius)
				_fit_puddle(b, n, v)
	if n != null:
		n.visible = true
		v.kind_made = true
	elif kind in ["stone", "wave", "wall", "blob", "ribbon", "puddle"]:
		v.kind_made = true
	v.node = n
	return v


## Acquire a pooled node the caller keeps across frames. At its cap VfxPool recycles the oldest
## active node, which would leave two owners driving (and later releasing) one node, so a kept
## kind grows its cap instead: the sim already bounds how many bodies can be alive.
func acquire(key: String) -> Node3D:
	var st: Array = pool.get_stats().get(key, [])
	if st.size() == 3 and int(st[1]) == 0 and int(st[0]) >= int(st[2]):
		pool.set_cap(key, int(st[0]) + 1)
	return pool.get_fx(key) as Node3D


func _release(id: int) -> void:
	var v: Dictionary = _views.get(id, {})
	if v.get("aux") != null:
		pool.release(v.aux)
		v.aux = null
	if v.get("surf") != null:
		_set_sorting_offset(v.surf, 0.0)
		pool.release(v.surf)
		v.surf = null
	if v.is_empty() or v.node == null:
		return
	# Moveset views orient / stretch their root (spears, lances, discs): pooled nodes start square.
	(v.node as Node3D).basis = Basis()
	pool.release(v.node)
	v.node = null


## Puddles grow as water merges in (melted ice, landed streams) and shrink as they are drawn or
## boiled: keep the wet mark and the water lens on the live radius and position. The placed mark is
## scaled rather than re-placed (re-placing restarts its fade-in). Runs only when the puddle changes.
func _fit_puddle(b: MatBody, n: Node3D, v: Dictionary) -> void:
	var r := maxf(b.radius, 0.05)
	var k := r / maxf(float(v.get("r", b.radius)), 0.05)
	n.scale = Vector3(k, 1.0, k)
	# Pooled nodes live under the pool (a plain Node): their local transform is their world one.
	n.position = b.pos + Vector3(0.0, ScorchDecal.LIFT, 0.0)
	var s: Node3D = v.get("surf")
	if s == null:
		return
	var rl := r * PUDDLE_LENS
	s.call("setup", rl)
	s.scale = Vector3(1.0, PUDDLE_LENS_H / rl, 1.0)
	# Just above the mark, so the lens sorts (and refracts) over it.
	s.position = b.pos + Vector3(0.0, ScorchDecal.LIFT + 0.006, 0.0)
	v.liq = b.liquid
	s.call("set_state", 1.0 - b.liquid)


func _set_sorting_offset(n: Node, off: float) -> void:
	if n is VisualInstance3D:
		(n as VisualInstance3D).sorting_offset = off
	for c in n.get_children():
		_set_sorting_offset(c, off)


func _warm_light_variants() -> void:
	if _warm_lights.is_empty():
		for i in 2:
			var l := OmniLight3D.new()
			l.name = "WarmLight%d" % i
			l.shadow_enabled = false
			l.light_energy = 0.001
			l.omni_range = 30.0       # the whole 32 x 32 m arena from above its centre
			l.position = Vector3(0.0, 4.0, 0.0)
			l.visible = false
			add_child(l)
			_warm_lights.append(l)
	_warm -= 1
	# The first frame draws without them so its own (no omni light) pipelines are queued first and
	# are ready again when the lights go off; then one light, then two, then off.
	_warm_lights[0].visible = _warm == 2 or _warm == 1
	_warm_lights[1].visible = _warm == 1


func _update_wave(b: MatBody, n: Node3D, p: Vector3, v: Dictionary) -> void:
	# A moving wave's front is interpolated every frame; a settled ridge's path only changes when
	# the sim changes it, so its mesh is rebuilt only then (set_path re-uploads the whole strip).
	var path := b.wave_path
	var sig: Array = []
	if b.form != Sim.Form.WAVE and path.size() >= 2:
		sig = [path.size(), path[0], path[path.size() - 1], b.wave_width]
	if sig.is_empty() or sig != v.get("sig", []):
		v.sig = sig
		var pts := PackedVector3Array()
		for q in path:
			pts.append(q)
		if b.form == Sim.Form.WAVE:
			if pts.is_empty() or pts[pts.size() - 1].distance_to(p) > 0.05:
				pts.append(p)
		if pts.size() < 2:
			pts.insert(0, p - b.wave_dir * 0.4)
		_tmp_w.resize(pts.size())
		for k in pts.size():
			var t := float(k) / maxf(1.0, pts.size() - 1)
			_tmp_w[k] = b.wave_width * lerpf(0.55, 1.0, t)
		n.global_position = Vector3.ZERO
		n.call("set_path", pts, _tmp_w)
	var crust := clampf(1.0 - b.liquid / 0.85, 0.0, 1.0)
	if b.liquid <= 0.0:
		crust = 1.0
	var st := Vector3(b.liquid, crust, b.vel.length())
	if st != v.get("st", -Vector3.ONE):
		v.st = st
		n.call("set_state", b.liquid, crust, st.z)


func view_of(id: int) -> Node3D:
	var v: Dictionary = _views.get(id, {})
	return v.get("node", null)


# ------------------------------------------------------------------------------ moveset views

## Seconds a zone / field takes to fade in, and to fade out before its max_life.
const ZONE_FADE_IN := 0.3
const ZONE_FADE_OUT := 0.45
## Electric charge above which any body crackles.
const CRACKLE_CHARGE := 4.0


func _seed_of(b: MatBody) -> int:
	return b.id * 7919 + 13


func _make_moveset(b: MatBody, kind: String, style: String, v: Dictionary) -> void:
	var n: Node3D = null
	var sd := _seed_of(b)
	match kind:
		"metal":
			n = acquire("metal")
			if n:
				var sz := Vector3.ONE * maxf(b.radius, 0.12)
				match style:
					"disc":
						sz = Vector3.ONE * clampf(b.radius * 1.1, 0.14, 0.45)
					"lance", "rod":
						sz = Vector3(clampf(b.radius * 0.18, 0.025, 0.07), clampf(b.radius * 6.0, 0.9, 2.2), 0.0)
					"planted":
						# A planted lightning rod: a 2.2 m mast standing in the ground (the zone is its catch radius).
						sz = Vector3(0.06, 2.2, 0.0)
					"plate":
						if b.form == Sim.Form.WALL:
							sz = Vector3(b.wall_half.x * 2.0, b.wall_half.y * 2.0, maxf(b.wall_half.z * 0.5, 0.05))
						else:
							sz = Vector3(b.radius * 2.2, b.radius * 0.18, b.radius * 2.2)
				n.call("setup", "rod" if style == "planted" else style, sd, sz, b.zone_radius)
		"cloud":
			n = acquire("cloud")
			if n:
				n.call("configure", style, sd)
		"strip":
			n = acquire("ground_strip")
			if n:
				n.call("configure", style, sd)
		"crystal":
			n = acquire("crystal")
			if n:
				var r := maxf(b.radius, 0.06)
				n.call("setup", "shard", sd, style, Vector3(r * 2.2, r * 3.2, r * 2.2) if b.tag == &"needle" else Vector3(r * 3.0, r * 2.4, r * 3.0))
		"crystal_wall":
			n = acquire("crystal")
			if n:
				var mode := "ridge" if b.tag == &"ridge" or b.tag == &"rime" else "wall"
				n.call("setup", mode, sd, style, Vector3(maxf(b.wall_half.x, 0.4), maxf(b.wall_half.y * 2.0, 0.4), maxf(b.wall_half.z * 2.0, 0.3)))
		"spikes":
			n = acquire("spikes")
			if n:
				if b.form == Sim.Form.WAVE:
					n.call("setup", style, "path", sd, Vector3(1, 1, 1))
				elif b.form == Sim.Form.ZONE:
					n.call("setup", style, "ring", sd, Vector3(maxf(b.zone_radius, 0.5), 1.0, 1.0))
				else:
					n.call("setup", style, "row", sd, Vector3(maxf(b.wall_half.x, 0.5), maxf(b.wall_half.y * 2.0, 0.6), 1.0))
		"vine":
			n = acquire("vine")
			if n:
				match style:
					"lattice":
						n.call("setup", "lattice", sd, b.wall_half)
					"briar":
						n.call("setup", "briar", sd, Vector3.ONE * maxf(b.zone_radius, 0.5))
					_:
						n.call("setup", style, sd, Vector3.ONE)
		"flames":
			n = acquire("flame_field")
			if n:
				var blue := bool(b.props.get("blue", false))
				if style == "field":
					n.call("setup", "field", sd, maxf(b.zone_radius, 0.5), clampf(0.75 + 0.15 * b.tier, 0.7, 1.25), blue)
				else:
					n.call("setup", "line", sd, 0.6, clampf(0.8 + 0.15 * b.tier, 0.8, 1.4), blue)
		"fireball":
			n = acquire("fireball")
			if n:
				var fk := "comet" if b.tag == &"comet" else ("ember" if b.tag == &"ember" else "fireball")
				var fr := 0.09 if fk == "ember" else clampf(0.16 + 0.05 * b.tier + b.heat_payload / 4000.0, 0.16, 0.55)
				n.call("setup", fk, fr, bool(b.props.get("blue", false)))
		"shell":
			n = acquire("shell")
			if n:
				var col := Color(0, 0, 0, 0)
				if style == "mine" and b.mat == Sim.Mat.FIRE and bool(b.props.get("blue", false)):
					col = VfxPalette.color("blue")
				n.call("configure", style, col, sd)
		"vortex":
			n = acquire("vortex")
			if n:
				n.call("configure", style, _infusion(b), sd)
		"blade":
			n = acquire("wind_blade")
			if n:
				if style == "wall":
					n.call("setup", "wall", Vector3(maxf(b.wall_half.x, b.radius), maxf(b.wall_half.y * 2.0, 1.8), 1.0))
				else:
					n.call("setup", "crescent", Vector3.ONE * clampf(b.radius * 1.6, 0.5, 1.4))
		"crackle":
			n = acquire("crackle")
			if n:
				n.call("setup", "ground", 0.5, sd)
		"decal":
			n = acquire("ground_decal")
			if n:
				n.call("place", b.pos, maxf(b.zone_radius, 0.4), style, sd)
		"rings":
			n = null   # tremor / sound front: one-shot rings spawned while it travels (_render_moveset)
		"lava_pool":
			n = acquire("lava_wave")
	if n != null:
		n.visible = true
	v.node = n


func _infusion(b: MatBody) -> String:
	var inf := String(b.props.get("infused", ""))
	for k in ["fire", "sand", "steam", "water"]:
		if inf.contains(k):
			return k
	if inf.contains("magma"):
		return "fire"
	return ""


## Zone fade: in over ZONE_FADE_IN, out over the last ZONE_FADE_OUT of max_life.
func _life01(b: MatBody) -> float:
	var f := clampf(b.age / ZONE_FADE_IN, 0.0, 1.0)
	if b.max_life > 0.0:
		f = minf(f, clampf((b.max_life - b.age) / ZONE_FADE_OUT, 0.0, 1.0))
	return f


func _ground_y(p: Vector3) -> float:
	if world != null and world.arena != null:
		return world.arena.ground_height(p.x, p.z, p.y + 0.3)
	return 0.0


func _render_moveset(b: MatBody, n: Node3D, p: Vector3, v: Dictionary, dt: float) -> void:
	var style := String(v.get("style", ""))
	var vel := b.vel
	match String(v.kind):
		"metal":
			n.position = p
			var heat := Thermal.heat01(b)
			n.call("set_state", maxf(heat, b.liquid), b.spin)
			if style == "planted":
				n.position = Vector3(b.pos.x, _ground_y(b.pos) + 1.0, b.pos.z)
				n.basis = Basis(Vector3(0, 0, 1), 0.04)
			elif style == "caltrops" or b.form == Sim.Form.WALL:
				if b.form == Sim.Form.WALL:
					n.position = b.pos + Vector3(0, b.wall_half.y * (2.0 * b.wall_rise - 1.0), 0)
					n.rotation.y = b.wall_yaw
			elif vel.length_squared() > 0.5 and b.controller < 0:
				_orient_along(n, vel, style)
			elif b.controller >= 0:
				n.rotate_y(0.03)
		"cloud":
			if style == "slug":
				n.position = p
				if vel.length_squared() > 0.25:
					n.basis = Basis.looking_at(-vel.normalized(), Vector3.UP if absf(vel.normalized().y) < 0.95 else Vector3.RIGHT)
				n.call("set_shape", maxf(b.radius * 1.2, 0.18), maxf(b.radius * 1.6, 0.25))
			else:
				var gy := _ground_y(b.pos)
				n.position = Vector3(p.x, gy if b.form == Sim.Form.ZONE or style != "steam" else minf(p.y, gy + 0.2), p.z)
				var r := b.zone_radius if b.zone_radius > 0.0 else maxf(b.radius, 0.6)
				var h := r * 0.8
				var sq := 1.0
				match style:
					"fog", "mist":
						h = clampf(r * 0.45, 0.6, 1.6)
					"sandstorm":
						h = clampf(r * 0.9, 1.5, 3.5)
					"steam":
						h = clampf(r * 1.2, 1.2, 3.0)
					"geyser":
						h = clampf(2.5 + 0.6 * b.tier, 2.5, 5.0)
						r = clampf(r * 0.4, 0.4, 1.0)
					"steam_screen":
						h = 2.4
						sq = 2.2
						r = clampf(r * 0.5, 0.6, 1.6)
						n.rotation.y = b.wall_yaw
					"dust_line":
						h = 0.7
						r = maxf(b.radius, 0.6)
				n.call("set_shape", r, h, sq)
				n.call("set_floor", gy)
				var amt := _life01(b) if b.form == Sim.Form.ZONE else clampf(b.mass / maxf(float(v.get("m0", b.mass)), 0.01), 0.25, 1.0)
				if not v.has("m0"):
					v.m0 = maxf(b.mass, 0.01)
				n.call("set_amount", amt)
		"strip":
			_update_strip(b, n, p, v)
		"crystal":
			n.position = p
			var heat := Thermal.heat01(b) if b.mat == Sim.Mat.GLASS else 0.0
			n.call("set_state", heat, 0.05 if style == "glass" else 0.4 + 0.4 * (1.0 - b.liquid), 1.0)
			if vel.length_squared() > 0.5 and b.controller < 0:
				_orient_along(n, vel, "lance")
		"crystal_wall":
			n.position = b.pos
			n.rotation.y = b.wall_yaw
			var heat2 := Thermal.heat01(b) if b.mat == Sim.Mat.GLASS else 0.0
			n.call("set_state", heat2, (0.05 if style == "glass" else 0.45) + 0.4 * b.wall_damage, b.wall_rise)
			n.call("set_shatter", clampf(b.wall_damage * 1.4 - 0.2, 0.0, 1.0))
		"spikes":
			if b.form == Sim.Form.WAVE:
				var pts := PackedVector3Array(b.wave_path)
				if pts.is_empty() or pts[pts.size() - 1].distance_to(b.pos) > 0.05:
					pts.append(b.pos)
				if pts.size() >= 2:
					n.call("set_path", pts, clampf(0.6 + 0.2 * b.tier, 0.5, 1.3))
			else:
				n.position = Vector3(b.pos.x, _ground_y(b.pos), b.pos.z)
				n.rotation.y = b.wall_yaw
				n.call("set_rise", b.wall_rise if b.form == Sim.Form.WALL else _life01(b))
			n.call("set_heat", Thermal.heat01(b))
		"vine":
			var burn := clampf((b.temp - 120.0) / 300.0, 0.0, 1.0)
			var frozen := 1.0 if b.phase == Sim.Phase.FROZEN else 0.0
			match style:
				"lattice":
					n.position = b.pos
					n.rotation.y = b.wall_yaw
					n.call("set_state", b.wall_rise, burn, frozen)
				"briar":
					n.position = Vector3(b.pos.x, _ground_y(b.pos), b.pos.z)
					n.call("set_state", _life01(b), burn, frozen)
				"roots":
					var pts2 := PackedVector3Array(b.wave_path)
					pts2.append(b.pos)
					n.call("set_path", pts2, clampf(0.06 + 0.02 * b.tier, 0.05, 0.12))
					n.call("set_state", 1.0, burn, frozen)
				_:
					var tr: PackedVector3Array = v.trail
					var pts3 := PackedVector3Array(tr)
					pts3.append(p)
					if pts3.size() >= 2:
						n.call("set_path", pts3, clampf(b.radius * 0.35, 0.04, 0.1))
					n.call("set_state", 1.0, burn, frozen)
		"flames":
			if style == "field":
				n.position = Vector3(b.pos.x, _ground_y(b.pos), b.pos.z)
				n.call("set_intensity", _life01(b) * clampf(0.6 + b.power / 30.0, 0.6, 1.0))
			else:
				var pts4 := PackedVector3Array(b.wave_path)
				if pts4.is_empty() or pts4[pts4.size() - 1].distance_to(b.pos) > 0.05:
					pts4.append(b.pos)
				if pts4.size() >= 2:
					n.call("set_path", pts4)
				n.call("set_intensity", 1.0 if b.form == Sim.Form.WAVE else clampf(b.heat_payload / 200.0, 0.3, 1.0))
		"fireball":
			n.position = p
			n.call("set_motion", vel)
			n.call("set_power", clampf(0.6 + 0.15 * b.tier + b.heat_payload / 1500.0, 0.4, 1.4))
		"shell":
			var r2 := b.zone_radius if b.zone_radius > 0.0 else maxf(b.radius, 0.15)
			var hs := 1.0
			var at := p
			match style:
				"vacuum_well":
					hs = 0.45
					at = Vector3(p.x, _ground_y(p), p.z)
				"static_field":
					hs = 0.55
					at = Vector3(p.x, _ground_y(p), p.z)
				"inrush":
					# Air rushing back into a collapsed well: a low dome that closes in over its short life.
					hs = 0.5
					at = Vector3(p.x, _ground_y(p), p.z)
					if b.max_life > 0.0:
						r2 *= lerpf(1.0, 0.35, clampf(b.age / b.max_life, 0.0, 1.0))
				"corona", "wind_guard", "sound_barrier":
					hs = 1.15
					at = p + Vector3(0, 0.9, 0) if b.form == Sim.Form.ZONE else p
				"mine":
					r2 = clampf(b.radius, 0.1, 0.25)
			n.position = at
			n.call("set_shape", r2, hs)
			n.call("set_power", _life01(b) * clampf(0.5 + 0.17 * b.tier, 0.5, 1.0) if b.form == Sim.Form.ZONE else 1.0)
		"vortex":
			n.position = Vector3(p.x, _ground_y(p), p.z)
			var vr := b.zone_radius if b.zone_radius > 0.0 else maxf(b.radius * 1.5, 0.6)
			var vh := clampf(vr * 2.2, 1.6, 7.0)
			match style:
				"twister", "spiral":
					vr = clampf(b.radius * 2.0 + 0.3 * b.tier, 0.45, 1.2)
					vh = clampf(1.4 + 0.4 * b.tier, 1.2, 3.0)
				"eddy":
					vh = clampf(vr * 0.6, 0.6, 1.4)
				"vortex_wall":
					vh = 2.6
			n.call("set_shape", vr * (0.35 + 0.65 * _life01(b)) if b.form == Sim.Form.ZONE else vr, vh)
			n.call("set_spin", b.spin)
			var inf := _infusion(b)
			if inf != String(v.get("inf", "")):
				v.inf = inf
				n.call("configure", style, inf, _seed_of(b))
		"blade":
			if style == "wall":
				n.position = Vector3(b.pos.x, _ground_y(b.pos), b.pos.z)
				n.rotation.y = b.wall_yaw
			else:
				n.position = p
				n.call("set_motion", vel)
			n.call("set_power", clampf(0.6 + 0.2 * b.tier, 0.5, 1.3))
		"crackle":
			var pts5 := PackedVector3Array(b.wave_path)
			pts5.append(b.pos)
			n.call("set_target", b.pos, pts5)
			n.call("set_intensity", clampf(0.5 + b.power / 30.0, 0.4, 1.3))
		"decal":
			n.call("set_fade", _life01(b))
			if style == "melt_pit":
				n.call("set_heat", clampf(0.4 + b.power / 30.0, 0.4, 1.0))
		"lava_pool":
			# A lava pool is a short, wide lava strip (same material as waves and stones).
			var r3 := maxf(b.zone_radius, 0.5)
			var sig := Vector4(b.pos.x, b.pos.y, b.pos.z, r3)
			if sig != v.get("sig4", Vector4.INF):
				v.sig4 = sig
				var gy2 := _ground_y(b.pos)
				var c := Vector3(b.pos.x, gy2 - 0.1, b.pos.z)
				var d := Vector3(r3 * 0.45, 0, r3 * 0.2)
				_tmp_w.resize(3)
				_tmp_w[0] = r3 * 1.5
				_tmp_w[1] = r3 * 1.9
				_tmp_w[2] = r3 * 1.6
				n.position = Vector3.ZERO
				n.call("set_path", PackedVector3Array([c - d, c, c + d]), _tmp_w)
			var liq := clampf(b.liquid if b.liquid > 0.0 else _life01(b), 0.0, 1.0)
			n.call("set_state", liq, clampf(1.0 - liq / 0.85, 0.0, 1.0), 0.15)
	if String(v.kind) == "rings" or (b.tag == &"tremor" and b.form == Sim.Form.WAVE):
		_emit_rings(b, v, dt)


## Tremor / sound fronts travel as repeating ground rings (one-shots from the pool).
func _emit_rings(b: MatBody, v: Dictionary, dt: float) -> void:
	v.ring_t = float(v.get("ring_t", 0.0)) - dt
	if float(v.ring_t) > 0.0:
		return
	# A sound flight field (under a hovering fighter) pulses slower and smaller than a travelling front.
	var flight := b.tag == &"flight_field"
	v.ring_t = 0.3 if flight else 0.12
	var r := pool.get_fx("ring")
	if r:
		var col := VfxPalette.color("sound" if b.mat == Sim.Mat.AIR else "stone")
		var gy := _ground_y(b.pos)
		r.call("play", Vector3(b.pos.x, gy + 0.04, b.pos.z), Vector3.UP, 0.15 if flight else 0.2,
			0.9 if flight else 1.4 + 0.3 * b.tier, 0.4 if flight else 0.45, col,
			{"width": 0.08 if flight else 0.1, "cover": 0.5 if flight else 0.65, "glow": 1.2 if flight else 1.0})
	if b.mat == Sim.Mat.STONE and float(v.get("dust_t", 0.0)) <= 0.0:
		var bu := pool.get_fx("burst")
		if bu:
			bu.call("play", Vector3(b.pos.x, _ground_y(b.pos), b.pos.z), Vector3.UP, 0.5, "dust")
		v.dust_t = 0.3
	v.dust_t = float(v.get("dust_t", 0.0)) - 0.12


func _update_strip(b: MatBody, n: Node3D, p: Vector3, v: Dictionary) -> void:
	_update_wave(b, n, p, v)


## Overlays any body can carry: electric charge crackles over it.
func _render_aux(b: MatBody, v: Dictionary, p: Vector3, _dt: float) -> void:
	var want := b.charge > CRACKLE_CHARGE and String(v.kind) != "crackle"
	var aux: Node3D = v.get("aux")
	if want and aux == null:
		aux = acquire("crackle")
		if aux:
			# A zone's radius is its reach (a planted rod catches bolts over 6 m): crackle the mast's tip instead.
			aux.call("setup", "body", 0.3 if _is_mast(b) else maxf(b.radius, 0.2), _seed_of(b))
			aux.visible = true
		v.aux = aux
	elif not want and aux != null:
		pool.release(aux)
		v.aux = null
		aux = null
	if aux != null:
		aux.call("set_target", Vector3(p.x, _ground_y(p) + 2.1, p.z) if _is_mast(b) else p)
		aux.call("set_intensity", clampf(b.charge / 30.0, 0.3, 1.3))


func _is_mast(b: MatBody) -> bool:
	return b.form == Sim.Form.ZONE and b.tag == &"rod"


## Projectiles face their travel: lances / needles point along it, discs fly flat and bank, plates
## turn their face to it.
func _orient_along(n: Node3D, vel: Vector3, style: String) -> void:
	var f := vel.normalized()
	var x := f.cross(Vector3.UP)
	x = x.normalized() if x.length_squared() > 1e-6 else Vector3.RIGHT
	match style:
		"lance", "rod":
			# local +Y along the travel
			var z := x.cross(f).normalized()
			n.basis = Basis(x, f, z)
		"disc":
			var up := (Vector3.UP + x * 0.25).normalized()
			var z2 := x.cross(up).normalized()
			n.basis = Basis(x, up, z2)
		_:
			n.basis = Basis(x, f.cross(x).normalized(), f)


## Thrown stones tumble from their linear velocity (angular speed = v / r about up x v); spears fly
## point-first; resting stones stop.
func _tumble(b: MatBody, n: Node3D, v: Dictionary, dt: float) -> void:
	var spd := b.vel.length()
	if b.tag == &"spear":
		if spd > 1.0:
			var f := b.vel / spd
			var x := f.cross(Vector3.UP)
			x = x.normalized() if x.length_squared() > 1e-6 else Vector3.RIGHT
			n.basis = Basis(x, f.cross(x).normalized(), f).scaled_local(Vector3(1.0, 1.0, 2.4))
		return
	if b.on_ground or spd < 1.0 or dt <= 0.0:
		return
	var axis := Vector3.UP.cross(b.vel)
	if axis.length_squared() < 1e-6:
		return
	var w := spd / maxf(b.radius, 0.08) * (0.55 if b.tag == &"crag" else 0.8)
	n.basis = Basis(axis.normalized(), w * dt) * n.basis.orthonormalized()
