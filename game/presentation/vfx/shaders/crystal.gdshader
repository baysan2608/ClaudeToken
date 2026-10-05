// Faceted transparent solids: glass (clear, faintly green, bright edges) and ice (pale cyan,
// frosted). One alpha layer, back faces culled, depth written so a cluster sorts against itself.
// No screen read. Flat-shaded mesh contract (VfxMesh.crystal_mesh):
//   UV = (across the facet 0..1, along the crystal 0..1)   UV2 = (crystal random, crystal height frac)
shader_type spatial;
render_mode blend_mix, depth_draw_always, cull_back, diffuse_burley, specular_schlick_ggx;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 tint : source_color = vec3(0.70, 0.90, 0.98);
uniform float frost : hint_range(0.0, 1.0) = 0.4;     // 0 clear glass .. 1 snow-white rime
uniform float heat : hint_range(0.0, 1.0) = 0.0;      // molten glass glow
uniform float opacity : hint_range(0.0, 1.0) = 0.55;
uniform float edge_glow = 0.6;
uniform float rise : hint_range(0.0, 1.0) = 1.0;      // crystals grow out of the ground (per-crystal stagger)
uniform float rise_height = 1.0;
uniform float shatter : hint_range(0.0, 1.0) = 0.0;   // crack lines brighten before a break

varying float v_rand;
varying float v_hf;

void vertex() {
	v_rand = UV2.x;
	v_hf = UV2.y;
	float r = clamp(rise * 1.4 - v_rand * 0.4, 0.0, 1.0);
	r = 1.0 - pow(1.0 - r, 3.0);
	VERTEX.y -= (1.0 - r) * rise_height;
}

void fragment() {
	vec3 view = normalize(VIEW);
	vec3 n = normalize(NORMAL);
	float ndv = clamp(dot(n, view), 0.0, 1.0);
	float fres = pow(1.0 - ndv, 3.0);
	vec2 nuv = vec2(UV.x * 0.6 + v_rand * 7.0, UV.y * 1.4 + v_rand * 3.0);
	vec4 nz = texture(noise_tex, nuv);
	float edge = 1.0 - smoothstep(0.0, 0.07, min(UV.x, 1.0 - UV.x));
	float fr = clamp(frost * (0.55 + 0.9 * nz.r) , 0.0, 1.0);
	float crack = smoothstep(0.86, 0.95, nz.b) * shatter;
	vec3 body = tint * (0.18 + 0.55 * fr);
	ALBEDO = body;
	ROUGHNESS = mix(0.04, 0.5, fr);
	SPECULAR = 0.9;
	// inner scatter: thick (face-on) parts glow faintly in the tint, so ice reads blue on any ground
	EMISSION = tint * (edge * edge_glow * (0.35 + 0.65 * fres) + crack * 1.5) * 0.6
		+ tint * tint * 0.22 * (1.0 - fres) * (0.6 + 0.4 * nz.r)
		+ vfx_heat(heat, nz.g);
	ALPHA = clamp(opacity * (0.35 + 0.55 * fres + 0.45 * fr) + edge * 0.35 + heat * 0.5, 0.0, 1.0);
}
