// Cut stone for ledges / walls / pillars / plinths / coping: two tileable PBR sets in world space.
// Vertical faces use the ashlar wall set (wall_*, 3 m repeat, 0.3 m courses); top faces use the cut
// slab set (ledge_cap_*, 2.4 m repeat).  Faces pick their set by orientation (axis-aligned boxes), so
// neither branch costs more than one set's fetches (6 with anti_tile, 3 without).
// Procedural extras: rising damp / moss and ground grime on the lower part of vertical faces, sun-bleached
// upper courses, baked contact AO from the arena AO map.  Without textures bound it falls back to a plain colour.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/env_common.gdshaderinc"

uniform sampler2D wall_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D wall_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D wall_orm : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D cap_albedo : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D cap_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D cap_orm : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float ao_strength : hint_range(0.0, 1.0) = 0.9;
uniform float wall_tile_m = 3.0;
uniform float cap_tile_m = 2.4;
uniform bool anti_tile = true;
uniform vec3 tint : source_color = vec3(1.0);
uniform float saturation : hint_range(0.0, 1.5) = 0.72;   // colour grade of the generated stone palette
uniform vec3 fallback_color : source_color = vec3(0.47, 0.45, 0.41);
uniform vec3 moss_color : source_color = vec3(0.16, 0.24, 0.08);
uniform float grime : hint_range(0.0, 1.0) = 0.6;         // ground grime + rising damp on vertical faces
uniform float moss_amount : hint_range(0.0, 1.0) = 0.55;
uniform float wetness : hint_range(0.0, 1.0) = 0.0;       // 0 dry .. 1 soaked (see VfxMaterials.set_arena_wetness)

varying vec3 v_world;

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 wn0 = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec2 uv; vec3 T; vec3 B; vec3 N;
	env_face_frame(wn0, v_world, uv, T, B, N);
	bool top = N.y > 0.5;
	vec3 alb; float hgt; vec3 nrm; vec3 orm; float mask;
	if (top) {
		env_sample(cap_albedo, cap_normal, cap_orm, uv / cap_tile_m, v_world.xz / cap_tile_m, anti_tile, alb, hgt, nrm, orm, mask);
	} else {
		// vertical: anti-tile mask keyed on the horizontal run + height so courses vary along the wall
		vec2 wm = vec2(v_world.x + v_world.z, v_world.y * 2.3) / wall_tile_m;
		env_sample(wall_albedo, wall_normal, wall_orm, uv / wall_tile_m, wm, anti_tile, alb, hgt, nrm, orm, mask);
	}
	if (orm.r + orm.g + orm.b < 0.004) {
		alb = fallback_color;
		orm = vec3(1.0, 0.85, 0.0);
		nrm = vec3(0.0, 0.0, 1.0);
		hgt = 1.0;
	}
	vec2 p = top ? v_world.xz : vec2(v_world.x + v_world.z, v_world.y * 2.0);
	vec3 albedo = env_grade(alb, saturation) * tint * env_macro_tint(p * 0.6, 0.5);

	float low = top ? 0.0 : 1.0 - smoothstep(0.0, 0.9, v_world.y);
	float damp_n = env_vnoise(vec2(v_world.x + v_world.z, v_world.y) * 1.3 + 7.0);
	float rise = low * smoothstep(0.25, 0.8, damp_n + low * 0.35);
	// ground grime / rising damp darken, a little moss in the damp band (lower in the joints, hgt < 0.45)
	albedo *= 1.0 - grime * 0.45 * rise;
	float moss = clamp(rise * moss_amount * smoothstep(0.2, 0.6, damp_n) * (1.3 - hgt), 0.0, 1.0) * 0.6;
	albedo = mix(albedo, moss_color * (0.8 + 0.4 * damp_n), moss);
	// sun-bleached upper courses of tall faces
	float high = top ? 0.0 : smoothstep(1.4, 3.4, v_world.y);
	albedo = mix(albedo, albedo * 1.12 + vec3(0.015), high * 0.5);

	float wet = clamp(wetness, 0.0, 1.0);
	albedo *= mix(1.0, 0.6, wet);
	NORMAL = env_world_to_view_normal(nrm, T, B, N, 1.0, VIEW_MATRIX);
	ALBEDO = albedo;
	ROUGHNESS = mix(clamp(orm.g, 0.2, 1.0), 0.25, wet) * (1.0 - 0.1 * moss) + 0.1 * moss;
	SPECULAR = 0.4 + 0.3 * wet;
	AO = orm.r * (1.0 - 0.3 * low * ao_strength);
	AO_LIGHT_AFFECT = 0.55;
}
