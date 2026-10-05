// One layer of a vortex (twister, tornado, eddy, funnel, vortex wall): an open funnel surface whose
// streaks pan around the axis and climb. VortexView draws two layers (outer slow + inner fast).
// Mesh contract (VfxMesh.funnel_mesh): unit height, UV = (angle fraction, height fraction),
// the radius profile is applied here so one mesh serves every vortex size.
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float spin_phase = 0.0;     // accumulated revolutions (integrated by the view)
uniform float rise_phase = 0.0;
uniform float r_base = 0.35;        // radius at the ground (m), before the skirt
uniform float r_top = 1.6;          // radius at the top (m)
uniform float skirt = 0.6;          // extra radius flaring out at the ground (dust skirt)
uniform float height = 3.0;
uniform float sway = 0.25;          // m of axis wander at the top
uniform float opacity = 0.5;
uniform float cover = 0.55;
uniform vec3 tint_a : source_color = vec3(0.78, 0.74, 0.66);
uniform vec3 tint_b : source_color = vec3(0.95, 0.96, 1.0);
uniform float glow = 1.0;           // >1 for fire infusion
uniform float seed = 0.0;

varying float v_h;

void vertex() {
	float h = UV.y;
	float ang = UV.x * 6.2831853;
	float r = mix(r_base, r_top, pow(h, 1.35)) + skirt * pow(1.0 - h, 6.0);
	float wob = 1.0 + 0.08 * sin(ang * 3.0 + h * 7.0 - spin_phase * 6.2831853 * 0.5 + seed);
	vec3 p = vec3(cos(ang) * r * wob, h * height, sin(ang) * r * wob);
	p.x += sin(h * 2.6 + rise_phase * 0.9 + seed) * sway * h * h;
	p.z += cos(h * 2.1 + rise_phase * 0.7 + seed * 1.3) * sway * h * h;
	VERTEX = p;
	v_h = h;
}

void fragment() {
	float h = v_h;
	vec2 uv1 = vec2(UV.x * 3.0 - spin_phase * (1.0 + 0.6 * (1.0 - h)), h * 1.3 - rise_phase * 0.5);
	vec2 uv2 = vec2(UV.x * 6.0 - spin_phase * 1.7 + 0.31, h * 2.4 - rise_phase * 0.9);
	float a = texture(noise_tex, uv1).r;
	float b = texture(noise_tex, uv2).g;
	float streak = smoothstep(0.45, 0.85, a * 0.6 + b * 0.55);
	float band = 0.55 + 0.45 * sin(h * 18.0 - rise_phase * 4.0 + a * 3.0);
	vec3 view = normalize(VIEW);
	float ndv = abs(dot(normalize(NORMAL), view));
	float sil = 0.45 + 0.55 * pow(1.0 - ndv, 1.2);   // denser at the silhouette, like a real funnel
	float fade = smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.78, 1.0, h));
	float k = streak * band * sil * fade * opacity;
	vec3 c = mix(tint_a, tint_b, clamp(h * 0.9 + b * 0.3, 0.0, 1.0));
	ALBEDO = c * glow * k;
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
