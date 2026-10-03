// Dust streaks carried by an air push: N thin camera-facing ribbons that race along the cone axis.
// Fully vertex-animated (no CPU per frame): the node's air_push body transform (scale = cone
// radius, length) maps unit cone space to the world.
//   UV = (side 0/1, along 0/1)   COLOR = (angle rand, radius rand, phase rand, speed rand)
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform float age : hint_range(0.0, 1.0) = 0.0;
uniform float streak_len = 0.9;     // metres
uniform float streak_width = 0.028;  // metres (half width)
uniform vec3 tint : source_color = vec3(0.78, 0.72, 0.64);
uniform float opacity = 0.5;

varying float v_alpha;
varying float v_across;
varying float v_along;

void vertex() {
	float ang = COLOR.r * 6.2831853;
	float rad = sqrt(COLOR.g) * 0.8;
	float travel = clamp(age * (1.15 + 0.5 * COLOR.a) + COLOR.b * 0.25, 0.0, 1.2);
	float y = travel;            // unit cone space, 0 apex .. 1 end
	float r = rad * (0.06 + 0.94 * y);
	vec3 c_local = vec3(cos(ang) * r, y, sin(ang) * r);
	vec3 c_world = (MODEL_MATRIX * vec4(c_local, 1.0)).xyz;
	vec3 along = normalize((MODEL_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
	vec3 to_cam = CAMERA_POSITION_WORLD - c_world;
	vec3 side = cross(along, to_cam);
	side = side / max(length(side), 1e-5);
	float sd = UV.x * 2.0 - 1.0;
	float al = UV.y * 2.0 - 1.0;
	vec3 wpos = c_world + along * al * streak_len * 0.5 + side * sd * streak_width;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wpos, 1.0);
	v_alpha = smoothstep(0.0, 0.18, y) * (1.0 - smoothstep(0.7, 1.05, y)) * (1.0 - smoothstep(0.7, 1.0, age));
	v_across = sd;
	v_along = al;
}

void fragment() {
	float shape = (1.0 - abs(v_across)) * (1.0 - abs(v_along));
	shape = pow(clamp(shape, 0.0, 1.0), 1.5);
	ALBEDO = tint;
	ALPHA = clamp(shape * v_alpha * opacity, 0.0, 1.0);
}
