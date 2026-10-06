"""Design constants shared by the Blender build, the previews and character.json (palettes are linear colours)."""

# Fighter palettes (linear RGBA). Main = tunic (+ darker trousers, shoes' uppers), Accent = collar / cuffs / hem
# border / sash / hair tie, Trim = forearm and shin wraps.
PALETTES = {
    "player": {"FF_Main": [0.090, 0.120, 0.230, 1.0], "FF_Accent": [0.560, 0.350, 0.110, 1.0],
               "FF_Trim": [0.800, 0.740, 0.620, 1.0]},
    "rival": {"FF_Main": [0.330, 0.085, 0.060, 1.0], "FF_Accent": [0.760, 0.560, 0.200, 1.0],
              "FF_Trim": [0.190, 0.170, 0.160, 1.0]},
    "dummy": {"FF_Main": [0.520, 0.470, 0.380, 1.0], "FF_Accent": [0.360, 0.330, 0.280, 1.0],
              "FF_Trim": [0.680, 0.640, 0.560, 1.0]},
}

# Which palette entry each material slot is tinted by (TintSelect vector in the Unreal material instances).
SLOT_TINT = {"cloth_main": "FF_Main", "shoes": "FF_Main", "cloth_accent": "FF_Accent", "sash": "FF_Accent",
             "wraps": "FF_Trim"}
TINT_GAIN = 2.0          # BaseColor (neutral, ~0.42 linear) x tint x gain
