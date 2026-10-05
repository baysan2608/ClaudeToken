// Fresnel sphere shell shared by shell.gdshader (no screen read) and shell_refract.gdshader.
// Null bubbles, vacuum wells, coronas, static fields, wind guards, sound barriers, T3 auras, mines.
//   streak: lines that run from the rim toward the centre (vacuum inflow; negative = outward)
//   crackle: thin bright ridges that flicker (static, corona)
//   core: fill opacity of the face-on centre (0 = rim only, 1 = solid ember core for mines)
#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 color : source_color = vec3(0.6, 0.5, 0.9);
uniform vec3 color_core : source_color = vec3(0.1, 0.05, 0.2);
uniform float opacity = 0.6;
uniform float rim_power = 2.5;
uniform float streak = 0.0;
uniform float crackle = 0.0;
uniform float core = 0.0;
uniform float pulse = 0.0;
uniform float phase = 0.0;
uniform float glow = 1.0;
uniform float seed = 0.0;
#ifdef SHELL_REFRACT
uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform float refract = 0.03;
#endif

void fragment() {
	vec3 view = normalize(VIEW);
	vec3 nn = normalize(NORMAL);
	float ndv = clamp(dot(nn, view), 0.0, 1.0);
	float rim = pow(1.0 - ndv, rim_power);
	vec2 nuv = vec2(UV.x * 3.0 + seed, UV.y * 2.0 + phase * 0.13);
	float n = texture(noise_tex, nuv).r;
	float n2 = texture(noise_tex, nuv * 2.3 + vec2(phase * 0.21, 0.0)).g;
	float lines = 0.0;
	if (abs(streak) > 0.001) {
		float s = fract((1.0 - ndv) * 3.0 + phase * streak + n * 0.35);
		lines = smoothstep(0.75, 1.0, s) * (0.4 + 0.6 * n2) * smoothstep(0.05, 0.6, 1.0 - ndv);
	}
	float crk = 0.0;
	if (crackle > 0.001) {
		float c = 1.0 - abs(texture(noise_tex, vec2(UV.x * 5.0 + floor(phase * 12.0) * 0.37, UV.y * 4.0)).b * 2.0 - 1.0);
		crk = pow(c, 14.0) * crackle;
	}
	float pl = 1.0 + pulse * sin(phase * 9.0);
	float k = (rim * (0.7 + 0.5 * n) + lines * 0.9 + crk) * opacity * pl;
	float fill = core * (0.55 + 0.45 * pow(ndv, 0.7)) * (0.8 + 0.4 * n2) * pl;
#ifdef SHELL_REFRACT
	vec2 off = nn.xy * refract * (1.0 - ndv) * (1.0 + streak * 0.5);
	vec3 bg = textureLod(screen_tex, SCREEN_UV - off, 0.0).rgb;
	bg *= 1.0 - 0.35 * pow(1.0 - ndv, 1.5);
	// unshaded: ALBEDO is the output colour
	ALBEDO = bg + color * glow * k + color_core * fill * glow;
	ALPHA = 1.0;
#else
	ALBEDO = color * glow * k + color_core * glow * fill;
	ALPHA = clamp(k * 0.35 + fill, 0.0, 1.0);
#endif
}
