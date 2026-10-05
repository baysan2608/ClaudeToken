// Brushed dark-steel deck plates: tileable PBR set (metal_plate_*, 2 m repeat = 2x2 one-metre plates with
// bolts, scratches, rust at the seams) in world XZ, brushed streak anisotropy, baked contact AO.
// Without textures bound it falls back to plain gunmetal.  Mobile cost: 3 fetches (+1 AO map), no anti-tile
// (the plate grid is the structure; the per-plate tone/rust already varies across the 2 m repeat).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/env_common.gdshaderinc"

uniform sampler2D albedo_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_tex : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D orm_tex : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ao_map : hint_default_white, filter_linear, repeat_disable;
uniform vec4 ao_rect = vec4(-17.0, -17.0, 34.0, 34.0);
uniform float ao_strength : hint_range(0.0, 1.0) = 0.9;
uniform float tile_m = 2.0;
uniform vec3 tint : source_color = vec3(1.0);
uniform vec3 fallback_color : source_color = vec3(0.22, 0.23, 0.25);
uniform float rust : hint_range(0.0, 1.5) = 1.0;          // scales the baked rust (<1 cleans it up)
uniform float wetness : hint_range(0.0, 1.0) = 0.0;       // 0 dry .. 1 soaked

varying vec3 v_world;

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
		orm = vec3(1.0, 0.4, 0.85);
		nrm = vec3(0.0, 0.0, 1.0);
	}
	vec2 p = v_world.xz;
	// rust scaling: rusty texels have low metalness; treat that as the mask
	float rusty = 1.0 - smoothstep(0.1, 0.7, orm.b);
	vec3 albedo = alb * tint * mix(vec3(1.0), vec3(0.8), (1.0 - rust) * rusty) * env_macro_tint(p, 0.3);
	float wet = clamp(wetness, 0.0, 1.0);
	albedo *= mix(1.0, 0.7, wet);
	NORMAL = env_world_to_view_normal(normalize(nrm), T, B, N, 1.0, VIEW_MATRIX);
	ALBEDO = albedo;
	METALLIC = orm.b * 0.88 * (1.0 - (1.0 - rust) * rusty);
	ROUGHNESS = clamp(mix(orm.g + 0.06, 0.1, wet * 0.8), 0.05, 1.0);
	SPECULAR = 0.6;
	float ao_b = mix(1.0, env_arena_ao(ao_map, ao_rect, p), ao_strength * smoothstep(0.5, 0.95, N.y));
	AO = orm.r * ao_b;
	AO_LIGHT_AFFECT = 0.6;
}
