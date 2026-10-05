// Blue steel (discs, lances, rods, plates, caltrops): brushed streaks along UV.x, sharp specular,
// a thin edge glint, and red-hot incandescence with heat. Opaque, one draw.
//   UV.x = along the brushing direction, UV.y = across. COLOR (optional) = per-instance tint factor.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

#include "res://presentation/vfx/shaders/fx_lib.gdshaderinc"

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 base_color : source_color = vec3(0.56, 0.62, 0.70);
uniform float heat : hint_range(0.0, 1.0) = 0.0;
uniform float brushed = 28.0;
uniform float glint = 0.35;
uniform float seed = 0.0;

void fragment() {
	vec2 uv = UV + vec2(seed * 0.37, seed * 0.11);
	float s = texture(noise_tex, vec2(uv.x * 0.15, uv.y * brushed)).r;
	float s2 = texture(noise_tex, uv * 1.7 + 0.4).g;
	vec3 view = normalize(VIEW);
	float ndv = clamp(dot(normalize(NORMAL), view), 0.0, 1.0);
	float rim = pow(1.0 - ndv, 5.0);
	vec3 col = base_color * (0.78 + 0.34 * s) * (0.9 + 0.2 * s2);
	// heat: temper colours (straw -> blue) give way to a dark red-hot body
	float hk = smoothstep(0.05, 0.6, heat);
	col = mix(col, vec3(0.22, 0.08, 0.05), hk * 0.75);
	ALBEDO = col;
	METALLIC = mix(0.95, 0.4, hk);
	ROUGHNESS = mix(0.24 + 0.16 * s, 0.6, hk);
	SPECULAR = 0.6;
	// Kept under ~1.2 so cherry / orange heat survives tonemapping (2x washed a red-hot rod out to cream);
	// the brushed streaks modulate it so the glowing body keeps its shape.
	EMISSION = vfx_heat(heat, s2) * (0.5 + 0.25 * s) + vec3(0.8, 0.9, 1.0) * rim * glint * (1.0 - hk);
}
