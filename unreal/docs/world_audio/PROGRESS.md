# world_audio stream - PROGRESS (checkpoint file; update after every sub-step)

Resume rule: read this file first, continue at "CURRENT" / "NEXT STEPS". Do not restart finished items.
Scratch: /tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/world_audio/  (call it $S)
Python for audio / blender: /home/user/tools/bpyenv/bin/python (numpy 1.26, scipy 1.17, matplotlib; system python3 has NO numpy).
UE 5.8.2 public-header mirror (for API verification, read-only, made by the game stream):
  /tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/game/uecheck58/ue/Engine/Source/Runtime
  (+ mock UHT / syntax-check scripts in unreal/Source/Fourfold/Private/Logic/tools/ue_syntax_check; if the scratch dir is gone, re-run its setup.sh)

## RUN 3 (started): audio A1-A4 are DONE (see below). Remaining, in order:
 A5 docs/audio/{README,API_NOTES}.md (not yet written) -> W1 env art (Tools/world) -> W2 fourfold/world + Shaders/Env -> S1 fourfold_setup.py + init_unreal.py
 -> C1 Config review (+ requests from game stream) -> docs/world/README.md -> final mock dry run + report.
 (Run-3 log lines are appended at the bottom under "RUN 3 LOG".)

## State at start of run 2 (this file did not exist before)
- Existing partial files at start: Config/{DefaultEngine,DefaultInput,DefaultGame,DefaultEditor}.ini, Config/IOS/IOSEngine.ini
  (architect versions, unreviewed), Source/FourfoldAudio/{FourfoldAudio.Build.cs, Private/FourfoldAudioModule.cpp} (architect stubs).
- Nothing else of this stream existed.

## Requests from other streams that I must honour (docs/game/REQUESTS.md)
- Config: bShowConsoleOnFourFingerTap=False; iOS 120 Hz (FrameRateLock PUFRL_None / 120) + Info.plist CADisableMinimumFrameDurationOnPhone
  (try IOSRuntimeSettings AdditionalPlistData); keep GlobalDefaultGameMode.
- World: root actor tag `FourfoldArena`; every mesh standing in for a sim solid tagged `FFSolid_<ArenaBox.name>` (game hides it when the
  camera is behind it: SetRenderInMainPass(false)); floor top at Z=0.
- Audio: OnUiCue names: ui_tap ui_select ui_back ui_open ui_close ui_toggle ui_ring_open ui_ring_pick ui_error ui_pause ui_resume ui_toast.
  Settings volumes: MasterVolume SfxVolume AmbienceVolume UiVolume (UFourfoldSettingsSubsystem::GetSettings, OnChanged fires on every slider move).

