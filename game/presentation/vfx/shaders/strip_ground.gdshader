// Opaque ground-hugging strips that reuse LavaWaveView's geometry (GroundStripView):
//   style 0 sand surge   granular ochre wave, ripples travelling with the flow, pale dusty crest
//   style 1 rime / ice   pale frosted ice sheet / ridge, crystal cells, glossy
//   style 2 mud          dark wet earth, glossy puddles
// UV = (metres along the path, metres across), UV2 = (metres to the front, cross-section v: 1 = crest).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float style = 0.0;
uniform float phase = 0.0;      // flow phase (m), integrated by the view
uniform float melt = 1.0;       // LavaWaveView state (1 = moving): reused as "liveliness"
uniform float crust = 0.0;      // settled / frozen amount
uniform float seed = 0.0;

void fragment() {
	vec2 uv = vec2(UV.x - phase, UV.y) + vec2(seed * 3.1, seed * 1.7);
	float crest = UV2.y;
	float front = UV2.x;
	vec4 n1 = texture(noise_tex, uv * 0.6);
	vec4 n2 = texture(noise_tex, uv * 2.9 + 0.37);
	vec3 col; float rough; float spec = 0.4; vec3 emis = vec3(0.0);
	if (style < 0.5) {
		float rip = sin(uv.x * 9.0 + n1.r * 4.0) * 0.5 + 0.5;
		float grain = n2.r * 0.6 + n2.g * 0.4;
		col = mix(vec3(0.13, 0.085, 0.045), vec3(0.34, 0.24, 0.13), smoothstep(0.2, 0.9, crest * 0.7 + grain * 0.4));
		col *= 0.7 + 0.45 * rip * (1.0 - crust * 0.6);
		col = mix(col, vec3(0.45, 0.36, 0.22), smoothstep(0.88, 1.0, crest) * 0.45 * (1.0 - crust));
		rough = 0.95;
		spec = 0.2;
		NORMAL = vfx_bump(VERTEX, NORMAL, grain * 0.03 + rip * 0.02, 1.0);
	} else if (style < 1.5) {
		vec2 g;
		vec4 vr = vfx_voronoi2(uv * 2.2, g);
		float cell = smoothstep(0.0, 0.05, vr.y);
		float fr = smoothstep(0.35, 0.9, n1.r * 0.7 + n2.b * 0.5);
		col = mix(vec3(0.20, 0.36, 0.46), vec3(0.55, 0.68, 0.76), clamp(fr * 0.8 + crest * 0.3, 0.0, 1.0));
		col = mix(col * 1.15, col, cell);
		rough = mix(0.12, 0.6, fr);
		spec = 0.75;
		emis = vec3(0.10, 0.18, 0.24) * (1.0 - cell) * 0.6;
	} else {
		float wet = smoothstep(0.45, 0.7, n1.r);
		col = mix(vec3(0.10, 0.075, 0.05), vec3(0.20, 0.15, 0.10), n2.r);
		col *= 1.0 - 0.45 * wet;
		rough = mix(0.75, 0.12, wet);
		spec = mix(0.3, 0.8, wet);
	}
	float tail = smoothstep(0.0, 0.2, crest);
	ALBEDO = col * (0.75 + 0.25 * tail);
	ROUGHNESS = rough;
	SPECULAR = spec;
	EMISSION = emis;
}
