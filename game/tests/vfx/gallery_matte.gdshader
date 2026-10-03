// Plain matte PBR surface for gallery set pieces. (StandardMaterial3D surfaces render shadowed under
// the lavapipe Mobile path used for headless screenshots; a ShaderMaterial does not.)
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;
uniform vec3 albedo = vec3(0.2);   // linear (the gallery converts from sRGB)
uniform float rough : hint_range(0.0, 1.0) = 0.9;
void fragment() {
	ALBEDO = albedo;
	ROUGHNESS = rough;
	SPECULAR = 0.35;
}
