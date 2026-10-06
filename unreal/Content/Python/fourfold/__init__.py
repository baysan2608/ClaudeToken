"""Fourfold editor-Python package (architect-owned, frozen).

Sub-packages, one per build stream (each owns its folder):
    fourfold.fx         VFX textures / flipbooks / static meshes import, VFX materials        (stream fx)
    fourfold.character  fighter skeletal mesh, textures, character materials                (stream character)
    fourfold.animation  animation sequences onto SKEL_Fighter                                (stream animation)
    fourfold.world      environment textures / materials, level L_Lab, lighting, post        (stream world_audio)
    fourfold.audio      sound waves, ambience                                                (stream world_audio)
Every sub-package exposes build_all(force: bool = False) -> dict (a report: created / skipped / failed lists).
The orchestrator Content/Python/fourfold_setup.py (stream world_audio) calls them in a fixed order.
"""
