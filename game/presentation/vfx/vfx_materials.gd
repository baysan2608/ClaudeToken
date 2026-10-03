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


## Shared material for the stone family (StoneView, EarthWallView). State lives in instance uniforms.
static func stone() -> ShaderMaterial:
	var m: ShaderMaterial = _shared.get("stone")
	if m == null:
		m = make("stone")
		m.set_shader_parameter("noise_vol", VfxTextures.noise_volume())
		_shared["stone"] = m
	return m


## Shared material for an additive/unshaded shader that has no per-instance state.
static func shared(shader_name: String) -> ShaderMaterial:
	var m: ShaderMaterial = _shared.get(shader_name)
	if m == null:
		m = make(shader_name)
		_shared[shader_name] = m
	return m
