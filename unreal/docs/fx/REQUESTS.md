# fx stream - requests to other streams

## core
1. `slick` (Water / Water, sink slot) spawns, besides its `slick` zone, a second untagged AIR zone (tag `"zone"`, no
   props) - the generic zone verb's default tag. Godot spawns only the slick zone. FX ignores zones tagged `"zone"`
   (no view). File: `Source/FourfoldCore/Private/Combat/Verbs/VerbZone.cpp` / the slick def (`verb: zone` without a
   tag). Change: skip the generic zone when the move's hook already spawned its zone (or give the def its tag).
