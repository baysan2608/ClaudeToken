// Water -> ice. One transparent layer (blend_mix, no depth write, back faces culled).
// Refraction uses the single optional cheap screen read (hint_screen_texture; the engine copies the
// opaque framebuffer once per frame no matter how many materials read it): a lens-like offset by the
// view-space normal with slight chromatic split, depth-of-volume absorption, fresnel reflection of
// the sky through the normal PBR specular path, scrolling ripples. frozen -> pale frosted, faceted
// ice with blurred (frosted) refraction and internal fractures.
// Used by WaterRibbonView (UV = (metres along, angle fraction), CUSTOM0 = (ring, side, radius, t))
// and WaterBlobView (blob = 1, sphere mesh).
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/vfx_common.gdshaderinc"

uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_linear_mipmap;
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform sampler3D noise_vol : filter_linear, repeat_disable;
uniform float frozen : hint_range(0.0, 1.0) = 0.0;
uniform float flow = 0.0;            // accumulated flow phase (integrated by the view)
uniform float blob = 0.0;            // 1 = held orb (wobble in vertex stage)
uniform float wobble = 0.5;          // orb wobble amount
uniform float tex_scale = 0.7;       // noise repeats per metre along a ribbon
uniform float crystal = 0.16;        // ice crystal displacement (fraction of radius)
uniform float refraction : hint_range(0.0, 2.0) = 1.0;
uniform vec3 water_shallow : source_color = vec3(0.45, 0.82, 0.92);
uniform vec3 water_deep : source_color = vec3(0.30, 0.66, 0.82);   // transmission tint (not 'dark')
uniform vec3 ice_tint : source_color = vec3(0.70, 0.86, 0.95);

varying float v_rad;
varying vec3 v_obj;

float hash21(float a, float b) {
	return fract(sin(a * 12.9898 + b * 78.233) * 43758.5453);
}

void vertex() {
	float fr = frozen;
	v_obj = VERTEX;
	if (blob > 0.5) {
		// viscous wobble of a held orb (lobes drift over time), freezes into a rigid crystal
		vec3 p = VERTEX;
		v_rad = length(MODEL_MATRIX[0].xyz);
		float w = sin(p.x * 5.0 + TIME * 2.3) + sin(p.y * 6.0 - TIME * 1.9) + sin(p.z * 4.5 + TIME * 2.7);
		VERTEX += NORMAL * (w * 0.035 * wobble * (1.0 - fr * 0.85));
		// crystalline facets: stepped noise over the sphere direction, only when frozen
		vec3 q = floor(normalize(p) * 2.2 + 0.5);
		float h = hash21(q.x * 3.1 + q.z * 7.7, q.y * 5.3 + q.z);
		VERTEX += NORMAL * ((h - 0.4) * crystal * fr);
	} else {
		// CUSTOM0 = (ring index, side index, radius, 0..1 along)
		float rad = CUSTOM0.z;
		v_rad = rad;
		float swell = sin(UV.x * 7.0 - flow * 5.0) * 0.07 * (1.0 - fr);
		float h = hash21(CUSTOM0.x * 3.17, floor(CUSTOM0.y * 0.5) * 7.31 + 1.0);
		VERTEX += NORMAL * rad * (swell + (h - 0.4) * crystal * 1.6 * fr);
	}
}

