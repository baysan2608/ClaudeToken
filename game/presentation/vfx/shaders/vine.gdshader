// Living vines, roots and lattice (VineView tubes). Deep green skin with darker spiral grooves and
// thorn bumps, bark-brown toward thick bases; grows along the tube (vertices past `grow` collapse
// onto the centreline, no discard) and chars with `burn` (black skin, ember cracks).
// Mesh contract: UV = (angle fraction, metres along), CUSTOM0 = (centreline xyz, arc fraction 0..1),
// UV2 = (tube random, tube radius m).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float grow : hint_range(0.0, 1.0) = 1.0;
uniform float burn : hint_range(0.0, 1.0) = 0.0;
uniform float frozen : hint_range(0.0, 1.0) = 0.0;
uniform float sway = 0.03;
uniform float phase = 0.0;
uniform vec3 green : source_color = vec3(0.10, 0.30, 0.07);
uniform vec3 bark : source_color = vec3(0.20, 0.13, 0.07);

varying float v_s;
varying float v_rand;

void vertex() {
	vec3 c = CUSTOM0.xyz;
	float s = CUSTOM0.w;
	float k = 1.0 - smoothstep(grow - 0.06, grow, s);
	vec3 off = VERTEX - c;
	vec3 sw = vec3(sin(phase * 1.3 + s * 6.0 + UV2.x * 9.0), 0.0, cos(phase * 1.1 + s * 5.0 + UV2.x * 7.0)) * sway * s * (1.0 - frozen);
	VERTEX = c + off * k + sw;
	v_s = s;
	v_rand = UV2.x;
}

void fragment() {
	vec2 uv = vec2(UV.x + UV.y * 0.6, UV.y * 1.5) + v_rand * 5.0;
	vec4 n1 = texture(noise_tex, uv * vec2(1.0, 0.7));
	vec4 n2 = texture(noise_tex, uv * 3.1 + 0.4);
	float groove = smoothstep(0.55, 0.9, sin((UV.x * 6.0 + UV.y * 2.2) * 6.2831853 * 0.5) * 0.5 + 0.5);
	float thorn = smoothstep(0.82, 0.95, n2.r);
	vec3 col = mix(green * 1.35, green * 0.55, groove * 0.7);
	col = mix(col, bark, smoothstep(0.6, 0.0, v_s) * 0.55 + n1.r * 0.15);
	col = mix(col, vec3(0.35, 0.30, 0.12), thorn * 0.6);
	NORMAL = vfx_bump(VERTEX, NORMAL, groove * 0.01 + thorn * 0.02, 1.0);
	// burn: char from the noise up, ember cracks where it is still burning
	float charred = smoothstep(0.0, 1.0, burn * 1.6 - n1.g * 0.6);
	col = mix(col, vec3(0.02, 0.018, 0.015), charred);
	vec3 emis = vfx_blackbody(0.7) * smoothstep(0.75, 0.95, n2.g) * charred * (1.0 - smoothstep(0.85, 1.0, burn)) * 2.0;
	col = mix(col, vec3(0.70, 0.84, 0.92), frozen * 0.7);
	ALBEDO = col;
	ROUGHNESS = mix(0.55, 0.9, charred);
	SPECULAR = 0.35 + 0.3 * frozen;
	EMISSION = emis;
}
