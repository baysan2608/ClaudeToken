// Shared helpers for the Fourfold VFX shaders (shader include).
// Noise volume contract (see VfxTextures.noise_volume): RGBA = smooth fbm at 4 frequencies (low -> grain).

// Triangle-wave coordinate remap: continuous (mirrored) tiling instead of hard repeat seams.
vec3 vfx_mirror(vec3 x) {
	vec3 f = fract(x * 0.5) * 2.0;
	return 1.0 - abs(f - 1.0);
}

vec2 vfx_mirror2(vec2 x) {
	vec2 f = fract(x * 0.5) * 2.0;
	return 1.0 - abs(f - 1.0);
}

float vfx_hash11(float p) {
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

vec3 vfx_hash31(float p) {
	vec3 p3 = fract(vec3(p) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xxy + p3.yzz) * p3.zyx);
}

// Black-body-ish emission ramp (linear colour, HDR-ready). t in 0..1:
// 0 = cold/black, 0.25 = dull red, 0.5 = orange-red, 0.75 = orange-yellow, 1 = hot yellow-white.
vec3 vfx_blackbody(float t) {
	t = clamp(t, 0.0, 1.0);
	vec3 c = vec3(0.0);
	c = mix(c, vec3(0.30, 0.012, 0.0), smoothstep(0.0, 0.28, t));
	c = mix(c, vec3(0.95, 0.15, 0.012), smoothstep(0.22, 0.55, t));
	c = mix(c, vec3(1.0, 0.46, 0.06), smoothstep(0.5, 0.8, t));
	c = mix(c, vec3(1.0, 0.82, 0.38), smoothstep(0.78, 1.0, t));
	return c;
}

// Surface-gradient bump mapping from a scalar height (derivative based, no tangents needed).
// view_pos / n are view-space; strength is in "height per metre" units.
vec3 vfx_bump(vec3 view_pos, vec3 n, float h, float strength) {
	vec3 dpdx = dFdx(view_pos);
	vec3 dpdy = dFdy(view_pos);
	float dhdx = dFdx(h);
	float dhdy = dFdy(h);
	vec3 r1 = cross(dpdy, n);
	vec3 r2 = cross(n, dpdx);
	float det = dot(dpdx, r1);
	vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
	return normalize(abs(det) * n - strength * grad);
}

vec3 vfx_hash33(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}

vec2 vfx_hash22(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

// Analytic 3D Voronoi. Returns (F1 distance, F2 - F1 edge distance proxy, nearest cell id 0..1,
// symmetric edge id 0..1 that is identical on both sides of a cell border) and the analytic
// gradient of (F2 - F1) in `grad` (used for derivative-free bump mapping).
// 27 cell evaluations, pure ALU. Cell size = 1.0 in input units.
vec4 vfx_voronoi3(vec3 p, out vec3 grad) {
	vec3 ip = floor(p);
	vec3 fp = p - ip;
	float f1 = 9.0;
	float f2 = 9.0;
	float id1 = 0.0;
	float id2 = 0.0;
	vec3 v1 = vec3(0.0);
	vec3 v2 = vec3(0.0);
	for (int k = -1; k <= 1; k++) {
		for (int j = -1; j <= 1; j++) {
			for (int i = -1; i <= 1; i++) {
				vec3 g = vec3(float(i), float(j), float(k));
				vec3 h = vfx_hash33(ip + g);
				vec3 d = g + 0.5 + (h - 0.5) * 0.92 - fp;
				float dd = dot(d, d);
				if (dd < f1) {
					f2 = f1;
					id2 = id1;
					v2 = v1;
					f1 = dd;
					id1 = h.x;
					v1 = d;
				} else if (dd < f2) {
					f2 = dd;
					id2 = h.x;
					v2 = d;
				}
			}
		}
	}
	f1 = sqrt(f1);
	f2 = sqrt(f2);
	// d = feature - p  =>  grad F = -d / |d|
	grad = v1 / max(f1, 1e-4) - v2 / max(f2, 1e-4);
	return vec4(f1, f2 - f1, id1, fract((id1 + id2) * 7.31 + id1 * id2 * 13.7));
}

// 2D variant (9 cells) for flat sheets (lava wave, ground).
vec4 vfx_voronoi2(vec2 p, out vec2 grad) {
	vec2 ip = floor(p);
	vec2 fp = p - ip;
	float f1 = 9.0;
	float f2 = 9.0;
	float id1 = 0.0;
	float id2 = 0.0;
	vec2 v1 = vec2(0.0);
	vec2 v2 = vec2(0.0);
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			vec2 g = vec2(float(i), float(j));
			vec2 h = vfx_hash22(ip + g);
			vec2 d = g + 0.5 + (h - 0.5) * 0.92 - fp;
			float dd = dot(d, d);
			if (dd < f1) {
				f2 = f1;
				id2 = id1;
				v2 = v1;
				f1 = dd;
				id1 = h.x;
				v1 = d;
			} else if (dd < f2) {
				f2 = dd;
				id2 = h.x;
				v2 = d;
			}
		}
	}
	f1 = sqrt(f1);
	f2 = sqrt(f2);
	grad = v1 / max(f1, 1e-4) - v2 / max(f2, 1e-4);
	return vec4(f1, f2 - f1, id1, fract((id1 + id2) * 7.31 + id1 * id2 * 13.7));
}
