// Shared helpers for the arena environment shaders (flagstones, masonry, metal deck, pool basin).
//
// Texture sets (see tools/textures/gen_env_textures.py): <set>_albedo (sRGB rgb, A = height), <set>_normal
// (OpenGL tangent space), <set>_orm (R = AO, G = roughness, B = metallic).
// Sampling is world-space "box" projection: every arena surface is axis aligned, so each face picks the
// dominant axis (no triplanar blend cost, no stretching) and neighbouring meshes line up exactly.
// Anti-tiling: the set is sampled twice (second layer at a different scale/offset) and the layers are
// height-blended by a low-frequency mask, so repeats dissolve along stone joints instead of ghosting.

float env_h21(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float env_vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(env_h21(i), env_h21(i + vec2(1.0, 0.0)), f.x), mix(env_h21(i + vec2(0.0, 1.0)), env_h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

// Normal textures are VRAM-compressed to two channels (RG), so z is rebuilt here.
vec3 env_decode_normal(vec3 t) {
	vec2 xy = t.xy * 2.0 - 1.0;
	return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

// Axis-aligned face frame. T = world direction of texture u, B = world direction of texture "up";
// uv (metres) = (dot(p,T), -dot(p,B)).  T x B = outward normal for every face.
void env_face_frame(vec3 wn, vec3 wp, out vec2 uv, out vec3 T, out vec3 B, out vec3 N) {
	vec3 an = abs(wn);
	if (an.y >= an.x && an.y >= an.z) {
		float s = wn.y < 0.0 ? -1.0 : 1.0;
		N = vec3(0.0, s, 0.0);
		T = vec3(1.0, 0.0, 0.0);
		B = vec3(0.0, 0.0, -s);
	} else if (an.z > an.x) {
		float s = wn.z < 0.0 ? -1.0 : 1.0;
		N = vec3(0.0, 0.0, s);
		T = vec3(s, 0.0, 0.0);
		B = vec3(0.0, 1.0, 0.0);
	} else {
		float s = wn.x < 0.0 ? -1.0 : 1.0;
		N = vec3(s, 0.0, 0.0);
		T = vec3(0.0, 0.0, -s);
		B = vec3(0.0, 1.0, 0.0);
	}
	uv = vec2(dot(wp, T), -dot(wp, B));
}

// Samples a texture set with optional anti-tiling. Returns tangent-space normal in `nrm` (unit),
// height 0..1 in `hgt` and the repeat-blend mask in `mask` (0 = layer 1 only).
void env_sample(sampler2D a_tex, sampler2D n_tex, sampler2D o_tex, vec2 uv, vec2 world_xz, bool anti,
		out vec3 alb, out float hgt, out vec3 nrm, out vec3 orm, out float mask) {
	vec4 a1 = texture(a_tex, uv);
	vec3 n1 = env_decode_normal(texture(n_tex, uv).xyz);
	vec3 o1 = texture(o_tex, uv).xyz;
	mask = 0.0;
	if (anti) {
		vec2 uv2 = uv * 1.137 + vec2(0.37, 0.61);
		vec4 a2 = texture(a_tex, uv2);
		vec3 n2 = env_decode_normal(texture(n_tex, uv2).xyz);
		vec3 o2 = texture(o_tex, uv2).xyz;
		float m = env_vnoise(world_xz * 0.21 + 3.7) * 0.65 + env_vnoise(world_xz * 0.53 + 11.1) * 0.35;
		m = smoothstep(0.36, 0.64, m);
		float b1 = a1.a + 1.5 * (1.0 - m);
		float b2 = a2.a + 1.5 * m;
		float mx = max(b1, b2) - 0.22;
		float w1 = max(b1 - mx, 0.0);
		float w2 = max(b2 - mx, 0.0);
		float t = w2 / max(w1 + w2, 1e-4);
		a1 = mix(a1, a2, t);
		n1 = mix(n1, n2, t);
		o1 = mix(o1, o2, t);
		mask = t;
	}
	alb = a1.rgb;
	hgt = a1.a;
	nrm = normalize(n1);
	orm = o1;
}

// Tangent-space normal -> view-space NORMAL for an axis-aligned face frame.
vec3 env_world_to_view_normal(vec3 nrm_ts, vec3 T, vec3 B, vec3 N, float flatten, mat4 view) {
	vec3 n = vec3(nrm_ts.xy * flatten, nrm_ts.z);
	vec3 wn = normalize(T * n.x + B * n.y + N * n.z);
	return normalize((view * vec4(wn, 0.0)).xyz);
}

// Baked arena ambient-occlusion map (single channel, see ArenaView._bake_ao): world XZ -> 0..1.
float env_arena_ao(sampler2D ao_map, vec4 rect, vec2 xz) {
	vec2 uv = (xz - rect.xy) / rect.zw;
	return mix(1.0, texture(ao_map, clamp(uv, vec2(0.0), vec2(1.0))).r, step(0.0, min(uv.x, uv.y)) * step(max(uv.x, uv.y), 1.0));
}

// Late-day colour grade shared by all stone-like surfaces: lifts warm highlights a touch.
vec3 env_macro_tint(vec2 xz, float amount) {
	float n = env_vnoise(xz * 0.33 + 5.0) * 0.6 + env_vnoise(xz * 0.9 + 17.0) * 0.4;
	return mix(vec3(0.93, 0.92, 0.9), vec3(1.06, 1.02, 0.95), n) * (1.0 - amount + amount * (0.82 + 0.36 * n));
}

// Saturation grade (1 = texture as authored).  Keeps the generated stone palettes from going orange
// under the warm key light.
vec3 env_grade(vec3 c, float sat) {
	return mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, sat);
}
