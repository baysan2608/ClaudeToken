// Tiny additive ember / spark billboard for GPU particles. HDR core, round soft falloff.
shader_type spatial;
render_mode blend_add, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform float brightness = 3.0;

void vertex() {
	mat4 mat_world = mat4(normalize(INV_VIEW_MATRIX[0]), normalize(INV_VIEW_MATRIX[1]), normalize(INV_VIEW_MATRIX[2]), MODEL_MATRIX[3]);
	MODELVIEW_MATRIX = VIEW_MATRIX * mat_world;
	MODELVIEW_MATRIX = MODELVIEW_MATRIX * mat4(vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0), vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0), vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0), vec4(0.0, 0.0, 0.0, 1.0));
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float d = dot(p, p);
	float core = exp(-d * 9.0);
	float halo = exp(-d * 2.2) * 0.22;
	ALBEDO = COLOR.rgb * brightness;
	ALPHA = clamp((core + halo) * COLOR.a, 0.0, 1.0);
}
