class_name VfxPool
extends Node
## Generic pool for VFX nodes (effects, views, decals).
##
##   var fx := pool.get_fx("fire_burst")        # or a PackedScene / Script key
##   fx.play(...)                               # effects release themselves when finished
##   pool.release(node)                         # manual release (views such as stones / waves)
##
## Keys: the built-in string names below, any registered name (register()), or a PackedScene /
## GDScript used directly (auto-registered with `default_cap`).
##
## Caps (per type, built-ins) -- when the cap is reached get_fx() recycles the OLDEST active
## instance (reset + reuse) if `recycle_oldest` is true, otherwise returns null:
##   stone 24, earth_wall 6, lava_wave 4, water_ribbon 4, water_blob 3,
##   fire_burst 4, fire_charge 2, lightning_arc 3, charge_aim 2, air_push 3, glide_trail 2,
##   dust_puff 8, steam 6, splash 6, ember 4, scorch_decal 8
## Pooled nodes stay children of the pool node (their own transform is global, so the pool can
## live anywhere in the tree, ideally a plain Node under the world root).

## True: at cap, steal the oldest active instance. False: return null at cap.
@export var recycle_oldest: bool = true
@export var default_cap: int = 8

var _types: Dictionary = {}  # key -> {factory: Callable, cap: int, free: Array, active: Array}
var _owner_of: Dictionary = {}  # instance_id -> type entry


func _init() -> void:
	register("stone", func() -> Node: return StoneView.new(), 24)
	register("earth_wall", func() -> Node: return EarthWallView.new(), 6)
	register("lava_wave", func() -> Node: return LavaWaveView.new(), 4)
	register("water_ribbon", func() -> Node: return WaterRibbonView.new(), 4)
	register("water_blob", func() -> Node: return WaterBlobView.new(), 3)
	register("fire_burst", func() -> Node: return FireBurstFX.new(), 4)
	register("fire_charge", func() -> Node: return FireChargeFX.new(), 2)
	register("lightning_arc", func() -> Node: return LightningArcFX.new(), 3)
	register("charge_aim", func() -> Node: return ChargeAimFX.new(), 2)
	register("air_push", func() -> Node: return AirPushFX.new(), 3)
	register("glide_trail", func() -> Node: return GlideTrailFX.new(), 2)
	register("dust_puff", func() -> Node: return DustPuffFX.new(), 8)
	register("steam", func() -> Node: return SteamFX.new(), 6)
	register("splash", func() -> Node: return SplashFX.new(), 6)
	register("ember", func() -> Node: return EmberFX.new(), 4)
	register("scorch_decal", func() -> Node: return ScorchDecal.new(), 8)


## Register (or replace) a type. `factory` returns a fresh Node.
func register(key: Variant, factory: Callable, cap: int) -> void:
	_types[key] = {"factory": factory, "cap": cap, "free": [], "active": []}


func set_cap(key: Variant, cap: int) -> void:
	if _types.has(key):
		_types[key].cap = cap


## Acquire an idle, reset instance of `key`. Returns null when the type is capped and
## recycle_oldest is false (or the key is unknown).
func get_fx(key: Variant) -> Node:
	var entry: Dictionary = _entry_for(key)
	if entry.is_empty():
		push_warning("VfxPool: unknown key %s" % [key])
		return null
	var node: Node = null
	var free: Array = entry.free
	var active: Array = entry.active
	while node == null and not free.is_empty():
		var cand: Node = free.pop_back()
		if is_instance_valid(cand):
			node = cand
	if node == null:
		if active.size() + free.size() < entry.cap:
			node = entry.factory.call()
			if node == null:
				return null
			add_child(node)
			_owner_of[node.get_instance_id()] = entry
		elif recycle_oldest and not active.is_empty():
			node = active.pop_front()
			if not is_instance_valid(node):
				return get_fx(key)
			_reset_node(node)
		else:
			return null
	active.append(node)
	if node is VfxEffect:
		(node as VfxEffect).pool = self
		(node as VfxEffect).on_acquire()
	elif node is Node3D:
		(node as Node3D).visible = true  # plain views such as StoneView
	return node


