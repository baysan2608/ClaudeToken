// Distant hall surfaces (ArenaScenery MultiMeshes): lime-plaster walls and clay-tile hip roofs, drawn
// procedurally in world space so a whole ring of buildings costs two draw calls and no texture fetches.
//   kind 0  plaster: per-building tone from the instance position, stucco grain, rain streaks, damp band at
//           the foot, shaded eaves, a timber string-course;
//   kind 1  roof: staggered clay tiles (rows by height, columns along the hip face), per-tile tone, moss on
//           the lower courses.
// Mobile cost: 3 value-noise evaluations, no texture reads.  Lit normally, fogged by the environment.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://presentation/shaders/env_common.gdshaderinc"

uniform int kind = 0;
uniform vec3 plaster_a : source_color = vec3(0.78, 0.68, 0.52);
uniform vec3 plaster_b : source_color = vec3(0.7, 0.56, 0.4);
uniform vec3 tile_a : source_color = vec3(0.42, 0.2, 0.13);
uniform vec3 tile_b : source_color = vec3(0.3, 0.17, 0.12);
uniform vec3 timber : source_color = vec3(0.22, 0.14, 0.09);

varying vec3 v_world;
varying vec3 v_inst;
varying vec3 v_size;

void vertex() {
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_inst = MODEL_MATRIX[3].xyz;
	v_size = vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
}

void fragment() {
	vec3 wn = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	float id = env_h21(floor(v_inst.xz * 0.5));
	float h = v_world.y - (v_inst.y - (kind == 0 ? v_size.y * 0.5 : 0.0));   // height above the building's base
	vec3 alb;
	float rough = 0.92;
	if (kind == 0) {
		vec2 uv = vec2(v_world.x + v_world.z, v_world.y);
		vec3 base = mix(plaster_a, plaster_b, smoothstep(0.25, 0.75, id));
		float grain = env_vnoise(uv * 7.0) * 0.55 + env_vnoise(uv * 23.0) * 0.45;
		float streak = env_vnoise(vec2(uv.x * 2.2, uv.y * 0.28 + id * 9.0));
		alb = base * (0.88 + 0.2 * grain);
		alb *= 1.0 - 0.22 * smoothstep(0.5, 0.9, streak) * smoothstep(v_size.y, v_size.y * 0.3, h);
		alb *= 1.0 - 0.35 * (1.0 - smoothstep(0.0, 1.1, h));                       // damp / ground grime
		float eave = smoothstep(v_size.y - 1.4, v_size.y, h);
		alb *= 1.0 - 0.3 * eave;                                                   // shadow under the roof
		float band = 1.0 - smoothstep(0.0, 0.06, abs(h - v_size.y * 0.62 - 0.3 * id));
		alb = mix(alb, timber, band * step(wn.y, 0.5));
		if (wn.y > 0.5) {
			alb = mix(alb, tile_b, 0.6);
		}
	} else {
		vec3 tg = normalize(cross(vec3(0.0, 1.0, 0.0), wn));
		float row = h / 0.34;
		float col = dot(v_world, tg) / 0.3 + 0.5 * mod(floor(row), 2.0);
		vec2 cell = vec2(floor(col), floor(row));
		float th = env_h21(cell + id * 31.0);
		vec2 f = vec2(fract(col), fract(row));
		float edge = smoothstep(0.0, 0.14, f.x) * smoothstep(1.0, 0.86, f.x) * smoothstep(0.0, 0.2, f.y);
		alb = mix(tile_a, tile_b, th * 0.8 + 0.2 * id);
		alb *= 0.62 + 0.38 * edge;
		alb *= 0.9 + 0.2 * f.y;                                                    // each tile lighter at its lip
		float moss = smoothstep(0.55, 0.85, env_vnoise(vec2(v_world.x + v_world.z, v_world.y) * 1.6 + id * 7.0)) * (1.0 - smoothstep(0.0, 1.8, h - v_size.y * 0.0));
		alb = mix(alb, vec3(0.17, 0.2, 0.09), moss * 0.35);
		rough = 0.75;
	}
	ALBEDO = alb;
	ROUGHNESS = rough;
	SPECULAR = 0.3;
}
