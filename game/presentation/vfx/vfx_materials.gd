class_name VfxMaterials
extends RefCounted
## Shader / material cache for the VFX layer. Shaders are loaded once; materials that carry only
## per-instance uniforms (stone family) are shared, everything else is created per effect so that
## pooled effects can animate their own uniforms.

const SHADER_DIR: String = "res://presentation/shaders/"

static var _shaders: Dictionary = {}
static var _shared: Dictionary = {}


static func shader(shader_name: String) -> Shader:
	var s: Shader = _shaders.get(shader_name)
	if s == null:
		s = load(SHADER_DIR + shader_name + ".gdshader") as Shader
		_shaders[shader_name] = s
	return s


## New, unshared ShaderMaterial for a shader file (without the .gdshader extension).
static func make(shader_name: String) -> ShaderMaterial:
	var m := ShaderMaterial.new()
	m.shader = shader(shader_name)
	return m


## New ShaderMaterial for the stone family (StoneView, EarthWallView). One per view: the stone
## shader keeps its state in plain uniforms (instance uniforms were unreliable on Mobile).
static func make_stone() -> ShaderMaterial:
	var m: ShaderMaterial = make("stone")
	m.set_shader_parameter("noise_vol", VfxTextures.noise_volume())
	return m


## Shared material for an additive/unshaded shader that has no per-instance state.
static func shared(shader_name: String) -> ShaderMaterial:
	var m: ShaderMaterial = _shared.get(shader_name)
	if m == null:
		m = make(shader_name)
		_shared[shader_name] = m
	return m


const ARENA_SHADERS: Array[String] = ["arena_ground", "metal_plate", "ledge_stone", "pool_water"]


## Shared arena material (arena_ground / metal_plate / ledge_stone / pool_water). One instance per
## shader so wetness can be driven globally; duplicate() it for a local variation.
static func arena(shader_name: String) -> ShaderMaterial:
	return shared(shader_name)


## "Global" wetness 0..1 for every shared arena surface (flagstones, metal, ledge stone).
static func set_arena_wetness(w: float) -> void:
	for n in ["arena_ground", "metal_plate", "ledge_stone"]:
		shared(n).set_shader_parameter("wetness", clampf(w, 0.0, 1.0))
