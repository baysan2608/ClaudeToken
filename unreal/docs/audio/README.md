# Audio - what exists and how it works

Stream `world_audio`, mission B. 233 original sounds (SFX 222 incl. 29 loops, ambience 9, 48 kHz mono 16-bit, peaks -3 dBFS,
phone-safe: nothing below ~70 Hz), a data-driven event table, and the `FourfoldAudio` runtime module.

## Pieces
| Path | What |
|---|---|
| `Tools/audio/synth_sfx.py` (+ `synth_martial.py`, `synth_impacts.py`, `synth_music.py`, `synth_ambience.py`) | deterministic numpy synthesis (port + upgrade of the Godot generator): modal impacts for stone / metal / ice / glass / wood, granular sand / steam / fire / water, filtered-noise wind / vortex / vacuum, lightning arcs and thunder, per-element charge risers T1-T3 (D-pentatonic), martial swings / kicks / stomps per element, footsteps per surface, cloth, breath, UI family (wood / bell), KO / round / combo stingers, 3 ambience beds + chimes + bamboo knock |
| `Tools/audio/render_audio.py` | writes `SourceArt/Audio/{SFX,Ambience}/*.wav` + `sound_index.json` (`--only name...` renders a subset) |
| `Tools/audio/validate_audio.py` | format / peak / DC / clipping / silence / loop seam / manifest agreement, K-weighted loudness per category, spectrogram sheets (`--sheet OUT prefix...`), determinism (`--determinism`) |
| `Tools/audio/event_rules.py`, `build_manifest.py` | rule data (ported from the Godot AudioDirector / FxDirector / FxCues) -> `Content/Fourfold/Data/sfx_manifest.json` (233 sounds, 168 event rules, 35 loop rules, steps, ambience, UI map, mix) + validation (every referenced sound exists, every Godot-manifest sound ships, event-type coverage report) |
| `Source/FourfoldAudio/Private/Logic/` | engine-free logic island: manifest model, rule engine, limiters (stinger <= 1 / 0.1 s ...), voice caps + stealing, loop fades per body / zone, footsteps, ambience accents, ducking, mix maths |
| `Source/FourfoldAudio/Private/FourfoldAudioSubsystem.cpp` | the Unreal glue (executes the logic's decisions) |
| `Content/Python/fourfold/audio/__init__.py` | `build_all(force)`: WAV -> `/Game/Fourfold/Audio/{SFX,Ambience}/S_<name>`, loop flags, optional sound classes |

Spectrogram sheets of the main families (looked at, per sound: time x 0-16 kHz): `docs/audio/spectrograms/sheet_<family>.jpg`
(swing, kick, stomp, step, impact, charge, ui, ko, round, amb, lightning, thunder, water, fire, stone); regenerate with
`validate_audio.py --sheet OUTDIR prefix...`.  Validation: 233 files, `RESULT: PASS` (peak -3 dBFS, no DC / clipping, loop seams < 0.005, fades).

## Run / test here
```bash
PY=/home/user/tools/bpyenv/bin/python            # numpy + scipy + matplotlib
$PY unreal/Tools/audio/render_audio.py            # ~1-2 min, deterministic (sha-256 identical on re-render)
$PY unreal/Tools/audio/build_manifest.py          # manifest + validation
$PY unreal/Tools/audio/validate_audio.py          # exit 1 on any failure; --loudness / --sheet / --determinism
cmake -S unreal/Source/FourfoldAudio/Private/Logic/tests -B build/ffa -G Ninja && cmake --build build/ffa && build/ffa/ffa_logic_tests
# -> 24 tests, 1603 checks (includes tests against the real manifest and the real WAV list)
unreal/Tools/audio/ue_syntax_check/check_audio.sh # needs the UE header mirror (see the script header)
```

## Owner steps (Mac)
Nothing manual: `fourfold_setup.py` imports the WAVs (part `audio`). Volumes come from the game's Settings page (master x SFX /
UI / ambience). Debug: console `ff.audio.dump`; log category `LogFourfoldAudio`.

## Rules the runtime follows (so the mix stays clean)
* One-shots: rule match -> named limiter -> stinger gap -> voice cap per sound (`max_voices`) and global (28) with 30 ms steal
  fades -> pitch variation -> 2D or positioned with a runtime `USoundAttenuation` class (near / mid / far / feel).
* Loops: one `UAudioComponent` per body / zone / actor state key, fade in / out in dB per second (body fast, zone slow), positions
  follow the interpolated snapshot; at most 14 loop voices (running loops keep their voice; new ones beyond the budget wait, ordered by key so it is deterministic).
* Ambience beds are 2D, -4 dB bus offset, ducked 4 dB for 0.5 s on big events (`mix.duck`), UI / ambience keep playing while paused.
* Rule language: docstring of `Tools/audio/event_rules.py`; field names a rule can read: `ff::Event.data` keys + derived `$player`,
  `$el`, `$weight`, `$body_mat`, `$threat_mat`, `$counter_mat`, `$surface`, `$involves_player`; loops read body / actor fields (listed in the same file).

## Integration notes
* Game: `OnUiCue` names `ui_tap ui_select ui_back ui_open ui_close ui_toggle ui_ring_open ui_ring_pick ui_error ui_pause ui_resume ui_toast`
  are all mapped (`manifest.ui`). `UFourfoldAudioSubsystem::PlaySoundByName / PlayUiCue` are available for direct calls.
* The module only needs `Core CoreUObject Engine FourfoldCore Fourfold`; no plugin, no asset except the imported waves.

## Project configuration (Config/, reviewed in docs/world/README.md)
iOS audio mixer: `AudioSampleRate=48000` (same as the sound set, no resampling), `AudioCallbackBufferFrameSize=512` (~10.7 ms), `AudioNumBuffersToEnqueue=2`,
`AudioMaxChannels=32`; the global audio quality level also caps at 32 channels (Project Settings > Audio > Quality Levels).  The module's own caps
(28 one-shots + 14 loop voices in the manifest `mix`) sit around that: if `ff.audio.dump` shows many `dropped` plays, raise the quality-level
MaxChannels or lower `mix.global_voices`.  Background audio is off; a pause menu keeps UI / ambience playing (`SetUISound`).

## Known gaps
* No MetaSounds / submix effects (reverb): the courtyard "air" is baked into the beds. A reverb submix can be added later.
* Mixing was tuned by measurement (K-weighted loudness per category, phone-weighted), not by ear on a device: expect to trim
  `gain_db` of a few families after the first listen; edit `Tools/audio/synth_*.py` Spec gains or the manifest `gain_db`.
* Compile / run of the C++ is unverified until the Mac build (API_NOTES.md lists everything it relies on).
