// Travelling water wave on LavaWaveView's strip geometry (GroundStripView style "water"): teal body
// with fresnel sky, ripples moving with the flow and a white foam lip along the crest and the front.
// One alpha layer, no screen read. UV / UV2 as in strip_ground.gdshader.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, diffuse_burley, specular_schlick_ggx;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float phase = 0.0;
uniform float melt = 1.0;
uniform float crust = 0.0;       // > 0: the wave freezes (pale, opaque, still)
uniform float seed = 0.0;
uniform vec3 deep : source_color = vec3(0.02, 0.12, 0.17);
uniform vec3 shallow : source_color = vec3(0.10, 0.36, 0.44);

void vertex() {
	// crest curl: the top of the front leans forward a little and bobs
	float crest = UV2.y;
	float near_front = 1.0 - smoothstep(0.0, 1.4, UV2.x);
	VERTEX += NORMAL * (sin(UV.x * 5.0 - phase * 3.0) * 0.03 * crest * (1.0 - crust));
	VERTEX.y += near_front * crest * 0.08 * (1.0 - crust);
}

void fragment() {
	vec2 uv = vec2(UV.x * 0.8 - phase * 0.9, UV.y * 0.9) + seed;
	vec4 n1 = texture(noise_tex, uv);
	vec4 n2 = texture(noise_tex, uv * 2.7 + vec2(0.3, phase * 0.2));
	vec3 view = normalize(VIEW);
	vec3 nn = normalize(NORMAL + vec3(n1.g - 0.5, n2.g - 0.5, 0.0) * 0.35 * (1.0 - crust));
	NORMAL = nn;
	float ndv = clamp(dot(nn, view), 0.0, 1.0);
	float fres = pow(1.0 - ndv, 4.0);
	float crest = UV2.y;
	float front = UV2.x;
	float foam = smoothstep(0.80, 0.98, crest + (n2.r - 0.5) * 0.25) + (1.0 - smoothstep(0.05, 0.6, front + (n1.b - 0.5) * 0.4)) * 0.85;
	foam = clamp(foam * (0.6 + 0.6 * n2.b), 0.0, 1.0) * (1.0 - crust);
	vec3 wn = (INV_VIEW_MATRIX * vec4(nn, 0.0)).xyz;
	vec3 sky = mix(vec3(0.30, 0.42, 0.55), vec3(0.85, 0.92, 1.0), clamp(wn.y * 0.5 + 0.5, 0.0, 1.0));
	vec3 water = mix(deep, shallow, smoothstep(0.1, 0.9, crest) * 0.8 + n1.r * 0.2) + sky * fres * 0.35;
	vec3 ice = vec3(0.40, 0.58, 0.68) * (0.85 + 0.2 * n1.r);
	vec3 c = mix(water, ice, crust);
	c = mix(c, vec3(0.80, 0.86, 0.88), foam);
	ALBEDO = c;
	ROUGHNESS = mix(0.06, 0.4, max(foam, crust * 0.6));
	SPECULAR = 0.8;
	ALPHA = clamp(mix(0.62 + 0.3 * fres, 0.92, crust) + foam * 0.5, 0.0, 1.0);
}
