extends TestCase
## Thermal transitions on MatBody via Thermal.apply_heat / flash_boil / update_phase.
## Unit conventions (Sim): stone 0.01 HU/kg/degC, melts at 1000 degC with 10 HU/kg latent;
## water 0.05 HU/kg/degC, fusion 3.3 HU/kg, vaporisation 22 HU/kg.

const EPS := 1e-6


func _stone(mass: float = 10.0, temp: float = Sim.AMBIENT_C, liquid: float = 0.0) -> MatBody:
	var b := MatBody.new()
	b.mat = Sim.Mat.STONE
	b.mass = mass
	b.temp = temp
	b.liquid = liquid
	return b


func _water(mass: float = 4.0, temp: float = Sim.AMBIENT_C, liquid: float = 1.0) -> MatBody:
	var b := MatBody.new()
	b.mat = Sim.Mat.WATER
	b.phase = Sim.Phase.LIQUID if liquid >= 1.0 else Sim.Phase.FROZEN
	b.mass = mass
	b.temp = temp
	b.liquid = liquid
	return b


## Sensible HU to bring a stone from ambient to its melting point.
func _stone_to_melt(m: float) -> float:
	return (Sim.STONE_MELT_C - Sim.AMBIENT_C) * m * Sim.STONE_C


# ---------------------------------------------------------------- stone heating

func test_stone_heats_to_melting_point_before_latent() -> void:
	var b := _stone(10.0)
	var need := _stone_to_melt(10.0)
	var r := Thermal.apply_heat(b, need * 0.5)
	near(r.x, need * 0.5, EPS, "all energy absorbed while below the melting point")
	near(b.temp, Sim.AMBIENT_C + (Sim.STONE_MELT_C - Sim.AMBIENT_C) * 0.5, 1e-4, "temperature rose proportionally")
	check(b.liquid == 0.0, "no latent heat absorbed below the melting point")
	Thermal.apply_heat(b, need * 0.5 - 1.0)
	check(b.temp < Sim.STONE_MELT_C and b.liquid == 0.0, "1 HU short of melting: still solid at %.3f degC" % b.temp)
	Thermal.apply_heat(b, 1.0)
	near(b.temp, Sim.STONE_MELT_C, 1e-3, "exactly the sensible budget reaches the melting point")
	check(b.liquid < 1e-6, "no melting yet (liquid %.6f)" % b.liquid)


func test_stone_latent_heat_holds_temperature() -> void:
	var b := _stone(10.0)
	Thermal.apply_heat(b, _stone_to_melt(10.0))
	var latent_total := 10.0 * Sim.STONE_LATENT
	var r := Thermal.apply_heat(b, latent_total * 0.25)
	near(r.x, latent_total * 0.25, EPS, "energy absorbed as latent heat")
	near(b.liquid, 0.25, 1e-6, "liquid fraction follows latent energy")
	near(b.temp, Sim.STONE_MELT_C, 1e-6, "temperature pinned at the melting point during melting")
	Thermal.apply_heat(b, latent_total * 0.75)
	near(b.liquid, 1.0, 1e-6, "fully liquid")
	near(b.temp, Sim.STONE_MELT_C, 1e-6, "still at the melting point")
	# Superheating only after fully liquid, up to STONE_MAX_C.
	Thermal.apply_heat(b, 10.0)
	near(b.temp, Sim.STONE_MELT_C + 10.0 / (10.0 * Sim.STONE_C), 1e-4, "superheat after the latent budget")


func test_stone_heat_cap_returns_only_what_was_absorbed() -> void:
	var b := _stone(10.0)
	var cap := _stone_to_melt(10.0) + 10.0 * Sim.STONE_LATENT + (Thermal.STONE_MAX_C - Sim.STONE_MELT_C) * 10.0 * Sim.STONE_C
	var r := Thermal.apply_heat(b, cap + 500.0)
	near(r.x, cap, 1e-3, "excess beyond STONE_MAX_C is refused, not absorbed")
	near(b.temp, Thermal.STONE_MAX_C, 1e-6, "temperature capped")
	near(b.liquid, 1.0, 1e-9, "liquid fraction capped at 1")
	var again := Thermal.apply_heat(b, 100.0)
	near(again.x, 0.0, EPS, "a body at the cap absorbs nothing more")


