// Air push pressure cone: a travelling pressure band drawn on an open cone, refracting the screen
// with a SMALL noise-driven offset (the single optional cheap distortion; the engine copies the
// opaque framebuffer once per frame no matter how many materials read it). Opacity is low so
// enemies behind / inside the cone stay clearly visible. Axis +Y, unit length, unit end radius.
//   UV.x = angle fraction, UV.y = along the cone (0 apex .. 1 end)
// Shared body of air_push.gdshader (screen refraction) and air_push_lite.gdshader (no screen read).

#ifndef AIR_LITE
uniform sampler2D screen_tex : hint_screen_texture, repeat_disable, filter_linear_mipmap;
#endif
uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float age : hint_range(0.0, 1.0) = 0.0;
uniform float strength = 1.0;        // distortion strength multiplier (0 disables the refraction)
uniform float max_offset = 0.0075;    // max screen-UV offset (~1.2 % of the screen)
uniform float opacity = 0.55;

varying float v_y;

void vertex() {
	v_y = UV.y;
	// cone surface expands slightly as the push travels
	float grow = 1.0 + 0.12 * age;
	VERTEX.xz *= grow;
}

void fragment() {
	float y = v_y;
	float front = clamp(age * 1.5, 0.0, 1.0);
	float fade = 1.0 - smoothstep(0.55, 1.0, age);
	float base = smoothstep(0.0, 0.1, y) * (1.0 - smoothstep(0.92, 1.0, y));
	// soft toward the cone silhouette so no hard wedge outline shows
	float ndv = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float soft = smoothstep(0.04, 0.55, ndv);

	// travelling pressure band (long soft tail) + three thin wave crests
	float band = smoothstep(front - 0.5, front - 0.03, y) * (1.0 - smoothstep(front - 0.03, front + 0.04, y));
	float c1 = exp(-pow((y - front) * 13.0, 2.0));
	float c2 = exp(-pow((y - front + 0.17) * 13.0, 2.0)) * 0.6;
	float c3 = exp(-pow((y - front + 0.34) * 13.0, 2.0)) * 0.35;
	float rings = c1 + c2 + c3;
	float mask = (0.55 * band + 1.0 * rings) * fade * base * soft;

	vec2 uv = vec2(UV.x * 2.0, y * 1.0 - age * 2.5);
	vec2 nv = vec2(texture(noise_tex, uv).r, texture(noise_tex, uv + vec2(0.31, 0.77)).r) - 0.5;
	vec2 off = (nv * 2.0 + vec2(0.0, 0.25)) * max_offset * mask * strength;
#ifdef AIR_LITE
	vec3 col = vec3(0.72, 0.78, 0.84);
#else
	vec3 col = textureLod(screen_tex, SCREEN_UV + off, 0.0).rgb;
#endif
	col += vec3(0.16, 0.18, 0.20) * rings * soft * fade * base;   // crest highlight (reads over flat colour)
	ALBEDO = col;
	ALPHA = clamp(mask * opacity * 1.6, 0.0, 1.0);
}
