// Steady flame tongues drawn as one MultiMesh (fire field, fire line, burning status, flame
// columns): the flame.gdshader look in loop mode, but every instance flickers on its own clock.
// Mesh: VfxMesh.flame_mesh() (axis +Y, UV = (angle fraction, y)).
//   INSTANCE_CUSTOM = (random, height scale, width scale, life 0..1)
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float scroll = 0.0;
uniform float intensity = 1.0;
uniform float cover = 0.7;
uniform float flame_h = 0.8;      // m
uniform float flame_w = 0.22;     // m (radius)
uniform vec3 col_deep : source_color = vec3(0.62, 0.07, 0.01);
uniform vec3 col_mid : source_color = vec3(1.0, 0.36, 0.05);
uniform vec3 col_hot : source_color = vec3(1.0, 0.78, 0.30);
uniform vec3 col_white : source_color = vec3(1.0, 0.95, 0.75);
uniform vec3 wind = vec3(0.0);    // m of tip lean (world-ish, applied in local space)

varying float v_y;
varying float v_seed;
varying float v_life;

void vertex() {
	float seed = INSTANCE_CUSTOM.x;
	float hs = INSTANCE_CUSTOM.y;
	float ws = INSTANCE_CUSTOM.z;
	v_life = INSTANCE_CUSTOM.w;
	float y = UV.y;
	float ang = UV.x * 6.2831853;
	float t = scroll * (0.85 + 0.3 * seed) + seed * 7.0;
	float tear = (sin(clamp(y * 1.1, 0.0, 1.0) * 3.14159) * 0.9 + 0.1 * (1.0 - y)) * (1.0 - 0.3 * y);
	float lobes = 1.0 + 0.42 * sin(ang * 3.0 + y * 3.5 + seed * 6.28 + t * 5.0) + 0.24 * sin(ang * 5.0 - y * 6.0 + seed * 3.0 + t * 8.0);
	float r = tear * lobes * flame_w * ws;
	float hh = flame_h * hs * (0.85 + 0.15 * sin(t * 3.1 + seed * 11.0));
	vec3 p = vec3(cos(ang) * r, y * hh, sin(ang) * r);
	p.x += sin(y * 6.0 + t * 6.0) * 0.07 * y * hh + wind.x * y * y;
	p.z += cos(y * 5.0 + t * 5.0) * 0.06 * y * hh + wind.z * y * y;
	VERTEX = p;
	v_y = y;
	v_seed = seed;
}

void fragment() {
	float y = v_y;
	float tt = scroll * (0.85 + 0.3 * v_seed);
	vec2 uv_a = vec2(UV.x * 6.0 + v_seed * 5.0, y * 0.55 - tt * 1.3);
	vec2 uv_b = vec2(UV.x * 2.0 + v_seed * 9.0 + 0.3, y * 1.6 - tt * 0.8);
	float n = mix(texture(noise_tex, uv_b).r, texture(noise_tex, uv_a).g, 0.55);
	float taper = 1.0 - smoothstep(0.7, 1.0, y + (n - 0.5) * 0.3);
	float thr = 0.26 + 0.46 * y;
	float tongues = smoothstep(thr, thr + 0.08, n + 0.26 * (1.0 - y));
	float ndv = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float rimfade = smoothstep(0.02, 0.55, ndv + (n - 0.5) * 0.45);
	float base = smoothstep(0.0, 0.08, y);
	float a = taper * tongues * rimfade * base * intensity * v_life;
	// Orange body with a yellow heart low in each tongue (0.72 / 0.30 left most of the tongue yellow-white).
	float heat = clamp((1.0 - y) * 0.62 + n * 0.30, 0.0, 1.0);
	vec3 c = mix(col_deep, col_mid, smoothstep(0.12, 0.48, heat));
	c = mix(c, col_hot, smoothstep(0.48, 0.80, heat));
	c = mix(c, col_white, smoothstep(0.93, 1.0, heat) * 0.6);
	float k = a * 0.85;
	ALBEDO = c * k;
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
