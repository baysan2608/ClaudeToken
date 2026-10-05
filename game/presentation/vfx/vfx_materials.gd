class_name VfxMaterials
extends RefCounted
## Shader / material cache for the VFX layer. Shaders are loaded once; materials that carry only
## per-instance uniforms (stone family) are shared, everything else is created per effect so that
## pooled effects can animate their own uniforms.

const SHADER_DIR: String = "res://presentation/shaders/"
## Moveset VFX shaders (cloud, crystal, metal, ring, shell, vortex, strips, flames, vine, beam, blast...).
const FX_SHADER_DIR: String = "res://presentation/vfx/shaders/"

## Global quality switch. False drops every screen-texture read (water refraction, air-push
## distortion), so the engine skips the per-frame opaque-framebuffer copy. Set it BEFORE creating
## the water / air effects (e.g. from the graphics options); it only affects newly made materials.
static var screen_refraction: bool = true

static var _shaders: Dictionary = {}
static var _shared: Dictionary = {}


static func shader(shader_name: String) -> Shader:
	var s: Shader = _shaders.get(shader_name)
	if s == null:
		s = load(SHADER_DIR + shader_name + ".gdshader") as Shader
		_shaders[shader_name] = s
	return s


## New ShaderMaterial for a moveset shader in presentation/vfx/shaders, with the shared 2D noise bound
## to `noise_tex` (every moveset shader that has the uniform uses it).
static func make_fx(shader_name: String) -> ShaderMaterial:
	var key := "fx/" + shader_name
	var s: Shader = _shaders.get(key)
	if s == null:
		s = load(FX_SHADER_DIR + shader_name + ".gdshader") as Shader
		_shaders[key] = s
	var m := ShaderMaterial.new()
	m.shader = s
	m.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	return m


## Lite path for the moveset effects (quality 0): no screen reads, fewer particles / puffs.
static func lite() -> bool:
	return not screen_refraction


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


## Water material (screen refraction or the "lite" alpha-only variant, see screen_refraction).
static func make_water() -> ShaderMaterial:
	var m: ShaderMaterial = make("water" if screen_refraction else "water_lite")
	m.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	m.set_shader_parameter("noise_vol", VfxTextures.noise_volume())
	return m


## Air-push cone material (screen distortion or the "lite" tinted band, see screen_refraction).
static func make_air_push() -> ShaderMaterial:
	var m: ShaderMaterial = make("air_push" if screen_refraction else "air_push_lite")
	m.set_shader_parameter("noise_tex", VfxTextures.noise_2d())
	return m
