// Flat ring / disc effects on a unit quad (UV 0..1): shock rings, sound rings, charge rings, ground
// ripples, the spiral inflow of a vacuum well, a spinning disc's blur ring. Premultiplied alpha:
// `cover` 0 = pure glow (additive), 1 = covering tint. Optional camera billboard.
//   style 0 ring band, 1 spiral inflow (arms turning inward), 2 filled soft disc, 3 arc blur ring
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 color : source_color = vec3(1.0);
uniform float alpha = 1.0;
uniform float radius = 0.8;      // band centre, fraction of the quad half size
uniform float width = 0.06;
uniform float cover = 0.3;
uniform float noise_amt = 0.35;
uniform float style = 0.0;
uniform float phase = 0.0;
uniform float arms = 3.0;
uniform float seed = 0.0;
uniform float billboard = 0.0;
uniform float glow = 1.0;        // HDR multiplier on the colour

void vertex() {
	if (billboard > 0.5) {
		mat4 mw = mat4(normalize(INV_VIEW_MATRIX[0]), normalize(INV_VIEW_MATRIX[1]), normalize(INV_VIEW_MATRIX[2]), MODEL_MATRIX[3]);
		MODELVIEW_MATRIX = VIEW_MATRIX * mw * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0), vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	}
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float d = length(p);
	float ang = atan(p.y, p.x);
	float n = texture(noise_tex, vec2(ang * 0.477 + seed, d * 0.8 - phase * 0.3)).r;
	float k = 0.0;
	if (style < 0.5) {
		float x = (d - radius + (n - 0.5) * noise_amt * width * 2.0) / max(width, 1e-3);
		k = exp(-x * x * 2.2);
		k *= 0.75 + 0.5 * texture(noise_tex, vec2(ang * 1.2 + seed * 3.0, phase * 0.5)).g;
	} else if (style < 1.5) {
		float s = sin(ang * arms + d * 9.0 + phase * 6.0 + (n - 0.5) * 2.0);
		k = smoothstep(0.35, 1.0, s) * smoothstep(1.0, 0.55, d) * smoothstep(0.04, 0.3, d);
		k += exp(-pow((d - 0.12) / 0.08, 2.0)) * 0.6;
	} else if (style < 2.5) {
		k = (1.0 - smoothstep(radius * 0.4, radius, d + (n - 0.5) * noise_amt));
	} else {
		float x = (d - radius) / max(width, 1e-3);
		float band = exp(-x * x * 2.0);
		float streak = 0.55 + 0.45 * sin(ang * 2.0 - phase * 30.0);
		k = band * streak * (0.7 + 0.6 * n);
	}
	k = clamp(k * alpha, 0.0, 1.0);
	ALBEDO = color * glow * k;
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
