// Calm pool water surface. Cheap approach chosen for Mobile: fully OPAQUE (no alpha blend, no depth
// prepass, no screen texture). Depth is faked from a rounded-rectangle signed distance in the mesh's
// local XZ plane: shallow turquoise near the edge, deep blue-teal in the middle, a wet-stone shoreline
// band and soft foam line at the rim, gentle two-layer ripples and a faint caustic shimmer on the
// "floor". Fresnel sky reflection comes from the PBR path (low roughness) plus a cheap fake term.
// Cost: 3 value-noise evaluations, no texture fetches.
//
// Use on a PlaneMesh/QuadMesh centred on the pool; set half_extent to the pool half size in metres.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec2 half_extent = vec2(3.0, 2.0);   // pool half size (local X, Z) in metres
uniform float corner_radius = 0.6;
uniform float shore_width : hint_range(0.02, 1.0) = 0.22;   // wet-stone / foam band width
uniform float depth_range : hint_range(0.3, 4.0) = 1.4;     // distance from the rim at which the water is fully deep
uniform vec3 shallow_color : source_color = vec3(0.17, 0.44, 0.44);
uniform vec3 deep_color : source_color = vec3(0.02, 0.12, 0.19);
uniform vec3 shore_color : source_color = vec3(0.12, 0.115, 0.11);
uniform float ripple_strength : hint_range(0.0, 1.0) = 0.22;
uniform float flow_speed = 0.12;

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

void fragment() {
	vec2 p = v_local.xz;
	float sd = sd_round_box(p, half_extent, corner_radius);   // < 0 inside the pool
	if (sd > 0.0) {
		discard;   // the mesh may be larger than the pool: only the rounded-rectangle interior is water
	}
	float inside = -sd;                                        // distance from the rim, metres
	float depth01 = smoothstep(0.0, depth_range, inside);

	float t = TIME * flow_speed;
	float n1 = vnoise(p * 2.4 + vec2(t, t * 0.7));
	float n2 = vnoise(p * 5.3 - vec2(t * 1.3, t * 0.4) + 7.0);
	float n3 = vnoise(p * 0.55 + vec2(-t * 0.5, t * 0.3) + 21.0);

	// gentle ripples: perturb the normal in the water plane (stronger toward the middle)
	vec2 slope = vec2(n1 - 0.5, n2 - 0.5) * ripple_strength * (0.4 + 0.6 * depth01);
	vec3 wn = normalize(vec3(slope.x, 1.0, slope.y));
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	// depth tint + faint floor caustics in the shallows
	vec3 col = mix(shallow_color, deep_color, pow(depth01, 0.8));
	float caustic = pow(1.0 - abs(n1 + n2 - 1.0), 6.0) * (1.0 - depth01) * 0.35;
	col += vec3(0.5, 0.8, 0.8) * caustic * 0.12;
	col *= 0.94 + 0.12 * n3;

	// shoreline: darker wet stone band at the very rim, soft foam line just inside it
	float shore = 1.0 - smoothstep(0.0, shore_width, inside);
	float foam_line = smoothstep(shore_width * 0.35, shore_width * 0.8, inside) * (1.0 - smoothstep(shore_width * 0.8, shore_width * 1.5, inside));
	float foam = foam_line * smoothstep(0.38, 0.78, n2 + n1 * 0.4) * 0.30;
	col = mix(col, shore_color, shore * 0.85);
	col = mix(col, vec3(0.55, 0.62, 0.64), foam);

	float fres = pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 4.0);
	ALBEDO = col;
	EMISSION = vec3(0.30, 0.40, 0.52) * fres * 0.35 * (1.0 - shore);
	ROUGHNESS = mix(0.04, 0.5, shore);
	SPECULAR = 0.8;
	METALLIC = 0.0;
}
