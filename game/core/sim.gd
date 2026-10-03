class_name Sim
extends RefCounted
## Shared enums and tuning constants for the authoritative simulation.
## Units (gameplay units, documented in docs/COMBAT_SPEC.md):
##   distance m, time s, mass kg (gameplay scale), temperature °C (game),
##   heat HU (heat units), resources Focus (0..100), Balance (0..100).

const HZ := 60
const DT := 1.0 / 60.0
const GRAVITY := 18.0

enum Element { EARTH, WATER, FIRE, AIR }
const ELEMENT_NAMES := ["Earth", "Water", "Fire", "Air"]

## Composition class. Lava = STONE in MOLTEN phase; ice = WATER in FROZEN phase.
enum Mat { STONE, WATER, STEAM }
enum Phase { SOLID, SOFTENED, MOLTEN, LIQUID, FROZEN, GAS }
const PHASE_NAMES := ["solid", "softened", "molten", "liquid", "frozen", "gas"]
## Representation of the same logical body. Changing form never changes identity.
enum Form { CHUNK, BLOB, WAVE, WALL, STREAM, SHARD, PUDDLE, POOL, CLOUD }
const FORM_NAMES := ["chunk", "blob", "wave", "wall", "stream", "shard", "puddle", "pool", "cloud"]

# --- Thermal (see Thermal) -------------------------------------------------
const AMBIENT_C := 20.0
const STONE_C := 0.01           # HU per kg per °C
const STONE_MELT_C := 1000.0    # melting point (game)
const STONE_LATENT := 10.0      # HU per kg to fully melt at the melting point
const WATER_C := 0.05
const WATER_FREEZE_C := 0.0
const WATER_BOIL_C := 100.0
const WATER_LATENT_FUSION := 3.3
const WATER_LATENT_VAPOR := 22.0
## Phase-label hysteresis on liquid fraction (stone). Labels never flicker:
## SOLID -> SOFTENED at >= 0.15, SOFTENED -> MOLTEN at >= 0.80,
## MOLTEN -> SOFTENED at <= 0.55, SOFTENED -> SOLID at <= 0.05.
const SOFTEN_UP := 0.15
const MOLTEN_UP := 0.80
const MOLTEN_DOWN := 0.55
const SOLID_DOWN := 0.05
## Water: LIQUID -> FROZEN at liquid <= 0.10, FROZEN -> LIQUID at liquid >= 0.60.
const ICE_DOWN := 0.10
const ICE_UP := 0.60
## Passive heat loss to ambient for exposed bodies: HU/s per kg^(2/3) per 100 °C above ambient.
const AMBIENT_LOSS := 0.6
## Molten material held by a magma-capable controller does not lose heat (upkeep is paid in Focus).
const HOLD_UPKEEP_FOCUS := 3.0          # Focus per second while holding molten mass
const QUENCH_RATE := 1400.0             # HU/s lava loses while touching water
const HOT_ROCK_C := 300.0               # above this a solid rock burns on contact

# --- Actor resources ---------------------------------------------------------
const FOCUS_MAX := 100.0
const FOCUS_REGEN := 14.0               # per s
const FOCUS_REGEN_DELAY := 0.7          # s after last spend
const HU_PER_FOCUS := 10.0              # generating heat costs Focus
const DRAW_HU_PER_FOCUS := 25.0         # extracting heat costs Focus (control effort)
const RESERVE_MAX := 500.0              # HU heat reserve cap
const RESERVE_DISSIPATE := 12.0         # HU/s passive dissipation
const BALANCE_MAX := 100.0
const BALANCE_REGEN := 22.0
const BALANCE_REGEN_DELAY := 1.0
const HEALTH_MAX := 100.0

# --- Bodies ------------------------------------------------------------------
const MAX_BODIES := 32
const MAX_PUDDLES := 8
const MAX_REMNANTS := 6                 # inert rock remnants (oldest decays first)
const REMNANT_LIFETIME := 45.0
const STONE_SHOT_MASS := 20.0
const STONE_HEAVY_MASS := 45.0
const WALL_MASS := 120.0

# --- Actor body --------------------------------------------------------------
const ACTOR_RADIUS := 0.35
const ACTOR_HEIGHT := 1.75
const ACTOR_MASS := 70.0
const STEP_HEIGHT := 0.4
const WAVE_STEP := 0.3                  # lava wave climbs at most this


static func stone_radius(mass: float) -> float:
	# Gameplay scale: 20 kg reads as a ~0.45 m stone, 200 kg as a ~1 m boulder.
	return 0.18 * pow(maxf(mass, 0.5) / 10.0, 1.0 / 3.0)


static func water_radius(mass: float) -> float:
	return 0.12 * pow(maxf(mass, 0.2) / 2.0, 1.0 / 3.0)
