// Stone / molten blob / cooled rock — one material, continuous thermal state.
// Used by StoneView (instance uniforms drive the state so one material is shared by all stones)
// and by EarthWallView (cold state + damage cracks + rise).
// Mesh contract: CUSTOM0 = (relaxed blob position xyz, per-vertex random w); NORMAL = flat face normal.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

#include "res://presentation/shaders/vfx_rock.gdshaderinc"

uniform sampler3D noise_vol : filter_linear, repeat_disable;
uniform float wobble_amount = 0.04;
uniform float noise_scale = 0.36;
uniform float plate_scale = 2.3;

instance uniform float u_heat = 0.0;
instance uniform float u_melt = 0.0;
instance uniform float u_crust = 0.0;
instance uniform float u_seed = 0.0;
instance uniform float u_damage = 0.0;
instance uniform float u_rise = 1.0;
instance uniform float u_rise_height = 1.0;
instance uniform float u_detail = 1.0;   // >1 = finer plates / grain (large objects such as walls)

varying vec3 v_p;

void vertex() {
	float melt = u_melt;
	float rnd = CUSTOM0.w;
	vec3 blob = CUSTOM0.xyz;
	v_p = VERTEX;
	// vertices relax toward the smooth blob at slightly different times so the rock slumps unevenly
	float mv = smoothstep(0.08, 0.9, melt * 1.3 - rnd * 0.3);
	vec3 dir = normalize(blob + vec3(1e-5));
	vec3 pos = mix(VERTEX, blob, mv);
	// viscous slump: flatten the bottom, bulge the belly
	float sag = smoothstep(0.3, 1.0, melt);
	pos.y *= 1.0 - 0.16 * sag;
	pos.xz *= 1.0 + 0.07 * sag;
	// slow viscous wobble along the blob normal (stills as the crust forms)
	float calm = 1.0 - 0.9 * u_crust;
	float wob = (sin(TIME * 1.15 + dir.x * 3.7 + u_seed) + sin(TIME * 0.83 + dir.y * 4.3 + u_seed * 1.7) + sin(TIME * 1.37 + dir.z * 3.1 + u_seed * 0.6)) * (wobble_amount * 0.3333);
	pos += dir * wob * smoothstep(0.25, 1.0, melt) * calm;
	// earth wall rise (rnd carries the per-block delay for wall meshes; stones keep u_rise = 1)
	float r = clamp(u_rise * 1.5 - rnd * 0.5, 0.0, 1.0);
	r = 1.0 - pow(1.0 - r, 3.0);
	pos.y -= (1.0 - r) * u_rise_height;
	VERTEX = pos;
	NORMAL = normalize(mix(NORMAL, dir, smoothstep(0.08, 0.75, melt)));
}

void fragment() {
	vec3 off = vec3(fract(u_seed * 0.1371), fract(u_seed * 0.2913), fract(u_seed * 0.4573)) * 2.0;
	vec3 pp = v_p * u_detail;
	vec4 n1 = texture(noise_vol, vfx_mirror(pp * noise_scale + 0.5 + off));
	vec4 n2 = texture(noise_vol, vfx_mirror(pp * noise_scale * 2.7 + 0.31 + off));
	// organic crack network: analytic Voronoi, domain-warped by the smooth noise
	vec3 vgrad;
	vec4 vr = vfx_voronoi3(pp * plate_scale + off * 3.0 + (n1.gba - 0.5) * 0.55, vgrad);
	float ed = vr.y;
	float grain = mix(n2.a, n1.a, 0.35);
	float lowf = n1.r;
	vec3 wn = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	float flick = 0.93 + 0.07 * sin(TIME * 2.3 + lowf * 12.0 + u_seed);

	vec3 albedo; float rough; vec3 emis; float dh; float hgrain; float fissure;
	rock_surface(ed, vr.z, vr.w, lowf, grain, wn.y, flick, u_heat, u_melt, u_crust, u_damage, 0.165, albedo, rough, emis, dh, hgrain, fissure);

	// derivative-free relief: tilt the normal by the analytic plate-profile gradient (object space -> view space)
	float relief = mix(0.055, 0.085, smoothstep(0.0, 0.7, max(u_melt, u_crust)));
	float sc_m = 1.0;
	vec3 g_view = mat3(VIEW_MATRIX) * (mat3(MODEL_MATRIX) * (vgrad * dh * plate_scale * u_detail * relief / max(sc_m, 1e-3)));
	g_view -= NORMAL * dot(g_view, NORMAL);
	NORMAL = normalize(NORMAL - g_view);
	// fine rock grain: tiny derivative bump (smooth input, no stipple)
	float sc = length(MODEL_MATRIX[0].xyz);
	NORMAL = vfx_bump(VERTEX, NORMAL, hgrain, 0.5 * sc);

	ALBEDO = albedo;
	ROUGHNESS = rough;
	SPECULAR = mix(0.28, 0.5, smoothstep(0.0, 0.6, u_melt) * (1.0 - u_crust));
	EMISSION = emis;
	EMISSION = vec3(lowf, grain, ed) * 0.5;
}
