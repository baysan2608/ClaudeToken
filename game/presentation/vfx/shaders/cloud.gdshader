// Soft volume of camera-facing noise puffs (sand cloud, sandstorm, fog, mist, steam, geyser column,
// dust line, sand slug). Drawn as ONE MultiMesh per layer (CloudView uses two layers: a wide soft
// outer shell and a denser core), entirely vertex-animated from uniforms + per-instance data, so the
// CPU never touches the instances after setup.
//   INSTANCE_CUSTOM = (angle 0..1, radius fraction 0..1, height fraction 0..1, random 0..1)
//   Instance transforms are identity; the node transform places the volume (y = ground).
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_disabled, fog_disabled;

uniform sampler2D noise_tex : filter_linear_mipmap, repeat_enable;
uniform float phase = 0.0;          // accumulated time (integrated by the view)
uniform float radius = 1.5;         // m
uniform float height = 1.0;         // m
uniform float puff_size = 0.9;      // m (diameter)
uniform float swirl = 0.3;          // rad/s at the rim (inner puffs turn faster)
uniform float rise = 0.0;           // cycles/s: puffs travel up the column and recycle (steam, geyser)
uniform float column = 0.0;         // 1 = the volume narrows toward the base (geyser / jet column)
uniform float flatten = 0.0;        // 1 = low flattened fog billboards
uniform float grow = 0.6;           // puffs grow by this fraction toward the top
uniform float wander = 0.15;        // m of slow drift per puff
uniform float opacity = 0.3;
uniform vec3 tint_top : source_color = vec3(0.85, 0.80, 0.70);
uniform vec3 tint_low : source_color = vec3(0.55, 0.48, 0.38);
uniform float erosion = 0.55;
uniform float floor_y = -1000.0;
uniform float amount : hint_range(0.0, 1.0) = 1.0;   // 0..1 fraction of puffs shown (fade in/out)
uniform float stretch = 0.0;        // >0: puffs elongate along local -Z (slug trail)
uniform float y0 = 0.0;             // base height of the volume (m, local)
uniform float squash_x = 1.0;       // >1 widens the volume along local X (steam screen, dust line)

varying float v_alpha;
varying float v_rand;
varying float v_h;
varying float v_wy;

void vertex() {
	vec4 ic = INSTANCE_CUSTOM;
	float rnd = ic.w;
	float h = ic.z;
	float life = 1.0;
	if (rise > 0.0) {
		h = fract(ic.z + phase * rise * (0.75 + 0.5 * rnd));
		life = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.62, 1.0, h));
	}
	float rf = ic.y;
	float a = ic.x * 6.2831853 + phase * swirl * (1.6 - rf);
	float r = radius * rf * mix(1.0, 0.25 + 1.1 * h, column);
	vec3 c = vec3(cos(a) * r * squash_x, y0 + h * height, sin(a) * r);
	c += vec3(sin(phase * 0.71 + rnd * 21.0), 0.35 * sin(phase * 0.93 + rnd * 13.0), cos(phase * 0.63 + rnd * 17.0)) * wander;
	c.z -= stretch * rnd * 1.2;
	vec3 center = (MODEL_MATRIX * vec4(c, 1.0)).xyz;
	float size = puff_size * (0.65 + 0.7 * rnd) * (1.0 + grow * h);
	float rot = rnd * 6.28 + phase * 0.25 * (rnd - 0.5);
	vec2 q = vec2(VERTEX.x * cos(rot) - VERTEX.y * sin(rot), VERTEX.x * sin(rot) + VERTEX.y * cos(rot));
	vec3 right = normalize(INV_VIEW_MATRIX[0].xyz);
	vec3 up = normalize(INV_VIEW_MATRIX[1].xyz);
	// flattened fog: keep the billboard width, squash it toward the ground plane
	up = normalize(mix(up, vec3(0.0, 1.0, 0.0), 0.5 * flatten)) * (1.0 - 0.55 * flatten);
	vec3 wpos = center + (right * q.x + up * q.y) * size;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(wpos, 1.0);
	float shown = step(rnd, amount * 1.02);
	v_alpha = life * shown * smoothstep(0.0, 0.15, amount);
	v_rand = rnd;
	v_h = h;
	v_wy = wpos.y;
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = length(p);
	vec2 nuv = UV * 0.75 + vec2(v_rand * 3.7, v_rand * 5.1) + vec2(phase * 0.03, -phase * 0.02);
	float n = texture(noise_tex, nuv).r * 0.65 + texture(noise_tex, nuv * 2.6 + 0.3).g * 0.35;
	float shape = 1.0 - smoothstep(0.35, 1.0, r + (n - 0.5) * erosion * 1.4);
	float shade = 0.78 + 0.32 * (0.5 - 0.5 * p.y) + (n - 0.5) * 0.3;
	vec3 col = mix(tint_low, tint_top, clamp(v_h * 0.8 + (0.5 - 0.5 * p.y) * 0.4, 0.0, 1.0));
	ALBEDO = col * shade;
	float above = smoothstep(floor_y, floor_y + 0.25, v_wy);
	ALPHA = clamp(shape * opacity * v_alpha * above, 0.0, 1.0);
}
