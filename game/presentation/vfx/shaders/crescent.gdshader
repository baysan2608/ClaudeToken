// Wind blades and sheets (crescent, wind wall, downdraft ring): a thin ribbon carrying fast speed
// streaks along UV.x, barely visible white with a dust tint. Premultiplied alpha, one layer.
//   UV.x = along the arc (0..1), UV.y = across (0 = leading edge .. 1 = trailing edge)
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 tint : source_color = vec3(0.92, 0.95, 1.0);
uniform vec3 dust : source_color = vec3(0.70, 0.64, 0.55);
uniform float opacity = 0.6;
uniform float scroll = 0.0;
uniform float cover = 0.35;
uniform float age : hint_range(0.0, 1.0) = 0.5;
uniform float dusty = 0.3;

void fragment() {
	float x = UV.x;
	float y = UV.y;
	float n = texture(noise_tex, vec2(x * 1.5 - scroll, y * 0.6)).r;
	float s = texture(noise_tex, vec2(x * 0.6 - scroll * 1.6, y * 9.0)).g;
	float lead = exp(-pow(y / 0.18, 2.0));
	float trail = (1.0 - smoothstep(0.0, 1.0, y)) * smoothstep(0.62, 0.9, s);
	float ends = smoothstep(0.0, 0.12, x) * (1.0 - smoothstep(0.88, 1.0, x));
	float life = smoothstep(0.0, 0.1, age) * (1.0 - smoothstep(0.7, 1.0, age));
	float k = (lead * (0.6 + 0.6 * n) + trail * 0.8) * ends * life * opacity;
	vec3 c = mix(tint, dust, dusty * (0.5 + 0.5 * n));
	ALBEDO = c * k;
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
