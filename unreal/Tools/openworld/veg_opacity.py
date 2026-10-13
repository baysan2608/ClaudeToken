#!/usr/bin/env python3
"""Writes T_OW_Veg_<Set>_OP.png (opacity mask in R) from the alpha of each foliage BC made by prep_vegetation.py: the
tree-leaf master (Content/Python/fourfold/world/foliage.py) reads opacity from its own grayscale texture.
  python3 unreal/Tools/openworld/veg_opacity.py"""
import glob
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MESHES = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "OpenWorld", "Meshes"))
for p in sorted(glob.glob(os.path.join(MESHES, "T_OW_Veg_*_BC.png"))):
    im = Image.open(p)
    if im.mode == "RGBA":
        im.getchannel("A").save(p.replace("_BC.png", "_OP.png"))
        print(os.path.basename(p).replace("_BC", "_OP"))
