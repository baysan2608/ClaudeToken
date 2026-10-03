// Scorch / wet ground mark drawn on a lifted quad (ScorchDecal). Used instead of a Decal node
// because projected decals are limited and unreliable on the Mobile renderer; one transparent quad,
// depth-tested, no depth write, no screen texture.
//   kind 0 = scorch: ragged soot blotch, rough, with a short-lived ember glow rim
//   kind 1 = wet: soft dark puddle with a glossy (low roughness) sheen
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_burley, specular_schlick_ggx;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float kind = 0.0;
uniform float fade : hint_range(0.0, 1.0) = 1.0;     // fade in / out multiplier
uniform float ember : hint_range(0.0, 1.0) = 0.0;    // scorch glow (decays over the first ~1 s)
uniform float seed = 0.0;

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	vec2 nuv = UV * 1.3 + vec2(seed * 3.7, seed * 5.9);
	vec4 n = texture(noise_tex, nuv);
	vec4 n2 = texture(noise_tex, nuv * 2.7 + 0.31);
	float ragged = (n.r - 0.5) * 0.7 + (n2.g - 0.5) * 0.25;

	if (kind < 0.5) {
		// scorch: dense core, ragged edge
		float body = 1.0 - smoothstep(0.30, 0.95, r + ragged);
		float core = 1.0 - smoothstep(0.0, 0.55, r + ragged * 0.5);
		float a = clamp(body * (0.55 + 0.45 * core), 0.0, 1.0) * 0.92;
		float rim = smoothstep(0.35, 0.7, r + ragged) * (1.0 - smoothstep(0.7, 1.0, r + ragged)) * (0.4 + 0.6 * n2.r);
		ALBEDO = vec3(0.022, 0.019, 0.017);
		ROUGHNESS = 0.96;
		SPECULAR = 0.15;
		EMISSION = vec3(1.0, 0.30, 0.04) * rim * ember * 2.4;
		ALPHA = a * fade;
	} else {
		// wet: smooth blob, darker and glossy
		float body = 1.0 - smoothstep(0.35, 0.92, r + ragged * 0.55);
		float edge = smoothstep(0.55, 0.85, r + ragged * 0.5) * body;
		ALBEDO = vec3(0.02, 0.028, 0.035);
		ROUGHNESS = 0.05;
		SPECULAR = 0.9;
		ALPHA = clamp(body * 0.62 + edge * 0.2, 0.0, 1.0) * fade;
	}
}
