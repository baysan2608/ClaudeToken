# Media (real renders of the current build)

Recorded with Godot's movie writer from the actual game, driven through the real input path by `game/actors/autoplay.gd`
(`--autoplay=flagship` and `--autoplay=show_<element>`; choreographies in `game/actors/showcase_*.gd`).
Rendered on CPU (Mesa lavapipe, Mobile renderer) at 1280×720/592, 30 fps: lighting and shadow quality on a real iPhone will differ.

| File | Shows |
|---|---|
| `flagship.mp4` | Rival throws stone → magma grip → molten → pour lava wave → rival draws heat → rock; heavy stone sidestepped; rock reused |
| `earth.mp4` | stone shot, heavy heave, too-heavy boulder, seize/aim/throw, earth wall block, perfect-guard redirects |
| `water.mp4` | draw from pool, stream, lash, water shield vs flares (steam), ice lance shatter, melt to puddle |
| `air.mp4` | palm gust, gust deflecting a stone, cyclone push, air dash, updraft onto the high ledge, glide |
| `fire.mp4` | flare jab, blaze, magma melt + pour, heat draw, vent, lightning conducting through the pool |
| `owner_examples.mp4` | `--autoplay=show_owner:56`: hold-longer tiers T0→T3; a thrown stone split & spiked back / swallowed / carried back by a wave / melted to lava / deflected by wind; palm gust fails vs a lava wave, Cyclone Fortress sets it to rock; a wall lanced (glows), slumped and surged back; Bolt grounded, Storm Bolt through stone |
| `owner_lava_vs_palm_gust.jpg`, `owner_lava_vs_cyclone_fortress.jpg` | Counter strength scales with the threat: a T0 gust lets the lava wave through; a Vortex T3 tornado cools it to rock |
| `owner_lanced_wall_glows.jpg`, `owner_storm_bolt_through_wall.jpg` | A Molten Lance heats a Bulwark's face to melting before it slumps; a Storm Bolt (T2) shatters the wall that grounded the T1 Bolt |
