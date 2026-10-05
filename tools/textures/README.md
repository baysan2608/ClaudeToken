# Environment textures (arena)

Reproducible, tileable 1024x1024 PBR sets for the arena; numpy + scipy only (no Pillow, no Blender needed).

```
/home/user/tools/bpyenv/bin/python tools/textures/gen_env_textures.py [--only flagstone,wall,ledge_cap,pool_tile,metal_plate]
/home/user/tools/bpyenv/bin/python tools/textures/gen_banners.py
tools/scripts/godot.sh --headless --import        # once after (re)generating
```

Outputs go to `game/assets/textures/` with hand-written `.import` files (mipmaps on, VRAM compression, normal
map flag) so the iOS export gets ASTC/ETC2.  Fixed seeds: re-running is byte-identical.

| Set | Repeat | Contents |
|---|---|---|
| `flagstone` | 4.0 m | hand-laid running-bond flags, wobbly joints, bevels, chips, cracks, grout, moss, lichen |
| `wall` | 3.0 m | ashlar courses (0.3 m), pitch-faced bosses, recessed mortar, rain streaks, lichen |
| `ledge_cap` | 2.4 m | cut slabs, chamfered arrises, pick-tool marks, weathering |
| `pool_tile` | 1.6 m | 10 cm glazed mosaic, crazing, chips, algae in the joints |
| `metal_plate` | 2.0 m | 2x2 one-metre deck plates, brushed grain, scratches, bolts, seam rust |
| `banners` | atlas | four 256x640 cloth banners with the original earth / water / fire / air glyphs |

Channel contract: `*_albedo` RGB = sRGB colour, **A = height** (used by the shaders to height-blend the
anti-tiling second layer); `*_normal` = OpenGL tangent space (stored RG-compressed: shaders rebuild z with
`env_decode_normal`); `*_orm` R = AO, G = roughness, B = metallic.

Shaders: `game/presentation/shaders/{arena_ground,ledge_stone,metal_plate,pool_basin,pool_water}.gdshader`
+ `env_common.gdshaderinc` (world-space box projection, anti-tiling, normal decode).  Textures are bound by
`ArenaView` (`game/presentation/arena_view.gd`); shaders fall back to a plain colour when unbound.

`env_preview.gd` renders fixed-camera stills for before/after comparisons:
`tools/scripts/godot.sh --render --resolution 1280x592 -s ../tools/textures/env_preview.gd -- /abs/outdir prefix [quality] [shot=<name>] [nowater] [noprops]`.
Set `TEX_PREVIEW=<dir>` when running the generator to also dump 2x2 tiled lit previews (seam / scale check).

## Environment pass 2 (water, sky, scenery)
* `pool_water.gdshader`: travelling-sine swells with analytic slope (smooth normal, no contour lines in the reflection),
  depth tint, crest brightening, rim foam + bubbles.  `pool_basin.gdshader`: web-like caustic filaments under the water line.
* `arena_sky.gdshader` (shader sky: gradient, low sun glow from LIGHT0, cloud deck evaluated only in the visible pass);
  `ArenaView.set_quality(0)` swaps back to ProceduralSkyMaterial.
* `scenery_hall.gdshader`: procedural plaster / clay-tile surfaces for the distant halls (2 draws, no texture reads).
* `ledge_stone.gdshader`: sub-5 cm vertical lips (pool coping) sample the slab set instead of a masonry course (no black seam).
* `arena_ground.gdshader`: ragged wall-edge grime band (`arena_half`, `edge_grime`).
* LightmapGI was evaluated and not used: it cannot be baked from a running/exported project (editor-only bake), the arena
  boxes carry no UV2, and the arena is built at runtime from `ArenaMap`.  The analytic contact-AO map + one-shot
  ReflectionProbe (quality 2) remain the baked-lighting substitute.
