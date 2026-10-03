// Arena courtyard flagstones (procedural, self-contained, no textures).
// World-space running-bond flagstones with per-stone tone, bevelled seams, subtle wear and grime.
// Expects mostly horizontal (+Y up) surfaces; the layout is in world XZ so adjacent meshes line up.
// Cost: 2 value-noise evaluations + tile hashing, no texture fetches.
//
// Wetness: set the per-instance `wetness` (0 dry .. 1 soaked) with
//   mesh_instance.set_instance_shader_parameter("wetness", 0.6)
// or override globally by assigning the `wetness_global` uniform on the material.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec2 tile_size = vec2(1.5, 0.95);          // flagstone width / depth in metres
uniform float seam_width : hint_range(0.005, 0.08) = 0.022;
uniform vec3 stone_a : source_color = vec3(0.50, 0.47, 0.43);   // warm sandstone
uniform vec3 stone_b : source_color = vec3(0.38, 0.38, 0.38);   // cool grey granite
uniform vec3 seam_color : source_color = vec3(0.12, 0.11, 0.10);
uniform float wear : hint_range(0.0, 1.0) = 0.55;  // dust / scuffing amount
uniform float wetness_global : hint_range(0.0, 1.0) = 0.0;
instance uniform float wetness : hint_range(0.0, 1.0) = 0.0;

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
	// running bond: rows of equal depth, per-row random stone width and offset
	float row = floor(p.y / tile_size.y);
	float rw = tile_size.x * (0.78 + 0.44 * h21(vec2(row, 3.7)));
	float xo = h21(vec2(row, 9.1)) * rw;
	float col = floor((p.x + xo) / rw);
	vec2 cell = vec2((p.x + xo) / rw - col, p.y / tile_size.y - row);
	vec2 id = vec2(col, row);
	float th = h21(id + 17.0);
	float th2 = h21(id + 41.0);

	// distance to the nearest stone edge in metres, and which edge
	vec2 dm = vec2(min(cell.x, 1.0 - cell.x) * rw, min(cell.y, 1.0 - cell.y) * tile_size.y);
	float d = min(dm.x, dm.y);
	float aa = fwidth(d) * 1.2;
	float seam = (1.0 - smoothstep(seam_width * 0.4 - aa, seam_width * 1.3 + aa, d)) * (1.0 - smoothstep(seam_width * 4.0, seam_width * 12.0, aa));
	float bevel = 1.0 - smoothstep(0.0, 0.05, d);

	// tone: per-stone blend between warm and cool stone + low frequency mottling
	float mott = vnoise(p * 0.9 + th * 13.0);
	vec3 albedo = mix(stone_a, stone_b, smoothstep(0.25, 0.75, th * 0.7 + mott * 0.45));
	albedo *= 0.82 + 0.3 * th2;
	// wear: worn lighter dust in the middle of stones, darker grime in seams and corners
	float grain = vnoise(p * 6.5 + th * 5.0);
	albedo *= 1.0 + (grain - 0.5) * 0.35;
	albedo = mix(albedo, albedo * 1.18 + vec3(0.02), wear * smoothstep(0.1, 0.9, mott) * 0.45);
	albedo = mix(albedo, seam_color, seam * 0.75);
	albedo *= 1.0 - 0.28 * (1.0 - smoothstep(0.0, 0.12, d)) * (1.0 - seam);

	// per-stone tilt and bevel normal (world space -> view space)
	vec2 tilt = (vec2(h21(id + 5.0), h21(id + 9.0)) - 0.5) * 0.06;
	vec2 toward_edge = dm.x < dm.y ? vec2(cell.x < 0.5 ? -1.0 : 1.0, 0.0) : vec2(0.0, cell.y < 0.5 ? -1.0 : 1.0);
	vec2 bump = tilt + toward_edge * bevel * 0.55 + (vec2(grain, mott) - 0.5) * 0.10;
	vec3 wn = normalize(vec3(bump.x, 1.0, bump.y));
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	// wetness: darker, glossy, puddling in the lower parts of stones and along seams
	float wet = clamp(max(wetness, wetness_global), 0.0, 1.0);
	float puddle = smoothstep(0.35, 0.7, vnoise(p * 1.7 + 11.0) + seam * 0.25);
	float w_amt = wet * mix(0.6, 1.0, puddle);
	albedo *= mix(1.0, 0.52, w_amt);
	ALBEDO = albedo;
	ROUGHNESS = mix(0.86 - 0.12 * grain, mix(0.35, 0.05, puddle), w_amt);
	SPECULAR = mix(0.4, 0.75, w_amt);
}
