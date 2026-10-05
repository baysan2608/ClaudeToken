// Lantern glass: warm emissive core with a gentle flame flicker (phase from world position, so every lantern
// breathes differently without per-instance uniforms).  Lit, slightly translucent look; feeds glow/bloom.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform vec3 glow_color : source_color = vec3(1.0, 0.62, 0.24);
uniform float glow_energy = 2.4;
uniform float flicker : hint_range(0.0, 0.5) = 0.12;

void fragment() {
	float ph = MODEL_MATRIX[3].x * 1.7 + MODEL_MATRIX[3].z * 2.3;
	float f = 1.0 - flicker * (0.5 + 0.5 * sin(TIME * 7.0 + ph) * sin(TIME * 3.1 + ph * 0.7) );
	float rim = pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 2.0);
	ALBEDO = glow_color * 0.35;
	EMISSION = glow_color * glow_energy * f * (0.75 + 0.5 * rim);
	ROUGHNESS = 0.3;
}
