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
	if node is Node3D:
		(node as Node3D).visible = true
	return node


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
