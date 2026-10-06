# fx stream - requests to other streams

## core
1. `slick` (Water / Water, sink slot) spawns, besides its `slick` zone, a second untagged AIR zone (tag `"zone"`, no
   props) - the generic zone verb's default tag. Godot spawns only the slick zone. FX ignores zones tagged `"zone"`
   (no view). File: `Source/FourfoldCore/Private/Combat/Verbs/VerbZone.cpp` / the slick def (`verb: zone` without a
   tag). Change: skip the generic zone when the move's hook already spawned its zone (or give the def its tag).

## world_audio
1. Arena sun direction: the unlit / translucent FX fake their lighting with `FFKeyDir()` (Shaders/Common/FFLighting.ush)
   = UE world direction toward the light (-0.45, 0.35, 0.82), normalised. File: the level builder in
   `Content/Python/fourfold/world/`. Change: point the directional light along it (or tell `fx` the direction used so
   `FFKeyDir` can follow). Reason: crystal glints, water reflections and smoke shading should agree with the real sun.
2. Exposure: FX emissive levels assume a fixed exposure near EV 0 with bloom (no auto exposure, as ARCHITECTURE §10
   says). If the post process volume uses another fixed exposure, tell `fx` the value (the master defaults
   `GlowScale` / `EmissiveScale` will be rescaled).
