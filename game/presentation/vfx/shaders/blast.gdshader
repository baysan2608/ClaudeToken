// Explosion fireball on a sphere (BlastFX): white flash -> orange fireball eaten by noise -> grey
// smoke. `age` 0..1 drives everything; the view scales the node. Premultiplied alpha, one layer.
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_back, fog_disabled;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float age : hint_range(0.0, 1.0) = 0.0;
uniform float seed = 0.0;
uniform float intensity = 1.0;
uniform vec3 smoke : source_color = vec3(0.22, 0.21, 0.20);
uniform vec3 hot_tint : source_color = vec3(1.0, 1.0, 1.0);   // blue blasts tint the fire

varying vec3 v_n;

void vertex() {
	v_n = NORMAL;
	float n = texture(noise_tex, UV * vec2(2.0, 1.0) + seed).r;
	float swell = 0.85 + 0.15 * smoothstep(0.0, 0.4, age);
	VERTEX *= swell * (1.0 + (n - 0.5) * 0.35 * smoothstep(0.05, 0.5, age));
	VERTEX.y += age * age * 0.35;
}

void fragment() {
	float ndv = clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0);
	vec2 uv = UV * vec2(3.0, 2.0) + vec2(seed, -age * 0.6);
	float n = texture(noise_tex, uv).r * 0.6 + texture(noise_tex, uv * 2.3 + 0.2).g * 0.4;
	float flash = 1.0 - smoothstep(0.0, 0.14, age);
	float fire = smoothstep(0.02, 0.1, age) * (1.0 - smoothstep(0.35, 0.65, age + (n - 0.5) * 0.3));
	float sm = smoothstep(0.3, 0.6, age) * (1.0 - smoothstep(0.75, 1.0, age));
	float erode = smoothstep(age * 0.9 - 0.05, age * 0.9 + 0.2, n * 0.8 + pow(ndv, 0.6) * 0.5);
	float T = clamp(0.45 + 0.55 * pow(ndv, 1.5) + (n - 0.5) * 0.5 - age * 0.8, 0.0, 1.0);
	vec3 fc = vfx_blackbody(T) * hot_tint * 3.0;
	vec3 c = vec3(1.0, 0.97, 0.9) * 6.0 * flash + fc * fire;
	float k_fire = clamp(flash + fire, 0.0, 1.0) * erode;
	float k_smoke = sm * erode * 0.75 * (1.0 - fire);
	ALBEDO = (c * k_fire + smoke * k_smoke) * intensity;
	ALPHA = clamp(k_fire * 0.6 + k_smoke, 0.0, 1.0) * intensity;
}