func test_apply_heat_zero_cases() -> void:
	var s := _stone(10.0)
	check(Thermal.apply_heat(s, 0.0) == Vector2.ZERO, "zero energy is a no-op")
	check(s.temp == Sim.AMBIENT_C and s.liquid == 0.0, "no state change for zero energy")
	var massless := _stone(0.0)
	check(Thermal.apply_heat(massless, 100.0) == Vector2.ZERO, "massless body absorbs nothing")
	var steam := MatBody.new()
	steam.mat = Sim.Mat.STEAM
	steam.mass = 1.0
	check(Thermal.apply_heat(steam, 100.0) == Vector2.ZERO, "steam is not modelled by apply_heat")
	check(Thermal.apply_heat(steam, -100.0) == Vector2.ZERO, "steam is not cooled by apply_heat")


# ---------------------------------------------------------------- stone cooling

func test_cooling_removes_latent_before_sensible() -> void:
	var b := _stone(10.0, Sim.STONE_MELT_C, 1.0)
	var latent_total := 10.0 * Sim.STONE_LATENT
	var r := Thermal.apply_heat(b, -latent_total * 0.4)
	near(r.x, -latent_total * 0.4, EPS, "returns the (negative) energy removed")
	near(b.liquid, 0.6, 1e-6, "latent heat removed first (liquid fraction fell)")
	near(b.temp, Sim.STONE_MELT_C, 1e-6, "temperature unchanged while any liquid remains")
	Thermal.apply_heat(b, -latent_total * 0.6)
	check(b.liquid == 0.0, "fully solidified")
	near(b.temp, Sim.STONE_MELT_C, 1e-6, "still at the melting point right after solidifying")
	var r2 := Thermal.apply_heat(b, -50.0)
	near(r2.x, -50.0, EPS, "then sensible heat comes out")
	near(b.temp, Sim.STONE_MELT_C - 50.0 / (10.0 * Sim.STONE_C), 1e-4, "temperature finally falls")


func test_cooling_removes_superheat_first() -> void:
	var b := _stone(10.0, 1200.0, 1.0)
	var superheat := 200.0 * 10.0 * Sim.STONE_C
	Thermal.apply_heat(b, -superheat * 0.5)
	near(b.temp, 1100.0, 1e-4, "superheat comes off first")
	near(b.liquid, 1.0, 1e-9, "still fully liquid while superheated")
	Thermal.apply_heat(b, -superheat * 0.5)
	near(b.temp, Sim.STONE_MELT_C, 1e-4, "back at the melting point")
	near(b.liquid, 1.0, 1e-9, "latent heat untouched until the superheat is gone")


func test_cannot_extract_below_ambient() -> void:
	var b := _stone(10.0, 100.0)
	var avail := (100.0 - Sim.AMBIENT_C) * 10.0 * Sim.STONE_C
	var r := Thermal.apply_heat(b, -avail - 500.0)
	near(r.x, -avail, 1e-6, "only the heat above ambient can be extracted")
	near(b.temp, Sim.AMBIENT_C, 1e-6, "stone stops at ambient")
	var r2 := Thermal.apply_heat(b, -100.0)
	near(r2.x, 0.0, EPS, "nothing to extract from a body at ambient")
	near(b.temp, Sim.AMBIENT_C, 1e-9, "no refrigeration below ambient")
	check(b.thermal_energy() >= -1e-9, "stone never holds negative thermal energy")
	# A hot stone drained with one enormous request stops exactly at ambient too.
	var hot := _stone(10.0, 1100.0, 1.0)
	Thermal.apply_heat(hot, -1e9)
	near(hot.temp, Sim.AMBIENT_C, 1e-6, "even a molten stone cannot be pulled below ambient")
	check(hot.liquid == 0.0, "and ends solid")


