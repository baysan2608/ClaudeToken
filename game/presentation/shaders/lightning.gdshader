// Camera-facing lightning ribbon (billboarded in the vertex stage so the strip always faces the
// camera without rebuilding the mesh). Additive, unshaded, one draw call for main bolt + branches.
// Mesh contract (two vertices per polyline point):
//   VERTEX = point (world space, node must be identity / top_level)   NORMAL = smoothed tangent
//   UV = (side 0/1, distance along bolt in m)
//   COLOR = (half width / 0.25 m, branch life 0..1, 0, brightness variation)
shader_type spatial;
render_mode blend_add, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform float age : hint_range(0.0, 1.0) = 0.0;      // 0..1 over the strike duration
uniform float duration = 0.2;
uniform float intensity = 1.0;
uniform vec3 core_color : source_color = vec3(1.0, 0.96, 1.0);
uniform vec3 halo_color : source_color = vec3(0.42, 0.52, 1.0);
uniform float jitter = 0.028;

varying float v_across;
varying float v_dist;
varying float v_life;
varying float v_var;

float hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

void vertex() {
	vec3 wpos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 tdir = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 to_cam = CAMERA_POSITION_WORLD - wpos;
	vec3 side = cross(tdir, to_cam);
	side = side / max(length(side), 1e-5);
	float s = UV.x * 2.0 - 1.0;
	float step_t = floor(age * duration * 40.0);
	// quantised wiggle: the bolt re-seats itself a few times while it lives (crackle)
	vec3 j = vec3(hash11(UV.y * 17.0 + step_t * 3.7), hash11(UV.y * 23.0 + step_t * 5.3 + 1.0), hash11(UV.y * 29.0 + step_t * 7.1 + 2.0)) - 0.5;
	wpos += j * jitter * 2.0;
	float hw = COLOR.r * 0.25;
	wpos += side * s * hw;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wpos, 1.0);
	v_across = s;
	v_dist = UV.y;
	v_life = COLOR.g;
	v_var = COLOR.a;
}

void fragment() {
	float x = abs(v_across);
	float core = exp(-x * x * 20.0);
	float halo = exp(-x * x * 2.4);
	// strike flash, then flicker with a couple of re-strikes, then fade
	float t = age;
	float step_t = floor(t * duration * 60.0);
	float flick = mix(0.35, 1.0, step(0.45, hash11(step_t * 1.31 + 3.0)));
	float env = (1.0 - smoothstep(0.55, 1.0, t)) * mix(1.0, flick, smoothstep(0.08, 0.3, t));
	// branches die earlier than the main bolt
	float life_mask = 1.0 - smoothstep(0.25 + 0.5 * v_life, 0.4 + 0.6 * v_life, t) * (1.0 - step(0.999, v_life));
	// small brightness crackle along the bolt
	float var_b = 0.85 + 0.30 * sin(v_dist * 9.0 + step_t * 2.3 + hash11(step_t) * 6.0) * v_var;
	float e = env * life_mask * var_b * intensity;
	vec3 c = core_color * core * 3.0 + halo_color * halo * 1.5;
	ALBEDO = c * e;
	ALPHA = clamp((core + halo * 0.7) * e, 0.0, 1.0);
}
