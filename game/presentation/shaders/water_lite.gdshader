// Water -> ice. One transparent layer (blend_mix, no depth write, back faces culled).
// Refraction uses the single optional cheap screen read (hint_screen_texture; the engine copies the
// opaque framebuffer once per frame no matter how many materials read it): a lens-like offset by the
// view-space normal with slight chromatic split, depth-of-volume absorption, fresnel reflection of
// the sky through the normal PBR specular path, scrolling ripples. frozen -> pale frosted, faceted
// ice with blurred (frosted) refraction and internal fractures.
// Used by WaterRibbonView (UV = (metres along, angle fraction), CUSTOM0 = (ring, side, radius, t))
// and WaterBlobView (blob = 1, sphere mesh).
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back, diffuse_burley, specular_schlick_ggx;

#define WATER_LITE
#include "res://presentation/shaders/water_core.gdshaderinc"