void fragment() {
	vec4 n1;
	vec4 n2;
	float crack;
	if (blob > 0.5) {
		vec3 d = normalize(v_obj) * 0.42 + 0.5;
		n1 = texture(noise_vol, vfx_mirror(d + vec3(flow * 0.03, 0.0, flow * 0.02)));
		n2 = texture(noise_vol, vfx_mirror(d * 2.3 + vec3(0.37, 0.11 - flow * 0.05, 0.2)));
		vec3 vg;
		crack = vfx_voronoi3(normalize(v_obj) * 2.2, vg).y;
	} else {
		vec2 uv = vec2(UV.x * tex_scale - flow * 0.55, UV.y * 2.0);
		n1 = texture(noise_tex, uv);
		n2 = texture(noise_tex, uv * 2.3 + vec2(0.37, 0.11 - flow * 0.13));
		vec2 vg2;
		crack = vfx_voronoi2(vec2(UV.x * 4.0 / max(v_rad * 6.0, 0.2) * 0.25, UV.y * 3.0), vg2).y;
	}

	vec3 view = normalize(VIEW);
	vec3 n = normalize(NORMAL);
	float ndv0 = clamp(dot(n, view), 0.0, 1.0);

	// ripples on the surface; faceted ice swaps them for the geometric normal
	float rip = (1.0 - frozen) * 0.32 + frozen * 0.03;
	n = normalize(n + vec3(n1.g - 0.5, n2.g - 0.5, 0.0) * rip * (0.3 + 0.7 * (1.0 - ndv0)));
	vec3 fc = cross(dFdx(VERTEX), dFdy(VERTEX));
	vec3 fn = fc * inversesqrt(max(dot(fc, fc), 1e-12));
	fn = dot(fn, view) < 0.0 ? -fn : fn;
	n = normalize(mix(n, fn, smoothstep(0.15, 0.7, frozen)));
	NORMAL = n;

	float ndv = clamp(dot(n, view), 0.0, 1.0);
	float thick = pow(ndv0, 0.55);
	float fres = pow(1.0 - ndv, 4.0);

	// ---- refraction (lens-like screen offset, chromatic split, frosted blur for ice)
	float depth = max(-VERTEX.z, 0.05);
	float frost = smoothstep(0.50, 0.85, n1.r * 0.75 + n1.b * 0.35) * frozen;
	vec2 off = n.xy * (v_rad / depth) * PROJECTION_MATRIX[1][1] * 0.5 * refraction;
	off *= (0.45 + 0.9 * (1.0 - ndv0)) * mix(1.0, 1.6, frozen);
	off.y = -off.y;
	float lod = frozen * (1.5 + 3.0 * frost);
	vec3 refr;
	refr.r = textureLod(screen_tex, SCREEN_UV + off * 0.92, lod).r;
	refr.g = textureLod(screen_tex, SCREEN_UV + off, lod).g;
	refr.b = textureLod(screen_tex, SCREEN_UV + off * 1.08, lod).b;

	// ---- water: absorption by volume, bright flow streaks, soft inner scatter
	vec3 absorb = mix(vec3(1.0), water_deep, 0.72 * thick);
	float rim_dark = pow(1.0 - ndv0, 2.2);        // refraction darkens the silhouette of a water column
	refr *= 1.0 - 0.40 * rim_dark;
	float streak = smoothstep(0.74, 0.94, n2.b) * smoothstep(0.1, 0.8, thick);
	vec3 wn = (INV_VIEW_MATRIX * vec4(n, 0.0)).xyz;
	vec3 sky = mix(vec3(0.30, 0.42, 0.55), vec3(0.85, 0.92, 1.0), clamp(wn.y * 0.5 + 0.5, 0.0, 1.0));
	vec3 w_col = refr * absorb + water_shallow * (0.10 + 0.20 * thick) * (0.8 + 0.4 * n1.r) + vec3(0.7, 0.92, 1.0) * streak * 0.22 + sky * fres * 0.55;

	// ---- ice: pale blue transmission, frost haze, internal fractures
	float fracture = (1.0 - smoothstep(0.0, 0.03, crack)) * smoothstep(0.05, 0.6, thick);
	vec3 i_col = mix(refr * ice_tint, vec3(0.80, 0.90, 0.97), 0.30 + 0.45 * frost);
	i_col += vec3(0.7, 0.88, 1.0) * fracture * 0.18;

	EMISSION = mix(w_col, i_col, frozen);
	ALBEDO = vec3(0.0);
	ALPHA = 0.999;
	ROUGHNESS = mix(0.03, 0.08 + 0.55 * frost, frozen);
	SPECULAR = mix(0.85, 0.7, frozen);
	METALLIC = 0.0;
}