## DONE
- (A1.0) base synth copied to Tools/audio/synth_sfx.py with SR=48000 (116 sounds, 7.5 s). Registry extended (Spec.bus/cat2/max_loop/gap),
  new render_all()/main() writing SourceArt/Audio/{SFX,Ambience}/*.wav + SourceArt/Audio/sound_index.json; runner Tools/audio/render_audio.py.
- (A1.1) Tools/audio/validate_audio.py written (format/peak/DC/seam/tail checks, K-weighted loudness table, spectrogram sheets via --sheet OUTDIR prefix...).
- (A1.2) synth_martial.py DONE+rendered+looked at (swings/kicks/stomps/breath/cloth/steps per surface/lands: ~60 sounds).
- (A1.3) synth_impacts.py DONE+rendered+looked at (impact_<mat>_light/heavy x10 mats, status_burn_loop, status_off, status_wet_on, armor_up, stance_settle).

- (A1.4) synth_music.py (charge_<el>_t1-3 x12, UI family replaces base ui_tap/select/back + ui_open/close/toggle/ring_open/ring_pick/error/pause/resume/toast,
  ko_bell, round_start, round_reset, victory, scenario_start, combo_*, challenge_done, chime_wind_1-4, bamboo_knock) and synth_ambience.py (3 beds 12-16 s) DONE.
  Phone-safe mastering added (pthump floor 210/130 Hz, Spec.lowcut 70 Hz default, phone-weighted loudness for suggested gain). 230 WAVs (SFX 222 + Ambience 8...
  see SourceArt/Audio), validate_audio.py PASS, determinism True, spectrograms looked at.
- Specs sheets in $S/specs (scratch).

- (A2) Tools/audio/event_rules.py (rule data ported from fx_director/fx_cues + new swing/step/status/zone/app rules) + build_manifest.py (validates, writes
  Content/Fourfold/Data/sfx_manifest.json, 133 KB, 233 sounds, 168 event rules, 35 loop rules). Added water_jet, geyser_loop, fuse_loop sounds.
  RULE LANGUAGE documented in the docstring of event_rules.py (when/play/at/table/template/gain_map/limit/delay_s; loops.bodies|actors|events; steps; ambience; ui; mix).
  Field names for loop rules (C++ must expose): body: mat(name) form(name) phase tag fx_mat liquid temp on_ground controller $speed ...; actor: element(name) surface
  grounded guarding gliding flying action.id action.phase(name) action.data.<k> $speed $status(array of names).

- (A3.1) FourfoldAudio Logic island DONE + tested: Source/FourfoldAudio/Private/Logic/{FFAManifest,FFAMix.h,FFARules,FFAVoices,FFALoops}.{h,cpp}, tests/{CMakeLists.txt,logic_tests.cpp}.
  Run: cmake -S unreal/Source/FourfoldAudio/Private/Logic/tests -B $S/ffa_gcc -G Ninja && cmake --build $S/ffa_gcc && $S/ffa_gcc/ffa_logic_tests
  -> 24 tests, 1603 checks, 0 failures (g++ Debug + clang Release, -Werror, UE-macro poison, no exceptions/RTTI). Includes tests against the REAL sfx_manifest.json.
  The test checks every manifest sound has its WAV in SourceArt/Audio.

- (A3.2) UE glue WRITTEN: Public/FourfoldAudioSubsystem.h, Private/FourfoldAudioSubsystem.cpp (UFourfoldAudioSubsystem), FourfoldAudio.Build.cs (private include paths).
  Syntax check harness: Tools/audio/ue_syntax_check/check_audio.sh (FF_GEN=$S/uegen already generated; FF_ONLY=FourfoldAudioSubsystem.cpp for one file; log $S/check1.log).
  FourfoldAudioSubsystem.cpp + 4 Logic files + Module.cpp: 0 errors against UE 5.8.2 headers ($S/check1.log exit=0). Warning pass running: $S/check2.log.
- (A4) fourfold/audio/__init__.py build_all WRITTEN + mock dry run OK (237 created in the mock = 233 waves + 4 sound classes).
  Python names to verify in API_NOTES: unreal.AssetImportTask for WAV (SoundFactory), SoundWave props looping/sound_group/loading_behavior/compression_quality/sound_class_object,
  enums unreal.SoundGroup.SOUNDGROUP_*, unreal.SoundWaveLoadingBehavior.*, unreal.SoundClassFactory (all wrapped in try/except, non-fatal).

## (old) NEXT in A3: UE glue: Public/FourfoldAudioSubsystem.h + Private/FourfoldAudioSubsystem.cpp (UFourfoldAudioSubsystem : UWorldSubsystem), update Build.cs
  (Private include paths), syntax-check against the UE 5.8.2 header mirror (adapt unreal/Source/Fourfold/Private/Logic/tools/ue_syntax_check/cc_ue.sh for module FourfoldAudio), then
  docs/audio/API_NOTES.md. Design decisions: voices tracked by VoiceBook, stolen with UAudioComponent::FadeOut; plays via UGameplayStatics::SpawnSoundAtLocation /
  SpawnSound2D; loops are persistent UAudioComponents (bAutoDestroy=false) with SetVolumeMultiplier per frame; attenuation = runtime USoundAttenuation per class
  (NaturalSound, AttenuationShapeExtents.X=inner, FalloffDistance, dBAttenuationAtMax); time dilation does NOT pitch UE sounds (verified web) so hit-stop is fine;
  UI + ambience components SetUISound(true) so they play while paused.

## CURRENT (old note, A1 essentially done; remaining: final listing + docs)
- A1 remaining: nothing blocking. (old text: write synth_music.py (charge risers per element T1-T3, UI family wooden/bell, round/KO/combo/challenge, wind chimes + bamboo knock) and
  synth_ambience.py (3 long beds 12-16 s: courtyard air, wind in trees, distant water; peaks -3 dBFS like everything else; amb_courtyard_loop peak -> -3),
  then tune levels (validate_audio.py --loudness), final spectrogram review of the whole set, determinism check.
  Known small TODO: swing_earth_* has broadband haze from warm(): low-pass it (3.5 kHz) before adding the cloth layer.

## NEXT STEPS (plan, in order)
1. A1 audio synthesis (above) -> SourceArt/Audio/{SFX,Ambience}; validate_sfx port; spectrogram sheets LOOKED at.
2. A2 sfx_manifest.json + events rule table (Tools/audio/build_manifest.py + event_rules.py).
3. A3 FourfoldAudio C++ (Private/Logic UE-free + g++ tests under FF_LOGIC_TESTS; subsystem; syntax check vs UE 5.8.2 headers); docs/audio/API_NOTES.md.
4. A4 fourfold/audio/__init__.py build_all.
5. W1 environment textures + Blender hero meshes (Tools/world) -> SourceArt/Environment; Blender mock-up renders from the gameplay camera.
6. W2 fourfold/world/__init__.py (+ Shaders/Env/*.ush), reads sim.json.
7. S1 fourfold_setup.py + init_unreal.py; mock dry run (Tools/py_mock/run_with_mock_unreal.py).
8. C1 Config review (see requests above) + docs (docs/world/README.md, docs/audio/README.md, API_NOTES, REQUESTS).

## DECISIONS / ASSUMPTIONS
- Manifest `category` uses the ARCHITECTURE set impact|element|ui|ambience|loop; extra fields: family (Godot category), bus (sfx|ui|ambience), stinger flag.
- Actor surface strings from the sim: stone | water | puddle | metal | zone:<tag> (zone:ice, zone:sand..., zone:mud...). Footstep sets: stone, sand, water, puddle, metal, ice, mud.
- ~219 sounds planned: base 116 + martial swings/kicks/steps/cloth/breath + material impacts + per-element charge T1-T3 + UI/round/KO + ambience beds & chimes.

## RUN 3 LOG
- [r3] read ARCHITECTURE/MAC_SETUP/Godot env sources; check_audio.sh warning pass of run 2: exit=0 (clean). Next: re-run logic tests, write docs/audio.
