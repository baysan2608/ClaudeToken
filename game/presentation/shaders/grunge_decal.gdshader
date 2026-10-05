// Flat ground decal quad: soil / moss / dust grime that gathers along wall bases and corners.
// Alpha is built from world-space noise and the quad's UV.y (0 = hugging the wall, 1 = outer fringe), so one
// long strip per wall gives a ragged, non-repeating band.  Blended (not additive), no depth write.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform vec3 soil_color : source_color = vec3(0.1, 0.08, 0.055);
uniform vec3 moss_color : source_color = vec3(0.12, 0.2, 0.07);
uniform float strength : hint_range(0.0, 1.0) = 0.55;
uniform float reach : hint_range(0.1, 1.0) = 0.8;       // how far (fraction of the quad) the grime creeps out

varying vec3 v_world;

float h21(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float n = vnoise(v_world.xz * 1.1) * 0.55 + vnoise(v_world.xz * 3.7 + 9.0) * 0.3 + vnoise(v_world.xz * 11.0) * 0.15;
	float fall = 1.0 - smoothstep(0.0, reach * (0.35 + 0.9 * n), UV.y);
	float a = fall * smoothstep(0.25, 0.7, n + fall * 0.35) * strength;
	float m = smoothstep(0.45, 0.8, vnoise(v_world.xz * 2.3 + 31.0)) * fall;
	ALBEDO = mix(soil_color, moss_color, m * 0.8) * (0.8 + 0.4 * n);
	ALPHA = clamp(a, 0.0, 0.85);
	ROUGHNESS = 0.95;
	SPECULAR = 0.1;
}
