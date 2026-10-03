// Crackling aim line: a thin camera-facing ribbon from p_from to p_to (world space) with quantised
// electric jitter that calms down as the charge completes, plus a small glow dot at the source.
// Mesh: line vertices UV = (side 0/1, along 0..1), COLOR.a = 0; glow quad vertices COLOR.a = 1 with
// UV = corner (0/1, 0/1). Additive, unshaded, one draw call.
shader_type spatial;
render_mode blend_add, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform vec3 p_from = vec3(0.0);
uniform vec3 p_to = vec3(0.0, 0.0, -3.0);
uniform float charge : hint_range(0.0, 1.0) = 0.0;
uniform float intensity = 1.0;
uniform vec3 core_color : source_color = vec3(0.92, 0.97, 1.0);
uniform vec3 halo_color : source_color = vec3(0.35, 0.55, 1.0);

varying float v_across;
varying float v_s;
varying float v_glow;
varying vec2 v_corner;

float hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

void vertex() {
	vec3 dir = p_to - p_from;
	float len = max(length(dir), 1e-4);
	dir /= len;
	v_glow = COLOR.a;
	if (COLOR.a > 0.5) {
		vec2 c = UV * 2.0 - 1.0;
		float size = 0.05 + 0.13 * charge;
		vec3 wp = p_from + (INV_VIEW_MATRIX[0].xyz * c.x + INV_VIEW_MATRIX[1].xyz * c.y) * size;
		POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wp, 1.0);
		v_corner = c;
		v_across = 0.0;
		v_s = 0.0;
	} else {
		float s = UV.y;
		vec3 center = p_from + dir * len * s;
		vec3 to_cam = CAMERA_POSITION_WORLD - center;
		vec3 side = cross(dir, to_cam);
		side = side / max(length(side), 1e-5);
		vec3 up2 = cross(dir, side);
		float step_t = floor(TIME * 26.0);
		float env = sin(3.14159 * s);
		float amp = mix(0.12, 0.018, charge);
		float n1 = hash11(floor(s * 22.0) + step_t * 1.7) - 0.5;
		float n2 = hash11(floor(s * 22.0) + step_t * 2.3 + 7.0) - 0.5;
		// interpolate between the stepped noise values to keep the line continuous
		float fs = fract(s * 22.0);
		float m1 = hash11(floor(s * 22.0) + 1.0 + step_t * 1.7) - 0.5;
		float m2 = hash11(floor(s * 22.0) + 1.0 + step_t * 2.3 + 7.0) - 0.5;
		n1 = mix(n1, m1, fs);
		n2 = mix(n2, m2, fs);
		center += (side * n1 + up2 * n2) * 2.0 * amp * env;
		float hw = mix(0.010, 0.026, charge) * (0.5 + 0.5 * env);
		float sd = UV.x * 2.0 - 1.0;
		center += side * sd * hw;
		POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(center, 1.0);
		v_across = sd;
		v_s = s;
		v_corner = vec2(0.0);
	}
}

void fragment() {
	float step_t = floor(TIME * 26.0);
	if (v_glow > 0.5) {
		float d = dot(v_corner, v_corner);
		float g = exp(-d * 5.0) * (0.35 + 0.65 * charge) * (0.8 + 0.2 * hash11(step_t));
		ALBEDO = mix(halo_color, core_color, exp(-d * 14.0)) * g * 2.2 * intensity;
		ALPHA = clamp(g, 0.0, 1.0);
	} else {
		float x = abs(v_across);
		float core = exp(-x * x * 7.0);
		float halo = exp(-x * x * 1.6);
		// travelling crackle dashes
		float dash = 0.55 + 0.45 * hash11(floor(v_s * 60.0) + step_t * 3.1);
		float fade = smoothstep(0.0, 0.08, v_s) * (1.0 - smoothstep(0.88, 1.0, v_s));
		float b = (0.30 + 0.70 * charge) * dash * fade * intensity;
		ALBEDO = (core_color * core * 2.4 + halo_color * halo * 0.8) * b;
		ALPHA = clamp((core + halo * 0.4) * b, 0.0, 1.0);
	}
}
