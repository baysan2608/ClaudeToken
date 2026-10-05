// Pool basin lining (floor + walls seen through the water): glazed mosaic tile set (pool_tile_*, 1.6 m
// repeat of 10 cm tiles) in world space, with depth absorption below `water_y` and animated caustics
// (cheap 4-iteration interference pattern, drawn only for fragments under the water line).
// Pair with pool_water.gdshader (alpha-blended surface) so the tiles show through.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/env_common.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D orm_tex : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float tile_m = 1.6;
uniform vec3 tint : source_color = vec3(1.0);
uniform vec3 fallback_color : source_color = vec3(0.2, 0.5, 0.52);
uniform float water_y = -0.05;
uniform vec3 absorb_color : source_color = vec3(0.04, 0.2, 0.26);   // colour the water pulls toward with depth
uniform float absorb_per_m = 3.5;
uniform float caustic_strength : hint_range(0.0, 3.0) = 1.1;

varying vec3 v_world;

float caustic(vec2 uv, float t) {
	// three domain-warped layers of |sin| ridges: bright web-like filaments that slide and merge, like light
	// focused by a rippled surface (no rings, no texture fetch)
	vec2 a = uv * 5.0;
	float v = 0.0;
	for (int i = 0; i < 3; i++) {
		float fi = float(i);
		a += 0.55 * vec2(sin(a.y * 1.3 + t * (0.8 + 0.3 * fi)), cos(a.x * 1.1 - t * (0.7 + 0.2 * fi)));
		v += pow(1.0 - abs(sin(a.x * 1.3 + a.y * 0.8 + fi * 1.9)), 7.0);
		a = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8)) * a * 1.37;
	}
	return clamp(v, 0.0, 2.0) * 0.9;
}

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 wn0 = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec2 uv; vec3 T; vec3 B; vec3 N;
	env_face_frame(wn0, v_world, uv, T, B, N);
	vec2 tuv = uv / tile_m;
	vec3 alb = texture(albedo_tex, tuv).rgb;
	vec3 nrm = env_decode_normal(texture(normal_tex, tuv).xyz);
	vec3 orm = texture(orm_tex, tuv).xyz;
	if (orm.r + orm.g + orm.b < 0.004) {
		alb = fallback_color;
		orm = vec3(1.0, 0.3, 0.0);
		nrm = vec3(0.0, 0.0, 1.0);
	}
	float under = clamp((water_y - v_world.y) * 1.0, 0.0, 2.0);
	float is_under = step(0.0005, water_y - v_world.y);
	vec3 albedo = alb * tint;
	// absorption: the deeper a fragment sits, the more it shifts toward the water colour
	float k = 1.0 - exp(-under * absorb_per_m);
	albedo = mix(albedo, absorb_color, k * 0.75 * is_under);
	float cs = caustic(v_world.xz * 0.55 + vec2(0.0, v_world.y * 0.4), TIME * 0.55) * caustic_strength * is_under;
	cs *= (1.0 - 0.6 * k);
	NORMAL = env_world_to_view_normal(normalize(nrm), T, B, N, 1.0, VIEW_MATRIX);
	ALBEDO = albedo;
	EMISSION = vec3(0.55, 0.85, 0.8) * cs * 0.25;
	ROUGHNESS = mix(orm.g, 0.15, is_under);
	SPECULAR = 0.5;
	AO = mix(orm.r, orm.r * 0.8, is_under);
}
