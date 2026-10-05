// Pool water surface. Alpha-blended on purpose: the pool is a single small plane, so the blend cost is
// negligible and the tiled basin (pool_basin.gdshader) shows through with its own caustics.
//  - depth tint: opacity and colour deepen away from the rim (rounded-rectangle SDF as a depth proxy),
//    plus a view-angle term so the surface turns more opaque / reflective at grazing angles;
//  - ripples: a sum of travelling sine swells (analytic slope, so the normal is smooth: no contour lines in the
//    sky reflection), phase-warped by slow noise so they never read as a regular grid; detail adds fine chop;
//  - rim foam + shore line: a soft animated foam band along every wall, a few drifting bubbles;
//  - sky reflection comes from the PBR path (very low roughness, Fresnel via SPECULAR) and, where
//    present, the arena reflection probe; a cheap fake Fresnel emission keeps the sheen on Low.
// No screen texture / depth texture reads (Mobile friendly).
//
// Use on a PlaneMesh/QuadMesh centred on the pool; set half_extent to the pool half size in metres.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec2 half_extent = vec2(3.0, 2.0);   // pool half size (local X, Z) in metres
uniform float corner_radius = 0.6;
uniform float shore_width : hint_range(0.02, 1.0) = 0.22;   // foam band width
uniform float depth_range : hint_range(0.3, 4.0) = 1.4;     // distance from the rim at which the water is fully deep
uniform vec3 shallow_color : source_color = vec3(0.22, 0.66, 0.62);
uniform vec3 deep_color : source_color = vec3(0.03, 0.22, 0.28);
uniform vec3 foam_color : source_color = vec3(0.86, 0.9, 0.88);
uniform float shallow_alpha : hint_range(0.0, 1.0) = 0.3;
uniform float deep_alpha : hint_range(0.0, 1.0) = 0.7;
uniform float ripple_strength : hint_range(0.0, 1.0) = 0.22;
uniform float flow_speed = 0.12;
uniform float foam_amount : hint_range(0.0, 1.0) = 0.7;
uniform bool detail = true;                                  // second ripple layer + bubbles (off on Low)

varying vec3 v_local;

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

float sd_round_box(vec2 p, vec2 b, float r) {
	vec2 q = abs(p) - b + r;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void vertex() {
	v_local = VERTEX;
}

// One travelling swell: returns its slope (d height / d p) for direction d, wave number k, speed s.
vec2 swell(vec2 p, vec2 d, float k, float s, float t, float ph, float amp) {
	return d * (cos(dot(p, d) * k - t * s + ph) * k * amp);
}

void fragment() {
	vec2 p = v_local.xz;
	float sd = sd_round_box(p, half_extent, corner_radius);   // < 0 inside the pool
	if (sd > 0.0) {
		discard;   // the mesh may be larger than the pool: only the rounded-rectangle interior is water
	}
	float inside = -sd;                                        // distance from the rim, metres
	float depth01 = smoothstep(0.0, depth_range, inside);

	float t = TIME * flow_speed * 8.0;
	// slow phase warp breaks the regularity of the swells
	vec2 w = vec2(vnoise(p * 0.7 + vec2(t * 0.05, -t * 0.03)), vnoise(p * 0.7 + 9.0 + vec2(-t * 0.04, t * 0.05))) - 0.5;
	vec2 q = p + w * 0.5;
	vec2 slope = swell(q, normalize(vec2(0.8, 0.6)), 3.1, 1.1, t, 0.0, 0.0075);
	slope += swell(q, normalize(vec2(-0.5, 0.9)), 4.7, 1.5, t, 1.7, 0.0058);
	slope += swell(q, normalize(vec2(0.2, -1.0)), 7.3, 1.9, t, 4.1, 0.0036);
	if (detail) {
		slope += swell(q, normalize(vec2(-0.9, -0.35)), 12.5, 2.6, t, 2.2, 0.0022);
		slope += swell(q * 1.07, normalize(vec2(0.55, -0.8)), 19.0, 3.4, t, 5.3, 0.0013);
	}
	slope *= (ripple_strength / 0.22) * (0.35 + 0.65 * smoothstep(0.0, 0.5, inside));
	vec3 wn = normalize(vec3(-slope.x, 1.0, -slope.y));
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	float ndv = clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0);
	float fres = pow(1.0 - ndv, 4.0);
	// depth tint: shallow turquoise at the rim, deep teal toward the middle; slightly lighter where a swell
	// tilts toward the viewer (the light comes through the crest)
	float crest = clamp(dot(slope, vec2(0.0, 1.0)) * 6.0, -0.5, 0.5);
	vec3 col = mix(shallow_color, deep_color, pow(depth01, 0.8));
	col *= 1.0 + crest * 0.18;
	float alpha = mix(shallow_alpha, deep_alpha, depth01);

	// foam: ragged band hugging the rim (wobbled by two noise octaves), faint drifting bubbles further in
	float edge_n = vnoise(p * 9.0 + vec2(t * 0.35, -t * 0.25));
	float edge_n2 = vnoise(p * 21.0 - vec2(t * 0.25, t * 0.12) + 13.0);
	float band = shore_width * (0.55 + 0.9 * edge_n);
	float foam = (1.0 - smoothstep(band * 0.55, band, inside)) * smoothstep(0.25, 0.6, edge_n2 + 0.25 * edge_n);
	foam += (1.0 - smoothstep(0.0, shore_width * 0.35, inside)) * 0.6;
	if (detail) {
		float bub = smoothstep(0.78, 0.86, vnoise(p * 7.0 + vec2(t * 0.2, 0.4 * t) + 40.0)) * smoothstep(0.1, 0.45, inside) * (1.0 - smoothstep(0.45, 1.2, inside));
		foam += bub * 0.3;
	}
	foam = clamp(foam * foam_amount, 0.0, 1.0);
	col = mix(col, foam_color, foam);
	alpha = mix(alpha, 0.95, foam);
	alpha = clamp(alpha + fres * 0.35, 0.0, 1.0);

	ALBEDO = col;
	ALPHA = alpha;
	EMISSION = vec3(0.30, 0.40, 0.52) * fres * 0.3 * (1.0 - foam) + col * 0.14;
	ROUGHNESS = mix(0.03, 0.55, foam);
	SPECULAR = 0.9;
	METALLIC = 0.0;
}