# ---------------------------------------------------------------- conservation

func test_energy_returned_equals_energy_applied_stone() -> void:
	var b := _stone(7.5)
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	var total := 0.0
	var e0 := b.thermal_energy()
	for k in 500:
		var e := rng.randf_range(-120.0, 150.0)
		var r := Thermal.apply_heat(b, e)
		total += r.x
		check(sign(r.x) == sign(e) or r.x == 0.0, "step %d: result has the sign of the request (%.3f for %.3f)" % [k, r.x, e])
		check(absf(r.x) <= absf(e) + 1e-9, "step %d: never absorbs more than requested" % k)
		check(b.liquid >= 0.0 and b.liquid <= 1.0, "step %d: liquid in 0..1 (%.4f)" % [k, b.liquid])
		check(b.temp >= Sim.AMBIENT_C - 1e-6 and b.temp <= Thermal.STONE_MAX_C + 1e-6, "step %d: temp in bounds (%.2f)" % [k, b.temp])
		# apply_heat returns a Vector2, whose components are float32: every returned amount carries up
		# to ~1e-5 HU of rounding (and the 1e-6 liquid snap may drop ~1e-4 HU). Allow 1e-3 HU of
		# drift over the whole walk; anything larger would be a real accounting error.
		if k % 50 == 49:
			near(b.thermal_energy() - e0, total, 1e-3, "step %d: thermal_energy() delta equals the sum of returned HU" % k)
	near(b.thermal_energy() - e0, total, 1e-3, "stone: delta energy == sum of returns after random walk")


func test_energy_returned_equals_energy_applied_water() -> void:
	# Without vaporisation: delta thermal energy equals the sum of returned HU.
	var b := _water(6.0, 10.0)
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var total := 0.0
	var e0 := b.thermal_energy()
	for k in 600:
		var e := rng.randf_range(-4.0, 4.0)
		# Keep the walk between the ice floor and the boiling point (it may cross 0 degC freely).
		if b.temp > 80.0:
			e = -absf(e)
		elif b.temp < -15.0:
			e = absf(e)
		var r := Thermal.apply_heat(b, e)
		total += r.x
		check(r.y == 0.0, "step %d: no vapour below the boiling point" % k)
		check(b.liquid >= 0.0 and b.liquid <= 1.0, "step %d: liquid in 0..1" % k)
		check(b.temp <= Sim.WATER_BOIL_C + 1e-6, "step %d: below boiling (%.2f)" % [k, b.temp])
	near(b.thermal_energy() - e0, total, 1e-3, "water: delta energy == sum of returned HU")


func test_merge_conserves_mass_and_thermal_energy() -> void:
	var w := CombatWorld.new(1)
	var hot := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 10.0, Vector3(0, 1, 0), "t")
	hot.temp = 600.0
	var cold := w.spawn_body(Sim.Mat.STONE, Sim.Form.CHUNK, 30.0, Vector3(1, 1, 0), "t")
	var e := hot.thermal_energy() + cold.thermal_energy()
	w.merge_bodies(hot, cold)
	near(hot.mass, 40.0, 1e-9, "merged mass is the sum")
	near(hot.thermal_energy(), e, 1e-6, "merged thermal energy is the sum")
	check(not cold.alive and cold.mass == 0.0, "absorbed body is gone")
	check(hot.absorbed.has(cold.id), "absorption recorded")
	# Molten + frozen water.
	var m1 := w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 3.0, Vector3(3, 0, 0), "t")
	m1.temp = 60.0
	var m2 := w.spawn_body(Sim.Mat.WATER, Sim.Form.PUDDLE, 1.0, Vector3(3.2, 0, 0), "t")
	m2.liquid = 0.0
	m2.temp = -5.0
	m2.phase = Sim.Phase.FROZEN
	var ew := m1.thermal_energy() + m2.thermal_energy()
	w.merge_bodies(m1, m2)
	near(m1.mass, 4.0, 1e-9, "water merge conserves mass")
	near(m1.thermal_energy(), ew, 1e-4, "water merge conserves thermal energy (got %.4f want %.4f)" % [m1.thermal_energy(), ew])


