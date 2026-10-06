#!/usr/bin/env python3
"""Render the complete Fourfold sound set (base 116 + martial / impacts / music / ambience extensions).

    /home/user/tools/bpyenv/bin/python unreal/Tools/audio/render_audio.py                 # everything
    ... render_audio.py --only swing_earth_heavy step_sand_1                              # a subset (index is merged)
    ... render_audio.py --out /tmp/audio --list

Outputs unreal/SourceArt/Audio/SFX/*.wav, unreal/SourceArt/Audio/Ambience/*.wav and SourceArt/Audio/sound_index.json.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import synth_sfx  # noqa: E402  (registers the base sounds)

for _mod in ("synth_martial", "synth_impacts", "synth_music", "synth_ambience"):
    try:
        __import__(_mod)
    except ModuleNotFoundError as exc:      # an extension that is not written yet is skipped, anything else is an error
        if exc.name != _mod:
            raise

if __name__ == "__main__":
    sys.exit(synth_sfx.main())
