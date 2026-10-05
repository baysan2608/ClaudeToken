// Fresnel shell, no screen read (lite path and every non-vacuum use). See shell_core.gdshaderinc.
shader_type spatial;
render_mode blend_premul_alpha, unshaded, depth_draw_never, cull_back, fog_disabled;

#include "res://presentation/vfx/shaders/shell_core.gdshaderinc"
