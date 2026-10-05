// Arena courtyard flagstones: tileable PBR set (flagstone_albedo / _normal / _orm, 4 m repeat) projected
// in world XZ (every mesh lines up), anti-tiled by height-blended second layer, macro tone variation,
// baked contact-AO map, dust and rain wetness.  Mobile cost: 6 texture fetches (3 with anti_tile off,
// the Low quality tier) + 3 value-noise evaluations.
//
// Wetness: the `wetness` uniform (0 dry .. 1 soaked); VfxMaterials.set_arena_wetness(w) drives the shared
// material.  Without textures bound (e.g. VfxMaterials.make("arena_ground") in the VFX gallery) the shader
// falls back to a plain stone colour.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/env_common.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D orm_tex : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ao_map : hint_default_white, filter_linear, repeat_disable;
uniform vec4 ao_rect = vec4(-17.0, -17.0, 34.0, 34.0);   // world x0, z0, size x, size z of ao_map
uniform float ao_strength : hint_range(0.0, 1.0) = 0.9;
uniform float tile_m = 4.0;                               // metres per texture repeat
uniform bool anti_tile = true;
uniform vec3 tint : source_color = vec3(1.0);
uniform float saturation : hint_range(0.0, 1.5) = 0.72;   // colour grade of the generated stone palette
uniform vec3 fallback_color : source_color = vec3(0.5, 0.47, 0.43);
uniform float normal_strength : hint_range(0.0, 2.0) = 1.0;
uniform float wear : hint_range(0.0, 1.0) = 0.55;        // dust / scuffing amount
uniform float wetness : hint_range(0.0, 1.0) = 0.0;      // 0 dry .. 1 soaked
uniform float arena_half = 16.0;                          // half size of the walled square (grime gathers along it)
uniform float edge_grime : hint_range(0.0, 1.0) = 0.6;

varying vec3 v_world;

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 wn0 = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec2 uv; vec3 T; vec3 B; vec3 N;
	env_face_frame(wn0, v_world, uv, T, B, N);
	vec3 alb; float hgt; vec3 nrm; vec3 orm; float mask;
	env_sample(albedo_tex, normal_tex, orm_tex, uv / tile_m, v_world.xz / tile_m, anti_tile, alb, hgt, nrm, orm, mask);
	if (orm.r + orm.g + orm.b < 0.004) {      // no texture set bound: plain stone
		alb = fallback_color;
		orm = vec3(1.0, 0.85, 0.0);
		nrm = vec3(0.0, 0.0, 1.0);
		hgt = 1.0;
	}
	vec2 p = v_world.xz;
	vec3 albedo = env_grade(alb, saturation) * tint * env_macro_tint(p, 0.55);
	// dust: pale, slightly rougher haze on the high spots, thicker in the worn mid-field
	float dust = smoothstep(0.35, 0.8, env_vnoise(p * 0.7 + 23.0) * 0.7 + env_vnoise(p * 2.3) * 0.3) * wear * smoothstep(0.3, 0.8, hgt);
	albedo = mix(albedo, albedo * 1.14 + vec3(0.012), dust * 0.5);

	// wall-edge grime: blown dirt and damp gather in a ragged band along the boundary walls (and a hint of
	// green where the noise says it stays wet), so the field of play reads as the cleanest, most-walked part
	float de = arena_half - max(abs(p.x), abs(p.y));
	float gn = env_vnoise(p * 1.3 + 4.0);
	float eg = (1.0 - smoothstep(0.0, 1.5 + 1.6 * gn, de)) * edge_grime;
	albedo *= 1.0 - 0.3 * eg;
	albedo = mix(albedo, albedo * vec3(0.8, 0.95, 0.62), eg * smoothstep(0.45, 0.8, env_vnoise(p * 3.1 + 2.0)) * 0.6);

	// wetness: darker, glossy; puddles gather in the joints / low spots
	float wet = clamp(wetness, 0.0, 1.0);
	float puddle = smoothstep(0.35, 0.7, env_vnoise(p * 1.7 + 11.0) + (1.0 - hgt) * 0.3);
	float w_amt = wet * mix(0.55, 1.0, puddle);
	albedo *= mix(1.0, 0.55, w_amt);
	float flat_n = mix(1.0, 0.25, w_amt * puddle);

	NORMAL = env_world_to_view_normal(nrm, T, B, N, normal_strength * flat_n, VIEW_MATRIX);
	ALBEDO = albedo;
	ROUGHNESS = mix(clamp(orm.g + dust * 0.06, 0.2, 1.0), mix(0.32, 0.05, puddle), w_amt);
	SPECULAR = mix(0.45, 0.75, w_amt);
	float ao_b = mix(1.0, env_arena_ao(ao_map, ao_rect, p), ao_strength * smoothstep(0.5, 0.95, N.y));
	AO = orm.r * ao_b;
	AO_LIGHT_AFFECT = 0.55;
}