func test_ambient_step_return_matches_energy_change() -> void:
	var hot := _stone(10.0, 500.0)
	var e0 := hot.thermal_energy()
	var moved := Thermal.ambient_step(hot, Sim.DT)
	check(moved < 0.0, "a hot stone loses heat to the air (%.5f)" % moved)
	near(hot.thermal_energy() - e0, moved, 1e-9, "returned HU equals the energy change")
	var cold := _stone(10.0, Sim.AMBIENT_C)
	near(Thermal.ambient_step(cold, Sim.DT), 0.0, EPS, "a stone at ambient exchanges nothing")
	var molten := _stone(10.0, Sim.STONE_MELT_C, 1.0)
	var em := molten.thermal_energy()
	var m2 := Thermal.ambient_step(molten, Sim.DT)
	near(molten.thermal_energy() - em, m2, 1e-9, "molten stone: ambient loss equals the energy change")
	check(molten.liquid < 1.0 and molten.temp == Sim.STONE_MELT_C, "a molten stone cools by solidifying first")
	var ice := _water(2.0, -5.0, 0.0)
	var ei := ice.thermal_energy()
	var gain := Thermal.ambient_step(ice, Sim.DT)
	check(gain > 0.0, "ice warms from the air (%.5f)" % gain)
	near(ice.thermal_energy() - ei, gain, 1e-9, "ice: returned HU equals the energy change")


# ---------------------------------------------------------------- phase labels

func _label_changes_while_oscillating(b: MatBody, center_liquid: float, amp_hu: float, steps: int) -> int:
	## Alternately adds and removes amp_hu around a liquid fraction; counts label changes.
	var changes := 0
	for k in steps:
		Thermal.apply_heat(b, amp_hu if k % 2 == 0 else -amp_hu)
		if Thermal.update_phase(b):
			changes += 1
	return changes


func test_stone_phase_thresholds_and_hysteresis() -> void:
	var b := _stone(10.0, Sim.STONE_MELT_C, 0.0)
	b.phase = Sim.Phase.SOLID
	b.liquid = Sim.SOFTEN_UP - 0.0001
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.SOLID, "just below SOFTEN_UP stays SOLID")
	b.liquid = Sim.SOFTEN_UP
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.SOFTENED, "exactly SOFTEN_UP becomes SOFTENED")
	b.liquid = Sim.MOLTEN_UP - 0.0001
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.SOFTENED, "just below MOLTEN_UP stays SOFTENED")
	b.liquid = Sim.MOLTEN_UP
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.MOLTEN, "exactly MOLTEN_UP becomes MOLTEN")
	b.liquid = Sim.MOLTEN_UP - 0.1   # between the thresholds: must stay MOLTEN
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.MOLTEN, "0.70 stays MOLTEN (hysteresis)")
	b.liquid = Sim.MOLTEN_DOWN + 0.0001
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.MOLTEN, "just above MOLTEN_DOWN stays MOLTEN")
	b.liquid = Sim.MOLTEN_DOWN
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.SOFTENED, "exactly MOLTEN_DOWN drops to SOFTENED")
	b.liquid = 0.3   # between SOLID_DOWN and SOFTEN_UP going down: stays SOFTENED
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.SOFTENED, "0.30 stays SOFTENED on the way down")
	b.liquid = Sim.SOLID_DOWN + 0.0001
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.SOFTENED, "just above SOLID_DOWN stays SOFTENED")
	b.liquid = Sim.SOLID_DOWN
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.SOLID, "exactly SOLID_DOWN returns to SOLID")
	# A big jump skips straight to MOLTEN from SOLID.
	b.liquid = 0.95
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.MOLTEN, "SOLID -> MOLTEN directly when liquid jumps past both thresholds")
	b.liquid = 0.0
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.SOLID, "MOLTEN -> SOLID directly when liquid collapses")


