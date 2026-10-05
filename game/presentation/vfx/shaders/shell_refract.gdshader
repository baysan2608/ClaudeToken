// Fresnel shell with the shared opaque-screen read (vacuum null bubble / well at quality >= 1).
// Draws the scene behind it pulled toward the centre. See shell_core.gdshaderinc.
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_back, fog_disabled;

#define SHELL_REFRACT
#include "res://presentation/vfx/shaders/shell_core.gdshaderinc"
