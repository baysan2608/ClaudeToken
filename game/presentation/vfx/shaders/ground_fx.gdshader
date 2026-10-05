// Ground decals for zones, drawn on a lifted quad like ScorchDecal (no Decal node, no screen read).
//   style 0 quicksand swirl   1 ice floor   2 mud   3 melt pit   4 static field   5 caltrop shade
//   6 frost / rime patch      7 sand drift (under clouds)
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_burley, specular_schlick_ggx;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float style = 0.0;
uniform float phase = 0.0;
uniform float fade : hint_range(0.0, 1.0) = 1.0;
uniform float seed = 0.0;
uniform float heat : hint_range(0.0, 1.0) = 1.0;

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	float ang = atan(p.y, p.x);
	vec2 nuv = UV * 1.4 + vec2(seed * 3.7, seed * 5.9);
	vec4 n = texture(noise_tex, nuv);
	vec4 n2 = texture(noise_tex, nuv * 2.6 + 0.31);
	float ragged = (n.r - 0.5) * 0.35 + (n2.g - 0.5) * 0.12;
	float body = 1.0 - smoothstep(0.72, 0.98, r + ragged);
	vec3 col = vec3(0.5); float rough = 0.9; float spec = 0.3; vec3 emis = vec3(0.0); float a = body;
	if (style < 0.5) {
		float sw = sin(ang * 3.0 + r * 14.0 - phase * 2.5 + n.r * 2.0) * 0.5 + 0.5;
		col = mix(vec3(0.30, 0.22, 0.12), vec3(0.55, 0.43, 0.27), sw * (0.6 + 0.4 * n2.r));
		col *= 0.55 + 0.45 * smoothstep(0.0, 0.6, r);
		rough = 0.92;
		a = body * 0.95;
	} else if (style < 1.5) {
		vec2 g;
		vec4 vr = vfx_voronoi2(UV * 6.0 + seed, g);
		float crack = 1.0 - smoothstep(0.0, 0.04, vr.y);
		col = mix(vec3(0.30, 0.48, 0.58), vec3(0.55, 0.72, 0.80), n.r);
		col = mix(col, vec3(0.85, 0.93, 0.98), crack * 0.6);
		rough = mix(0.06, 0.3, n2.b);
		spec = 0.85;
		a = body * (0.55 + 0.2 * n.g);
	} else if (style < 2.5) {
		float wet = smoothstep(0.4, 0.7, n.r);
		col = mix(vec3(0.12, 0.09, 0.06), vec3(0.06, 0.045, 0.03), wet);
		rough = mix(0.8, 0.1, wet);
		spec = mix(0.3, 0.85, wet);
		a = body * 0.92;
	} else if (style < 3.5) {
		vec2 g;
		vec4 vr = vfx_voronoi2(UV * 5.0 + seed, g);
		float fis = 1.0 - smoothstep(0.0, 0.06 + 0.1 * heat, vr.y);
		float rim = smoothstep(0.55, 0.85, r) * (1.0 - smoothstep(0.85, 1.0, r + ragged));
		col = vec3(0.02, 0.017, 0.015);
		rough = 0.5;
		emis = vfx_blackbody(0.55 + 0.4 * heat * n2.r) * (fis * (1.0 - r * 0.5) + rim * 0.8) * 2.0 * heat;
		a = body;
	} else if (style < 4.5) {
		float c = 1.0 - abs(texture(noise_tex, UV * 2.0 + vec2(floor(phase * 10.0) * 0.23, 0.0)).b * 2.0 - 1.0);
		float line = pow(c, 18.0);
		col = vec3(0.05, 0.06, 0.1);
		emis = vec3(0.55, 0.65, 1.0) * line * 3.0;
		a = body * (0.25 + line);
	} else if (style < 5.5) {
		col = vec3(0.03, 0.03, 0.035);
		a = body * 0.35;
	} else if (style < 6.5) {
		col = mix(vec3(0.75, 0.85, 0.9), vec3(0.95, 0.98, 1.0), n2.r);
		rough = 0.5;
		a = body * smoothstep(0.3, 0.6, n.r + 0.25) * 0.85;
	} else {
		col = mix(vec3(0.42, 0.33, 0.20), vec3(0.62, 0.50, 0.33), n2.r);
		a = body * smoothstep(0.35, 0.65, n.g + 0.2) * 0.7;
	}
	ALBEDO = col;
	ROUGHNESS = rough;
	SPECULAR = spec;
	EMISSION = emis;
	ALPHA = clamp(a * fade, 0.0, 1.0);
}
