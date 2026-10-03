// Lava wave / sheet. Opaque. Same rock-surface model as stone.gdshader, driven by strip UVs:
//   UV  = (arc length from tail in m, signed across offset in m)
//   UV2 = (distance to the leading front in m, height fraction 0 edge .. 1 crest)
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

#include "res://presentation/shaders/vfx_rock.gdshaderinc"

uniform sampler3D noise_vol : filter_linear, repeat_disable;
uniform float melt : hint_range(0.0, 1.0) = 1.0;
uniform float crust : hint_range(0.0, 1.0) = 0.0;
uniform float phase = 0.0;   // accumulated flow distance (m), integrated by LavaWaveView
uniform float boil = 0.0;    // accumulated slow evolution time
uniform float plate_size = 0.34;
uniform float seed = 0.0;
uniform float glow_scale : hint_range(0.0, 2.0) = 1.0;

varying vec2 v_sa;
varying vec2 v_fh;

void vertex() {
	v_sa = UV;
	v_fh = UV2;
	// slow viscous undulation of the crest (stills as the crust forms)
	float live = melt * (1.0 - crust);
	VERTEX.y += sin(UV.x * 4.2 - boil * 2.1) * 0.012 * UV2.y * live;
}

void fragment() {
	float u = sqrt(max(1.0 - v_fh.y * v_fh.y, 0.0));   // |across| 0 centre .. 1 edge
	vec3 off = vec3(fract(seed * 0.137), fract(seed * 0.291), fract(seed * 0.457)) * 3.0;
	vec2 q = vec2((v_sa.x - phase) * 0.78, v_sa.y) / plate_size;   // cells elongated along the flow
	vec4 n1 = texture(noise_vol, vfx_smooth_uvw(vfx_mirror(vec3(q * 0.16 + off.xy, boil * 0.035 + off.z)), 32.0));
	vec4 n2 = texture(noise_vol, vfx_smooth_uvw(vfx_mirror(vec3(q * 0.5 + 0.3, boil * 0.05 + 0.2) + off), 32.0));
	vec2 vgrad;
	vec4 vr = vfx_voronoi2(q * 1.0 + (n1.gb - 0.5) * 0.7 + vec2(0.0, boil * 0.04), vgrad);
	float ed = vr.y;
	float lowf = n1.r;
	float grain = n2.a;

	// thermal profile: banks cool and crust first, the front is fresh and hot
	float bank = smoothstep(0.45, 0.98, u);
	float front_hot = (1.0 - smoothstep(0.0, 0.9, v_fh.x)) * smoothstep(0.3, 0.8, melt);
	float crust_eff = clamp(crust + bank * (1.0 - crust) * 0.55 * (1.0 - front_hot) , 0.0, 1.0);
	float heat_eff = clamp(melt * (1.0 - 0.35 * bank) + front_hot * 0.2, 0.0, 1.0);
	float melt_eff = clamp(melt * (1.0 - 0.1 * bank), 0.0, 1.0);

	vec3 wn = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	float flick = 0.94 + 0.06 * sin(boil * 3.0 + lowf * 12.0);
	vec3 albedo; float rough; vec3 emis; float dh; float hgrain; float fissure;
	rock_surface(ed, vr.z, vr.w, lowf, grain, wn.y, flick, heat_eff, melt_eff, crust_eff, 0.0, mix(0.26, 0.15, bank), albedo, rough, emis, dh, hgrain, fissure);

	// residual glow of a fully cooled ridge: deep red hairlines
	float resid = crust * (1.0 - melt);
	emis += vfx_blackbody(0.18 + 0.1 * lowf) * 0.9 * fissure * resid;
	emis *= glow_scale;

	// derivative-free relief using the strip tangent frame (TANGENT along flow, BINORMAL across)
	vec2 gq = vgrad * dh * 0.07;
	vec3 g_view = TANGENT * (gq.x * 0.78) + BINORMAL * gq.y;
	g_view -= NORMAL * dot(g_view, NORMAL);
	NORMAL = normalize(NORMAL - g_view);

	NORMAL = vfx_bump(VERTEX, NORMAL, hgrain, 0.06);
	ALBEDO = albedo;
	ROUGHNESS = rough;
	SPECULAR = mix(0.28, 0.5, melt * (1.0 - crust));
	EMISSION = emis;
}
