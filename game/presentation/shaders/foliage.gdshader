// Stylised shrub / tree foliage: lit blobs and cones with a vertical colour gradient, leaf-clump mottling in
// world space, and a light wind sway (vertex only).  Used for planters and the distant tree line.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec3 color_low : source_color = vec3(0.07, 0.15, 0.05);
uniform vec3 color_high : source_color = vec3(0.3, 0.42, 0.12);
uniform float sway : hint_range(0.0, 0.3) = 0.04;
uniform float gradient_height = 1.0;          // model-space height over which low -> high blends

varying vec3 v_world;
varying float v_h;

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
	vec3 wp = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float w = sin(TIME * 1.3 + wp.x * 0.7 + wp.z * 0.5) + 0.5 * sin(TIME * 2.9 + wp.x * 1.9);
	float h = clamp(VERTEX.y / max(gradient_height, 0.01) + 0.5, 0.0, 1.5);   // meshes are centred on the origin
	VERTEX.x += w * sway * h * 0.5;
	VERTEX.z += w * sway * h * 0.35;
	v_world = wp;
	v_h = h;
}

void fragment() {
	float clump = vnoise(v_world.xz * 6.0 + v_world.y * 3.0) * 0.6 + vnoise(v_world.xz * 17.0 + v_world.y * 9.0 + 3.0) * 0.4;
	vec3 c = mix(color_low, color_high, clamp(v_h * 0.8 + (clump - 0.5) * 0.9, 0.0, 1.0));
	ALBEDO = c;
	ROUGHNESS = 0.85;
	SPECULAR = 0.2;
	AO = mix(0.55, 1.0, clamp(v_h, 0.0, 1.0));
	BACKLIGHT = c * 0.3;
}
