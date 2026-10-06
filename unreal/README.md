# Fourfold — Unreal Engine 5.8 project

Original elemental martial-arts combat lab for iPhone / iPad (and Mac), rebuilt in Unreal Engine 5.8 from the Godot
reference in `../game/`.

* **Owner setup on the Mac:** [`docs/MAC_SETUP.md`](docs/MAC_SETUP.md)
* **Architecture and contracts:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
* **Movement bible (Hung Gar / Tai Chi / Northern Shaolin / Baguazhang), clip catalogue, move → clip map:**
  [`docs/MARTIAL_ARTS.md`](docs/MARTIAL_ARTS.md)

| Path | What |
|---|---|
| `Fourfold.uproject`, `Config/`, `Source/*.Target.cs` | the Unreal project |
| `Source/FourfoldCore/` | engine-free C++20 gameplay simulation (also built and tested with CMake in `CoreTests/`) |
| `Source/Fourfold/` | game module: fighters, animation runtime, camera, input, Slate UI, game flow |
| `Source/FourfoldFX/`, `Source/FourfoldShaders/`, `Shaders/` | visual effects and shader includes |
| `Source/FourfoldAudio/` | sound |
| `Content/Python/` | editor scripts that import and build every asset (`fourfold_setup.py`) |
| `SourceArt/` | generated FBX / PNG / WAV inputs for the import scripts |
| `Tools/` | generators (Blender, audio, VFX, world, Godot data export) and the Python mock |
