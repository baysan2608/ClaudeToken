// Soft billboard puff for GPU particles (dust, steam, mist, splash droplets). One alpha-blended
// layer, noise-eroded round shape, fake top light, per-particle noise offset.
// Particle COLOR (colour ramp from the process material) drives tint and alpha over life;
// INSTANCE_CUSTOM.x = rotation, .y = life phase.
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform vec3 tint : source_color = vec3(0.60, 0.56, 0.50);
uniform float erosion : hint_range(0.0, 1.0) = 0.55;   // how ragged the puff edge is
uniform float light_top : hint_range(0.0, 1.0) = 0.25; // fake top lighting
uniform float soft_edge : hint_range(0.05, 1.0) = 0.6;
uniform float floor_y = -1000.0;   // world height of the ground under the effect (fades the quad out where it would clip into it)
uniform float floor_fade = 0.22;

varying float v_rand;
varying float v_life;
varying float v_wy;

void vertex() {
	mat4 mat_world = mat4(normalize(INV_VIEW_MATRIX[0]), normalize(INV_VIEW_MATRIX[1]), normalize(INV_VIEW_MATRIX[2]), MODEL_MATRIX[3]);
	float a = INSTANCE_CUSTOM.x;
	mat_world = mat_world * mat4(vec4(cos(a), -sin(a), 0.0, 0.0), vec4(sin(a), cos(a), 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	MODELVIEW_MATRIX = VIEW_MATRIX * mat_world;
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0), vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	v_rand = fract(sin(float(INSTANCE_ID) * 12.9898) * 43758.5453);
	v_life = INSTANCE_CUSTOM.y;
	v_wy = (INV_VIEW_MATRIX * (MODELVIEW_MATRIX * vec4(VERTEX, 1.0))).y;
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	vec2 nuv = UV * 0.8 + vec2(v_rand * 3.7, v_rand * 5.1) + vec2(v_life * 0.15, 0.0);
	float n = texture(noise_tex, nuv).r * 0.65 + texture(noise_tex, nuv * 2.7).g * 0.35;
	float shape = 1.0 - smoothstep(1.0 - soft_edge, 1.0, r + (n - 0.5) * erosion * 1.4);
	// fake lighting: lighter on the upper side of the puff, darker core-bottom
	float shade = 1.0 - light_top * (0.5 - 0.5 * p.y) * 1.4 + (n - 0.5) * 0.25;
	ALBEDO = tint * COLOR.rgb * shade;
	float above = smoothstep(floor_y, floor_y + floor_fade, v_wy);
	ALPHA = clamp(shape * COLOR.a * above, 0.0, 1.0);
}
