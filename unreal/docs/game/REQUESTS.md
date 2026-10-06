# Stream `game` - requests to other owners

Changes the game module needs outside `unreal/Source/Fourfold/**` and `unreal/docs/game/**`. Each entry names the
file, the exact change and the reason. Nothing here blocks compiling; the game degrades gracefully without them.

## Config (owner: `world_audio`)

| File | Change | Reason |
|---|---|---|
| `Config/DefaultInput.ini` `[/Script/Engine.InputSettings]` | `bShowConsoleOnFourFingerTap=False` | Normal play puts 3-4 fingers on the glass (stick + ATTACK + TECH + a chip). A four-finger tap opens the console mid-fight. Keep it only in a dev-only ini if wanted. |
| `Config/DefaultEngine.ini` `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]` | Allow 120 Hz: set `FrameRateLock` to the 5.8 value that does not cap at 60 (`PUFRL_None`, or `PUFRL_120` if 5.8 lists it). If 5.8 has a ProMotion switch, enable it. | Settings > Graphics > Frame rate offers 30 / 60 / 120. The game applies the cap with `t.MaxFPS`, but the iOS frame pacer lock wins over it. With `PUFRL_60` the 120 option stays at 60. |
| `Build/IOS/Info.plist` additions (or the 5.8 equivalent setting) | `CADisableMinimumFrameDurationOnPhone = YES` | iPhone ProMotion only runs above 60 Hz when this key is present. |
| `Config/DefaultEngine.ini` | Keep `GlobalDefaultGameMode=/Script/Fourfold.FourfoldGameMode` (already there). | The class exists: `AFourfoldGameMode`, no pawn, no spectator. |

## World (owner: `world_audio`)

- Tag the level's arena root actor `FourfoldArena`. Without that tag the game spawns its placeholder arena of
  engine cubes.
- Tag every mesh that stands in for a sim solid `FFSolid_<name>`, where `<name>` is the `ArenaBox.name` from
  `ff::Session::Arena()`. When the camera is behind one of those solids, the game calls `SetRenderInMainPass(false)`
  on its primitives. They stay out of the main pass but still cast shadows.
- Sim space maps to Unreal as `UE(X, Y, Z) = 100 * (x, z, y)` (`FourfoldCoords.h`). The floor top is at Z = 0.

## Core (owner: `core`)

- `PracticeItems()` in Godot returns a `__lab_mode` toggle item. The game calls `LoadScenario("__lab_mode")` for it
  and shows "not available" when that fails. Either make that id toggle Lab mode and reload the current scenario, or
  add `void Session::SetLabMode(bool)` and `bool Session::LabMode() const`.
- `ScenarioOptions::spar_difficulty` / `spar_kit` come from the Free Spar page and from the practice option rows. The
  Godot build saves them in the progression, so please persist them in `SaveProgress()` the same way.
- The Lab Tuning page shows only move fields (`LabTuningFields`). The Godot page also tunes the numeric thresholds of
  the counter-rule cells. Adding `LabRuleCells()` / `LabRuleFields(key, idx)` / `LabSetRuleValue(...)` would restore
  that. Optional.

## Audio (owner: `world_audio`)

- `UFourfoldSimSubsystem::OnUiCue` fires these names: `ui_tap ui_select ui_back ui_open ui_close ui_toggle
  ui_ring_open ui_ring_pick ui_toast ui_pause ui_resume`. `ui_error` is reserved and not fired yet.
- Volumes are in `UFourfoldSettingsSubsystem::GetSettings()` (`MasterVolume SfxVolume AmbienceVolume UiVolume`).
  `OnChanged` broadcasts every edit, so a slider drag broadcasts many times.

## Animation / character (owners: `animation`, `character`)

- Data the runtime reads, all optional (each has built-in defaults and logs one warning when missing):
  `Content/Fourfold/Data/clips.json`, `anim_map.json` (schemas in ARCHITECTURE §8.3) and `character.json`
  (`mesh`, `mesh_yaw_offset_deg`, `height_m`, `palettes.{player,rival,dummy}.{main,accent,trim}`).
- Bones the runtime drives by name (UE5-Manny naming): `pelvis spine_01..05 neck_01 neck_02 head`,
  `clavicle/upperarm/lowerarm/hand_{l,r}`, `thigh/calf/foot/ball_{l,r}` (+ `thigh_twist_0x`, `calf_twist_0x`), the
  hand-shape fingers `thumb_0{1,2,3}_{l,r}` and `{index,middle,ring,pinky}_{metacarpal,01,02,03}_{l,r}`, and the
  spring chains `ff_hair_*`, `ff_sash_{l,r}_*`, `ff_hem_{fl,fr,bl,br}_*`. A missing bone skips its layer.
- Material parameters set on every slot's MID: `FF_Main FF_Accent FF_Trim` (vector), `FF_Wet FF_Frost FF_Burn
  FF_ElementGlow` (scalar 0..1) and `FF_ElementColor` (vector).

## Plugins / .uproject

- None. The module needs only `Slate`, `SlateCore` and `ApplicationCore` (engine modules) on top of
  `Core / CoreUObject / Engine / InputCore / FourfoldCore`. Input comes from `APlayerController` key state, so no
  Enhanced Input assets are needed. The configured `DefaultPlayerInputClass` (EnhancedPlayerInput) still fills the
  key state the game reads.
