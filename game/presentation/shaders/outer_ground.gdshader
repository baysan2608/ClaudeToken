// Ground beyond the arena walls: dry grass / packed earth with large-scale colour drift and a darker band
// where it meets the wall.  Pure procedural (3 value-noise taps); fog hides the far edge.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec3 earth_color : source_color = vec3(0.34, 0.27, 0.19);
uniform vec3 grass_color : source_color = vec3(0.3, 0.33, 0.13);
uniform vec3 grass_dry : source_color = vec3(0.5, 0.43, 0.24);

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
	vec2 p = v_world.xz;
	float big = vnoise(p * 0.035) * 0.6 + vnoise(p * 0.11 + 5.0) * 0.4;
	float mid = vnoise(p * 0.7 + 2.0);
	float fine = vnoise(p * 5.0) * 0.6 + vnoise(p * 14.0) * 0.4;
	vec3 grass = mix(grass_color, grass_dry, smoothstep(0.35, 0.75, big));
	grass *= 0.8 + 0.4 * fine;
	float dirt = smoothstep(0.55, 0.3, mid + (big - 0.5) * 0.5);
	vec3 c = mix(grass, earth_color * (0.85 + 0.3 * fine), dirt);
	float d = max(abs(p.x), abs(p.y)) - 17.0;                 // distance from the wall's outer face
	c *= mix(0.55, 1.0, smoothstep(0.0, 5.0, d));
	ALBEDO = c;
	ROUGHNESS = 0.95;
	SPECULAR = 0.15;
}
