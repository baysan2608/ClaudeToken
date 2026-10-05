// Camera-facing beam along local +Z (unit length; node scale.z = length): blue-fire needle / beam,
// sandblast, water jet core, sound lance. Billboarded around its own axis in the vertex stage.
// Premultiplied alpha (cover 0 = glow, 1 = covering). Mesh: VfxMesh.beam_mesh() (UV = (side, along)).
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 core_color : source_color = vec3(0.9, 0.97, 1.0);
uniform vec3 glow_color : source_color = vec3(0.35, 0.55, 1.0);
uniform float width = 0.12;       // m half width
uniform float age : hint_range(0.0, 1.0) = 0.0;
uniform float scroll = 0.0;
uniform float cover = 0.15;
uniform float intensity = 1.0;
uniform float taper = 0.6;        // 0 = constant width, 1 = needle (thin at the tip)
uniform float grain = 0.0;        // 1 = granular streaks (sand)

varying float v_across;
varying float v_along;

void vertex() {
	vec3 a = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 b = (MODEL_MATRIX * vec4(0.0, 0.0, 1.0, 1.0)).xyz;
	float along = UV.y;
	vec3 c = mix(a, b, along);
	vec3 axis = normalize(b - a + vec3(1e-5));
	vec3 side = normalize(cross(axis, CAMERA_POSITION_WORLD - c) + vec3(1e-5));
	float w = width * mix(1.0, 1.0 - along * 0.85, taper) * (0.85 + 0.3 * sin(along * 30.0 - scroll * 20.0));
	vec3 wpos = c + side * (UV.x * 2.0 - 1.0) * w;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wpos, 1.0);
	v_across = UV.x * 2.0 - 1.0;
	v_along = along;
}

void fragment() {
	float n = texture(noise_tex, vec2(v_along * 3.0 - scroll * 2.0, v_across * 0.5 + 0.5)).r;
	float g = texture(noise_tex, vec2(v_along * 14.0 - scroll * 6.0, v_across * 2.0)).g;
	float x = abs(v_across);
	float core = exp(-x * x * 18.0);
	float halo = exp(-x * x * 3.0) * (0.5 + 0.5 * n);
	float life = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.55, 1.0, age));
	float ends = smoothstep(0.0, 0.04, v_along) * (1.0 - smoothstep(0.85, 1.0, v_along + (n - 0.5) * 0.2));
	float gr = mix(1.0, smoothstep(0.35, 0.75, g), grain);
	float k = (core * 1.2 + halo * 0.6) * life * ends * intensity * gr;
	vec3 c = mix(glow_color, core_color, core);
	ALBEDO = c * k;
	ALPHA = clamp(k * cover, 0.0, 1.0);
}
