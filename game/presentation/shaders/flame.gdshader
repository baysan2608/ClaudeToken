// Mesh flame: an open trumpet/teardrop surface (axis = +Y, unit length, unit radius) animated entirely
// in the shaders. Premultiplied-alpha, unshaded, no depth write: `cover` 0 behaves like additive
// (glow), `cover` ~0.8 also darkens/covers the background so the flame stays readable on bright
// ground. Two layers (outer cover ~0.8, inner hot core cover ~0.25) are drawn per flame.
//   UV.x = angle fraction (0..1)   UV.y = position along the axis (0..1, base -> tip)
//   COLOR.r = per-flame random
// Burst mode (loop = 0): `age` 0..1 drives head/tail travel along the axis; loop mode (loop = 1):
// steady flame (FireChargeFX) with scrolling noise.
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float age : hint_range(0.0, 1.0) = 0.0;
uniform float intensity = 1.0;
uniform float core = 0.0;            // 0 outer layer, 1 inner (hotter, thinner) layer
uniform float cover = 0.8;           // 0 = purely additive glow, 1 = opaque body
uniform float loop = 0.0;            // 1 = steady flame
uniform float shape : hint_range(0.0, 1.0) = 0.0;   // 0 = burst trumpet, 1 = teardrop (held flame)
uniform float seed = 0.0;
uniform float scroll = 0.0;          // accumulated flame scroll (loop mode)
uniform vec3 col_deep : source_color = vec3(0.62, 0.07, 0.01);
uniform vec3 col_mid : source_color = vec3(1.0, 0.36, 0.05);
uniform vec3 col_hot : source_color = vec3(1.0, 0.78, 0.30);
uniform vec3 col_white : source_color = vec3(1.0, 0.95, 0.75);

varying float v_y;
varying float v_edge;

float hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

void vertex() {
	float y = UV.y;
	float ang = UV.x * 6.2831853;
	float t = loop > 0.5 ? scroll : age;
	float spd = loop > 0.5 ? 1.0 : 1.7;
	// radius profile along the axis: widening jet (burst) or teardrop (held flame)
	float trumpet = 0.05 + 0.95 * pow(y, 0.8);
	float tear = (sin(clamp(y * 1.1, 0.0, 1.0) * 3.14159) * 0.9 + 0.1 * (1.0 - y)) * (1.0 - 0.25 * y);
	float prof = mix(trumpet, tear, shape);
	float lobes = 1.0 + 0.42 * sin(ang * 3.0 + y * 3.5 + seed * 6.28 + t * 5.0 * spd)
			+ 0.24 * sin(ang * 5.0 - y * 6.0 + seed * 3.0 + t * 8.0 * spd);
	float r = prof * lobes * mix(1.0, 0.5, core);
	// burst travel: head races out, tail follows; the flame is stretched over [tail, head]
	float len_scale = 1.0;
	if (loop < 0.5) {
		float head = 1.0 - pow(1.0 - clamp(age * 2.2, 0.0, 1.0), 2.0);
		len_scale = 0.15 + 0.85 * head;
	}
	vec3 p = VERTEX;
	vec2 dir = normalize(p.xz + vec2(1e-5));
	p.xz = dir * r;
	p.y = y * len_scale;
	// licking sway
	p.x += sin(y * 6.0 + t * 6.0 * spd + seed * 9.0) * 0.07 * y;
	p.z += cos(y * 5.0 + t * 5.0 * spd + seed * 4.0) * 0.06 * y;
	VERTEX = p;
	v_y = y;
	v_edge = r;
}

void fragment() {
	float y = v_y;
	float tt = loop > 0.5 ? scroll : age * 2.4;
	// vertical streaks (ridged noise) + soft billows (fbm), both scrolling outward along the axis
	vec2 uv_a = vec2(UV.x * 6.0 + seed, y * 0.55 - tt * 1.3 * (1.0 + 0.5 * core));
	vec2 uv_b = vec2(UV.x * 2.0 + seed * 2.0 + 0.3, y * 1.6 - tt * 0.8);
	float streak = texture(noise_tex, uv_a).g;
	float blob = texture(noise_tex, uv_b).r;
	float n = mix(blob, streak, 0.55);

	// burst envelope: tail trailing behind the head
	float tail = 0.0;
	float fade = 1.0;
	if (loop < 0.5) {
		tail = smoothstep(0.3, 1.0, age) * 0.85;
		fade = 1.0 - smoothstep(0.62, 1.0, age);
	}
	float body = smoothstep(tail - 0.02, tail + 0.22, y);
	float taper = 1.0 - smoothstep(0.78, 1.0, y + (n - 0.5) * 0.25);

	// ragged tongues: erosion threshold grows toward the tip, so the silhouette breaks into licks
	float thr = 0.26 + 0.46 * y;
	float tongues = smoothstep(thr, thr + 0.08, n + 0.26 * (1.0 - y) * (1.0 - core * 0.35));

	// soft silhouette so the surface reads volumetric rather than a hard shell
	float ndv = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float rimfade = smoothstep(0.02, 0.55, ndv + (n - 0.5) * 0.45);

	float a = body * taper * tongues * rimfade * fade * intensity;

	// colour: hot at the base, cooling to deep red at the ragged tip
	float heat = clamp((1.0 - y) * 0.72 + n * 0.30 - tail * 0.2 + core * 0.22 - (1.0 - fade) * 0.3, 0.0, 1.0);
	vec3 c = mix(col_deep, col_mid, smoothstep(0.12, 0.48, heat));
	c = mix(c, col_hot, smoothstep(0.48, 0.80, heat));
	c = mix(c, col_white, smoothstep(0.93, 1.0, heat) * 0.6);
	float k = a * (0.40 + 0.34 * core);
	ALBEDO = c * k * (1.0 + core * 0.45);
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
