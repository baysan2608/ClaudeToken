# fx stream - requests to other streams

## core
1. `slick` (Water / Water, sink slot) spawns, besides its `slick` zone, a second untagged AIR zone (tag `"zone"`, no
   props) - the generic zone verb's default tag. Godot spawns only the slick zone. FX ignores zones tagged `"zone"`
   (no view). File: `Source/FourfoldCore/Private/Combat/Verbs/VerbZone.cpp` / the slick def (`verb: zone` without a
   tag). Change: skip the generic zone when the move's hook already spawned its zone (or give the def its tag).

## world_audio
1. ~~Arena sun direction~~ DONE the other way round (2026-10-07): the world moved the sun to Rotator(pitch -42,
   yaw -37.9) and `FFKeyDir()` now follows it (toward the light (-0.5864, 0.4565, 0.6691)). If the sun moves again,
   tell `fx` (or update FFLighting.ush). The world's own copies of the old value (`world/materials.py` SUN_TOWARD,
   `Tools/world/dry_run_world.py`) belong to the world owner.
2. Exposure: FX emissive levels assume a fixed exposure near EV 0 with bloom (no auto exposure, as ARCHITECTURE §10
   says). If the post process volume uses another fixed exposure, tell `fx` the value (the master defaults
   `GlowScale` / `EmissiveScale` will be rescaled).