func test_stone_phase_label_does_not_flicker_near_thresholds() -> void:
	# Latent heat for 10 kg is 100 HU: 2 HU = 0.02 liquid fraction.
	for center in [Sim.SOFTEN_UP, Sim.MOLTEN_UP, Sim.MOLTEN_DOWN, Sim.SOLID_DOWN]:
		var b := _stone(10.0, Sim.STONE_MELT_C, center - 0.01)
		# Start on the correct side of the threshold for the label.
		b.phase = Sim.Phase.SOLID if center == Sim.SOFTEN_UP else (Sim.Phase.SOFTENED if center == Sim.MOLTEN_UP or center == Sim.SOLID_DOWN else Sim.Phase.MOLTEN)
		var changes := _label_changes_while_oscillating(b, center, 2.0, 400)
		check(changes <= 1, "oscillating +-0.02 around %.2f flips the label at most once, got %d" % [center, changes])
	# Oscillation straddling the whole hysteresis band of the molten label never flips it.
	var m := _stone(10.0, Sim.STONE_MELT_C, 0.9)
	m.phase = Sim.Phase.MOLTEN
	var flips := 0
	for k in 600:
		Thermal.apply_heat(m, 20.0 if k % 2 == 0 else -20.0)   # liquid swings 0.9 <-> 0.7
		if Thermal.update_phase(m):
			flips += 1
	check(flips == 0, "liquid swinging between 0.7 and 0.9 never changes a MOLTEN label (%d flips)" % flips)
	check(m.phase == Sim.Phase.MOLTEN, "label is still MOLTEN")


func test_water_phase_hysteresis() -> void:
	var b := _water(4.0, 0.0, 1.0)
	var fusion_total := 4.0 * Sim.WATER_LATENT_FUSION
	Thermal.apply_heat(b, -fusion_total * 0.85)   # liquid 0.15
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.LIQUID, "liquid 0.15 still LIQUID")
	Thermal.apply_heat(b, -fusion_total * 0.06)   # liquid 0.09 -> frozen
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.FROZEN, "liquid <= ICE_DOWN freezes (liquid %.3f)" % b.liquid)
	Thermal.apply_heat(b, fusion_total * 0.46)    # 0.55
	check(not Thermal.update_phase(b) and b.phase == Sim.Phase.FROZEN, "liquid 0.55 stays FROZEN (hysteresis)")
	Thermal.apply_heat(b, fusion_total * 0.06)    # 0.61
	check(Thermal.update_phase(b) and b.phase == Sim.Phase.LIQUID, "liquid >= ICE_UP melts (liquid %.3f)" % b.liquid)
	# Oscillating across a threshold flips the label once, never back and forth.
	var up := _water(4.0, 0.0, 0.55)
	up.phase = Sim.Phase.FROZEN
	var flips_up := 0
	for k in 400:
		Thermal.apply_heat(up, fusion_total * (0.06 if k % 2 == 0 else -0.06))   # 0.55 <-> 0.61
		if Thermal.update_phase(up):
			flips_up += 1
	check(flips_up == 1, "0.55 <-> 0.61 around ICE_UP flips FROZEN->LIQUID exactly once (%d)" % flips_up)
	check(up.phase == Sim.Phase.LIQUID, "and stays LIQUID")
	var down := _water(4.0, 0.0, 0.14)
	down.phase = Sim.Phase.LIQUID
	var flips_down := 0
	for k in 400:
		Thermal.apply_heat(down, fusion_total * (-0.06 if k % 2 == 0 else 0.06))   # 0.14 <-> 0.08
		if Thermal.update_phase(down):
			flips_down += 1
	check(flips_down == 1, "0.14 <-> 0.08 around ICE_DOWN flips LIQUID->FROZEN exactly once (%d)" % flips_down)
	check(down.phase == Sim.Phase.FROZEN, "and stays FROZEN")
	# Exact thresholds.
	var c := _water(4.0, 0.0, 1.0)
	c.liquid = Sim.ICE_DOWN
	check(Thermal.update_phase(c) and c.phase == Sim.Phase.FROZEN, "exactly ICE_DOWN freezes")
	c.liquid = Sim.ICE_UP - 0.0001
	check(not Thermal.update_phase(c) and c.phase == Sim.Phase.FROZEN, "just below ICE_UP stays FROZEN")
	c.liquid = Sim.ICE_UP
	check(Thermal.update_phase(c) and c.phase == Sim.Phase.LIQUID, "exactly ICE_UP melts")
	c.liquid = Sim.ICE_DOWN + 0.0001
	check(not Thermal.update_phase(c) and c.phase == Sim.Phase.LIQUID, "just above ICE_DOWN stays LIQUID")


