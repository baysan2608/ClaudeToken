// Shared rock <-> molten <-> crusted material model used by stone.gdshader, lava_wave.gdshader
// and (cold state) the earth wall. One function, four state knobs:
//   heat   0..1  glowing crack network (solid rock)
//   melt   0..1  fissures widen into bright lava between dark crust plates, glossy sheen
//   crust  0..1  crust plates close the fissures, emission dims, surface turns rough basalt
//   damage 0..1  unlit cracks (earth wall damage)
// ed = Voronoi edge distance (0 on a plate border), cid = per-plate random, eid = per-border random,
// lowf = low-frequency tonal noise, grain = fine noise, up = world-space normal.y.
#include "res://presentation/shaders/vfx_common.gdshaderinc"

void rock_surface(float ed, float cid, float eid, float lowf, float grain, float up, float flick,
		float heat, float melt, float crust, float damage, float wmax,
		out vec3 albedo, out float rough, out vec3 emis, out float dh_ded, out float hgrain, out float fissure) {
	float k_melt = smoothstep(0.0, 1.0, melt);

	// Only some cell borders are "cracks" in cold rock; heat / melt / crust / damage recruit the rest,
	// so the crack network visibly propagates as the stone heats up.
	float drive = clamp(heat * 1.25 + melt * 2.0 + crust * 1.5 + damage * 0.9, 0.0, 1.0);
	float thr = mix(0.58, -0.25, drive);
	float act = smoothstep(thr, thr + 0.16, eid);

	// ---- cold stone: tonal variation, grain, dust on top faces, cavity darkening in cracks
	vec3 rock = mix(vec3(0.015, 0.0145, 0.0145), vec3(0.060, 0.056, 0.051), smoothstep(0.25, 0.75, lowf));
	rock *= 0.72 + 0.56 * grain;
	float dust = smoothstep(0.40, 0.95, up) * smoothstep(0.25, 0.75, grain * 0.7 + lowf * 0.5);
	rock = mix(rock, vec3(0.085, 0.078, 0.068), dust * 0.22);
	rock *= mix(1.0, 0.55 + 0.45 * smoothstep(0.0, 0.2, ed), act);

	// ---- cooled crust (basalt / glassy slag)
	vec3 basalt = mix(vec3(0.0045, 0.0042, 0.0042), vec3(0.022, 0.018, 0.016), grain);
	basalt *= mix(0.75, 1.25, lowf);
	float dark = smoothstep(0.06, 0.55, max(k_melt, crust));
	vec3 base = mix(rock * (1.0 - 0.38 * heat), basalt, dark);

	// ---- fissure network
	float w_cold = 0.022 + 0.07 * heat + 0.045 * damage;
	float w_melt = mix(w_cold, wmax, smoothstep(0.08, 1.0, melt));
	float w = max(w_melt * (1.0 - 0.84 * crust), 0.010) * act * (0.75 + 0.5 * lowf);
	float en = ed + (grain - 0.5) * 0.07;
	// NB: smoothstep with edge0 >= edge1 is undefined (NaN on some GPUs): keep edge1 strictly larger
	float fm = (1.0 - smoothstep(w * 0.40, w + 1e-4, en)) * step(0.001, w);
	fissure = fm;

	float tcore = clamp(0.75 * heat + 0.45 * k_melt, 0.0, 1.0) * (1.0 - 0.74 * crust);
	float depth = 1.0 - clamp(en / max(w, 1e-4), 0.0, 1.0);
	float T = min(tcore * (0.36 + 0.64 * depth) * (0.80 + 0.30 * lowf) * flick, 0.88);
	float amp = 1.5 + 0.9 * k_melt;
	emis = vfx_blackbody(T) * amp * fm * smoothstep(0.015, 0.2, T);
	// heat-affected zone: plate rims glow a little around the fissure
	float rim = exp(-max(en - w, 0.0) * 13.0) * (1.0 - fm);
	emis += vfx_blackbody(T * 0.6) * rim * 0.45 * smoothstep(0.1, 0.5, tcore) * act;

	// fissure floor / unlit crack interior
	vec3 floor_col = mix(vec3(0.006, 0.004, 0.004), vec3(0.03, 0.006, 0.002), tcore);
	albedo = mix(base, floor_col, fm);

	// ---- relief: plates stand proud of the fissures. Output is the slope of the plate profile
	// w.r.t. the Voronoi edge distance (callers multiply by the analytic gradient) plus a grain term.
	float wp = 0.14 + 0.12 * k_melt;
	float tp = clamp(en / wp, 0.0, 1.0);
	dh_ded = 6.0 * tp * (1.0 - tp) / wp * (0.35 + 0.65 * max(dark, damage)) * act * step(tp, 0.9999);
	hgrain = (grain - 0.5) * 0.07;

	// ---- roughness: dusty rock -> glassy molten sheen -> rough crust
	float r_rock = 0.80 + 0.18 * grain;
	float r_hot = mix(0.46, 0.86, smoothstep(0.0, 0.8, crust));
	rough = mix(r_rock, r_hot, dark);
	rough = mix(rough, 0.55, fm);
}
