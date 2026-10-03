// Cut stone block masonry for ledges / walls / plinths (procedural, self-contained, no textures).
// World-space layout: vertical faces get running-bond courses (0.28 m high), top faces get large
// paving slabs. Chisel marks, edge wear, grime toward the ground. Cost: 2 value-noise evaluations.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform float course_height = 0.28;
uniform float block_width = 0.82;
uniform float seam_width : hint_range(0.004, 0.05) = 0.018;
uniform vec3 stone_a : source_color = vec3(0.47, 0.45, 0.41);
uniform vec3 stone_b : source_color = vec3(0.36, 0.36, 0.37);
uniform vec3 seam_color : source_color = vec3(0.10, 0.09, 0.09);
uniform float grime : hint_range(0.0, 1.0) = 0.5;
uniform float wetness : hint_range(0.0, 1.0) = 0.0;   // 0 dry .. 1 soaked (see VfxMaterials.set_arena_wetness)

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
	vec3 wn0 = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 an = abs(wn0);
	bool top = an.y > 0.6;
	vec2 p;
	float ch = course_height;
	float bw = block_width;
	if (top) {
		p = v_world.xz;
		ch = block_width * 1.1;
		bw = block_width * 1.35;
	} else if (an.x > an.z) {
		p = vec2(v_world.z, v_world.y);
	} else {
		p = vec2(v_world.x, v_world.y);
	}
	float row = floor(p.y / ch);
	float rw = bw * (0.75 + 0.5 * h21(vec2(row, 5.3)));
	float xo = h21(vec2(row, 1.7)) * rw;
	float col = floor((p.x + xo) / rw);
	vec2 cell = vec2((p.x + xo) / rw - col, p.y / ch - row);
	vec2 id = vec2(col, row);
	float th = h21(id + 7.0);

	vec2 dm = vec2(min(cell.x, 1.0 - cell.x) * rw, min(cell.y, 1.0 - cell.y) * ch);
	float d = min(dm.x, dm.y);
	float aa = fwidth(d) * 1.2;
	float seam = (1.0 - smoothstep(seam_width * 0.4 - aa, seam_width * 1.3 + aa, d)) * (1.0 - smoothstep(seam_width * 4.0, seam_width * 12.0, aa));
	float edge = 1.0 - smoothstep(0.0, 0.045, d);

	float mott = vnoise(p * 1.4 + th * 11.0);
	vec3 albedo = mix(stone_a, stone_b, smoothstep(0.2, 0.8, th * 0.65 + mott * 0.45));
	albedo *= 0.8 + 0.35 * h21(id + 23.0);
	// chisel marks: fine diagonal tooling, per block direction
	float tool = vnoise(vec2(p.x * 38.0 + p.y * 12.0 * (th - 0.5), p.y * 9.0) + th * 20.0);
	albedo *= 0.92 + 0.16 * tool;
	// edge wear lightens chipped arrises, seams are dark mortar
	albedo = mix(albedo, albedo * 1.25, edge * 0.5);
	albedo = mix(albedo, seam_color, seam * 0.9);
	// ground grime: darker toward the bottom of vertical faces / in the seam corners
	float low = top ? 0.0 : (1.0 - smoothstep(0.0, 0.9, v_world.y));
	albedo *= 1.0 - grime * (0.45 * low * (0.5 + 0.5 * mott) + 0.15 * (1.0 - smoothstep(0.0, 0.12, d)));

	vec2 toward = dm.x < dm.y ? vec2(cell.x < 0.5 ? -1.0 : 1.0, 0.0) : vec2(0.0, cell.y < 0.5 ? -1.0 : 1.0);
	vec2 bump = toward * edge * 0.5 + (vec2(tool, vnoise(p * 38.0 + 3.0)) - 0.5) * 0.12;
	// build the perturbation in the surface plane (world space), then to view space
	vec3 t_axis = top ? vec3(1.0, 0.0, 0.0) : (an.x > an.z ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0));
	vec3 b_axis = top ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
	vec3 wn = normalize(wn0 + (t_axis * bump.x + b_axis * bump.y) * 0.8);
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	float wet = wetness;
	ALBEDO = albedo * mix(1.0, 0.6, wet);
	ROUGHNESS = mix(0.88 - 0.1 * tool, 0.25, wet);
	SPECULAR = 0.4 + 0.3 * wet;
}