# ---------------------------------------------------------------- water

func test_water_freezes_and_melts_with_latent_fusion() -> void:
	var b := _water(4.0, 20.0)
	var sensible := 20.0 * 4.0 * Sim.WATER_C
	var fusion := 4.0 * Sim.WATER_LATENT_FUSION
	var r := Thermal.apply_heat(b, -sensible)
	near(r.x, -sensible, EPS, "cooling to 0 degC removes only sensible heat")
	near(b.temp, 0.0, 1e-6, "water at the freezing point")
	near(b.liquid, 1.0, 1e-9, "not frozen yet")
	Thermal.apply_heat(b, -fusion * 0.5)
	near(b.temp, 0.0, 1e-6, "temperature pinned at 0 degC while freezing")
	near(b.liquid, 0.5, 1e-6, "half frozen after half the fusion energy")
	Thermal.apply_heat(b, -fusion * 0.5)
	near(b.liquid, 0.0, 1e-6, "fully frozen")
	var r2 := Thermal.apply_heat(b, -2.0)
	near(r2.x, -2.0, EPS, "ice cools further")
	check(b.temp < 0.0, "ice below zero (%.2f)" % b.temp)
	# Heating back: sensible to 0, then latent, then liquid warms.
	var to_zero := (0.0 - b.temp) * 4.0 * Sim.WATER_C
	var r3 := Thermal.apply_heat(b, to_zero)
	near(r3.x, to_zero, 1e-9, "ice warms to 0 degC")
	near(b.temp, 0.0, 1e-6, "at 0 degC")
	check(b.liquid == 0.0, "still frozen at 0 degC")
	Thermal.apply_heat(b, fusion * 0.25)
	near(b.liquid, 0.25, 1e-6, "melting follows latent energy")
	near(b.temp, 0.0, 1e-6, "pinned at 0 degC while melting")
	Thermal.apply_heat(b, fusion * 0.75)
	near(b.liquid, 1.0, 1e-6, "fully melted")
	check(b.mass == 4.0, "no mass change from freezing/melting")


func test_water_cooling_floor_is_ice_min() -> void:
	var b := _water(2.0, 0.0, 0.0)
	var avail := (0.0 - Thermal.ICE_MIN_C) * 2.0 * Sim.WATER_C
	var r := Thermal.apply_heat(b, -avail - 100.0)
	near(r.x, -avail, 1e-6, "ice cannot be cooled below ICE_MIN_C")
	near(b.temp, Thermal.ICE_MIN_C, 1e-6, "temperature floor")


func test_water_vaporisation_reduces_mass_and_reports_kg() -> void:
	var b := _water(2.0, 20.0)
	var to_boil := (Sim.WATER_BOIL_C - 20.0) * 2.0 * Sim.WATER_C   # 8 HU
	var e0 := b.thermal_energy()
	var r := Thermal.apply_heat(b, to_boil)
	near(r.x, to_boil, EPS, "heating to the boiling point is all sensible")
	near(r.y, 0.0, EPS, "no vapour yet")
	near(b.temp, Sim.WATER_BOIL_C, 1e-6, "boiling")
	near(b.mass, 2.0, 1e-9, "mass intact")
	var r2 := Thermal.apply_heat(b, Sim.WATER_LATENT_VAPOR)   # 22 HU = 1 kg
	near(r2.y, 1.0, 1e-6, "22 HU vaporises exactly 1 kg")
	near(r2.x, Sim.WATER_LATENT_VAPOR, 1e-6, "all of it was absorbed")
	near(b.mass, 1.0, 1e-9, "liquid mass fell by the vaporised amount")
	near(b.temp, Sim.WATER_BOIL_C, 1e-9, "boiling water does not superheat")
	# Conservation: heat in == heat left in the water + heat carried off by the vapour.
	near(e0 + r.x + r2.x, b.thermal_energy() + Thermal.vapor_energy(r2.y), 1e-6, "energy conserved across vaporisation")
	# Vapour is capped by the mass available.
	var r3 := Thermal.apply_heat(b, 1000.0)
	near(r3.y, 1.0, 1e-6, "cannot vaporise more than the remaining mass")
	near(b.mass, 0.0, 1e-9, "all water gone")
	near(r3.x, Sim.WATER_LATENT_VAPOR, 1e-6, "only the energy actually needed is absorbed")


