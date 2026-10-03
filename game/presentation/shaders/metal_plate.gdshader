// Dark brushed metal deck plates with bolts (procedural, self-contained, no textures).
// World-space XZ layout (plate = 1 m square by default), brushed streaks along X, bevelled seams,
// four bolts per plate. Cost: 2 value-noise evaluations, no texture fetches.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform float plate_size = 1.0;
uniform float seam_width : hint_range(0.004, 0.05) = 0.012;
uniform vec3 metal_color : source_color = vec3(0.22, 0.23, 0.25);
uniform float bolt_radius = 0.032;
uniform float bolt_inset = 0.085;
uniform float rust : hint_range(0.0, 1.0) = 0.12;
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
	vec2 p = v_world.xz / plate_size;
	vec2 id = floor(p);
	vec2 f = fract(p);
	float th = h21(id + 3.0);
	vec2 dm = min(f, 1.0 - f) * plate_size;
	float d = min(dm.x, dm.y);
	float aa = fwidth(d) * 1.2;
	float seam = (1.0 - smoothstep(seam_width * 0.4 - aa, seam_width * 1.2 + aa, d)) * (1.0 - smoothstep(seam_width * 4.0, seam_width * 12.0, aa));
	float bevel = 1.0 - smoothstep(0.0, 0.03, d);

	// brushed streaks along X: noise stretched 40:1, per-plate phase
	float brush = vnoise(vec2(p.x * 1.2 + th * 9.0, p.y * 55.0 + th * 31.0));
	float brush2 = vnoise(vec2(p.x * 4.0 + 17.0, p.y * 140.0));
	float streak = brush * 0.6 + brush2 * 0.4;

	// bolts at the four corners of each plate
	vec2 c = abs(f - 0.5) * plate_size;
	vec2 bpos = vec2(0.5 * plate_size - bolt_inset);
	float bd = length(c - bpos);
	float bolt = 1.0 - smoothstep(bolt_radius * 0.75, bolt_radius, bd);
	float bring = smoothstep(bolt_radius, bolt_radius * 1.5, bd) * (1.0 - smoothstep(bolt_radius * 1.5, bolt_radius * 2.4, bd));

	vec3 albedo = metal_color * (0.85 + 0.3 * th) * (0.9 + 0.2 * streak);
	albedo = mix(albedo, vec3(0.025), seam * 0.9);
	albedo *= 1.0 - 0.45 * bring;                       // dark ring around bolts
	albedo = mix(albedo, albedo * 1.35 + 0.02, bolt * 0.6);
	// sparse rust-brown staining near edges and bolts
	float stain = smoothstep(0.55, 0.9, vnoise(p * 2.3 + th * 7.0)) * rust * (0.4 + 0.6 * (1.0 - smoothstep(0.0, 0.2, d)));
	albedo = mix(albedo, vec3(0.20, 0.09, 0.04), stain);

	// normal: bevel at seams, dome on bolts, fine brushed grain
	vec2 toward = dm.x < dm.y ? vec2(f.x < 0.5 ? -1.0 : 1.0, 0.0) : vec2(0.0, f.y < 0.5 ? -1.0 : 1.0);
	vec2 to_bolt = (c - bpos) / max(bd, 1e-4) * vec2(f.x < 0.5 ? -1.0 : 1.0, f.y < 0.5 ? -1.0 : 1.0);
	vec2 bump = toward * bevel * 0.7 + to_bolt * bolt * 0.5 * smoothstep(0.0, bolt_radius, bd) + vec2(0.0, (brush2 - 0.5) * 0.05);
	vec3 wn = normalize(vec3(bump.x, 1.0, bump.y));
	NORMAL = normalize((VIEW_MATRIX * vec4(wn, 0.0)).xyz);

	float wet = wetness;
	ALBEDO = albedo * mix(1.0, 0.7, wet);
	METALLIC = mix(0.85, 0.2, stain) * (1.0 - bolt * 0.1);
	ROUGHNESS = clamp(mix(0.34 + 0.22 * streak, 0.1, wet * 0.8) + stain * 0.4, 0.05, 1.0);
	SPECULAR = 0.6;
}
