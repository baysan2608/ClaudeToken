// Helpers shared by the moveset VFX shaders (game/presentation/vfx/shaders). Builds on vfx_common.
#include "res://presentation/shaders/vfx_common.gdshaderinc"

// Incandescent emission for a heated solid (glass, metal): 0 = none, 1 = orange-yellow glow.
vec3 vfx_heat(float h, float n) {
	float t = clamp(h * (0.85 + 0.3 * n), 0.0, 1.0);
	return vfx_blackbody(t * 0.92) * smoothstep(0.05, 0.4, t) * (1.2 + 1.6 * t);
}

// Camera-facing billboard corner: centre + right/up from the inverse view matrix.
vec3 vfx_billboard(vec3 center, vec2 corner, mat4 inv_view) {
	return center + normalize(inv_view[0].xyz) * corner.x + normalize(inv_view[1].xyz) * corner.y;
}

// Ragged soft disc mask from a 2D noise texture value n (0..1); r = radius in -1..1 space.
float vfx_soft_disc(float r, float n, float erosion) {
	return 1.0 - smoothstep(0.35, 1.0, r + (n - 0.5) * erosion * 1.4);
}
