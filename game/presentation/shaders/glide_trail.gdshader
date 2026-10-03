// Glide trail: a soft camera-facing wind ribbon that follows a moving point and fades with age.
//   VERTEX = point (world space; node is top_level)  NORMAL = tangent
//   UV = (side 0/1, distance along m)   COLOR = (half width / 0.5 m, age 0..1 (0 = newest), 0, 1)
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float flow = 0.0;
uniform float opacity = 0.32;
uniform vec3 tint : source_color = vec3(0.86, 0.90, 0.95);

varying float v_across;
varying float v_age;
varying float v_dist;

void vertex() {
	vec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 tdir = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 to_cam = CAMERA_POSITION_WORLD - wpos;
	vec3 side = cross(tdir, to_cam);
	side = side / max(length(side), 1e-5);
	float s = UV.x * 2.0 - 1.0;
	float hw = COLOR.r * 0.5;
	wpos += side * s * hw;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wpos, 1.0);
	v_across = s;
	v_age = COLOR.g;
	v_dist = UV.y;
}

void fragment() {
	float n = texture(noise_tex, vec2(v_dist * 0.8 - flow, v_across * 0.4 + 0.5)).g;
	float x = abs(v_across);
	float body = smoothstep(1.0, 0.15, x + (n - 0.5) * 0.5);
	float life = (1.0 - v_age) * smoothstep(0.0, 0.08, v_age + 0.04);
	ALBEDO = tint;
	ALPHA = clamp(body * life * life * opacity * (0.6 + 0.8 * n), 0.0, 1.0);
}
