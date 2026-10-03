// Air push pressure cone: a travelling pressure band drawn on an open cone, refracting the screen
// with a SMALL noise-driven offset (the single optional cheap distortion; the engine copies the
// opaque framebuffer once per frame no matter how many materials read it). Opacity is low so
// enemies behind / inside the cone stay clearly visible. Axis +Y, unit length, unit end radius.
//   UV.x = angle fraction, UV.y = along the cone (0 apex .. 1 end)
shader_type spatial;
render_mode blend_mix, unshaded, depth_draw_never, cull_disabled, fog_disabled;

#define AIR_LITE
#include "res://presentation/shaders/air_push_core.gdshaderinc"
