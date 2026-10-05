// Late-day sky for the courtyard: warm horizon haze rising into a cool blue zenith, a soft low sun glow
// (aligned with the arena's DirectionalLight3D through LIGHT0_*) and a thin deck of stretched, sun-lit
// stratocumulus.  The clouds are only evaluated for the visible background pass (not for the radiance
// cubemap that lights the arena), so ambient light stays a smooth gradient and the per-pixel cost is four
// value-noise octaves on sky pixels only.  Quality 0 uses ProceduralSkyMaterial instead (ArenaView).
shader_type sky;

uniform vec3 top_color : source_color = vec3(0.24, 0.38, 0.6);
uniform vec3 horizon_color : source_color = vec3(0.9, 0.72, 0.56);
uniform vec3 ground_horizon_color : source_color = vec3(0.74, 0.62, 0.5);
uniform vec3 ground_bottom_color : source_color = vec3(0.17, 0.15, 0.13);
uniform vec3 cloud_lit_color : source_color = vec3(1.0, 0.78, 0.56);
uniform vec3 cloud_shade_color : source_color = vec3(0.4, 0.4, 0.55);
uniform float cloud_cover : hint_range(0.0, 1.0) = 0.55;
uniform float cloud_speed = 0.0035;

float sky_h21(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float sky_vnoise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(sky_h21(i), sky_h21(i + vec2(1.0, 0.0)), f.x), mix(sky_h21(i + vec2(0.0, 1.0)), sky_h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float sky_fbm(vec2 p) {
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 4; i++) {
		s += a * sky_vnoise(p);
		p = p * 2.03 + vec2(17.1, 5.3);
		a *= 0.5;
	}
	return s;
}

void sky() {
	vec3 d = EYEDIR;
	float up = d.y;
	vec3 col;
	if (up >= 0.0) {
		col = mix(horizon_color, top_color, smoothstep(0.0, 1.0, pow(up, 0.42)));
	} else {
		col = mix(ground_horizon_color, ground_bottom_color, smoothstep(0.0, 0.6, -up));
	}
	float sun = LIGHT0_ENABLED ? max(dot(d, LIGHT0_DIRECTION), 0.0) : 0.0;
	vec3 sun_col = LIGHT0_COLOR * min(LIGHT0_ENERGY, 1.0);
	col += sun_col * (pow(sun, 6.0) * 0.16 + pow(sun, 48.0) * 0.30);
	if (!AT_CUBEMAP_PASS && up > 0.0) {
		vec2 uv = d.xz / (up + 0.22) * vec2(0.8, 1.5) + vec2(TIME * cloud_speed, 3.0);
		float n = sky_fbm(uv);
		float dens = smoothstep(1.0 - cloud_cover, 1.0 - cloud_cover + 0.32, n);
		dens *= smoothstep(0.02, 0.22, up) * (1.0 - 0.55 * smoothstep(0.55, 1.0, up));
		// lighting: compare density a step toward the sun; thinner on the sun side = lit edge
		vec2 toward = LIGHT0_DIRECTION.xz * 0.12;
		float lit = clamp((n - sky_fbm(uv + toward)) * 4.0 + 0.5, 0.0, 1.0);
		vec3 cc = mix(cloud_shade_color, cloud_lit_color, lit);
		cc = mix(cc, horizon_color * 1.05, 1.0 - smoothstep(0.02, 0.3, up));   // melt into the haze at the horizon
		cc += sun_col * pow(sun, 5.0) * 0.35;
		col = mix(col, cc, dens * 0.92);
		// sun disc, dimmed by cloud
		col += sun_col * smoothstep(0.9993, 0.9998, sun) * 3.0 * (1.0 - dens);
	}
	COLOR = col;
}
