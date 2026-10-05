class_name VfxPalette
extends RefCounted
## One colour per material family (docs/MOVESET.md §11): cast rings, charge tiers, outcome cues and
## aura shells all use these, so a sub-element reads the same everywhere. Linear-ish display colours.

const MAT := {
	"stone": Color(0.78, 0.66, 0.50), "metal": Color(0.70, 0.80, 0.95), "sand": Color(0.90, 0.74, 0.46),
	"glass": Color(0.72, 0.96, 0.86), "magma": Color(1.00, 0.46, 0.12), "water": Color(0.34, 0.78, 1.00),
	"ice": Color(0.72, 0.93, 1.00), "mist": Color(0.86, 0.90, 0.94), "steam": Color(0.97, 0.97, 0.98),
	"plant": Color(0.42, 0.82, 0.30), "flame": Color(1.00, 0.55, 0.16), "blue": Color(0.45, 0.72, 1.00),
	"lightning": Color(0.74, 0.78, 1.00), "blast": Color(1.00, 0.70, 0.32), "wind": Color(0.90, 0.95, 1.00),
	"vortex": Color(0.80, 0.92, 0.98), "vacuum": Color(0.62, 0.48, 0.92), "sound": Color(1.00, 0.90, 0.66),
}
## Sim.SUB_NAMES order: the material family that stands for each (element, sub).
const SUB_MAT := [["stone", "metal", "sand", "magma"], ["water", "ice", "mist", "plant"],
	["flame", "blue", "lightning", "blast"], ["wind", "vortex", "vacuum", "sound"]]
## Dust / debris tint per family (what a hit throws up).
const DUST := {
	"stone": Color(0.50, 0.45, 0.38), "metal": Color(0.55, 0.55, 0.58), "sand": Color(0.78, 0.64, 0.42),
	"glass": Color(0.80, 0.92, 0.88), "magma": Color(0.24, 0.20, 0.18), "water": Color(0.80, 0.90, 0.95),
	"ice": Color(0.88, 0.95, 1.00), "mist": Color(0.82, 0.86, 0.90), "steam": Color(0.92, 0.93, 0.94),
	"plant": Color(0.36, 0.45, 0.22), "flame": Color(0.30, 0.28, 0.27), "blue": Color(0.32, 0.32, 0.36),
	"lightning": Color(0.40, 0.40, 0.45), "blast": Color(0.26, 0.25, 0.24), "wind": Color(0.62, 0.58, 0.52),
	"vortex": Color(0.62, 0.58, 0.52), "vacuum": Color(0.50, 0.47, 0.52), "sound": Color(0.58, 0.54, 0.48),
}


static func color(mat: String) -> Color:
	return MAT.get(mat, Color(0.9, 0.9, 0.9))


static func dust(mat: String) -> Color:
	return DUST.get(mat, Color(0.5, 0.45, 0.38))


static func mat_of(element: int, sub: int) -> String:
	if element < 0 or element > 3:
		return "wind"
	return SUB_MAT[element][clampi(sub, 0, 3)]


static func v3(c: Color) -> Vector3:
	return Vector3(c.r, c.g, c.b)
