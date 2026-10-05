// Hanging banner cloth: atlas texture (banners_albedo, RGBA, four 1/4-width banners, alpha = swallow-tail cut),
// wind sway that only ever pushes AWAY from the wall (+local Z), pinned at the top rod.
// Cost: 1 texture fetch, vertex sway only.  `column` selects the banner (0 earth, 1 water, 2 fire, 3 air).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx, cull_back;

uniform sampler2D banner_tex : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform int column = 0;
uniform float sway : hint_range(0.0, 0.12) = 0.04;      // metres at the hem
uniform float sway_speed = 0.9;

varying vec2 v_uv;

void vertex() {
	float k = clamp(UV.y, 0.0, 1.0);                      // 0 at the rod, 1 at the hem
	float ph = MODEL_MATRIX[3].x * 0.37 + MODEL_MATRIX[3].z * 0.53;
	float w = 0.5 + 0.5 * sin(TIME * sway_speed + ph + k * 2.4);
	float w2 = 0.5 + 0.5 * sin(TIME * sway_speed * 1.7 + ph * 1.3 + VERTEX.x * 3.0 + k * 3.0);
	VERTEX.z += sway * k * k * (0.35 + 0.65 * w) + sway * 0.25 * k * w2 * k;
	VERTEX.x *= 1.0 - 0.02 * k * w;
	v_uv = vec2((UV.x + float(column)) * 0.25, UV.y);
}

void fragment() {
	vec4 t = texture(banner_tex, v_uv);
	if (t.a < 0.5) {
		discard;
	}
	ALBEDO = t.rgb;
	ROUGHNESS = 0.88;
	SPECULAR = 0.25;
	BACKLIGHT = t.rgb * 0.25;
}
