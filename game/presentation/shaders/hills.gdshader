// Distant hill silhouettes: unshaded layers whose colour comes from vertex colour (set per layer in
// ArenaScenery) and is pulled toward the fog colour with height-independent distance haze by the engine fog.
shader_type spatial;
render_mode unshaded, cull_disabled;

void fragment() {
	ALBEDO = COLOR.rgb;
}
