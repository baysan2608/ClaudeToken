class_name Materials
extends RefCounted
## Per-material properties for every Sim.Mat (docs/COMBAT_SPEC.md "Engine" §E5).
## Thermal constants are game rules (HU, °C); hardness feeds the counter rule
## (barrier counter power = mass × hardness, docs/MOVESET.md §5.2).
##
## Keys: c (HU/(kg·°C)), melt (°C, start of the liquid fraction), latent (HU/kg to fully melt),
## max_temp (°C), hardness (CP per kg), conductive, brittle, porous, flammable, magnetic, insulator.
## Water's liquid/frozen split and the stone constants stay in Sim (legacy, unchanged).

const PROPS := {
	Sim.Mat.STONE: {"c": 0.01, "melt": 1000.0, "latent": 10.0, "max_temp": 1350.0, "hardness": 0.25,
		"conductive": false, "brittle": false, "porous": false, "flammable": false, "magnetic": false, "insulator": false},
	Sim.Mat.WATER: {"c": 0.05, "melt": 0.0, "latent": 3.3, "max_temp": 100.0, "hardness": 1.0, "hardness_frozen": 0.44,
		"conductive": true, "brittle": false, "porous": false, "flammable": false, "magnetic": false, "insulator": false},
	Sim.Mat.STEAM: {"c": 0.0, "melt": 0.0, "latent": 0.0, "max_temp": 100.0, "hardness": 0.0,
		"conductive": false, "brittle": false, "porous": false, "flammable": false, "magnetic": false, "insulator": false},
	# Game metal: melts to molten metal at 1200 °C; red-hot (drops from a grip) at 300 °C.
	Sim.Mat.METAL: {"c": 0.02, "melt": 1200.0, "latent": 8.0, "max_temp": 1600.0, "hardness": 3.3,
		"conductive": true, "brittle": false, "porous": false, "flammable": false, "magnetic": true, "insulator": false},
	# Sand fuses at 1200 °C and sets as GLASS when it cools (same thermal constants: the conversion is energy-neutral).
	Sim.Mat.SAND: {"c": 0.01, "melt": 1200.0, "latent": 10.0, "max_temp": 1600.0, "hardness": 0.25,
		"conductive": false, "brittle": false, "porous": true, "flammable": false, "magnetic": false, "insulator": true},
	Sim.Mat.GLASS: {"c": 0.01, "melt": 1200.0, "latent": 10.0, "max_temp": 1600.0, "hardness": 0.30,
		"conductive": false, "brittle": true, "porous": false, "flammable": false, "magnetic": false, "insulator": true},
	# Plant ignites at 250 °C and burns away (ledger "burned") at burn_rate kg/s.
	Sim.Mat.PLANT: {"c": 0.04, "melt": 1.0e9, "latent": 0.0, "max_temp": 600.0, "hardness": 0.4, "ignite": 250.0, "burn_rate": 1.5,
		"conductive": false, "brittle": false, "porous": true, "flammable": true, "magnetic": false, "insulator": false},
	# Fire bodies carry heat_payload (HU) only; it decays to ambient at fire_decay of itself per second.
	Sim.Mat.FIRE: {"c": 0.0, "melt": 0.0, "latent": 0.0, "max_temp": 0.0, "hardness": 0.0, "fire_decay": 0.35,
		"conductive": false, "brittle": false, "porous": false, "flammable": false, "magnetic": false, "insulator": false},
	Sim.Mat.AIR: {"c": 0.0, "melt": 0.0, "latent": 0.0, "max_temp": 0.0, "hardness": 0.0,
		"conductive": false, "brittle": false, "porous": false, "flammable": false, "magnetic": false, "insulator": false},
}

## Body tags that change a barrier's hardness (CP per kg) or brittleness.
const TAG_HARDNESS := {"obsidian": 0.33, "glass": 0.30, "ice": 0.44, "vine": 0.4, "mud": 0.25, "plate": 3.3, "sand": 0.25}
const BRITTLE_TAGS := ["obsidian", "glass", "ice", "crust"]
## Zone/body tags that conduct (lightning nodes) and that insulate (block E <= 1.5·CP).
const CONDUCTIVE_TAGS := ["caltrops", "rod", "plate"]
const INSULATING_TAGS := ["vacuum", "vacuum_well", "null_bubble", "ice_floor", "glass", "ice", "sand"]
## Fog zones conduct at this fraction (docs/MOVESET.md §7.7).
const FOG_CONDUCTION := 0.6


static func prop(mat: int, key: String, default: Variant = null) -> Variant:
	var p: Dictionary = PROPS.get(mat, {})
	return p.get(key, default)


## Melting solids share the stone thermal model (sensible -> latent -> superheat).
static func is_fusible(mat: int) -> bool:
	return mat == Sim.Mat.STONE or mat == Sim.Mat.METAL or mat == Sim.Mat.SAND or mat == Sim.Mat.GLASS


static func c(mat: int) -> float:
	return float(prop(mat, "c", 0.0))


static func melt(mat: int) -> float:
	return float(prop(mat, "melt", 0.0))


static func latent(mat: int) -> float:
	return float(prop(mat, "latent", 0.0))


static func max_temp(mat: int) -> float:
	return float(prop(mat, "max_temp", 0.0))


## Counter power per kg of a barrier made of this body (MOVESET §5.2). Body override first
## (MatBody.hardness >= 0), then the tag, then the material (ice for frozen water).
static func hardness(b: MatBody) -> float:
	if b.hardness >= 0.0:
		return b.hardness
	if TAG_HARDNESS.has(String(b.tag)):
		return float(TAG_HARDNESS[String(b.tag)])
	if b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.FROZEN:
		return float(PROPS[Sim.Mat.WATER].hardness_frozen)
	return float(prop(b.mat, "hardness", 0.0))


static func is_brittle(b: MatBody) -> bool:
	if BRITTLE_TAGS.has(String(b.tag)):
		return true
	if b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.FROZEN:
		return true
	return bool(prop(b.mat, "brittle", false))


## Lightning conductor node (docs/MOVESET.md §7.11): liquid water bodies (streams, blobs, waves,
## held shields, puddles, the pool), metal of any phase, conductive tags, fog zones and charged bodies.
static func conducts(b: MatBody) -> bool:
	if b.charge > 0.0:
		return true
	if CONDUCTIVE_TAGS.has(String(b.tag)):
		return true
	if b.mat == Sim.Mat.METAL:
		return true
	if b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.LIQUID:
		return true
	return b.tag == &"fog"


## Conduction factor of a node body (fog conducts weakly).
static func conduction_factor(b: MatBody) -> float:
	return FOG_CONDUCTION if b.tag == &"fog" else 1.0


static func insulates(b: MatBody) -> bool:
	if INSULATING_TAGS.has(String(b.tag)):
		return true
	if b.mat == Sim.Mat.WATER and b.phase == Sim.Phase.FROZEN:
		return true
	return bool(prop(b.mat, "insulator", false))


static func is_flammable(b: MatBody) -> bool:
	return bool(prop(b.mat, "flammable", false))


static func is_magnetic(b: MatBody) -> bool:
	return bool(prop(b.mat, "magnetic", false))