## Warm every built-in effect type once so shader pipelines compile during loading instead of on
## the first hit. Call while a loading screen covers the view, with the camera looking at `at`
## (the effects are drawn tiny for a couple of frames, then released). `frames` = frames to hold.
func prewarm(at: Vector3, frames: int = 3) -> void:
	var warmed: Array[Node] = []
	for key in _types.keys():
		if not (key is String):
			continue
		var node: Node = get_fx(key)
		if node == null:
			continue
		warmed.append(node)
		_prewarm_node(key, node, at)
	for _i in frames:
		await get_tree().process_frame
		for node in warmed:
			if is_instance_valid(node) and node.has_method("advance") and not (node is StoneView):
				node.call("advance", 0.016)
	for node in warmed:
		release(node)


func _prewarm_node(key: String, node: Node, at: Vector3) -> void:
	var tiny: float = 0.05
	match key:
		"stone":
			node.setup(1, tiny)
			node.position = at
			node.set_thermal(1.0, 1.0)
		"earth_wall":
			node.setup(1, tiny * 2.0, tiny, tiny)
			node.position = at
			node.set_rise(0.5)
		"lava_wave":
			node.set_path(PackedVector3Array([at, at + Vector3(tiny, 0, 0)]), PackedFloat32Array([tiny, tiny]))
			node.set_state(1.0, 0.3, 1.0)
		"water_ribbon":
			node.set_points(PackedVector3Array([at, at + Vector3(tiny, 0, 0)]), PackedFloat32Array([tiny * 0.2, tiny * 0.2]))
			node.set_state(0.5)
		"water_blob":
			node.setup(tiny)
			node.position = at
			node.set_state(0.5)
		"fire_burst":
			node.play(at, Vector3.UP, tiny, 0.3)
		"fire_charge":
			node.position = at
			node.set_charge(0.2)
		"lightning_arc":
			node.strike(PackedVector3Array([at, at + Vector3(tiny, tiny, 0)]), 1)
		"charge_aim":
			node.set_aim(at, at + Vector3(tiny, 0, 0), 0.5)
		"air_push":
			node.play(at, Vector3.UP, tiny, tiny * 2.0)
		"glide_trail":
			node.begin(null)
			node.push(at)
			node.push(at + Vector3(tiny, 0, 0))
		"dust_puff", "splash", "ember":
			node.play(at, Vector3.UP, 0.1)
		"steam":
			node.play(at, 0.1)
		"scorch_decal":
			node.place(at, tiny, "scorch", 1.0)


## Return a node to its pool (safe to call twice).
func release(node: Node) -> void:
	if node == null or not is_instance_valid(node):
		return
	var entry: Dictionary = _owner_of.get(node.get_instance_id(), {})
	if entry.is_empty():
		return
	var active: Array = entry.active
	var i: int = active.find(node)
	if i < 0:
		return
	active.remove_at(i)
	_reset_node(node)
	if node is Node3D:
		(node as Node3D).visible = false
	entry.free.append(node)


func release_all() -> void:
	for key in _types:
		var entry: Dictionary = _types[key]
		for node in entry.active.duplicate():
			release(node)


## {key: [active, free]} for debug overlays.
func get_stats() -> Dictionary:
	var out: Dictionary = {}
	for key in _types:
		var e: Dictionary = _types[key]
		out[key] = [e.active.size(), e.free.size(), e.cap]
	return out


func _reset_node(node: Node) -> void:
	if node.has_method("reset"):
		node.call("reset")
	elif node is Node3D:
		(node as Node3D).visible = false


func _entry_for(key: Variant) -> Dictionary:
	if _types.has(key):
		return _types[key]
	if key is PackedScene:
		var scene: PackedScene = key
		register(key, func() -> Node: return scene.instantiate(), default_cap)
		return _types[key]
	if key is GDScript:
		var script: GDScript = key
		register(key, func() -> Node: return script.new(), default_cap)
		return _types[key]
	return {}