func test_ice_must_melt_before_it_can_boil() -> void:
	var b := _water(1.0, -10.0, 0.0)
	var need := 10.0 * Sim.WATER_C + Sim.WATER_LATENT_FUSION + 100.0 * Sim.WATER_C   # to 0, melt, to 100
	var r := Thermal.apply_heat(b, need - 0.01)
	near(r.y, 0.0, EPS, "no vapour while ice is still thawing / warming")
	near(b.mass, 1.0, 1e-12, "mass untouched")
	check(b.temp < Sim.WATER_BOIL_C, "not yet boiling (%.3f)" % b.temp)
	var r2 := Thermal.apply_heat(b, 0.01 + Sim.WATER_LATENT_VAPOR * 0.5)
	near(r2.y, 0.5, 1e-6, "once boiling, the surplus vaporises 0.5 kg")


func test_flash_boil_returns_kg_and_energy() -> void:
	var b := _water(2.0, 20.0)
	var per_kg := Sim.WATER_LATENT_VAPOR + Sim.WATER_C * (Sim.WATER_BOIL_C - 20.0)
	var r := Thermal.flash_boil(b, per_kg * 0.5)
	near(r.x, 0.5, 1e-9, "x = kg boiled off")
	near(r.y, per_kg * 0.5, 1e-9, "y = HU actually used")
	near(b.mass, 1.5, 1e-9, "contact layer removed from the body")
	near(b.temp, 20.0, 1e-9, "the bulk is not warmed by flash boiling")
	# Energy beyond the available mass is not consumed.
	var r2 := Thermal.flash_boil(b, 1000.0)
	near(r2.x, 1.5, 1e-9, "capped at the remaining mass")
	near(r2.y, 1.5 * per_kg, 1e-9, "uses only what the mass needs")
	near(b.mass, 0.0, 1e-9, "everything boiled")
	# Consistency with vapor_energy: steam leaves carrying exactly the energy spent at 20 degC.
	near(Thermal.vapor_energy(r.x), r.y, 1e-9, "vapor_energy matches the flash-boil cost for ambient water")


func test_flash_boil_costs_more_for_ice_and_rejects_invalid_input() -> void:
	var liquid := _water(1.0, 20.0)
	var ice := _water(1.0, -5.0, 0.0)
	var big := 1000.0
	var kl := Thermal.flash_boil(liquid, big)
	var ki := Thermal.flash_boil(ice, big)
	check(kl.y < ki.y, "boiling ice costs more energy per kg (%.2f vs %.2f)" % [kl.y, ki.y])
	var stone := _stone(5.0)
	check(Thermal.flash_boil(stone, 100.0) == Vector2.ZERO, "stone cannot flash boil")
	var w := _water(1.0)
	check(Thermal.flash_boil(w, 0.0) == Vector2.ZERO, "zero energy boils nothing")
	check(Thermal.flash_boil(w, -50.0) == Vector2.ZERO, "negative energy boils nothing")
	near(w.mass, 1.0, 1e-12, "mass untouched by rejected calls")
	var empty := _water(0.0)
	check(Thermal.flash_boil(empty, 100.0) == Vector2.ZERO, "massless water boils nothing")
